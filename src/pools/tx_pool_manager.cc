#include "pools/tx_pool_manager.h"

#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

TxPoolManager::TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    security_ = security;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    prefix_db_->InitGidManager();
    kv_sync_ = kv_sync;
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(i, db, kv_sync);
    }

    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this, std::placeholders::_1));
    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::HandleMessage, this, std::placeholders::_1));
}

TxPoolManager::~TxPoolManager() {
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].FlushHeightTree();
    }

    if (tx_pool_ != nullptr) {
        delete []tx_pool_;
    }

    prefix_db_->Destroy();
}

void TxPoolManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_sync_height_tree_tm_ms_ < now_tm_ms) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            tx_pool_[i].FlushHeightTree();
        }

        prev_sync_height_tree_tm_ms_ = now_tm_ms + kFlushHeightTreePeriod;
    }

    if (prev_sync_check_ms_ < now_tm_ms) {
        SyncMinssingHeights(msg_ptr->thread_idx, now_tm_ms);
        prev_sync_check_ms_ = now_tm_ms + kSyncPoolsMaxHeightsPeriod;
    }

    if (prev_sync_heights_ms_ < now_tm_ms) {
        SyncPoolsMaxHeight(msg_ptr->thread_idx);
        prev_sync_heights_ms_ = now_tm_ms + kSyncPoolsMaxHeightsPeriod;
    }
}

void TxPoolManager::SyncMinssingHeights(uint8_t thread_idx, uint64_t now_tm_ms) {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        tx_pool_[common::kRootChainPoolIndex].SyncMissingBlocks(thread_idx, now_tm_ms);
        return;
    }

    prev_synced_pool_index_ %= common::kInvalidPoolIndex;
    auto begin_pool = prev_synced_pool_index_;
    for (; prev_synced_pool_index_ < common::kInvalidPoolIndex; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (res > 0) {
            ++prev_synced_pool_index_;
            return;
        }

        if (tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                thread_idx, prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
        }
    }

    for (prev_synced_pool_index_ = 0;
            prev_synced_pool_index_ < begin_pool; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (res > 0) {
            ++prev_synced_pool_index_;
            return;
        }

        if (tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                thread_idx, prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
        }
    }
}

void TxPoolManager::SyncBlockWithMaxHeights(uint8_t thread_idx, uint32_t pool_idx, uint64_t height) {
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
        net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    kv_sync_->AddSyncHeight(
        thread_idx,
        net_id,
        pool_idx,
        height,
        sync::kSyncHigh);
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
    ZJC_DEBUG("handle from message 11");
    auto& header = msg_ptr->header;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        switch (tx_msg.step()) {
        case pools::protobuf::kJoinElect:
            HandleElectTx(msg_ptr);
            break;
        case pools::protobuf::kNormalFrom:
            HandleNormalFromTx(msg_ptr);
            break;
        case pools::protobuf::kContractUserCreateCall:
            HandleCreateContractTx(msg_ptr);
            break;
        case pools::protobuf::kContractUserCall:
            HandleUserCallContractTx(msg_ptr);
            break;
        case pools::protobuf::kRootCreateAddress: {
            if (tx_msg.to().size() != security::kUnicastAddressLength) {
                return;
            }

            auto pool_index = common::Hash::Hash32(tx_msg.to()) % common::kImmutablePoolSize;
            msg_queues_[pool_index].push(msg_ptr);
            break;
        }
        case pools::protobuf::kContractExcute:
            HandleContractExcute(msg_ptr);
            break;
        default:
            msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
            break;
        }
    }
}

void TxPoolManager::SyncPoolsMaxHeight(uint8_t thread_idx) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    msg_ptr->header.set_src_sharding_id(net_id);
    dht::DhtKeyManager dht_key(net_id);
    msg_ptr->header.set_des_dht_key(dht_key.StrKey());
    msg_ptr->header.set_type(common::kPoolsMessage);
    auto* sync_heights = msg_ptr->header.mutable_sync_heights();
    sync_heights->set_req(true);
    transport::TcpTransport::Instance()->SetMessageHash(
        msg_ptr->header,
        msg_ptr->thread_idx);
    network::Route::Instance()->Send(msg_ptr);
}

