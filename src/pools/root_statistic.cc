#include "pools/root_statistic.h"

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
    "127a252b30589b8ed984cf437c475b079d0597fc6251ec6570e95a681ffa9fe1");

void RootStatistic::Init() {
    if (pools_mgr_ != nullptr) {
        LoadLatestHeights();
    }
}

void RootStatistic::OnNewBlock(const block::protobuf::Block& block) {
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        if (block.tx_list(i).status() != consensus::kConsensusSuccess) {
            continue;
        }

        if (block.tx_list(i).step() == pools::protobuf::kStatistic) {
            HandleStatisticBlock(block, block.tx_list(i));
            break;
        }
    }
}

void RootStatistic::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    assert(block.pool_index() == 0);
    auto shard_iter = handled_sharding_statistic_map_.find(block.network_id());
    if (shard_iter == handled_sharding_statistic_map_.end()) {
        handled_sharding_statistic_map_[block.network_id()] = std::set<uint64_t>();
        shard_iter = handled_sharding_statistic_map_.find(block.network_id());
    }

    if (shard_iter->second.find(block.height()) != shard_iter->second.end()) {
        return;
    }

    // members
    pools::protobuf::ElectStatistic elect_statistic;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kShardStatistic) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                return;
            }

            if (!elect_statistic.ParseFromString(val)) {
                return;
            }

            break;
        }
    }

    if (elect_statistic.statistics_size() <= 0) {
        return;
    }

    for (int32_t i = 0; i < elect_statistic.statistics_size(); ++i) {
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            elect_statistic.statistics(i).elect_height(),
            block.network_id(),
            nullptr,
            nullptr);
        if (members == nullptr) {
            return;
        }

        if (members->size() != elect_statistic.statistics(i).tx_count_size()) {
            assert(false);
            return;
        }

        for (int32_t member_idx = 0; member_idx < members->size(); ++member_idx) {
            auto& id = (*members)[member_idx]->id;
            auto iter = node_tx_count_map_.find(id);
            if (iter == node_tx_count_map_.end()) {
                node_tx_count_map_[id] = 0;
                iter = node_tx_count_map_.find(id);
            }

            iter->second += elect_statistic.statistics(i).tx_count(member_idx);
        }
    }

    shard_iter->second.insert(block.height());
}

void RootStatistic::OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height) {
    auto iter = latest_elect_height_map_.find(sharding_id);
    if (iter != latest_elect_height_map_.end() && iter->second >= elect_height) {
        return;
    }

    latest_elect_height_map_[sharding_id] = elect_height;
}

void RootStatistic::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    if (latest_timeblock_tm_ >= lastest_time_block_tm) {
        return;
    }

    latest_timeblock_tm_ = lastest_time_block_tm;
    CreateStatisticTransaction(latest_time_block_height);
}

void RootStatistic::GetStatisticInfo(
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

}  // namespace pools

}  // namespace zjchain
