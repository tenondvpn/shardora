#include "pools/shard_statistic.h"

#include "consensus/consensus_utils.h"
#include "common/global_info.h"
#include "common/encode.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/pools.pb.h"
#include "pools/tx_pool_manager.h"

namespace zjchain {

namespace pools {

static const std::string kShardFinalStaticPrefix = common::Encode::HexDecode(
    "027a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe7");

void ShardStatistic::Init() {
    if (pools_mgr_ != nullptr) {
        LoadLatestHeights();
    }
}

void ShardStatistic::OnNewBlock(const block::protobuf::Block& block) {
    if (block.network_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid %u, %u",
            block.network_id(), common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (block.height() > pool_max_heihgts_[block.pool_index()]) {
        pool_max_heihgts_[block.pool_index()] = block.height();
    }

    if (pool_consensus_heihgts_[block.pool_index()] + 1 == block.height()) {
        ++pool_consensus_heihgts_[block.pool_index()];
        for (; pool_consensus_heihgts_[block.pool_index()] <= pool_max_heihgts_[block.pool_index()];
                ++pool_consensus_heihgts_[block.pool_index()]) {
            auto iter = added_heights_[block.pool_index()].find(
                    pool_consensus_heihgts_[block.pool_index()] + 1);
            if (iter == added_heights_[block.pool_index()].end()) {
                break;
            }

            added_heights_[block.pool_index()].erase(iter);
        }
    } else {
        added_heights_[block.pool_index()].insert(block.height());
    }

    const auto& tx_list = block.tx_list();
    if (tx_list.empty()) {
        ZJC_DEBUG("tx list empty!");
        return;
    }

    HandleStatistic(block);
    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() != consensus::kConsensusSuccess) {
            continue;
        }

        if (tx_list[i].step() == pools::protobuf::kStatistic) {
            HandleStatisticBlock(block, tx_list[i]);
        }
    }
}

void ShardStatistic::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kShardStatistic) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                ZJC_ERROR("get statistic val failed: %s",
                    common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                return;
            }

            pools::protobuf::ElectStatistic elect_statistic;
            if (!elect_statistic.ParseFromString(val)) {
                ZJC_ERROR("get statistic val failed: %s",
                    common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                return;
            }

            for (int32_t height_idx = 0;
                    height_idx < elect_statistic.heights().heights_size(); ++height_idx) {
                for (auto iter = node_height_count_map_[i].begin();
                        iter != node_height_count_map_[i].end();) {
                    if (iter->first <= elect_statistic.heights().heights(height_idx)) {
                        node_height_count_map_[i].erase(iter++);
                        continue;
                    }

                    break;
                }
            }

            prefix_db_->SaveStatisticLatestHeihgts(
                common::GlobalInfo::Instance()->network_id(),
                elect_statistic.heights());
            tx_heights_ptr_ = std::make_shared<pools::protobuf::ToTxHeights>(elect_statistic.heights());
            std::string init_consensus_height;
            for (uint32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
                init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
            }

            ZJC_DEBUG("success change min elect statistic heights: %s", init_consensus_height.c_str());
            break;
        }
    }
}

void ShardStatistic::HandleStatistic(const block::protobuf::Block& block) {
    if (block.electblock_height() == 0) {
        ZJC_DEBUG("block elect height zero error");
        return;
    }

    if (block.pool_index() >= common::kInvalidPoolIndex) {
        ZJC_ERROR("block po0l index error: %u", block.pool_index());
        assert(false);
        return;
    }

    auto hiter = node_height_count_map_[block.pool_index()].find(block.height());
    if (hiter != node_height_count_map_[block.pool_index()].end()) {
        return;
    }

    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        common::GlobalInfo::Instance()->network_id(),
        &common_pk,
        &sec_key);
    if (members == nullptr) {
        ZJC_WARN("get members failed, elect height: %lu, net: %u",
            block.electblock_height(), common::GlobalInfo::Instance()->network_id());
        assert(false);
        return;
    }

    uint32_t member_count = members->size();
    if (members == nullptr || block.leader_index() >= members->size() ||
            (*members)[block.leader_index()]->pool_index_mod_num < 0) {
        assert(false);
        return;
    }

    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < block.precommit_bitmap_size(); ++i) {
        bitmap_data.push_back(block.precommit_bitmap(i));
    }

    common::Bitmap final_bitmap(bitmap_data);
    uint32_t bit_size = final_bitmap.data().size() * 64;
    if (member_count > bit_size || member_count > common::kEachShardMaxNodeCount) {
        assert(false);
        return;
    }

    auto statistic_info_ptr = std::make_shared<HeightStatisticInfo>();
    statistic_info_ptr->elect_height = block.electblock_height();
    for (uint32_t i = 0; i < member_count; ++i) {
        if (!final_bitmap.Valid(i)) {
            continue;
        }

        auto& id = (*members)[i]->id;
        auto node_iter = statistic_info_ptr->node_tx_count_map.find(id);
        if (node_iter == statistic_info_ptr->node_tx_count_map.end()) {
            statistic_info_ptr->node_tx_count_map[id] = 0;
        }

        statistic_info_ptr->node_tx_count_map[id] += block.tx_list_size();
    }

    node_height_count_map_[block.pool_index()][block.height()] = statistic_info_ptr;
    ZJC_DEBUG("success add statistic block: net; %u, pool: %u, height: %lu",
        block.network_id(), block.pool_index(), block.height());
}