void TxPoolManager::HandleSyncPoolsMaxHeightReq(const transport::MessagePtr& msg_ptr) {
    if (tx_pool_ == nullptr) {
        return;
    }

    if (!msg_ptr->header.has_sync_heights()) {
        return;
    }

    if (msg_ptr->header.sync_heights().req()) {
        transport::protobuf::Header msg;
        auto net_id = common::GlobalInfo::Instance()->network_id();
        msg.set_src_sharding_id(net_id);
        dht::DhtKeyManager dht_key(net_id);
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kPoolsMessage);
        auto* sync_heights = msg.mutable_sync_heights();
        uint32_t pool_idx = common::kInvalidPoolIndex;
        for (uint32_t i = 0; i < pool_idx; ++i) {
            sync_heights->add_heights(tx_pool_[i].latest_height());
        }

        transport::TcpTransport::Instance()->SetMessageHash(
            msg,
            msg_ptr->thread_idx);
        transport::TcpTransport::Instance()->Send(msg_ptr->thread_idx, msg_ptr->conn, msg);
    } else {
        auto& heights = msg_ptr->header.sync_heights().heights();
        if (heights.size() != common::kInvalidPoolIndex) {
            return;
        }

        std::string res_heights_debug;
        for (int32_t i = 0; i < heights.size(); ++i) {
            if (heights[i] != common::kInvalidUint64) {
                res_heights_debug += std::to_string(heights[i]) + " ";
                if (heights[i] > tx_pool_[i].latest_height() + 64) {
                    synced_max_heights_[i] = tx_pool_[i].latest_height() + 64;
                    continue;
                }

                if (heights[i] > tx_pool_[i].latest_height()) {
                    synced_max_heights_[i] = heights[i];
                }
            }
        }

        ZJC_DEBUG("synced heights: %s", res_heights_debug.c_str());
    }
}

void TxPoolManager::HandleElectTx(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    msg_ptr->address_info = GetAddressInfo(security_->GetAddress(tx_msg.pubkey()));
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info.");
        return;
    }

    if (msg_ptr->address_info->balance() < consensus::kJoinElectGas) {
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (!tx_msg.has_key() || tx_msg.key() != protos::kElectJoinShard) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return;
    }

    uint32_t* tmp = (uint32_t*)tx_msg.value().c_str();
    if (tmp[0] != network::kRootCongressNetworkId) {
        if (tmp[0] != msg_ptr->address_info->sharding_id()) {
            ZJC_DEBUG("join des shard error: %d,  %d.",
                tmp[0], msg_ptr->address_info->sharding_id());
            return;
        }
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        return;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        return;
    }

    prefix_db_->SaveAddressPubkey(msg_ptr->address_info->addr(), tx_msg.pubkey());
    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("success add elect tx.");
}

void TxPoolManager::HandleContractExcute(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    if (tx_msg.has_key() && tx_msg.key().size() > 0) {
        ZJC_DEBUG("call contract key must empty.");
        return;
    }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        return;
    }

    msg_ptr->address_info = GetAddressInfo(tx_msg.to());
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no contract address info.");
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    uint64_t height = 0;
    uint64_t prepayment = 0;
    if (!prefix_db_->GetContractUserPrepayment(
            tx_msg.to(),
            security_->GetAddress(tx_msg.pubkey()),
            &height,
            &prepayment)) {
        return;
    }

    if (prepayment < tx_msg.amount() + tx_msg.gas_limit() * tx_msg.gas_price()) {
        return;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        return;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("success add contract call.");
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
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
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

void TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("handle from message 0");
    if (!UserTxValid(msg_ptr)) {
        return;
    }

    ZJC_DEBUG("handle from message 1");
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
        if (tx_msg.step() == pools::protobuf::kNormalFrom ||
                tx_msg.step() == pools::protobuf::kJoinElect) {
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
    ZJC_DEBUG("success add local transfer to tx %u, %s, gid: %s",
        pool_index,
        common::Encode::HexEncode(tx_ptr->tx_hash).c_str(),
        common::Encode::HexEncode(tx_ptr->gid).c_str());
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
