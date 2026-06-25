#include "block/account_manager.h"

#include <algorithm>
#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <protos/pools.pb.h>

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

namespace shardora {

namespace block {

AccountManager::AccountManager() {
    
}

AccountManager::~AccountManager() {}

int AccountManager::Init(
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    uint16_t network_id = network::GetLocalConsensusNetworkId();
    for (uint32_t step = pools::protobuf::kNormalFrom; step <= pools::protobuf::kPoolStatisticTag; ++step) {
        std::unordered_set<uint32_t> pool_idx_set;
        for (uint32_t i = 0; i < common::kInvalidUint32; ++i) {
            auto hash = common::Hash::keccak256(std::to_string(i) + std::to_string(network_id) + std::to_string(step));
            auto addr = hash.substr(
                hash.size() - common::kUnicastAddressLength, 
                common::kUnicastAddressLength);
            auto pool_idx = common::GetAddressPoolIndex(addr);
            if (pool_idx == common::kGlobalPoolIndex) {
                continue;
            }

            if (pool_idx_set.size() >= common::kImmutablePoolSize) {
                break;
            }

            auto iter = pool_idx_set.find(pool_idx);
            if (iter != pool_idx_set.end()) {
                continue;
            }

            pool_base_addrs_[step][pool_idx] = addr;
            pool_idx_set.insert(pool_idx);
            SHARDORA_DEBUG("network_id: %u, init pool index: %u, base address: %s", 
                network_id, pool_idx, common::Encode::HexEncode(addr).c_str());
        }

        std::string immutable_pool_addr(common::kUnicastAddressLength, '0');
        uint16_t tmp_step = static_cast<uint16_t>(step);
        std::memcpy(
            &immutable_pool_addr[common::kUnicastAddressLength - sizeof(network_id) - sizeof(tmp_step)], 
            &network_id, 
            sizeof(network_id));
        std::memcpy(
            &immutable_pool_addr[common::kUnicastAddressLength - sizeof(tmp_step)], 
            &tmp_step, 
            sizeof(tmp_step));
        pool_base_addrs_[step][common::kGlobalPoolIndex] = immutable_pool_addr;
        SHARDORA_DEBUG("init pool immutable index net: %u, init pool index: %u, base address: %s", 
            network_id, 
            common::kGlobalPoolIndex, 
            common::Encode::HexEncode(immutable_pool_addr).c_str());
    }

    return kBlockSuccess;
}

bool AccountManager::AccountExists(const std::string& addr) {
    return GetAccountInfo(addr) != nullptr;
}

protos::AddressInfoPtr AccountManager::GetAcountInfoFromDb(const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
}

protos::AddressInfoPtr AccountManager::GetAccountInfo(const std::string& addr) {
    protos::AddressInfoPtr addr_info = account_lru_map_.get(addr);
    if (addr_info != nullptr) {
        return addr_info;
    }

    addr_info = GetAcountInfoFromDb(addr);
    if (!addr_info) {
        BLOCK_DEBUG(
            "get account failed[%s]", 
            common::Encode::HexEncode(addr).c_str());
    } else {
        addr_info = account_lru_map_.get_or_insert(addr, addr_info);
    }

    return addr_info;
}

void AccountManager::AddNewBlock(const view_block::protobuf::ViewBlockItem& view_block_item) {
    if (!network::IsSameToLocalShard(view_block_item.qc().network_id()))  {
        return;
    }

    for (int32_t i = 0; i < view_block_item.block_info().address_array_size(); ++i) {
        auto addr_info_ptr = std::make_shared<address::protobuf::AddressInfo>(
            view_block_item.block_info().address_array(i));
        auto acc_ptr = account_lru_map_.get(addr_info_ptr->addr());
        if (acc_ptr) {
            SHARDORA_DEBUG("account exists in lru map: %s, balance: %lu, nonce: %lu, "
                "latest height: %lu, tx index: %lu, block height: %lu, "
                "addr_info_ptr latest height: %lu, tx_index: %u",
                common::Encode::HexEncode(addr_info_ptr->addr()).c_str(),
                acc_ptr->balance(),
                acc_ptr->nonce(),
                acc_ptr->latest_height(),
                acc_ptr->tx_index(),
                view_block_item.block_info().height(),
                addr_info_ptr->latest_height(),
                addr_info_ptr->tx_index());
        } else {
            SHARDORA_DEBUG("account not exists in lru map: %s, balance: %lu, nonce: %lu, "
                "latest height: %lu, tx index: %lu, block height: %lu",
                common::Encode::HexEncode(addr_info_ptr->addr()).c_str(),
                addr_info_ptr->balance(),
                addr_info_ptr->nonce(),
                addr_info_ptr->latest_height(),
                addr_info_ptr->tx_index(),
                view_block_item.block_info().height());
        }

        if (!acc_ptr || 
                acc_ptr->latest_height() < addr_info_ptr->latest_height() || 
                (acc_ptr->latest_height() == addr_info_ptr->latest_height() &&
                acc_ptr->tx_index() < addr_info_ptr->tx_index())) {
            account_lru_map_.insert(addr_info_ptr);
        }
    }
}

}  // namespace block

}  //namespace shardora
