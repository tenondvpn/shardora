#include "pools/shard_statistic.h"

#include "consensus/consensus_utils.h"
#include "common/encode.h"
#include "common/ip.h"
#include "common/global_info.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/pools.pb.h"
#include "pools/tx_pool_manager.h"

namespace shardora {

namespace pools {

static const std::string kShardFinalStaticPrefix = common::Encode::HexDecode(
    "027a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe7");

void ShardStatistic::Init() {
    if (pools_mgr_ != nullptr) {
        LoadLatestHeights();
    }

    assert(tx_heights_ptr_->heights_size() == (int32_t)common::kInvalidPoolIndex);
}

void ShardStatistic::OnNewBlock(const std::shared_ptr<block::protobuf::Block>& block_ptr) {
#ifdef TEST_NO_CROSS
    return;
#endif

    ZJC_DEBUG("new block coming timeblock height: %lu, pool: %u, height: %lu",
        block_ptr->timeblock_height(),
        block_ptr->pool_index(), block_ptr->height());
    block::protobuf::Block& block = *block_ptr;
    if (block.network_id() != common::GlobalInfo::Instance()->network_id() &&
            block.network_id() + network::kConsensusWaitingShardOffset != 
            common::GlobalInfo::Instance()->network_id()) {
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

    if (tx_list.empty()) {
        ZJC_DEBUG("tx list empty!");
        assert(false);
        return;
    }

    if (block_ptr->height() != pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_ + 1) {
        pools_consensus_blocks_[block_ptr->pool_index()]->blocks[block_ptr->height()] = block_ptr;
    } else {
        HandleStatistic(block_ptr);
        pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_ = block_ptr->height();
        auto& block_map = pools_consensus_blocks_[block_ptr->pool_index()]->blocks;
        auto iter = block_map.begin();
        while (iter != block_map.end()) {
            if (iter->second->height() <= pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_) {
                iter = block_map.erase(iter);
                continue;
            }

            if (iter->first != pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_ + 1) {
                break;
            }

            HandleStatistic(iter->second);
            pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_ = iter->second->height();
            iter = block_map.erase(iter);
        }

        uint64_t first_block_tm_height = common::kInvalidUint64;
        if (!block_map.empty()) {
            first_block_tm_height = block_map.begin()->second->timeblock_height();
        }

        ZJC_DEBUG("block coming pool: %u, height: %lu, latest height: %lu, "
            "block map size: %u, first_block_tm_height: %lu", 
            block_ptr->pool_index(), block_ptr->height(), 
            pools_consensus_blocks_[block_ptr->pool_index()]->latest_consensus_height_, 
            block_map.size(), first_block_tm_height);
    }
}

void ShardStatistic::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    ZJC_DEBUG("now handle statisticed block.");
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kShardStatistic) {
            pools::protobuf::ElectStatistic elect_statistic;
            if (!elect_statistic.ParseFromString(tx.storages(i).value())) {
                ZJC_ERROR("get statistic val failed: %s",
                    common::Encode::HexEncode(tx.storages(i).value()).c_str());
                assert(false);
                return;
            }

            assert(elect_statistic.height_info().heights_size() > 0);
            auto& heights = elect_statistic.height_info();
            if (heights.tm_height() > statisticed_timeblock_height_) {
                statisticed_timeblock_height_ = heights.tm_height();

                for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
                    auto tm_iter = node_height_count_map_[pool_idx].begin();
                    while (tm_iter != node_height_count_map_[pool_idx].end()) {
                        if (tm_iter->first > statisticed_timeblock_height_) {
                            break;
                        }

                        tm_iter = node_height_count_map_[pool_idx].erase(tm_iter);
                    }
                }
            }

