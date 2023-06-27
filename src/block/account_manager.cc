#include "block/account_manager.h"

#include <algorithm>

#include "contract/contract_manager.h"
#include "common/encode.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h" 
#include "protos/elect.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace block {

AccountManager::AccountManager() {
}

AccountManager::~AccountManager() {
    if (address_map_ != nullptr) {
        delete[] address_map_;
    }
}

int AccountManager::Init(
        uint8_t thread_count,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    pools_mgr_ = pools_mgr;
    address_map_ = new common::UniqueMap<std::string, protos::AddressInfoPtr, 1024, 16>[thread_count];
    CreatePoolsAddressInfo();
    inited_ = true;
    return kBlockSuccess;
}

void AccountManager::CreatePoolsAddressInfo() {
    root_pool_address_info_ = std::make_shared<address::protobuf::AddressInfo>();
    root_pool_address_info_->set_pubkey("");
    root_pool_address_info_->set_balance(0);
    root_pool_address_info_->set_sharding_id(-1);
    root_pool_address_info_->set_pool_index(common::kRootChainPoolIndex);
    root_pool_address_info_->set_addr(common::kRootPoolsAddress);
    root_pool_address_info_->set_type(address::protobuf::kToTxAddress);
    root_pool_address_info_->set_latest_height(0);
    uint32_t i = 0;
    uint32_t valid_idx = 0;
    for (uint32_t i = 0; i < common::kInvalidUint32; ++i) {
        std::string addr = common::kRootPoolsAddress;
        uint32_t* tmp_data = (uint32_t*)addr.data();
        tmp_data[0] = i;
        auto pool_idx = common::GetAddressPoolIndex(addr);
        if (pool_address_info_[pool_idx] != nullptr) {
            continue;
        }

        pool_address_info_[pool_idx] = std::make_shared<address::protobuf::AddressInfo>();
        pool_address_info_[pool_idx]->set_pubkey("");
        pool_address_info_[pool_idx]->set_balance(0);
        pool_address_info_[pool_idx]->set_sharding_id(-1);
        pool_address_info_[pool_idx]->set_pool_index(pool_idx);
        pool_address_info_[pool_idx]->set_addr(addr);
        pool_address_info_[pool_idx]->set_type(address::protobuf::kToTxAddress);
        pool_address_info_[pool_idx]->set_latest_height(0);
        ++valid_idx;
        if (valid_idx >= common::kImmutablePoolSize) {
            break;
        }
    }
}

bool AccountManager::AccountExists(uint8_t thread_idx, const std::string& addr) {
    return GetAccountInfo(thread_idx, addr) != nullptr;
}

protos::AddressInfoPtr AccountManager::GetAcountInfoFromDb(const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
}

protos::AddressInfoPtr AccountManager::GetAccountInfo(
        uint8_t thread_idx,
        const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_[thread_idx].get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_[thread_idx].add(addr, address_info);
    } else {
        assert(false);
    }

    return address_info;
}

protos::AddressInfoPtr AccountManager::GetContractInfoByAddress(
        uint8_t thread_idx,
        const std::string& addr) {
    auto account_info = GetAccountInfo(thread_idx, addr);
    if (account_info == nullptr) {
        BLOCK_ERROR("get account failed[%s]", common::Encode::HexEncode(addr).c_str());
        return nullptr;
    }

    if (account_info->type() != address::protobuf::kContract) {
        return nullptr;
    }

    return account_info;
}

int AccountManager::GetAddressConsensusNetworkId(
        uint8_t thread_idx,
        const std::string& addr,
        uint32_t* network_id) {
    auto account_ptr = GetAccountInfo(thread_idx, addr);
    if (account_ptr == nullptr) {
        return kBlockAddressNotExists;
    }

    *network_id = account_ptr->sharding_id();
    return kBlockSuccess;
}

const std::string& AccountManager::GetTxValidAddress(const block::protobuf::BlockTx& tx_info) {
    switch (tx_info.step()) {
    case pools::protobuf::kNormalTo:
    case pools::protobuf::kRootCreateAddress:
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kConsensusLocalTos:
    case pools::protobuf::kConsensusRootElectShard:
    case pools::protobuf::kConsensusRootTimeBlock:
    case pools::protobuf::kConsensusCreateGenesisAcount:
    case pools::protobuf::kContractExcute:
    case pools::protobuf::kStatistic:
        return tx_info.to();
    case pools::protobuf::kJoinElect:
    case pools::protobuf::kNormalFrom:
        return tx_info.from();
    default:
        assert(false);
        return tx_info.from();
    }
}

void AccountManager::HandleNormalFromTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& account_id = GetTxValidAddress(tx);
    auto account_info = GetAccountInfo(thread_idx, account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    ZJC_DEBUG("transfer from address new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), block.pool_index());
}

void AccountManager::HandleLocalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    std::string to_txs_str;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kConsensusLocalNormalTos) {
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &to_txs_str)) {
                ZJC_DEBUG("handle local to tx failed get val hash error: %s",
                    common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                return;
            }

            break;
        }
    }

    if (to_txs_str.empty()) {
        ZJC_WARN("get local tos info failed!");
        return;
    }

    block::protobuf::ConsensusToTxs to_txs;
    if (!to_txs.ParseFromString(to_txs_str)) {
        return;
    }

    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        if (to_txs.tos(i).to().size() != security::kUnicastAddressLength) {
            continue;
        }

        auto account_info = GetAccountInfo(thread_idx, to_txs.tos(i).to());
        if (account_info == nullptr) {
            ZJC_INFO("0 get address info failed create new address to this id: %s,"
                "shard: %u, local shard: %u",
                common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), block.network_id(),
                common::GlobalInfo::Instance()->network_id());
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(block.pool_index());
            account_info->set_addr(to_txs.tos(i).to());
            account_info->set_type(address::protobuf::kNormal);
            account_info->set_sharding_id(block.network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            address_map_[thread_idx].add(to_txs.tos(i).to(), account_info);
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
        } else {
            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
        }

        ZJC_DEBUG("transfer to address new balance %s: %lu",
            common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), to_txs.tos(i).balance());
    }
}

