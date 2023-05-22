#include "pools/shard_statistic.h"

#include "consensus/consensus_utils.h"
#include "common/encode.h"
#include "common/ip.h"
#include "common/global_info.h"
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
    if (block.network_id() != common::GlobalInfo::Instance()->network_id() &&
            block.network_id() + network::kConsensusWaitingShardOffset != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid %u, %u",
            block.network_id(), common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (block.height() > pool_max_heihgts_[block.pool_index()]) {
        pool_max_heihgts_[block.pool_index()] = block.height();
    }

    const auto& tx_list = block.tx_list();
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

    if (block.height() > pool_consensus_heihgts_[block.pool_index()]) {
        added_heights_[block.pool_index()].insert(block.height());
    }

    if (pool_consensus_heihgts_[block.pool_index()] + 1 == block.height()) {
        while (pool_consensus_heihgts_[block.pool_index()] <=
                pool_max_heihgts_[block.pool_index()]) {
            auto iter = added_heights_[block.pool_index()].find(
                    pool_consensus_heihgts_[block.pool_index()] + 1);
            if (iter == added_heights_[block.pool_index()].end()) {
                ZJC_DEBUG("failed find pool: %u, height: %lu, max height: %lu, cons height: %lu",
                    block.pool_index(), pool_consensus_heihgts_[block.pool_index()] + 1,
                    pool_max_heihgts_[block.pool_index()], pool_consensus_heihgts_[block.pool_index()]);
                break;
            }

            ++pool_consensus_heihgts_[block.pool_index()];
            ZJC_DEBUG("success find pool: %u, height: %lu, max height: %lu, cons height: %lu",
                block.pool_index(), pool_consensus_heihgts_[block.pool_index()] + 1,
                pool_max_heihgts_[block.pool_index()], pool_consensus_heihgts_[block.pool_index()]);
            added_heights_[block.pool_index()].erase(iter);
        }
    }

    if (tx_list.empty()) {
        ZJC_DEBUG("tx list empty!");
        return;
    }

    HandleStatistic(block);
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

            if (tx_heights_ptr_ != nullptr) {
                if (tx_heights_ptr_->heights_size() == elect_statistic.heights().heights_size()) {
                    for (int32_t i = 0; i < elect_statistic.heights().heights_size(); ++i) {
                        if (tx_heights_ptr_->heights(i) > elect_statistic.heights().heights(i)) {
                            std::string init_consensus_height;
                            for (uint32_t i = 0; i < elect_statistic.heights().heights_size(); ++i) {
                                init_consensus_height += std::to_string(elect_statistic.heights().heights(i)) + " ";
                            }

                            std::string src_init_consensus_height;
                            for (uint32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
                                src_init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
                            }

                            ZJC_INFO("latest statistic block coming, ignore it. %s, %s",
                                src_init_consensus_height.c_str(), init_consensus_height.c_str());
                            return;
                        }
                    }
                } else {
                    ZJC_WARN("statistic heights size not equal: %u, %u",
                        tx_heights_ptr_->heights_size(), elect_statistic.heights().heights_size());
                }
            }

            for (int32_t height_idx = 0;
                    height_idx < elect_statistic.heights().heights_size(); ++height_idx) {
                if (elect_statistic.heights().heights(height_idx) > pool_consensus_heihgts_[height_idx]) {
                    pool_consensus_heihgts_[height_idx] = elect_statistic.heights().heights(height_idx);
                    ZJC_DEBUG("set new cons failed find pool: %u, height: %lu, max height: %lu, cons height: %lu",
                        height_idx,
                        pool_consensus_heihgts_[height_idx] + 1,
                        pool_max_heihgts_[height_idx], pool_consensus_heihgts_[height_idx]);
                    while (pool_consensus_heihgts_[height_idx] <= pool_max_heihgts_[height_idx]) {
                        auto iter = added_heights_[height_idx].find(
                            pool_consensus_heihgts_[height_idx] + 1);
                        if (iter == added_heights_[height_idx].end()) {
                            ZJC_DEBUG("failed find pool: %u, height: %lu, max height: %lu, cons height: %lu",
                                height_idx,
                                pool_consensus_heihgts_[height_idx] + 1,
                                pool_max_heihgts_[height_idx],
                                pool_consensus_heihgts_[height_idx]);
                            break;
                        }

                        ++pool_consensus_heihgts_[height_idx];
                        added_heights_[height_idx].erase(iter);
                    }
                }

                for (auto iter = node_height_count_map_[height_idx].begin();
                        iter != node_height_count_map_[height_idx].end();) {
                    if (iter->first <= elect_statistic.heights().heights(height_idx)) {
                        auto cross_iter = cross_shard_map_[height_idx].find(iter->first);
                        if (cross_iter != cross_shard_map_[height_idx].end()) {
                            cross_shard_map_[height_idx].erase(cross_iter);
                        }

                        node_height_count_map_[height_idx].erase(iter++);
                        ZJC_DEBUG("erase handled height pool: %u, height: %lu, handled_height: %lu",
                            height_idx, iter->first, elect_statistic.heights().heights(height_idx));
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

void ShardStatistic::HandleCrossShard(
        bool is_root,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    switch (tx.step()) {
    case pools::protobuf::kNormalTo: {
        if (!is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kNormalTos) {
                    std::string val;
                    if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                        return;
                    }

                    pools::protobuf::ToTxMessage to_tx;
                    if (!to_tx.ParseFromString(val)) {
                        return;
                    }

                    cross_shard_map_[block.pool_index()][block.height()] =
                        to_tx.to_heights().sharding_id();
                    break;
                }
            }
        }

        break;
    }
    case pools::protobuf::kStatistic: {
        if (!is_root) {
            cross_shard_map_[block.pool_index()][block.height()] = network::kRootCongressNetworkId;
        }
        
        break;
    }
    case pools::protobuf::kCreateLibrary: {
        if (is_root) {
            cross_shard_map_[block.pool_index()][block.height()] = network::kNodeNetworkId;
        } else {
            cross_shard_map_[block.pool_index()][block.height()] = network::kRootCongressNetworkId;
        }

        break;
    }
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kConsensusRootElectShard:
    case pools::protobuf::kConsensusRootTimeBlock: {
        if (!is_root) {
            return;
        }

        cross_shard_map_[block.pool_index()][block.height()] = network::kNodeNetworkId;
        break;
    }
    default:
        break;
    }
}

void ShardStatistic::HandleStatistic(const block::protobuf::Block& block) {
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
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

    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        common::GlobalInfo::Instance()->network_id(),
        nullptr,
        nullptr);
    if (members == nullptr) {
        ZJC_WARN("get members failed, elect height: %lu, net: %u",
            block.electblock_height(), common::GlobalInfo::Instance()->network_id());
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
    statistic_info_ptr->all_gas_amount = 0;
    for (uint32_t i = 0; i < member_count; ++i) {
        auto& id = (*members)[i]->id;
        auto node_iter = statistic_info_ptr->node_tx_count_map.find(id);
        if (node_iter == statistic_info_ptr->node_tx_count_map.end()) {
            statistic_info_ptr->node_tx_count_map[id] = { 0, i, block.leader_index() };
        }

        if (!final_bitmap.Valid(i)) {
            continue;
        }

        statistic_info_ptr->node_tx_count_map[id].tx_count += block.tx_list_size();
    }

    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        HandleCrossShard(is_root, block, block.tx_list(i));
        if (block.tx_list(i).step() == pools::protobuf::kNormalFrom ||
                block.tx_list(i).step() == pools::protobuf::kContractUserCreateCall ||
                block.tx_list(i).step() == pools::protobuf::kContractExcute ||
                block.tx_list(i).step() == pools::protobuf::kJoinElect ||
                block.tx_list(i).step() == pools::protobuf::kContractGasPrepayment ||
                block.tx_list(i).step() == pools::protobuf::kContractUserCall) {
            statistic_info_ptr->all_gas_amount += block.tx_list(i).gas_price() * block.tx_list(i).gas_used();
        }

        if (block.tx_list(i).step() == pools::protobuf::kJoinElect) {
            for (int32_t storage_idx = 0; storage_idx < block.tx_list(i).storages_size(); ++storage_idx) {
                if (block.tx_list(i).storages(storage_idx).key() == protos::kElectNodeStoke) {
                    uint64_t* tmp_stoke = (uint64_t*)block.tx_list(i).storages(storage_idx).val_hash().c_str();
                    statistic_info_ptr->node_stoke_map[block.tx_list(i).from()] = tmp_stoke[0];
                }

                if (block.tx_list(i).storages(storage_idx).key() == protos::kElectJoinShard) {
                    uint32_t* shard = (uint32_t*)block.tx_list(i).storages(storage_idx).val_hash().c_str();
                    statistic_info_ptr->node_shard_map[block.tx_list(i).from()] = shard[0];
                    ZJC_DEBUG("kJoinElect add new elect node: %s, shard: %u",
                        common::Encode::HexEncode(block.tx_list(i).from()).c_str(), shard[0]);
                }
            }
        }

        if (block.tx_list(i).step() == pools::protobuf::kConsensusRootElectShard && is_root) {
            for (int32_t storage_idx = 0; storage_idx < block.tx_list(i).storages_size(); ++storage_idx) {
                if (block.tx_list(i).storages(storage_idx).key() == protos::kElectNodeAttrElectBlock) {
                    std::string val;
                    if (!prefix_db_->GetTemporaryKv(block.tx_list(i).storages(storage_idx).val_hash(), &val)) {
                        break;
                    }

                    elect::protobuf::ElectBlock elect_block;
                    if (!elect_block.ParseFromString(val)) {
                        break;
                    }

                    if (elect_block.gas_for_root() > 0) {
                        statistic_info_ptr->all_gas_for_root += elect_block.gas_for_root();
                    }
                }
                if (block.tx_list(i).storages(storage_idx).key() == protos::kShardElection) {
                    uint64_t* tmp = (uint64_t*)block.tx_list(i).storages(storage_idx).val_hash().c_str();
                    pools::protobuf::ElectStatistic elect_statistic;
                    if (!prefix_db_->GetStatisticedShardingHeight(
                            tmp[0],
                            tmp[1],
                            &elect_statistic)) {
                        ZJC_WARN("get statistic elect statistic failed! net: %u, height: %lu",
                            tmp[0],
                            tmp[1]);
                        break;
                    }

                    for (int32_t node_idx = 0; node_idx < elect_statistic.join_elect_nodes_size(); ++node_idx) {
                        if (elect_statistic.join_elect_nodes(i).shard() == network::kRootCongressNetworkId) {
                            statistic_info_ptr->node_stoke_map[elect_statistic.join_elect_nodes(i).pubkey()] =
                                elect_statistic.join_elect_nodes(i).stoke();
                            ZJC_DEBUG("root sharding kJoinElect add new elect node: %s, stoke: %lu, elect height: %lu",
                                common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
                                elect_statistic.join_elect_nodes(i).stoke(),
                                block.electblock_height());
                            statistic_info_ptr->node_shard_map[elect_statistic.join_elect_nodes(i).pubkey()] = network::kRootCongressNetworkId;
                        }
                    }
                }
            }
        }
    }

    node_height_count_map_[block.pool_index()][block.height()] = statistic_info_ptr;
    ZJC_DEBUG("success add statistic block: net; %u, pool: %u, height: %lu,"
        "cons height: %lu, elect height: %lu",
        block.network_id(), block.pool_index(), block.height(),
        pool_consensus_heihgts_[block.pool_index()], block.electblock_height());
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
            heights += std::to_string(tx_heights_ptr_->heights(i)) + " ";
            to_heights.add_heights(tx_heights_ptr_->heights(i));
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

void ShardStatistic::OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t prepare_elect_height,
        uint64_t elect_height) {
    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    prev_elect_height_ = now_elect_height_;
    now_elect_height_ = elect_height;
    prepare_elect_height_ = prepare_elect_height;
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
    now_vss_random_ = vss_random;
}