            if (tx_heights_ptr_ != nullptr) {
                if (tx_heights_ptr_->heights_size() == heights.heights_size()) {
                    for (int32_t i = 0; i < heights.heights_size(); ++i) {
                        if (tx_heights_ptr_->heights(i) > heights.heights(i)) {
                            std::string init_consensus_height;
                            for (int32_t i = 0; i < heights.heights_size(); ++i) {
                                init_consensus_height += std::to_string(heights.heights(i)) + " ";
                            }

                            std::string src_init_consensus_height;
                            for (int32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
                                src_init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
                            }

                            ZJC_INFO("latest statistic block coming, ignore it. %s, %s",
                                src_init_consensus_height.c_str(), init_consensus_height.c_str());
                            return;
                        }
                    }
                } else {
                    ZJC_WARN("statistic heights size not equal: %u, %u",
                        tx_heights_ptr_->heights_size(), heights.heights_size());
                    assert(false);
                }
            }

            prefix_db_->SaveStatisticLatestHeihgts(
                common::GlobalInfo::Instance()->network_id(),
                heights);
            assert(heights.heights_size() == common::kInvalidPoolIndex);
            tx_heights_ptr_ = std::make_shared<pools::protobuf::StatisticTxItem>(heights);
            std::string init_consensus_height;
            for (int32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
                init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
            }

            ZJC_DEBUG("success change min elect statistic heights: %s, statisticed_timeblock_height_: %lu",
                init_consensus_height.c_str(), statisticed_timeblock_height_);
            break;
        }
    }
}

void ShardStatistic::HandleCrossShard(
        bool is_root,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        pools::protobuf::CrossShardStatistic& cross_statistic) {
    CrossStatisticItem cross_item;
    switch (tx.step()) {
    case pools::protobuf::kNormalTo: {
        if (!is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kNormalTos) {
                    assert(false);
                    pools::protobuf::ToTxMessage to_tx;
                    if (!to_tx.ParseFromString(tx.storages(i).value())) {
                        return;
                    }

                    cross_item = CrossStatisticItem(to_tx.to_heights().sharding_id());
                    ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                        tx.step(), block.pool_index(), block.height(), to_tx.to_heights().sharding_id());
                    break;
                }
            }
        }

        break;
    }
    case pools::protobuf::kRootCross: {
        if (is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kRootCross) {
                    cross_item = CrossStatisticItem(0);
                    cross_item.cross_ptr = std::make_shared<pools::protobuf::CrossShardStatistic>();
                    pools::protobuf::CrossShardStatistic& cross = *cross_item.cross_ptr;
                    if (!cross.ParseFromString(tx.storages(i).value())) {
                        assert(false);
                        break;
                    }
                }

                break;
            }
            ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                tx.step(), block.pool_index(), block.height(), 0);
        }
        break;
    }
    case pools::protobuf::kJoinElect: {
        if (!is_root) {
            cross_item = CrossStatisticItem(network::kRootCongressNetworkId);
            ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                tx.step(), block.pool_index(), block.height(), network::kRootCongressNetworkId);
        }
        
        break;
    }
    case pools::protobuf::kCreateLibrary: {
        if (is_root) {
            cross_item = CrossStatisticItem(network::kNodeNetworkId);
        } else {
            cross_item = CrossStatisticItem(network::kRootCongressNetworkId);
        }

        ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
            tx.step(), block.pool_index(), block.height(),
            cross_item.des_net);
        break;
    }
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kConsensusRootElectShard: {
        if (!is_root) {
            return;
        }

        cross_item = CrossStatisticItem(network::kNodeNetworkId);
        ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
            tx.step(), block.pool_index(), block.height(), network::kNodeNetworkId);
        break;
    }
    default:
        break;
    }

    uint32_t src_shard = common::GlobalInfo::Instance()->network_id();
    if (common::GlobalInfo::Instance()->network_id() >=
            network::kConsensusShardEndNetworkId) {
        src_shard -= network::kConsensusWaitingShardOffset;
    }

    if (cross_item.des_net != 0) {
        auto* proto_cross_item = cross_statistic.add_crosses();
        proto_cross_item->set_src_shard(src_shard);
        proto_cross_item->set_src_pool(block.pool_index());
        proto_cross_item->set_height(block.height());
        proto_cross_item->set_des_shard(cross_item.des_net);
    } else if (cross_item.cross_ptr != nullptr) {
        for (int32_t i = 0; i < cross_item.cross_ptr->crosses_size(); ++i) {
            auto* proto_cross_item = cross_statistic.add_crosses();
            proto_cross_item->set_src_shard(cross_item.cross_ptr->crosses(i).src_shard());
            proto_cross_item->set_src_pool(cross_item.cross_ptr->crosses(i).src_pool());
            proto_cross_item->set_height(cross_item.cross_ptr->crosses(i).height());
            proto_cross_item->set_des_shard(cross_item.cross_ptr->crosses(i).des_shard());
        }
    }
}