int ShardStatistic::LeaderCreateStatisticHeights(pools::protobuf::ToTxHeights& to_heights) {
    bool valid = false;
    std::string heights;
    uint32_t max_pool = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool;
    }

    for (uint32_t i = 0; i < max_pool; ++i) {
        auto pool_iter = node_height_count_map_[i].find(i);
        auto r_height_iter = node_height_count_map_[i].rbegin();
        if (r_height_iter == node_height_count_map_[i].rend()) {
            heights += std::to_string(0) + " ";
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

    ZJC_DEBUG("leader success create statistic heights: %s", heights.c_str());
    return kPoolsSuccess;
}

void ShardStatistic::OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height) {
    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    prev_elect_height_ = now_elect_height_;
    now_elect_height_ = elect_height;
    ZJC_INFO("new elect block: %lu, %lu", prev_elect_height_, now_elect_height_);
}

void ShardStatistic::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    if (latest_timeblock_tm_ >= lastest_time_block_tm) {
        return;
    }

    latest_timeblock_tm_ = lastest_time_block_tm;
    CreateStatisticTransaction(latest_time_block_height);
}

int ShardStatistic::StatisticWithHeights(
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* statistic_hash) {
    uint32_t pool_size = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++pool_size;
    }

    if (leader_to_heights.heights_size() != pool_size) {
        assert(false);
        return kPoolsError;
    }

    uint32_t max_pool = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool;
    }

    std::unordered_map<uint64_t, std::unordered_map<std::string, uint32_t>> height_node_count_map;
    for (uint32_t pool_idx = 0; pool_idx < max_pool; ++pool_idx) {
        uint64_t min_height = 1;
        if (tx_heights_ptr_ != nullptr) {
            min_height = tx_heights_ptr_->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            ZJC_WARN("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx, max_height, pool_consensus_heihgts_[pool_idx]);
            return kPoolsError;
        }

        uint64_t prev_height = 0;
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = node_height_count_map_[pool_idx].find(height);
            if (hiter == node_height_count_map_[pool_idx].end()) {
                ZJC_WARN("statistic get height failed, pool: %u, height: %lu", pool_idx, height);
                return kPoolsError;
            }

            auto elect_height = hiter->second->elect_height;
            auto iter = height_node_count_map.find(elect_height);
            if (iter == height_node_count_map.end()) {
                height_node_count_map[elect_height] = std::unordered_map<std::string, uint32_t>();
            }

            auto& node_count_map = height_node_count_map[elect_height];
            // epoch credit statistic
            for (auto niter = hiter->second->node_tx_count_map.begin();
                    niter != hiter->second->node_tx_count_map.end(); ++niter) {
                auto tmp_iter = node_count_map.find(niter->first);
                if (tmp_iter == node_count_map.end()) {
                    node_count_map[niter->first] = niter->second;
                } else {
                    tmp_iter->second += niter->second;
                }
            }
        }
    }

    std::string str_for_hash;
    pools::protobuf::ElectStatistic elect_statistic;
    for (auto hiter = height_node_count_map.begin(); hiter != height_node_count_map.end(); ++hiter) {
        auto& node_count_map = hiter->second;
        auto& statistic_item = *elect_statistic.add_statistics();
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            hiter->first,
            common::GlobalInfo::Instance()->network_id(),
            nullptr,
            nullptr);
        str_for_hash.reserve(1024 * 1024);
        for (uint32_t midx = 0; midx < members->size(); ++midx) {
            auto& id = (*members)[midx]->id;
            auto iter = node_count_map.find(id);
            uint32_t tx_count = 0;
            if (iter != node_count_map.end()) {
                tx_count = iter->second;
            }

            statistic_item.add_tx_count(tx_count);
            str_for_hash.append((char*)&tx_count, sizeof(tx_count));
        }

        statistic_item.set_elect_height(hiter->first);
        str_for_hash.append((char*)&hiter->first, sizeof(hiter->first));
    }

    *statistic_hash = common::Hash::keccak256(str_for_hash);
    *elect_statistic.mutable_heights() = leader_to_heights;
    prefix_db_->SaveTemporaryKv(*statistic_hash, elect_statistic.SerializeAsString());
    ZJC_DEBUG("success create statistic message.");
    return kPoolsSuccess;
}

