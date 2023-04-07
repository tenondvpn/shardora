#include "pools/tx_pool_manager.h"

#include "block/block_manager.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

TxPoolManager::TxPoolManager(
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    block_mgr_ = block_mgr;
    security_ = security;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(i, db, kv_sync);
    }

    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::HandleMessage, this, std::placeholders::_1));
}

TxPoolManager::~TxPoolManager() {
    prefix_db_->Destroy();
    if (tx_pool_ != nullptr) {
        delete []tx_pool_;
    }
}

std::shared_ptr<address::protobuf::AddressInfo> TxPoolManager::GetAddressInfo(
    const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_.get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_.add(addr, address_info);
    }

    return address_info;
}

void TxPoolManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // just one thread
    auto& header = msg_ptr->header;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        if (tx_msg.step() == pools::protobuf::kNormalFrom) {
            HandleNormalFromTx(msg_ptr);
        } else if (tx_msg.step() == pools::protobuf::kContractUserCreateCall) {
            HandleCreateContractTx(msg_ptr);
        } else if (tx_msg.step() == pools::protobuf::kContractUserCall) {
            HandleUserCallContractTx(msg_ptr);
        } else if (tx_msg.step() == pools::protobuf::kRootCreateAddress) {
            if (tx_msg.to().size() != security::kUnicastAddressLength) {
                return;
            }

            auto pool_index = common::Hash::Hash32(tx_msg.to()) % common::kImmutablePoolSize;
            msg_queues_[pool_index].push(msg_ptr);
        } else {
            // check valid
            msg_queues_[0].push(msg_ptr);
    //         auto ptr = msg_ptr;
    //         pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    //         tx_pool_[msg_ptr->address_info->pool_index()].AddTx(tx_ptr);
    //         ZJC_DEBUG("success add tx to queue: %d", msg_ptr->address_info->pool_index());
        }
    
        // storage item not package in block, just package storage hash 
        SaveStorageToDb(header);
    }

    if (header.has_cross_tos()) {
        HandleCrossShardingToTxs(msg_ptr);
    }
}

void TxPoolManager::SaveStorageToDb(const transport::protobuf::Header& msg) {

}

void TxPoolManager::HandleUserCallContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    // user can't direct call contract, pay contract prepayment and call contract direct
    if (!tx_msg.contract_input().empty() ||
            tx_msg.contract_prepayment() < consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("call contract not has valid contract input"
            "and contract prepayment invalid.");
        return;
    }

    if (!UserTxValid(msg_ptr)) {
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
}

bool TxPoolManager::UserTxValid(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    msg_ptr->address_info = GetAddressInfo(security_->GetAddress(tx_msg.pubkey()));
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info.");
        return false;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() + consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            consensus::kCallContractDefaultUseGas);
        return false;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return false;
    }

    if (tx_msg.has_key() && tx_msg.key().size() > kTxStorageKeyMaxSize) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return false;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        return false;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        return false;
    }

    return true;
}

void TxPoolManager::HandleCrossShardingToTxs(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    if (header.has_sync() && header.sync().items_size() > 0) {
        db::DbWriteBatch db_batch;
        for (int32_t i = 0; i < header.sync().items_size(); ++i) {
            prefix_db_->SaveTemporaryKv(
                header.sync().items(i).key(),
                header.sync().items(i).value(),
                db_batch);
            ZJC_DEBUG("save key: %s",
                common::Encode::HexEncode(header.sync().items(i).key()).c_str());
        }

        if (!db_->Put(db_batch).ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    auto block_ptr = std::make_shared<block::protobuf::Block>(header.cross_tos().block());
    block_mgr_->NetworkNewBlock(msg_ptr->thread_idx, block_ptr);
}

void TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    if (!UserTxValid(msg_ptr)) {
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
}

void TxPoolManager::HandleCreateContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!tx_msg.has_contract_code() || memcmp(
            tx_msg.contract_code().c_str(),
            protos::kContractBytesStartCode.c_str(),
            protos::kContractBytesStartCode.size()) != 0) {
        ZJC_DEBUG("create contract not has valid contract code: %s",
            common::Encode::HexEncode(tx_msg.contract_code()).c_str());
        return;
    }

    if (!UserTxValid(msg_ptr)) {
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
}

void TxPoolManager::PopTxs(uint32_t pool_index) {
    uint32_t count = 0;
    while (msg_queues_[pool_index].size() > 0 && ++count < kPopMessageCountEachTime) {
        transport::MessagePtr msg_ptr = nullptr;
        msg_queues_[pool_index].pop(&msg_ptr);
        auto& tx_msg = msg_ptr->header.tx_proto();
        if (tx_msg.step() == pools::protobuf::kNormalFrom) {
            if (security_->Verify(
                    msg_ptr->msg_hash,
                    tx_msg.pubkey(),
                    msg_ptr->header.sign()) != security::kSecuritySuccess) {
                ZJC_ERROR("verify signature failed!");
                continue;
            }
        }

        DispatchTx(pool_index, msg_ptr);
    }
}

void TxPoolManager::DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr) {
    if (msg_ptr->header.tx_proto().step() >= pools::protobuf::StepType_ARRAYSIZE) {
        assert(false);
        return;
    }

    if (item_functions_[msg_ptr->header.tx_proto().step()] == nullptr) {
        ZJC_DEBUG("not registered step : %d", msg_ptr->header.tx_proto().step());
        assert(false);
        return;
    }

    pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    if (tx_ptr == nullptr) {
        assert(false);
        return;
    }

    tx_pool_[pool_index].AddTx(tx_ptr);
    if (msg_ptr->header.tx_proto().step() == pools::protobuf::kConsensusLocalTos) {
        ZJC_DEBUG("success add local transfer to tx %u, %s", pool_index, common::Encode::HexEncode(tx_ptr->tx_hash).c_str());
    }
}

void TxPoolManager::GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map) {
    if (count > common::kSingleBlockMaxTransactions) {
        count = common::kSingleBlockMaxTransactions;
    }
       
    tx_pool_[pool_index].GetTx(res_map, count);
}

void TxPoolManager::GetTx(
        const common::BloomFilter& bloom_filter,
        uint32_t pool_index,
        std::map<std::string, TxItemPtr>& res_map) {
    tx_pool_[pool_index].GetTx(bloom_filter, res_map);
}

void TxPoolManager::TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxRecover(recover_txs);
}

void TxPoolManager::TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxOver(tx_list);
}

}  // namespace pools

}  // namespace zjchain
