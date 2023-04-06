#include "block/account_manager.h"

#include <algorithm>

#include "contract/contract_manager.h"
#include "common/encode.h"
#include "db/db.h"
#include "elect/member_manager.h"
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
    CreateNormalToAddressInfo();
    CreateNormalLocalToAddressInfo();
    inited_ = true;
    return kBlockSuccess;
}

void AccountManager::CreateNormalToAddressInfo() {
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        single_to_address_info_[i] = std::make_shared<address::protobuf::AddressInfo>();
        single_to_address_info_[i]->set_pubkey("");
        single_to_address_info_[i]->set_balance(0);
        single_to_address_info_[i]->set_sharding_id(-1);
        single_to_address_info_[i]->set_pool_index(i);
        single_to_address_info_[i]->set_addr(
            common::Hash::keccak256(common::kNormalToAddress + std::to_string(i)));
        single_to_address_info_[i]->set_type(address::protobuf::kToTxAddress);
        single_to_address_info_[i]->set_latest_height(0);
    }
}

void AccountManager::CreateNormalLocalToAddressInfo() {
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        single_local_to_address_info_[i] = std::make_shared<address::protobuf::AddressInfo>();
        single_local_to_address_info_[i]->set_pubkey("");
        single_local_to_address_info_[i]->set_balance(0);
        single_local_to_address_info_[i]->set_sharding_id(-1);
        single_local_to_address_info_[i]->set_pool_index(i);
        single_local_to_address_info_[i]->set_addr(
            common::Hash::keccak256(common::kNormalLocalToAddress + std::to_string(i)));
        single_local_to_address_info_[i]->set_type(address::protobuf::kLocalToTxAddress);
        single_local_to_address_info_[i]->set_latest_height(0);
    }
}

bool AccountManager::AccountExists(uint8_t thread_idx, const std::string& addr) {
    return GetAcountInfo(thread_idx, addr) != nullptr;
}

protos::AddressInfoPtr AccountManager::GetAcountInfoFromDb(const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
}

protos::AddressInfoPtr AccountManager::GetAcountInfo(
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
    }

    return address_info;
}

protos::AddressInfoPtr AccountManager::GetContractInfoByAddress(
        uint8_t thread_idx,
        const std::string& addr) {
    auto account_info = GetAcountInfo(thread_idx, addr);
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
    auto account_ptr = GetAcountInfo(thread_idx, addr);
    if (account_ptr == nullptr) {
        return kBlockAddressNotExists;
    }

    *network_id = account_ptr->sharding_id();
    return kBlockSuccess;
}

protos::AddressInfoPtr AccountManager::GetAcountInfo(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx_info) {
    std::string account_id = GetTxValidAddress(tx_info);
    auto account_info = std::make_shared<address::protobuf::AddressInfo>();
    account_info->set_pool_index(block_item->pool_index());
    account_info->set_addr(account_id);
    account_info->set_type(address::protobuf::kNormal);
    account_info->set_sharding_id(block_item->network_id());
    account_info->set_latest_height(block_item->height());
    account_info->set_balance(tx_info.balance());
    return account_info;
}

std::string AccountManager::GetTxValidAddress(const block::protobuf::BlockTx& tx_info) {
    switch (tx_info.step()) {
    case pools::protobuf::kNormalTo:
    case pools::protobuf::kRootCreateAddress:
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kConsensusLocalTos:
    case pools::protobuf::kConsensusRootElectShard:
    case pools::protobuf::kConsensusRootTimeBlock:
    case pools::protobuf::kConsensusFinalStatistic:
    case pools::protobuf::kConsensusCreateGenesisAcount:
        return tx_info.to();
    case pools::protobuf::kNormalFrom:
        return tx_info.from();
    default:
        assert(false);
        return "";
    }
}

void AccountManager::HandleNormalFromTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    std::string account_id = GetTxValidAddress(tx);
    auto account_info = GetAcountInfo(thread_idx, account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
//     ZJC_DEBUG("transfer from address new balance %s: %lu",
//         common::Encode::HexEncode(account_id).c_str(), tx.balance());
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
        auto account_info = GetAcountInfo(thread_idx, to_txs.tos(i).to());
        if (account_info == nullptr) {
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(block.pool_index());
            account_info->set_addr(to_txs.tos(i).to());
            account_info->set_type(address::protobuf::kNormal);
            account_info->set_sharding_id(block.network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            address_map_[thread_idx].add(to_txs.tos(i).to(), account_info);
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info);
        } else {
            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
        }

        ZJC_DEBUG("transfer to address new balance %s: %lu",
            common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), to_txs.tos(i).balance());
    }
}

void AccountManager::HandleContractCreateUserCall(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto account_info = GetAcountInfo(thread_idx, tx.to());
    if (account_info != nullptr) {
        return;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(block.pool_index());
            account_info->set_addr(tx.to());
            account_info->set_type(address::protobuf::kContract);
            account_info->set_sharding_id(block.network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(tx.amount());
            address_map_[thread_idx].add(tx.to(), account_info);
            prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
            prefix_db_->SaveAddressTmpBytesCode(tx.to(), tx.storages(i).val_hash(), db_batch);
        }
    }
}

void AccountManager::NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    switch (tx.step()) {
    case pools::protobuf::kNormalFrom:
        HandleNormalFromTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kConsensusLocalTos:
        HandleLocalToTx(thread_idx, *block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractUserCreateCall:
        HandleContractCreateUserCall(thread_idx, *block_item, tx, db_batch);
        break;
    default:
        break;
    }
}

}  // namespace block

}  //namespace zjchain