void AccountManager::HandleCreateContract(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto account_info = GetAccountInfo(thread_idx, tx.to());
    if (account_info != nullptr) {
        assert(false);
        return;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            auto& bytes_code = tx.storages(i).val_hash();
            account_info->set_type(address::protobuf::kContract);
            account_info->set_pool_index(block.pool_index());
            account_info->set_addr(tx.to());
            account_info->set_sharding_id(block.network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(tx.amount());
            account_info->set_bytes_code(bytes_code);
            address_map_[thread_idx].add(tx.to(), account_info);
            prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
            ZJC_INFO("1 get address info failed create new address to this id: %s,"
                "shard: %u, local shard: %u",
                common::Encode::HexEncode(tx.to()).c_str(), block.network_id(),
                common::GlobalInfo::Instance()->network_id());

            ZJC_DEBUG("create add contract direct: %s, amount: %lu, sharding: %u, pool index: %u",
                common::Encode::HexEncode(tx.to()).c_str(),
                tx.amount(),
                block.network_id(),
                block.pool_index());
            break;
        }
    }
}

void AccountManager::HandleContractExecuteTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& account_id = GetTxValidAddress(tx);
    auto account_info = GetAccountInfo(thread_idx, account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    account_info->set_latest_height(block.height());
    // amount is contract 's new balance
    account_info->set_balance(tx.amount());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    ZJC_DEBUG("contract call address new balance %s: %lu",
        common::Encode::HexEncode(account_id).c_str(), tx.amount());
}

void AccountManager::HandleRootCreateAddressTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto account_info = GetAccountInfo(thread_idx, tx.to());
    if (account_info != nullptr) {
//         assert(false);
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kRootCreateAddressKey) {
            uint32_t* tmp = (uint32_t*)tx.storages(i).val_hash().c_str();
            sharding_id = tmp[0];
            break;
        }
    }

    if (sharding_id == common::kInvalidUint32) {
        assert(false);
        return;
    }

    uint32_t pool_index = common::GetAddressPoolIndex(tx.to());
    account_info = std::make_shared<address::protobuf::AddressInfo>();
    account_info->set_pool_index(pool_index);
    account_info->set_addr(tx.to());
    account_info->set_type(address::protobuf::kNormal);
    account_info->set_sharding_id(sharding_id);
    account_info->set_latest_height(block.height());
    account_info->set_balance(0);  // root address balance invalid
    address_map_[thread_idx].add(tx.to(), account_info);
    prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
    ZJC_INFO("2 get address info failed create new address to this id: %s,"
        "shard: %u, local shard: %u",
        common::Encode::HexEncode(tx.to()).c_str(), sharding_id,
        common::GlobalInfo::Instance()->network_id());

    ZJC_DEBUG("create root address direct: %s, sharding: %u, pool index: %u",
        common::Encode::HexEncode(tx.to()).c_str(),
        sharding_id,
        pool_index);
}


void AccountManager::HandleJoinElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    bls::protobuf::JoinElectInfo join_info;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                ZJC_DEBUG("handle local to tx failed get val hash error: %s",
                    common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                return;
            }

            if (!join_info.ParseFromString(val)) {
                assert(false);
                break;
            }

            break;
        }
    }

    if (!join_info.has_member_idx()) {
        ZJC_WARN("get local tos info failed!");
        return;
    }

    auto account_info = GetAccountInfo(thread_idx, tx.from());
    if (account_info == nullptr) {
        ZJC_INFO("get address info failed create new address to this shard: %s",
            common::Encode::HexEncode(tx.from()).c_str());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(block.pool_index());
        account_info->set_addr(tx.from());
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(block.network_id());
        account_info->set_latest_height(block.height());
        account_info->set_balance(tx.balance());
        account_info->set_elect_pos(join_info.member_idx());
        address_map_[thread_idx].add(tx.from(), account_info);
        prefix_db_->AddAddressInfo(tx.from(), *account_info);
        ZJC_INFO("3 get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u",
            common::Encode::HexEncode(tx.from()).c_str(), block.network_id(),
            common::GlobalInfo::Instance()->network_id());

    } else {
        account_info->set_latest_height(block.height());
        account_info->set_balance(tx.balance());
        account_info->set_elect_pos(join_info.member_idx());
        prefix_db_->AddAddressInfo(tx.from(), *account_info, db_batch);
    }

    ZJC_DEBUG("join elect to address new elect pos %s: %lu",
        common::Encode::HexEncode(tx.from()).c_str(), join_info.member_idx());
}

void AccountManager::NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    if (tx.status() != consensus::kConsensusSuccess) {
        assert(false);
        return;
    }

    switch (tx.step()) {
    case pools::protobuf::kRootCreateAddress:
        HandleRootCreateAddressTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kNormalFrom:
        HandleNormalFromTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kConsensusLocalTos:
        HandleLocalToTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractCreate:
        HandleCreateContract(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractExcute:
        HandleContractExecuteTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kJoinElect:
        HandleJoinElectTx(thread_idx, *block_item, tx, db_batch);
        break;
    default:
        break;
    }
}

}  // namespace block

}  //namespace zjchain
