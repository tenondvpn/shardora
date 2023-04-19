#include "pools/shard_statistic.h"

#include "consensus/consensus_utils.h"
#include "common/global_info.h"
#include "common/encode.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/pools.pb.h"

namespace zjchain {

namespace pools {

static const std::string kShardFinalStaticPrefix = common::Encode::HexDecode(
    "027a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe7");

void ShardStatistic::OnNewBlock(const std::shared_ptr<block::protobuf::Block>& block_item) {
    const block::protobuf::Block& block = *block_item;
    if (block.network_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid!");
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
    
}

void ShardStatistic::HandleStatistic(const block::protobuf::Block& block) {
    if (block.electblock_height() == 0) {
        return;
    }

    ZJC_DEBUG("new block to statistic now: %lu", block.height());
    auto hiter = node_height_count_map_.find(block.height());
    if (hiter != node_height_count_map_.end()) {
        return;
    }

    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        common::GlobalInfo::Instance()->network_id(),
        &common_pk,
        &sec_key);
    uint32_t member_count = members->size();
    if (members == nullptr || block.leader_index() >= members->size() ||
            (*members)[block.leader_index()]->pool_index_mod_num < 0) {
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

    node_height_count_map_[block.height()] = statistic_info_ptr;
}

int ShardStatistic::LeaderCreateToHeights(pools::protobuf::ToTxHeights& to_heights) {
    bool valid = false;
    std::string heights;
    uint32_t max_pool = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool;
    }

    for (uint32_t i = 0; i < max_pool; ++i) {
        auto pool_iter = node_height_count_map_.find(i);
        auto r_height_iter = node_height_count_map_.rbegin();
        if (r_height_iter == node_height_count_map_.rend()) {
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

    ZJC_DEBUG("leader success create to heights: %s", heights.c_str());
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

int ShardStatistic::StatisticWithHeights(const pools::protobuf::ToTxHeights& leader_to_heights) {
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

    std::unordered_map<std::string, uint32_t> node_count_map;
    std::unordered_map<std::string, uint32_t> epoch_node_count_map;
    for (uint32_t pool_idx = 0; pool_idx < max_pool; ++pool_idx) {
        uint64_t min_height = 1;
        if (tx_heights_ptr_ != nullptr) {
            min_height = tx_heights_ptr_->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            ZJC_DEBUG("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx, max_height, pool_consensus_heihgts_[pool_idx]);
            return kPoolsError;
        }

        common::MembersPtr members = nullptr;
        uint64_t prev_height = 0;
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = node_height_count_map_.find(height);
            if (hiter == node_height_count_map_.end()) {
                return kPoolsError;
            }

            auto elect_height = hiter->second->elect_height;
            if (elect_height != prev_height || members == nullptr) {
                libff::alt_bn128_G2 common_pk;
                libff::alt_bn128_Fr sec_key;
                members = elect_mgr_->GetNetworkMembersWithHeight(
                    elect_height,
                    common::GlobalInfo::Instance()->network_id(),
                    &common_pk,
                    &sec_key);
            }

            assert(members != nullptr);
            if (members == nullptr) {
                assert(false);
                return kPoolsError;
            }

            // epoch credit statistic
            for (auto niter = hiter->second->node_tx_count_map.begin();
                    niter != hiter->second->node_tx_count_map.end(); ++niter) {
                if (elect_height == prev_elect_height_) {
                    auto tmp_iter = epoch_node_count_map.find(niter->first);
                    if (tmp_iter == epoch_node_count_map.end()) {
                        epoch_node_count_map[niter->first] = niter->second;
                    } else {
                        tmp_iter->second += niter->second;
                    }
                }

                auto tmp_iter = node_count_map.find(niter->first);
                if (tmp_iter == node_count_map.end()) {
                    node_count_map[niter->first] = niter->second;
                } else {
                    tmp_iter->second += niter->second;
                }
            }
        }
    }

    pools::protobuf::ElectStatistic elect_statistic;

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