void ShardStatistic::LoadLatestHeights() {
    tx_heights_ptr_ = std::make_shared<pools::protobuf::ToTxHeights>();
    auto& to_heights = *tx_heights_ptr_;
    if (!prefix_db_->GetStatisticLatestHeihgts(
            common::GlobalInfo::Instance()->network_id(),
            &to_heights)) {
        ZJC_FATAL("load init statistic heights failed!");
    }

    uint32_t max_pool_index = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool_index;
    }

    auto& this_net_heights = tx_heights_ptr_->heights();
    for (int32_t i = 0; i < this_net_heights.size(); ++i) {
        pool_consensus_heihgts_[i] = this_net_heights[i];
    }

    for (uint32_t i = 0; i < max_pool_index; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        bool consensus_stop = false;
        for (uint64_t height = pool_consensus_heihgts_[i];
                height <= pool_latest_height; ++height) {
            block::protobuf::Block block;
            if (!prefix_db_->GetBlockWithHeight(
                    common::GlobalInfo::Instance()->network_id(), i, height, &block)) {
                consensus_stop = true;
            } else {
                OnNewBlock(block);
            }

            if (!consensus_stop) {
                pool_consensus_heihgts_[i] = height;
            }
        }
    }

    std::string init_consensus_height;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
    }

    ZJC_DEBUG("init success change min elect statistic heights: %s", init_consensus_height.c_str());
}

void ShardStatistic::NormalizePoints(
        uint64_t elect_height,
        std::unordered_map<int32_t, std::shared_ptr<common::Point>>& leader_lof_map) {
    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        elect_height,
        common::GlobalInfo::Instance()->network_id(),
        &common_pk,
        &sec_key);
    if (members == nullptr) {
        return;
    }

    auto leader_count = elect_mgr_->GetNetworkLeaderCount(
        common::GlobalInfo::Instance()->network_id());
    if (leader_count <= 0) {
        ZJC_ERROR("leader_count invalid[%d] net: %d.",
            leader_count, common::GlobalInfo::Instance()->network_id());
        return;
    }

    for (auto iter = leader_lof_map.begin(); iter != leader_lof_map.end(); ++iter) {
        if ((*members)[iter->first]->pool_index_mod_num < 0) {
            continue;
        }

        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto need_mod_index = i % leader_count;
            if ((int32_t)need_mod_index == (*members)[iter->first]->pool_index_mod_num) {
//                 iter->second->AddPoolTxCount(tx_counts->pool_tx_counts[i]);
            }
        }
    }

    int32_t max_count = 0;
    for (auto iter = leader_lof_map.begin(); iter != leader_lof_map.end(); ++iter) {
        if (max_count < iter->second->GetPooTxCount()) {
            max_count = iter->second->GetPooTxCount();
        }
    }

    for (auto iter = leader_lof_map.begin(); iter != leader_lof_map.end();) {
        if (iter->second->GetPooTxCount() <= 0) {
            leader_lof_map.erase(iter++);
            continue;
        }

        for (int32_t i = 0; i < iter->second->GetDimension(); ++i) {
            (*iter->second)[i] = (*iter->second)[i] * max_count / iter->second->GetPooTxCount();
        }

        ++iter;
    }
}