void ShardStatistic::HandleStatistic(const std::shared_ptr<block::protobuf::Block>& block_ptr) {
    auto& block = *block_ptr;
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    if (block_ptr->timeblock_height() != 0 &&
            block_ptr->timeblock_height() <= tx_heights_ptr_->tm_height()) {
        ZJC_DEBUG("failed time block height %lu, %lu, block height: %lu, "
            "common::GlobalInfo::Instance()->main_inited_success(): %d", 
            block_ptr->timeblock_height(), tx_heights_ptr_->tm_height(), block_ptr->height(), 
            common::GlobalInfo::Instance()->main_inited_success());
        if (block_ptr->height() > 0 && common::GlobalInfo::Instance()->main_inited_success()) {
            assert(block_ptr->timeblock_height() > 0);
        }

        return;
    }

    if (block_ptr->timeblock_height() < latest_timeblock_height_) {
        ZJC_DEBUG("new_block_changed_ = true timeblock not less than latest timeblock: %lu, %lu", 
            block_ptr->timeblock_height(), latest_timeblock_height_);
    }

    std::shared_ptr<HeightStatisticInfo> tm_statistic_ptr = nullptr;
    auto& pool_tm_map = node_height_count_map_[block.pool_index()];
    auto statistic_iter = pool_tm_map.find(block.timeblock_height());
    if (statistic_iter != pool_tm_map.end()) {
        tm_statistic_ptr = statistic_iter->second;
    } else {
        tm_statistic_ptr = std::make_shared<HeightStatisticInfo>();
        tm_statistic_ptr->tm_height = block.timeblock_height();
        ZJC_DEBUG("new tm iter pool: %u, height: %lu, tm_height: %lu",
            block.pool_index(), block.height(), block.timeblock_height());
        pool_tm_map[block.timeblock_height()] = tm_statistic_ptr;
    }

    std::shared_ptr<ElectNodeStatisticInfo> elect_static_info_item = nullptr;
    auto siter = tm_statistic_ptr->elect_node_info_map.find(block_ptr->electblock_height());
    if (siter != tm_statistic_ptr->elect_node_info_map.end()) {
        elect_static_info_item = siter->second;
    } else {
        elect_static_info_item = std::make_shared<ElectNodeStatisticInfo>();
        tm_statistic_ptr->elect_node_info_map[block_ptr->electblock_height()] = elect_static_info_item;
    }

    auto callback = [&](const block::protobuf::Block& block) {
        for (int32_t i = 0; i < block.tx_list_size(); ++i) {
            HandleCrossShard(is_root, block, block.tx_list(i), tm_statistic_ptr->cross_statistic);
            if (block.tx_list(i).step() == pools::protobuf::kNormalFrom ||
                    block.tx_list(i).step() == pools::protobuf::kContractCreate ||
                    block.tx_list(i).step() == pools::protobuf::kContractCreateByRootFrom ||
                    block.tx_list(i).step() == pools::protobuf::kContractExcute ||
                    block.tx_list(i).step() == pools::protobuf::kJoinElect ||
                    block.tx_list(i).step() == pools::protobuf::kContractGasPrepayment) {
                elect_static_info_item->all_gas_amount += block.tx_list(i).gas_price() * block.tx_list(i).gas_used();
            }

            if (tm_statistic_ptr->max_height < block.height()) {
                tm_statistic_ptr->max_height = block.height();
            }

            if (block.tx_list(i).step() == pools::protobuf::kJoinElect) {
                ZJC_DEBUG("join elect tx comming.");
                for (int32_t storage_idx = 0; storage_idx < block.tx_list(i).storages_size(); ++storage_idx) {
                    if (block.tx_list(i).storages(storage_idx).key() == protos::kElectNodeStoke) {
                        uint64_t* tmp_stoke = (uint64_t*)block.tx_list(i).storages(storage_idx).value().c_str();
                        elect_static_info_item->node_stoke_map[block.tx_list(i).from()] = tmp_stoke[0];
                    }

                    if (block.tx_list(i).storages(storage_idx).key() == protos::kNodePublicKey) {
                        elect_static_info_item->node_pubkey_map[block.tx_list(i).from()] = 
                            block.tx_list(i).storages(storage_idx).value();
                    }

                    if (block.tx_list(i).storages(storage_idx).key() == protos::kJoinElectVerifyG2) {
                        bls::protobuf::JoinElectInfo join_info;
                        if (!join_info.ParseFromString(block.tx_list(i).storages(storage_idx).value())) {
                            assert(false);
                            break;
                        }

                        elect_static_info_item->node_shard_map[block.tx_list(i).from()] = join_info.shard_id();
                        ZJC_DEBUG("kJoinElect add new elect node: %s, shard: %u, pool: %u, height: %lu, "
                            "elect height: %lu, tm height: %lu",
                            common::Encode::HexEncode(block.tx_list(i).from()).c_str(),
                            join_info.shard_id(),
                            block.pool_index(),
                            block.height(),
                            block_ptr->electblock_height(),
                            block.timeblock_height());
                    }
                }
            }

            if (block.tx_list(i).step() == pools::protobuf::kConsensusRootElectShard && is_root) {
                ZJC_DEBUG("success handle kConsensusRootElectShard");
                for (int32_t storage_idx = 0; storage_idx < block.tx_list(i).storages_size(); ++storage_idx) {
                    if (block.tx_list(i).storages(storage_idx).key() == protos::kElectNodeAttrElectBlock) {
                        elect::protobuf::ElectBlock elect_block;
                        if (!elect_block.ParseFromString(block.tx_list(i).storages(storage_idx).value())) {
                            assert(false);
                            break;
                        }

                        if (elect_block.gas_for_root() > 0) {
                            elect_static_info_item->all_gas_for_root += elect_block.gas_for_root();
                        }
                    }

                    if (block.tx_list(i).storages(storage_idx).key() == protos::kShardElection) {
                        uint64_t* tmp = (uint64_t*)block.tx_list(i).storages(storage_idx).value().c_str();
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
                            ZJC_DEBUG("success get shard election: %lu, %lu, join nodes size: %u, shard: %u",
                                tmp[0], tmp[1], elect_statistic.join_elect_nodes_size(), elect_statistic.join_elect_nodes(i).shard());
                            if (elect_statistic.join_elect_nodes(i).shard() == network::kRootCongressNetworkId) {
                                elect_static_info_item->node_stoke_map[elect_statistic.join_elect_nodes(i).pubkey()] =
                                    elect_statistic.join_elect_nodes(i).stoke();
                                ZJC_DEBUG("root sharding kJoinElect add new elect node: %s, stoke: %lu, elect height: %lu",
                                    common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
                                    elect_statistic.join_elect_nodes(i).stoke(),
                                    block.electblock_height());
                                elect_static_info_item->node_shard_map[elect_statistic.join_elect_nodes(i).pubkey()] = network::kRootCongressNetworkId;
                            }
                        }
                    }
                }
            }
        }
    };
    
    callback(block);
    ZJC_DEBUG("success handle block pool: %u, height: %lu, tm height: %lu", 
        block.pool_index(), block.height(), block.timeblock_height());
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
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    ZJC_DEBUG("new_block_changed_ = true new timeblcok coming and should statistic new tx %lu, %lu.", 
        latest_timeblock_height_, latest_time_block_height);
    prev_timeblock_height_ = latest_timeblock_height_;
    latest_timeblock_height_ = latest_time_block_height;
    tick_to_statistic_.CutOff(10000000lu, std::bind(&ShardStatistic::SetCanStastisticTx, this));
}

