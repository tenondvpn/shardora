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
    std::string immutable_pool_addr;
    immutable_pool_addr.reserve(common::kUnicastAddressLength);
    immutable_pool_addr.append(common::kRootPoolsAddressPrefix);
    uint16_t network_id = network::GetLocalConsensusNetworkId();
    immutable_pool_addr.append(std::string((char*)&network_id, sizeof(network_id)));
    immutable_pool_addr_ = immutable_pool_addr;
    ZJC_DEBUG("init pool immutable index net: %u, base address: %s", 
        network_id, common::Encode::HexEncode(immutable_pool_addr_).c_str());
    std::unordered_set<uint32_t> pool_idx_set;
    for (uint32_t i = 0; i < common::kInvalidUint32; ++i) {
        auto hash = common::Hash::keccak256(std::to_string(i) + std::to_string(network_id));
        auto addr = hash.substr(
            hash.size() - common::kUnicastAddressLength, 
            common::kUnicastAddressLength);
        auto pool_idx = common::GetAddressPoolIndex(addr);
        if (pool_idx_set.size() >= common::kImmutablePoolSize) {
            break;
        }

        auto iter = pool_idx_set.find(pool_idx);
        if (iter != pool_idx_set.end()) {
            continue;
        }

        pool_base_addrs_[pool_idx] = addr;
        pool_idx_set.insert(pool_idx);
        ZJC_DEBUG("init pool index: %u, base address: %s", 
            pool_idx, common::Encode::HexEncode(addr).c_str());
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
    }

    return addr_info;
}

void AccountManager::AddNewBlock(const view_block::protobuf::ViewBlockItem& view_block_item) {
    for (uint32_t i = 0; i < view_block_item.block_info().address_array_size(); ++i) {
        auto addr_info_ptr = std::make_shared<address::protobuf::AddressInfo>(
            view_block_item.block_info().address_array(i));
        account_lru_map_.insert(addr_info_ptr);
    }
}

}  // namespace block

}  //namespace shardora