int ShardStatistic::StatisticWithHeights(
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* statistic_hash,
        std::string* cross_hash) {
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    uint32_t pool_size = common::kImmutablePoolSize;
    if (is_root) {
        ++pool_size;
    }

    if (leader_to_heights.heights_size() != pool_size) {
        ZJC_DEBUG("pool size error: %d, %d, local sharding: %d",
            leader_to_heights.heights_size(), pool_size,
            common::GlobalInfo::Instance()->network_id());
        assert(false);
        return kPoolsError;
    }

    uint32_t max_pool = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool;
    }

    if (tx_heights_ptr_ == nullptr) {
        return kPoolsError;
    }

    if (tx_heights_ptr_->heights_size() < max_pool) {
        return kPoolsError;
    }

    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> height_node_count_map;
    std::map<uint64_t, std::unordered_map<std::string, uint64_t>> join_elect_stoke_map;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> join_elect_shard_map;
    auto prepare_members = elect_mgr_->GetNetworkMembersWithHeight(
        prepare_elect_height_,
        common::GlobalInfo::Instance()->network_id(),
        nullptr,
        nullptr);
    auto now_elect_members = elect_mgr_->GetNetworkMembersWithHeight(
        now_elect_height_,
        common::GlobalInfo::Instance()->network_id(),
        nullptr,
        nullptr);
    if (now_elect_members == nullptr) {
        return kPoolsError;
    }

    std::unordered_set<std::string> added_id_set;
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
            added_id_set.insert((*now_elect_members)[i]->id);
            added_id_set.insert((*now_elect_members)[i]->pubkey);
        }
    }

    std::unordered_map<uint32_t, common::Point> lof_map;
    uint64_t all_gas_amount = 0;
    uint64_t root_all_gas_amount = 0;
    std::string cross_string_for_hash;
    pools::protobuf::CrossShardStatistic cross_statistic;
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

            auto cross_iter = cross_shard_map_[pool_idx].find(height);
            if (cross_iter != cross_shard_map_[pool_idx].end()) {
                auto cross_item = cross_statistic.add_crosses();
                uint32_t src_shard = common::GlobalInfo::Instance()->network_id();
                if (common::GlobalInfo::Instance()->network_id() >=
                        network::kConsensusShardEndNetworkId) {
                    src_shard -= network::kConsensusWaitingShardOffset;
                }

                cross_item->set_src_shard(src_shard);
                cross_item->set_src_pool(pool_idx);
                cross_item->set_height(height);
                cross_item->set_des_shard(cross_iter->second);
                cross_string_for_hash.append((char*)&src_shard, sizeof(src_shard));
                cross_string_for_hash.append((char*)&pool_idx, sizeof(pool_idx));
                cross_string_for_hash.append((char*)&height, sizeof(height));
                cross_string_for_hash.append((char*)&cross_iter->second, sizeof(cross_iter->second));
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
                    node_count_map[niter->first] = niter->second.tx_count;
                } else {
                    tmp_iter->second += niter->second.tx_count;
                }

                if (elect_height == now_elect_height_) {
                    auto liter = lof_map.find(niter->second.leader_index);
                    if (liter == lof_map.end()) {
                        lof_map[niter->second.leader_index] = common::Point(
                            now_elect_members->size(),
                            (*now_elect_members)[niter->second.leader_index]->pool_index_mod_num,
                            niter->second.leader_index);
                    }

                    lof_map[niter->second.leader_index][niter->second.member_index] +=
                        niter->second.tx_count;
                }
            }

            if (!hiter->second->node_stoke_map.empty() && !hiter->second->node_shard_map.empty()) {
                auto eiter = join_elect_stoke_map.find(elect_height);
                if (eiter == join_elect_stoke_map.end()) {
                    join_elect_stoke_map[elect_height] = std::unordered_map<std::string, uint64_t>();
                }

                auto& elect_stoke_map = join_elect_stoke_map[elect_height];
                for (auto elect_iter = hiter->second->node_stoke_map.begin();
                        elect_iter != hiter->second->node_stoke_map.end(); ++elect_iter) {
                    elect_stoke_map[elect_iter->first] = elect_iter->second;
                    ZJC_DEBUG("kJoinElect add new elect node elect_height: %lu", elect_height);
                }

                auto shard_iter = join_elect_shard_map.find(elect_height);
                if (shard_iter == join_elect_shard_map.end()) {
                    join_elect_shard_map[elect_height] = std::unordered_map<std::string, uint32_t>();
                }

                auto& elect_shard_map = join_elect_shard_map[elect_height];
                for (auto tmp_shard_iter = hiter->second->node_shard_map.begin();
                        tmp_shard_iter != hiter->second->node_shard_map.end(); ++tmp_shard_iter) {
                    elect_shard_map[tmp_shard_iter->first] = tmp_shard_iter->second;
                    ZJC_DEBUG("kJoinElect add new elect node shard: %lu", tmp_shard_iter->second);
                }
            }

            all_gas_amount += hiter->second->all_gas_amount;
            root_all_gas_amount += hiter->second->all_gas_for_root;
        }
    }

    auto r_eiter = join_elect_stoke_map.rbegin();
    auto r_siter = join_elect_shard_map.rbegin();
    std::vector<std::string> elect_nodes;
    if (r_eiter != join_elect_stoke_map.rend() &&
            r_siter != join_elect_shard_map.rend() &&
            r_eiter->first == r_siter->first) {
        for (auto iter = r_eiter->second.begin(); iter != r_eiter->second.end(); ++iter) {
            auto shard_iter = r_siter->second.find(iter->first);
            if (shard_iter == r_siter->second.end()) {
                continue;
            }

            auto inc_iter = added_id_set.find(iter->first);
            if (inc_iter != added_id_set.end()) {
                continue;
            }

            elect_nodes.push_back(iter->first);
            added_id_set.insert(iter->first);
        }
    }
    
    std::string str_for_hash;
    std::string debug_for_str;
    pools::protobuf::ElectStatistic elect_statistic;
    debug_for_str += "tx_count: ";
    auto r_hiter = height_node_count_map.rbegin();
    if (r_hiter == height_node_count_map.rend() || r_hiter->first < now_elect_height_) {
        height_node_count_map[now_elect_height_] = std::unordered_map<std::string, uint32_t>();
        auto& node_count_map = height_node_count_map[now_elect_height_];
        for (int32_t i = 0; i < now_elect_members->size(); ++i) {
            node_count_map[(*now_elect_members)[i]->id] = 0;
        }
    }

    for (auto hiter = height_node_count_map.begin();
            hiter != height_node_count_map.end(); ++hiter) {
        auto& node_count_map = hiter->second;
        auto& statistic_item = *elect_statistic.add_statistics();
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            hiter->first,
            common::GlobalInfo::Instance()->network_id(),
            nullptr,
            nullptr);
        ZJC_DEBUG("now get network members: %u, size: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), members->size(), hiter->first);
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
            debug_for_str += std::to_string(tx_count) + ",";
            uint64_t stoke = 0;
            if (!is_root) {
                prefix_db_->GetElectNodeMinStoke(
                    common::GlobalInfo::Instance()->network_id(), id, &stoke);
            }

            statistic_item.add_stokes(stoke);
            auto area_point = statistic_item.add_area_point();
            auto ip_int = (*members)[midx]->public_ip;
            area_point->set_x(0);
            area_point->set_y(0);
            if (ip_int != 0) {
                auto ip = common::Uint32ToIp(ip_int);
                ZJC_DEBUG("add ip %s, %u", ip.c_str(), ip_int);
                float x = 0.0;
                float y = 0.0;
                if (common::Ip::Instance()->GetIpLocation(ip, &x, &y) == 0) {
                    area_point->set_x(static_cast<int32_t>(x * 100));
                    area_point->set_y(static_cast<int32_t>(y * 100));
                }
            }

            int32_t x1 = area_point->x();
            int32_t y1 = area_point->y();
            str_for_hash.append((char*)&x1, sizeof(x1));
            str_for_hash.append((char*)&y1, sizeof(y1));
            debug_for_str += "xy: " + std::to_string(x1) + ":" + std::to_string(y1) + ",";
        }

        statistic_item.set_elect_height(hiter->first);
        str_for_hash.append((char*)&hiter->first, sizeof(hiter->first));
        debug_for_str += std::to_string(hiter->first) + ",";
    }

    debug_for_str += "stoke: ";
    for (int32_t i = 0; i < elect_nodes.size() && i < kWaitingElectNodesMaxCount; ++i) {
        auto join_elect_node = elect_statistic.add_join_elect_nodes();
        std::string pubkey = elect_nodes[i];
        if (pubkey.size() == security::kUnicastAddressLength) {
            if (!prefix_db_->GetAddressPubkey(elect_nodes[i], &pubkey)) {
                continue;
            }
        }

        auto addr_info = prefix_db_->GetAddressInfo(secptr_->GetAddress(pubkey));
        if (addr_info == nullptr ||
                addr_info->elect_pos() < 0 ||
                addr_info->elect_pos() >= common::GlobalInfo::Instance()->each_shard_max_members()) {
            continue;
        }

        join_elect_node->set_pubkey(pubkey);
        auto iter = r_eiter->second.find(elect_nodes[i]);
        auto shard_iter = r_siter->second.find(elect_nodes[i]);
        join_elect_node->set_stoke(iter->second);
        join_elect_node->set_shard(shard_iter->second);
        join_elect_node->set_elect_pos(addr_info->elect_pos());
        str_for_hash.append(elect_nodes[i]);
        str_for_hash.append((char*)&iter->second, sizeof(iter->second));
        str_for_hash.append((char*)&shard_iter->second, sizeof(shard_iter->second));
        debug_for_str += common::Encode::HexEncode(elect_nodes[i]) + "," + std::to_string(iter->second) + "," + std::to_string(shard_iter->second) + ",";
        ZJC_DEBUG("add new elect node: %s, stoke: %lu, shard: %u", common::Encode::HexEncode(pubkey).c_str(), iter->second, shard_iter->second);
    }

    if (prepare_members != nullptr) {
        for (int32_t i = 0; i < prepare_members->size(); ++i) {
            auto inc_iter = added_id_set.find((*prepare_members)[i]->pubkey);
            if (inc_iter != added_id_set.end()) {
                continue;
            }

            auto join_elect_node = elect_statistic.add_join_elect_nodes();
            join_elect_node->set_pubkey((*prepare_members)[i]->pubkey);
            auto addr_info = prefix_db_->GetAddressInfo(secptr_->GetAddress((*prepare_members)[i]->pubkey));
            if (addr_info == nullptr ||
                    addr_info->elect_pos() < 0 ||
                    addr_info->elect_pos() >= common::GlobalInfo::Instance()->each_shard_max_members()) {
                continue;
            }

            uint64_t stoke = 0;
            join_elect_node->set_elect_pos(addr_info->elect_pos());
            join_elect_node->set_stoke(stoke);
            uint32_t shard = common::GlobalInfo::Instance()->network_id();
            join_elect_node->set_shard(shard);
            str_for_hash.append((*prepare_members)[i]->pubkey);
            str_for_hash.append((char*)&stoke, sizeof(stoke));
            str_for_hash.append((char*)&shard, sizeof(shard));
            debug_for_str += common::Encode::HexEncode((*prepare_members)[i]->pubkey) + "," +
                std::to_string(stoke) + "," + std::to_string(shard) + ",";
        }
    }

    if (prepare_members != nullptr)
    ZJC_DEBUG("kJoinElect add new elect node now elect_height: %lu, prepare elect height: %lu, %d, %d,"
        "new nodes size: %u, now members size: %u, prepare members size: %u",
        now_elect_height_,
        prepare_elect_height_,
        (r_eiter != join_elect_stoke_map.rend()),
        (r_siter != join_elect_shard_map.rend()),
        elect_statistic.join_elect_nodes_size(),
        now_elect_members->size(),
        prepare_members->size());

    NormalizeLofMap(lof_map);
    if (!lof_map.empty()) {
        std::vector<common::Point> points;
        for (auto iter = lof_map.begin(); iter != lof_map.end(); ++iter) {
            points.push_back(iter->second);
        }

        auto leader_count = elect_mgr_->GetNetworkLeaderCount(
            common::GlobalInfo::Instance()->network_id());
        common::Lof lof(points);
        auto out = lof.GetOutliers(kLofRation);
        int32_t weedout_count = leader_count / 10 + 1;
        for (auto iter = out.begin(); iter != out.end(); ++iter) {
            if (elect_statistic.lof_leaders_size() >= weedout_count || (*iter).second <= 2.0) {
                break;
            }

            elect_statistic.add_lof_leaders((*iter).first);
            str_for_hash.append((char*)&(*iter).first, sizeof((*iter).first));
            debug_for_str += std::to_string((*iter).first) + ",";
        }
    }

    if (is_root) {
        elect_statistic.set_gas_amount(root_all_gas_amount);
    } else {
        elect_statistic.set_gas_amount(all_gas_amount);
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    elect_statistic.set_sharding_id(net_id);
    str_for_hash.append((char*)&all_gas_amount, sizeof(all_gas_amount));
    str_for_hash.append((char*)&net_id, sizeof(net_id));
    debug_for_str += std::to_string(all_gas_amount) + ",";
    debug_for_str += std::to_string(net_id) + ",";
    *statistic_hash = common::Hash::keccak256(str_for_hash);
    std::string heights;
    for (uint32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        heights += std::to_string(leader_to_heights.heights(i)) + " ";
    }

    *elect_statistic.mutable_heights() = leader_to_heights;
    prefix_db_->SaveTemporaryKv(*statistic_hash, elect_statistic.SerializeAsString());
    if (!cross_string_for_hash.empty()) {
        *cross_hash = common::Hash::keccak256(cross_string_for_hash);
        prefix_db_->SaveTemporaryKv(*cross_hash, cross_statistic.SerializeAsString());
    }

    ZJC_DEBUG("success create statistic message: %s, heights: %s",
        debug_for_str.c_str(), heights.c_str());
    return kPoolsSuccess;
}