bool ShardStatistic::CheckAllBlockStatisticed(uint32_t local_net_id) {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        if (pools_consensus_blocks_[pool_idx]->blocks.empty()) {
            continue;
        }
        
        auto& first_block = pools_consensus_blocks_[pool_idx]->blocks.begin()->second;
        for (auto tm_iter = node_height_count_map_[pool_idx].begin(); 
                tm_iter != node_height_count_map_[pool_idx].end(); ++tm_iter) {
            if (tm_iter->first < latest_timeblock_height_) {
                if (tm_iter->first >= first_block->timeblock_height()) {
                    ZJC_DEBUG("failed check timeblock height %lu, %lu, pool: %u, "
                        "height: %lu, consensused max_height: %lu", 
                        tm_iter->first, first_block->timeblock_height(), 
                        pool_idx, first_block->height(), tm_iter->second->max_height);
                    return false;
                }
            }
        }
    }

    return true;
}

int ShardStatistic::StatisticWithHeights(
        pools::protobuf::ElectStatistic& elect_statistic,
        pools::protobuf::CrossShardStatistic& cross_statistic,
        uint64_t* statisticed_timeblock_height) {
    if (!new_block_changed_) {
        return kPoolsError;
    }

    ZJC_DEBUG("now statistic tx.");
#ifdef TEST_NO_CROSS
        return kPoolsError;
#endif
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    uint32_t local_net_id = common::GlobalInfo::Instance()->network_id();
    if (local_net_id >= network::kConsensusShardEndNetworkId) {
        local_net_id -= network::kConsensusWaitingShardOffset;
    }

    if (!CheckAllBlockStatisticed(local_net_id)) {
        ZJC_DEBUG("not all block statisticed.");
        return kPoolsError;
    }

    if (tx_heights_ptr_ == nullptr) {
        ZJC_DEBUG("x_heights_ptr_ == nullptr.");
        return kPoolsError;
    }

    if (tx_heights_ptr_->heights_size() < (int32_t)common::kInvalidPoolIndex) {
        ZJC_DEBUG("tx_heights_ptr_->heights_size() < (int32_t)common::kInvalidPoolIndex.");
        return kPoolsError;
    }

    if (prepare_elect_height_ == 0) {
        return kPoolsError;
    }

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
        ZJC_DEBUG("now_elect_members == nullptr.");
        return kPoolsError;
    }

    std::unordered_set<std::string> added_id_set;
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
            added_id_set.insert((*now_elect_members)[i]->id);
            added_id_set.insert((*now_elect_members)[i]->pubkey);
        }
    }

    uint64_t all_gas_amount = 0;
    uint64_t root_all_gas_amount = 0;
    std::string cross_string_for_hash;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> height_node_count_map;
    std::map<uint64_t, std::unordered_map<std::string, uint64_t>> join_elect_stoke_map;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> join_elect_shard_map;
    std::unordered_map<std::string, std::string> id_pk_map;
    uint64_t pools_max_height[common::kInvalidPoolIndex] = {0};
    uint64_t max_tm_height = 0;
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        for (auto tm_iter = node_height_count_map_[pool_idx].begin(); 
                tm_iter != node_height_count_map_[pool_idx].end(); ++tm_iter) {
            ZJC_DEBUG("0 pool: %u, elect height: %lu, tm height: %lu, latest tm height: %lu, "
                "statisticed_timeblock_height_: %lu", 
                pool_idx, 0, tm_iter->first, latest_timeblock_height_, statisticed_timeblock_height_);
            if (tm_iter->first > latest_timeblock_height_) {
                assert(false);
                break;
            }

            if (tm_iter->first <= statisticed_timeblock_height_) {
                continue;
            }

            if (tm_iter->first > max_tm_height) {
                max_tm_height = tm_iter->first;
            }

            for (uint32_t i = 0; i < tm_iter->second->cross_statistic.crosses_size(); ++i) {
                auto* cross_item = cross_statistic.add_crosses();
                *cross_item = tm_iter->second->cross_statistic.crosses(i);
            }
            
            for (auto elect_iter = tm_iter->second->elect_node_info_map.begin(); 
                    elect_iter != tm_iter->second->elect_node_info_map.end(); ++elect_iter) {
                auto elect_height = elect_iter->first;
                ZJC_DEBUG("1 pool: %u, elect height: %lu, tm height: %lu, latest tm height: %lu", 
                    pool_idx, elect_height, tm_iter->first, latest_timeblock_height_);
                auto iter = height_node_count_map.find(elect_height);
                if (iter == height_node_count_map.end()) {
                    height_node_count_map[elect_height] = std::unordered_map<std::string, uint32_t>();
                }

                ZJC_DEBUG("handle elect height: %lu, node stake size: %u, node shard map: %u", 
                    elect_height, 
                    elect_iter->second->node_stoke_map.size(), 
                    elect_iter->second->node_shard_map.size());
                if (!elect_iter->second->node_stoke_map.empty() && !elect_iter->second->node_shard_map.empty()) {
                    auto eiter = join_elect_stoke_map.find(elect_height);
                    if (eiter == join_elect_stoke_map.end()) {
                        join_elect_stoke_map[elect_height] = std::unordered_map<std::string, uint64_t>();
                    }

                    auto& elect_stoke_map = join_elect_stoke_map[elect_height];
                    for (auto stoke_iter = elect_iter->second->node_stoke_map.begin();
                            stoke_iter != elect_iter->second->node_stoke_map.end(); ++stoke_iter) {
                        elect_stoke_map[stoke_iter->first] = stoke_iter->second;
                        ZJC_DEBUG("kJoinElect add new elect node elect_height: %lu, %s, %lu", 
                            elect_height, 
                            common::Encode::HexEncode(stoke_iter->first).c_str(), 
                            stoke_iter->second);
                    }

                    for (auto pk_iter = elect_iter->second->node_pubkey_map.begin();
                            pk_iter != elect_iter->second->node_pubkey_map.end();
                            ++pk_iter) {
                        auto tmp_id = secptr_->GetAddress(pk_iter->second);
                        if (tmp_id != pk_iter->first) {
                            assert(false);
                            continue;
                        }

                        id_pk_map[pk_iter->first] = pk_iter->second;
                    }

                    auto shard_iter = join_elect_shard_map.find(elect_height);
                    if (shard_iter == join_elect_shard_map.end()) {
                        join_elect_shard_map[elect_height] = std::unordered_map<std::string, uint32_t>();
                    }

                    auto& elect_shard_map = join_elect_shard_map[elect_height];
                    for (auto tmp_shard_iter = elect_iter->second->node_shard_map.begin();
                            tmp_shard_iter != elect_iter->second->node_shard_map.end(); ++tmp_shard_iter) {
                        elect_shard_map[tmp_shard_iter->first] = tmp_shard_iter->second;
                        ZJC_DEBUG("kJoinElect add new elect node shard: %u , %s", 
                            tmp_shard_iter->second, 
                            common::Encode::HexEncode(tmp_shard_iter->first).c_str());
                    }
                }

                all_gas_amount += elect_iter->second->all_gas_amount;
                root_all_gas_amount += elect_iter->second->all_gas_for_root;
            }
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
                ZJC_DEBUG("failed get shard: %s", common::Encode::HexEncode(iter->first).c_str());
                continue;
            }

            auto inc_iter = added_id_set.find(iter->first);
            if (inc_iter != added_id_set.end()) {
                ZJC_DEBUG("not added id: %s", common::Encode::HexEncode(iter->first).c_str());
                continue;
            }

            elect_nodes.push_back(iter->first);
            ZJC_DEBUG("elect nodes add: %s, %lu", 
                common::Encode::HexEncode(iter->first).c_str(), iter->second);
            added_id_set.insert(iter->first);
        }
    }
    
    std::string debug_for_str;
    auto r_hiter = height_node_count_map.rbegin();
    if (r_hiter == height_node_count_map.rend() || r_hiter->first < now_elect_height_) {
        height_node_count_map[now_elect_height_] = std::unordered_map<std::string, uint32_t>();
        auto& node_count_map = height_node_count_map[now_elect_height_];
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
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
        if (members == nullptr) {
             continue;
        }

        for (uint32_t midx = 0; midx < members->size(); ++midx) {
            auto& id = (*members)[midx]->id;
            auto iter = node_count_map.find(id);
            uint32_t tx_count = 0;
            if (iter != node_count_map.end()) {
                tx_count = iter->second;
            }

            statistic_item.add_tx_count(tx_count);
            debug_for_str += "tx_count: " + std::to_string(tx_count) + ", ";
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
                float x = 0.0;
                float y = 0.0;
                if (common::Ip::Instance()->GetIpLocation(ip, &x, &y) == 0) {
                    area_point->set_x(static_cast<int32_t>(x * 100));
                    area_point->set_y(static_cast<int32_t>(y * 100));
                }
            }

            int32_t x1 = area_point->x();
            int32_t y1 = area_point->y();
            debug_for_str += "xy: " + std::to_string(x1) + "-" + std::to_string(y1) + ",";
            ZJC_DEBUG("elect height: %lu, id: %s, tx count: %u",
                hiter->first, common::Encode::HexEncode(id).c_str(), tx_count);
        }

        statistic_item.set_elect_height(hiter->first);
        debug_for_str += " elect height: " + std::to_string(hiter->first) + ",";
    }

    debug_for_str += "stoke: ";
    for (uint32_t i = 0; i < elect_nodes.size() && i < kWaitingElectNodesMaxCount; ++i) {
        std::string pubkey = elect_nodes[i];
        if (pubkey.size() == security::kUnicastAddressLength) {
            auto iter = id_pk_map.find(pubkey);
            if (iter == id_pk_map.end()) {
                assert(false);
                continue;
            }

            pubkey = iter->second;
        }

        auto addr_info = pools_mgr_->GetAddressInfo(secptr_->GetAddress(pubkey));
        if (addr_info == nullptr) {
            ZJC_WARN("failed get address info pk: %s, addr: %s", 
                common::Encode::HexEncode(pubkey).c_str(), 
                common::Encode::HexEncode(secptr_->GetAddress(pubkey)).c_str());
            assert(false);
            continue;
        }

        if (addr_info->elect_pos() != common::kInvalidUint32) {
            if (addr_info->elect_pos() < 0 ||
                    addr_info->elect_pos() >= common::GlobalInfo::Instance()->each_shard_max_members()) {
                assert(false);
                continue;
            }
        }
        
        auto join_elect_node = elect_statistic.add_join_elect_nodes();
        join_elect_node->set_pubkey(pubkey);
        auto iter = r_eiter->second.find(elect_nodes[i]);
        auto shard_iter = r_siter->second.find(elect_nodes[i]);
        join_elect_node->set_stoke(iter->second);
        join_elect_node->set_shard(shard_iter->second);
        join_elect_node->set_elect_pos(addr_info->elect_pos());
        debug_for_str += common::Encode::HexEncode(elect_nodes[i]) + "," + std::to_string(iter->second) + "," + std::to_string(shard_iter->second) + ",";
        ZJC_DEBUG("add new elect node: %s, stoke: %lu, shard: %u", common::Encode::HexEncode(pubkey).c_str(), iter->second, shard_iter->second);
    }

    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < prepare_members->size(); ++i) {
            auto inc_iter = added_id_set.find((*prepare_members)[i]->pubkey);
            if (inc_iter != added_id_set.end()) {
                continue;
            }

            auto addr_info = pools_mgr_->GetAddressInfo(secptr_->GetAddress((*prepare_members)[i]->pubkey));
            if (addr_info == nullptr) {
                continue;
            }

            if (addr_info->elect_pos() != common::kInvalidUint32) {
                if (addr_info->elect_pos() < 0 ||
                        addr_info->elect_pos() >= common::GlobalInfo::Instance()->each_shard_max_members()) {
                    continue;
                }
            }

            uint64_t stoke = 0;
            auto join_elect_node = elect_statistic.add_join_elect_nodes();
            join_elect_node->set_pubkey((*prepare_members)[i]->pubkey);
            join_elect_node->set_elect_pos(addr_info->elect_pos());
            join_elect_node->set_stoke(stoke);
            uint32_t shard = common::GlobalInfo::Instance()->network_id();
            join_elect_node->set_shard(shard);
            debug_for_str += common::Encode::HexEncode((*prepare_members)[i]->pubkey) + "," +
                std::to_string(stoke) + "," + std::to_string(shard) + ",";
        }
    }

    if (prepare_members != nullptr) {
        ZJC_DEBUG("kJoinElect add new elect node now elect_height: %lu, prepare elect height: %lu, %d, %d,"
            "new nodes size: %u, now members size: %u, prepare members size: %u",
            now_elect_height_,
            prepare_elect_height_,
            (r_eiter != join_elect_stoke_map.rend()),
            (r_siter != join_elect_shard_map.rend()),
            elect_statistic.join_elect_nodes_size(),
            now_elect_members->size(),
            prepare_members->size());
    }

    if (is_root) {
        elect_statistic.set_gas_amount(root_all_gas_amount);
    } else {
        elect_statistic.set_gas_amount(all_gas_amount);
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    elect_statistic.set_sharding_id(net_id);
    auto* heights_ptr = elect_statistic.mutable_height_info();
    heights_ptr->set_tm_height(max_tm_height);
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        bool valid = false;
        for (auto tm_iter = node_height_count_map_[pool_idx].rbegin(); 
                tm_iter != node_height_count_map_[pool_idx].rend(); ++tm_iter) {
            if (tm_iter->first >= latest_timeblock_height_) {
                continue;
            }

            heights_ptr->add_heights(tm_iter->second->max_height);
            valid = true;
            break;
        }

        if (!valid) {
            heights_ptr->add_heights(tx_heights_ptr_->heights(pool_idx));
        }
    }

    heights_ptr->set_tm_height(prev_timeblock_height_);
    debug_for_str += std::to_string(all_gas_amount) + ",";
    debug_for_str += std::to_string(net_id) + ",";
    if (!cross_string_for_hash.empty()) {
        if (is_root) {
        } else {
            *elect_statistic.mutable_cross() = cross_statistic;
        }
    }

    {
        ZJC_DEBUG("LLLLLL statistic :%s", ProtobufToJson(elect_statistic).c_str());
    }

    ZJC_DEBUG("success create statistic message: %s, heights: %s, prev_timeblock_height_: %lu",
        debug_for_str.c_str(),
        "heights.c_str()",
        prev_timeblock_height_);
    new_block_changed_ = false;
    *statisticed_timeblock_height = prev_timeblock_height_;
    return kPoolsSuccess;
}