void ShardStatistic::GetStatisticInfo(
        uint64_t timeblock_height,
        block::protobuf::StatisticInfo* statistic_info) {
//     std::shared_ptr<StatisticItem> statistic_ptr = nullptr;
//     for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
//         if (statistic_items_[i]->tmblock_height == timeblock_height) {
//             statistic_ptr = statistic_items_[i];
//             break;
//         }
//     }
// 
//     if (statistic_ptr == nullptr) {
//         return;
//     }
// 
//     statistic_info->set_timeblock_height(statistic_ptr->tmblock_height);
//     statistic_info->set_all_tx_count(statistic_ptr->all_tx_count);
//     StatisticElectItemPtr elect_item_ptr = nullptr;
//     for (uint32_t elect_idx = 0; elect_idx < kStatisticMaxCount; ++elect_idx) {
//         auto elect_height = statistic_ptr->elect_items[elect_idx]->elect_height;
//         if (elect_height == 0) {
//             continue;
//         }
// 
//         elect_item_ptr = statistic_ptr->elect_items[elect_idx];
//         auto elect_st = statistic_info->add_elect_statistic();
//         auto leader_count = elect_item_ptr->leader_lof_map.size();
//         if (leader_count >= kLofMaxNodes) {
//             std::unordered_map<int32_t, std::shared_ptr<common::Point>> leader_lof_map;
//             {
//                 std::lock_guard<std::mutex> g(elect_item_ptr->leader_lof_map_mutex);
//                 leader_lof_map = elect_item_ptr->leader_lof_map;
//             }
// 
//             NormalizePoints(elect_item_ptr->elect_height, leader_lof_map);
//             if (leader_lof_map.size() >= kLofMaxNodes) {
//                 std::vector<common::Point> points;
//                 for (auto iter = leader_lof_map.begin();
//                         iter != leader_lof_map.end(); ++iter) {
//                     points.push_back(*iter->second);
//                 }
// 
//                 common::Lof lof(points);
//                 auto out = lof.GetOutliers(kLofRation);
//                 int32_t weedout_count = leader_count / 10 + 1;
//                 for (auto iter = out.begin(); iter != out.end(); ++iter) {
//                     if (elect_st->lof_leaders_size() >= weedout_count || (*iter).second <= 2.0) {
//                         break;
//                     }
// 
//                     elect_st->add_lof_leaders((*iter).first);
//                 }
//             }
//         }
// 
//         elect_st->set_elect_height(elect_item_ptr->elect_height);
//         auto member_count = elect_mgr_->GetMemberCountWithHeight(
//             elect_st->elect_height(),
//             common::GlobalInfo::Instance()->network_id());
//         for (uint32_t m = 0; m < member_count; ++m) {
//             elect_st->add_succ_tx_count(elect_item_ptr->succ_tx_count[m]);
//         }
//     }
}

void ShardStatistic::CreateStatisticTransaction(uint64_t timeblock_height) {
    if (common::GlobalInfo::Instance()->network_id() < network::kRootCongressNetworkId ||
            common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        return;
    }

    int32_t pool_idx = 0;
    pools::protobuf::TxMessage tx_info;
    tx_info.set_step(pools::protobuf::kConsensusFinalStatistic);
//     tx_info.set_from(account_mgr_->GetPoolBaseAddr(pool_idx));
//     if (tx_info.from().empty()) {
//         return;
//     }

    tx_info.set_gid(common::Hash::Hash256(
        kShardFinalStaticPrefix + "_" +
        std::to_string(common::GlobalInfo::Instance()->network_id()) + "_" +
        std::to_string(latest_timeblock_tm_)));
//     BLOCK_INFO("create new final statistic time stamp: %lu",
//         tmblock::TimeBlockManager::Instance()->LatestTimestamp());
    tx_info.set_gas_limit(0llu);
    tx_info.set_amount(0);
    tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
    tx_info.set_key(protos::kAttrTimerBlockHeight);
    tx_info.set_value(std::to_string(timeblock_height));
//     if (bft::DispatchPool::Instance()->Dispatch(tx_info) != bft::kBftSuccess) {
//         ZJC_ERROR("CreateStatisticTransaction dispatch pool failed!");
//     }
}

}  // namespace pools

}  // namespace zjchain
