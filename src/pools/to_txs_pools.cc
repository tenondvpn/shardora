#include "pools/to_txs_pools.h"

#include "common/global_info.h"
#include "common/user_property_key_define.h"
#include "network/network_utils.h"
#include "network/route.h"

namespace zjchain {

namespace pools {

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id) : db_(db), local_id_(local_id) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    PoolMap pool_map;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        pool_map[i] = HeightMap();
    }

    for (uint32_t i = network::kRootCongressNetworkId; i <= max_sharding_id; ++i) {
        network_txs_pools_[i] = pool_map;
    }

    LoadLatestHeights();
    address_map_.Init(10240, 16);
}

ToTxsPools::~ToTxsPools() {}

void ToTxsPools::NewBlock(const block::protobuf::Block& block, db::DbWriteBatch& db_batch) {
    if (block.network_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid!");
        return;
    }

    for (auto net_iter = network_txs_pools_.begin(); net_iter != network_txs_pools_.end(); ++net_iter) {
        auto handled_iter = handled_map_.find(net_iter->first);
        if (handled_iter != handled_map_.end()) {
            if (handled_iter->second->heights(block.pool_index()) >= block.height()) {
                continue;
            }
        }

        TxMap tx_map;
        // just clear and reload txs, height must unique
        net_iter->second[block.pool_index()][block.height()] = tx_map;
    }

    const auto& tx_list = block.tx_list();
    if (tx_list.empty()) {
        ZJC_DEBUG("tx list empty!");
        return;
    }
    
    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].step() == pools::protobuf::kNormalTo) {
            // remove each less height
            ZJC_DEBUG("new to coming.");
            HandleNormalToTx(block.height(), tx_list[i], db_batch);
            continue;
        }

        if (tx_list[i].step() != pools::protobuf::kNormalFrom) {
            ZJC_DEBUG("invalid from coming: %d", tx_list[i].step());
            continue;
        }

        if (tx_list[i].amount() <= 0) {
            ZJC_DEBUG("from transfer amount invalid!");
            continue;
        }

        uint32_t sharding_id = common::kInvalidUint32;
        auto addr_info = GetAddressInfo(tx_list[i].to());
        if (addr_info == nullptr) {
            sharding_id = network::kRootCongressNetworkId;
        } else {
            sharding_id = addr_info->sharding_id();
        }

        auto handled_iter = handled_map_.find(sharding_id);
        if (handled_iter != handled_map_.end()) {
            if (handled_iter->second->heights(block.pool_index()) >= block.height()) {
                ZJC_DEBUG("height invalid, %lu: %lu, sharding: %u, pool: %u!",
                    handled_iter->second->heights(block.pool_index()),
                    block.height(),
                    block.network_id(),
                    block.pool_index());
                continue;
            }
        }

        auto net_iter = network_txs_pools_.find(sharding_id);
        if (net_iter == network_txs_pools_.end()) {
            PoolMap pool_map;
            network_txs_pools_[sharding_id] = pool_map;
            net_iter = network_txs_pools_.find(sharding_id);
//             ZJC_DEBUG("reset pools network: %u", sharding_id);
        }

        auto pool_iter = net_iter->second.find(block.pool_index());
        if (pool_iter == net_iter->second.end()) {
            HeightMap height_map;
            net_iter->second[block.pool_index()] = height_map;
            pool_iter = net_iter->second.find(block.pool_index());
        }

        auto height_iter = pool_iter->second.find(block.height());
        if (height_iter == pool_iter->second.end()) {
            TxMap tx_map;
            pool_iter->second[block.height()] = tx_map;
            height_iter = pool_iter->second.find(block.height());
        }

        auto to_iter = height_iter->second.find(tx_list[i].to());
        if (to_iter == height_iter->second.end()) {
            height_iter->second[tx_list[i].to()] = 0;
        }

        height_iter->second[tx_list[i].to()] += tx_list[i].amount();
//         ZJC_DEBUG("new from add new to sharding: %u, id: %s, amount: %lu, pool: %u, height: %lu, pool size: %u",
//             sharding_id,
//             common::Encode::HexEncode(tx_list[i].to()).c_str(),
//             height_iter->second[tx_list[i].to()],
//             block.pool_index(),
//             block.height(),
//             net_iter->second.size());
    }
}