void ShardStatistic::NormalizeLofMap(std::unordered_map<uint32_t, common::Point>& lof_map) {
    if (lof_map.size() < kLofMaxNodes) {
        lof_map.clear();
        return;
    }

    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        now_elect_height_,
        common::GlobalInfo::Instance()->network_id(),
        nullptr,
        nullptr);
    std::unordered_map<uint32_t, uint32_t> avg_map;
    uint32_t max_avg = 0;
    for (auto iter = lof_map.begin(); iter != lof_map.end(); ++iter) {
        uint32_t sum = 0;
        for (auto miter = iter->second.coordinate().begin();
                miter != iter->second.coordinate().end(); ++miter) {
            sum += *miter;
        }

        uint32_t avg = sum / members->size();
        avg_map[iter->first] = avg;
        if (max_avg < avg) {
            max_avg = avg;
        }
    }

    if (max_avg < kLofValidMaxAvgTxCount) {
        lof_map.clear();
        return;
    }

    for (auto iter = lof_map.begin(); iter != lof_map.end(); ++iter) {
        auto avg = avg_map[iter->first];
        for (int32_t i = 0; i < iter->second.coordinate().size(); ++i) {
            iter->second[i] = iter->second[i] * max_avg / avg;
        }
    }
}

void ShardStatistic::LoadLatestHeights() {
    tx_heights_ptr_ = std::make_shared<pools::protobuf::ToTxHeights>();
    auto& to_heights = *tx_heights_ptr_;
    if (!prefix_db_->GetStatisticLatestHeihgts(
            common::GlobalInfo::Instance()->network_id(),
            &to_heights)) {
        ZJC_ERROR("load init statistic heights failed!");
        return;
    }

    uint32_t max_pool_index = common::kImmutablePoolSize;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        ++max_pool_index;
    }

    auto& this_net_heights = tx_heights_ptr_->heights();
    for (int32_t i = 0; i < this_net_heights.size(); ++i) {
        pool_consensus_heihgts_[i] = this_net_heights[i];
    }

    std::string init_consensus_height;
    for (uint32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
        init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
    }

    ZJC_DEBUG("init success change min elect statistic heights: %s", init_consensus_height.c_str());
    for (uint32_t i = 0; i < max_pool_index; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        if (pool_latest_height == common::kInvalidUint64) {
            continue;
        }

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
}

}  // namespace pools

}  // namespace zjchain