void ShardStatistic::LoadLatestHeights() {
    tx_heights_ptr_ = std::make_shared<pools::protobuf::StatisticTxItem>();
    auto& to_heights = *tx_heights_ptr_;
    if (!prefix_db_->GetStatisticLatestHeihgts(
            common::GlobalInfo::Instance()->network_id(),
            &to_heights)) {
        for (int32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            pools_consensus_blocks_[i] = std::make_shared<PoolBlocksInfo>();
            pools_consensus_blocks_[i]->latest_consensus_height_ = 0;
            tx_heights_ptr_->add_heights(0);
        }

        ZJC_ERROR("load init statistic heights failed: %u",
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    std::string init_consensus_height;
    for (int32_t i = 0; i < tx_heights_ptr_->heights_size(); ++i) {
        init_consensus_height += std::to_string(tx_heights_ptr_->heights(i)) + " ";
        pools_consensus_blocks_[i] = std::make_shared<PoolBlocksInfo>();
        pools_consensus_blocks_[i]->latest_consensus_height_ = tx_heights_ptr_->heights(i);
    }

    ZJC_DEBUG("init success change min elect statistic heights: %s", init_consensus_height.c_str());
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        if (pool_latest_height == common::kInvalidUint64) {
            continue;
        }

        bool consensus_stop = false;
        auto& this_net_heights = tx_heights_ptr_->heights();
        for (uint64_t height = this_net_heights[i];
                height <= pool_latest_height; ++height) {
            auto block_ptr = std::make_shared<block::protobuf::Block>();
            block::protobuf::Block& block = *block_ptr;
            if (!prefix_db_->GetBlockWithHeight(
                    common::GlobalInfo::Instance()->network_id(), i, height, &block)) {
                consensus_stop = true;
            } else {
                OnNewBlock(block_ptr);
            }
        }
    }
}

}  // namespace pools

}  // namespace shardora