void ToTxsPools::HandleNormalToTx(
        uint64_t block_height,
        const block::protobuf::BlockTx& tx_info,
        db::DbWriteBatch& db_batch) {
    if (tx_info.storages_size() <= 0) {
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
    auto& heights = *heights_ptr;
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.storages(i).key() == protos::kNormalTos) {
            std::string to_txs_str;
            if (!prefix_db_->GetTemporaryKv(tx_info.storages(i).val_hash(), &to_txs_str)) {
                ZJC_WARN("get to tx heights failed: %s!",
                    common::Encode::HexEncode(tx_info.storages(i).val_hash()).c_str());
                return;
            }
            
            pools::protobuf::ToTxMessage to_tx;
            if (!to_tx.ParseFromString(to_txs_str)) {
                ZJC_WARN("parse from to txs message failed: %s",
                    common::Encode::HexEncode(tx_info.storages(i).val_hash()).c_str());
                return;
            }

            heights = to_tx.to_heights();
            break;
        }
    }

    if (heights.heights_size() != common::kImmutablePoolSize) {
        ZJC_ERROR("invalid heights size: %d, %d",
            heights.heights_size(), common::kImmutablePoolSize);
        return;
    }

    auto handled_iter = handled_map_.find(heights.sharding_id());
    if (handled_iter != handled_map_.end()) {
        if (handled_iter->second->block_height() >= block_height) {
            ZJC_WARN("block_height failed: %lu, %lu!",
                handled_iter->second->block_height(), block_height);
            return;
        }
    }

    handled_map_[heights.sharding_id()] = heights_ptr;
    prefix_db_->SaveLatestToTxsHeights(heights, db_batch);
    auto net_iter = network_txs_pools_.find(heights.sharding_id());
    if (net_iter == network_txs_pools_.end()) {
        ZJC_DEBUG("no sharding exists.");
        return;
    }

    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        auto pool_iter = net_iter->second.find(i);
        if (pool_iter == net_iter->second.end()) {
            continue;
        }

        auto height_iter = pool_iter->second.begin();
        while (height_iter != pool_iter->second.end()) {
            if (height_iter->first > heights.heights(i)) {
                break;
            }

            ZJC_DEBUG("erase sharding: %u, height: %lu", heights.sharding_id(), height_iter->first);
            pool_iter->second.erase(height_iter++);
        }
    }
}

void ToTxsPools::LoadLatestHeights() {
    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
        pools::protobuf::ToTxHeights& to_heights = *heights_ptr;
        if (!prefix_db_->GetLatestToTxsHeights(i, &to_heights)) {
            continue;
        }

        handled_map_[i] = heights_ptr;
    }
}

std::shared_ptr<address::protobuf::AddressInfo> ToTxsPools::GetAddressInfo(
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

int ToTxsPools::LeaderCreateToHeights(
        uint32_t sharding_id,
        pools::protobuf::ToTxHeights& to_heights) {
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        return kPoolsError;
    }

    to_heights.set_sharding_id(sharding_id);
    bool valid = false;
    std::string heights;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto pool_iter = net_iter->second.find(i);
        auto r_height_iter = pool_iter->second.rbegin();
        if (r_height_iter == pool_iter->second.rend()) {
            to_heights.add_heights(0);
        } else {
            to_heights.add_heights(r_height_iter->first);
            heights += std::to_string(r_height_iter->first) + " ";
            valid = true;
        }
    }

    if (!valid) {
        return kPoolsError;
    }

    ZJC_DEBUG("leader success create to heights: %s", heights.c_str());
    return kPoolsSuccess;
}

int ToTxsPools::CreateToTxWithHeights(
        uint32_t sharding_id,
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* to_hash) {
    pools::protobuf::ToTxMessage to_tx;
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        assert(false);
        return kPoolsError;
    }

    if (leader_to_heights.heights_size() != common::kImmutablePoolSize) {
        assert(false);
        return kPoolsError;
    }

    auto handled_iter = handled_map_.find(sharding_id);
    std::map<std::string, uint64_t> acc_amount_map;
    for (uint32_t pool_idx = 0; pool_idx < common::kImmutablePoolSize; ++pool_idx) {
        auto pool_iter = net_iter->second.find(pool_idx);
        if (pool_iter == net_iter->second.end()) {
            assert(false);
            return kPoolsError;
        }

        uint64_t min_height = 1;
        if (handled_iter != handled_map_.end()) {
            min_height = handled_iter->second->heights(pool_idx);
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        for (auto height = min_height; height < max_height; ++height) {
            auto hiter = pool_iter->second.find(height);
            if (hiter == pool_iter->second.end()) {
                ZJC_DEBUG("pool %u, invalid height: %lu", pool_idx, height)
                return kPoolsError;
            }
        }

        ZJC_DEBUG("pool: %d, min_height: %lu, max_height: %lu", pool_idx, min_height, max_height);
        for (auto height = min_height; height < max_height; ++height) {
            auto hiter = pool_iter->second.find(height);
            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    acc_amount_map[to_iter->first] = to_iter->second;
                } else {
                    amount_iter->second += to_iter->second;
                }
            }
        }
    }

    if (acc_amount_map.empty()) {
        return kPoolsError;
    }

    std::string str_for_hash;
    str_for_hash.reserve(common::kImmutablePoolSize * 8 + acc_amount_map.size() * 48);
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto height = leader_to_heights.heights(i);
        str_for_hash.append((char*)&height, sizeof(height));
    }

    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        str_for_hash.append(iter->first);
        str_for_hash.append((char*)&iter->second, sizeof(iter->second));
        auto to_item = to_tx.add_tos();
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second);
        ZJC_DEBUG("set to %s amount %lu", common::Encode::HexEncode(iter->first).c_str(), iter->second);
    }

    *to_hash = common::Hash::keccak256(str_for_hash);
    ZJC_DEBUG("backup sharding: %u to_hash: %s, str for hash: %s",
        sharding_id,
        common::Encode::HexEncode(*to_hash).c_str(),
        common::Encode::HexEncode(str_for_hash).c_str());
    to_tx.set_heights_hash(*to_hash);
    *to_tx.mutable_to_heights() = leader_to_heights;
    auto val = to_tx.SerializeAsString();
    prefix_db_->SaveTemporaryKv(*to_hash, val);
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace zjchain
