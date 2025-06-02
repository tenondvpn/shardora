#include "pools/shard_statistic.h"

#include "consensus/consensus_utils.h"
#include "common/encode.h"
#include "common/ip.h"
#include "common/global_info.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/pools.pb.h"
#include "pools/tx_pool_manager.h"
#include "zjcvm/execution.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"
#include <bls/bls_utils.h>
#include <protos/elect.pb.h>
#include <protos/tx_storage_key.h>
// #include <iostream>
// #include "shard_statistic.h"


namespace shardora {

namespace pools {

static const std::string kShardFinalStaticPrefix = common::Encode::HexDecode(
    "027a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe7");

int ShardStatistic::Init() {
    if (common::GlobalInfo::Instance()->network_id() < network::kRootCongressNetworkId ||
            common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        assert(false);
        return kPoolsError;
    }

    pools::protobuf::PoolStatisticTxInfo statistic_info;
    if (prefix_db_->GetLatestPoolStatisticTag(
            common::GlobalInfo::Instance()->network_id(), 
            &statistic_info)) {
        latest_statisticed_height_ = statistic_info.height();
        ZJC_INFO("success set latest statisticed height: %lu, info: %s", 
            latest_statisticed_height_, 
            ProtobufToJson(statistic_info).c_str());
        if (statistic_info.pool_statisitcs_size() != common::kInvalidPoolIndex) {
            assert(false);
            return kPoolsError;
        }

        std::map<uint32_t, StatisticInfoItem> tmp_pool_map;
        statistic_pool_info_[statistic_info.height()] = tmp_pool_map;
        CHECK_MEMORY_SIZE(statistic_pool_info_);
        auto& pool_map = statistic_pool_info_[statistic_info.height()];
        for (int32_t i = 0; i < statistic_info.pool_statisitcs_size(); ++i) {
            StatisticInfoItem statistic_item;
            statistic_item.statistic_min_height = statistic_info.pool_statisitcs(i).max_height() + 1;
            pool_map[i] = statistic_item;
        }
    } else {
        ZJC_INFO("failed load latest pool statistic tag: %d", common::GlobalInfo::Instance()->network_id());
        assert(false);
        return kPoolsError;
    }

    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        pools_consensus_blocks_[i] = std::make_shared<PoolBlocksInfo>();
        pools_consensus_blocks_[i]->latest_consensus_height_ =
            statistic_info.pool_statisitcs(i).max_height();
        for (uint64_t height = statistic_info.pool_statisitcs(i).max_height();; ++height) {
            auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
            auto& view_block = *view_block_ptr;
            if (!prefix_db_->GetBlockWithHeight(
                    common::GlobalInfo::Instance()->network_id(), 
                    i, 
                    height, 
                    &view_block)) {
                break;
            }
                
            OnNewBlock(view_block_ptr);
        }
    }

    return kPoolsSuccess;
}

void ShardStatistic::OnNewBlock(
    const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
#ifdef TEST_NO_CROSS
    return;
#endif
    view_block_queue_.push(view_block_ptr);
    thread_wait_conn_.notify_one();
}

void ShardStatistic::ThreadCallback() {
    common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        std::shared_ptr<view_block::protobuf::ViewBlockItem> block_ptr;
        while (view_block_queue_.pop(&block_ptr)) {
            ThreadToStatistic(block_ptr);
        }

        std::unique_lock<std::mutex> lock(thread_wait_mutex_);
        thread_wait_conn_.wait_for(lock, std::chrono::milliseconds(1000));
    }
}

void ShardStatistic::ThreadToStatistic(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
    auto* block_ptr = &view_block_ptr->block_info();
    ZJC_INFO("new block coming net: %u, pool: %u, height: %lu, timeblock height: %lu",
        view_block_ptr->qc().network_id(),
        view_block_ptr->qc().pool_index(),
        block_ptr->height(),
        block_ptr->timeblock_height());
    auto& block = *block_ptr;
    if (!network::IsSameToLocalShard(view_block_ptr->qc().network_id())) {
        return;
    }
    
    auto& pool_blocks_info = pools_consensus_blocks_[view_block_ptr->qc().pool_index()];
    if (block_ptr->height() != pool_blocks_info->latest_consensus_height_ + 1) {
        pool_blocks_info->blocks[block_ptr->height()] = view_block_ptr;
        ZJC_INFO("pool latest height not continus: %u_%u_%lu, view: %lu, %lu,",
            view_block_ptr->qc().network_id(),
            view_block_ptr->qc().pool_index(),
            block_ptr->height(),
            view_block_ptr->qc().view(),
            pool_blocks_info->latest_consensus_height_);
    } else {
        HandleStatistic(view_block_ptr);
        pool_blocks_info->latest_consensus_height_ = block_ptr->height();
        cleanUpBlocks(*pool_blocks_info);
    }
}

void ShardStatistic::cleanUpBlocks(PoolBlocksInfo& pool_blocks_info) {
    auto& block_map = pool_blocks_info.blocks;
    for (auto iter = block_map.begin(); iter != block_map.end(); ) {
        if (iter->second->block_info().height() <= pool_blocks_info.latest_consensus_height_) {
            iter = block_map.erase(iter);
        } else if (iter->first == pool_blocks_info.latest_consensus_height_ + 1) {
            HandleStatistic(iter->second);
            pool_blocks_info.latest_consensus_height_ = iter->second->block_info().height();
            iter = block_map.erase(iter);
        } else {
            ++iter;
        }
    }
}

// void ShardStatistic::HandleStatisticBlock(
//         const block::protobuf::Block& block) {
//     auto& elect_statistic = block.elect_statistic();
//     ZJC_INFO("success handle statistic block: %s, latest_statisticed_height_: %lu, %d, %d",
//         ProtobufToJson(elect_statistic).c_str(), latest_statisticed_height_,
//         block.has_elect_statistic(),
//         block.has_pool_st_info());
//     auto& heights = elect_statistic.height_info();
    // auto st_iter = statistic_pool_info_.begin();
    // while (st_iter != statistic_pool_info_.end()) {
    //     if (st_iter->first >= latest_statisticed_height_) {
    //         break;
    //     }
            
    //     ZJC_INFO("erase statistic height: %lu", st_iter->first);
    //     st_iter = statistic_pool_info_.erase(st_iter);
    //     CHECK_MEMORY_SIZE(statistic_pool_info_);
    // }

//     latest_statisticed_height_ = elect_statistic.statistic_height();
//     latest_statistic_item_ = std::make_shared<pools::protobuf::StatisticTxItem>(heights);

// }

void ShardStatistic::HandleStatistic(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
    auto& block = view_block_ptr->block_info();
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    if (block.timeblock_height() < latest_timeblock_height_) {
        ZJC_INFO("timeblock not less than latest timeblock: %lu, %lu", 
            block.timeblock_height(), latest_timeblock_height_);
    }

    auto pool_idx = view_block_ptr->qc().pool_index();
    if (block.has_pool_statistic_height() &&
            network::IsSameToLocalShard(view_block_ptr->qc().network_id())) {
        pool_statistic_height_with_block_height_map_[block.pool_statistic_height()][pool_idx] = block.height();
        auto exist_iter = statistic_pool_info_.find(block.pool_statistic_height());
        if (exist_iter == statistic_pool_info_.end()) {
            StatisticInfoItem statistic_item;
            statistic_item.statistic_min_height = block.height() + 1;
            std::map<uint32_t, StatisticInfoItem> pool_map;
            pool_map[pool_idx] = statistic_item;
            statistic_pool_info_[block.pool_statistic_height()] = pool_map;
            CHECK_MEMORY_SIZE(statistic_pool_info_);
            ZJC_INFO(
                "new success handle kPoolStatisticTag tx statistic_height: %lu, "
                "pool: %u, height: %lu, statistic_max_height: %lu, nonce: %lu", 
                block.pool_statistic_height(), 
                pool_idx, 
                block.height(), 
                statistic_item.statistic_min_height,
                0);
        } else {
            StatisticInfoItem statistic_item;
            statistic_item.statistic_min_height = block.height() + 1;
            exist_iter->second[pool_idx] = statistic_item;
            ZJC_INFO(
                "exists success handle kPoolStatisticTag tx statistic_height: %lu, "
                "pool: %u, height: %lu, statistic_max_height: %lu, nonce: %lu", 
                block.pool_statistic_height(), 
                pool_idx, 
                block.height(), 
                statistic_item.statistic_min_height,
                0);
        }
    }

    ZJC_INFO("real handle new block coming net: %u, pool: %u, height: %lu, timeblock height: %lu",
        view_block_ptr->qc().network_id(),
        view_block_ptr->qc().pool_index(),
        block.height(),
        block.timeblock_height());
    // if (block.has_elect_statistic() || block.has_pool_st_info()) {
    //     HandleStatisticBlock(block);
    // }

    std::string statistic_pool_debug_str;
    auto pool_statistic_riter = statistic_pool_info_.rbegin();
    while (pool_statistic_riter != statistic_pool_info_.rend()) {
        auto pool_iter = pool_statistic_riter->second.find(pool_idx);
        ZJC_INFO("check elect height: %lu, pool: %u, block height: %lu, find: %d",
            pool_iter->first, pool_idx, block.height(), 
            (pool_iter != pool_statistic_riter->second.end()));
        if (pool_iter != pool_statistic_riter->second.end()) {
            ZJC_INFO("pool: %u, get block height and statistic height: %lu, max_height: %lu",
                pool_idx,
                block.height(),
                pool_iter->second.statistic_max_height);
            if (pool_iter->second.statistic_max_height <= block.height()) {
                break;
            }
        }

        ++pool_statistic_riter;
    }

    if (pool_statistic_riter == statistic_pool_info_.rend()) {
        ZJC_INFO("statistic_pool_debug_str failed, has statisticed: %s", statistic_pool_debug_str.c_str());
        // assert(false);
        return;
    }

    auto pool_iter = pool_statistic_riter->second.find(pool_idx);
    if (pool_iter == pool_statistic_riter->second.end()) {
        assert(false);
        return;
        // pool_statistic_riter->second[pool_idx] = StatisticInfoItem();
        // pool_iter = pool_statistic_riter->second.find(pool_idx);
    }

    auto& pool_statistic_info = pool_iter->second;
    auto* statistic_info_ptr = &pool_statistic_info;
    if (statistic_info_ptr->statistic_max_height == 0llu) {
        for (auto iter = pool_statistic_height_with_block_height_map_.begin(); 
                iter != pool_statistic_height_with_block_height_map_.end(); ++iter) {
            if (iter->first > pool_statistic_riter->first) {
                auto tmp_pool_iter = iter->second.find(pool_idx);
                if (tmp_pool_iter != iter->second.end()) {
                    statistic_info_ptr->statistic_max_height = tmp_pool_iter->second;
                    ZJC_INFO("pool statistic set min and max height: %u, %lu, %lu, "
                        "exists statistic height: %lu, prev statistic height: %lu",
                        pool_iter->first, 
                        statistic_info_ptr->statistic_min_height, 
                        statistic_info_ptr->statistic_max_height,
                        iter->first,
                        pool_statistic_riter->first);
                }
                    
                break;
            }
        }
    }

    auto& join_elect_stoke_map = statistic_info_ptr->join_elect_stoke_map;
    auto& join_elect_shard_map = statistic_info_ptr->join_elect_shard_map;
    auto& height_node_collect_info_map = statistic_info_ptr->height_node_collect_info_map;
    auto& id_pk_map = statistic_info_ptr->id_pk_map;
    auto& id_agg_bls_pk_map = statistic_info_ptr->id_agg_bls_pk_map;
    auto& id_agg_bls_pk_proof_map = statistic_info_ptr->id_agg_bls_pk_proof_map;
    auto handle_joins_func = [&](const bls::protobuf::JoinElectInfo& join_info) {
        ZJC_INFO("join elect tx comming.");
        auto join_addr = secptr_->GetAddress(join_info.public_key());
        id_pk_map[join_addr] =join_info.public_key();
        {
            auto eiter = join_elect_stoke_map.find(view_block_ptr->qc().elect_height());
            if (eiter == join_elect_stoke_map.end()) {
                join_elect_stoke_map[view_block_ptr->qc().elect_height()] = 
                    std::unordered_map<std::string, uint64_t>();
            }

            auto& elect_stoke_map = join_elect_stoke_map[view_block_ptr->qc().elect_height()];
            elect_stoke_map[join_addr] = join_info.stoke();
            ZJC_INFO("success add elect node stoke %s, %lu, "
                "elect height: %lu, tm height: %lu",
                common::Encode::HexEncode(join_addr).c_str(), 
                join_info.stoke(),
                view_block_ptr->qc().elect_height(),
                block.timeblock_height());
        }

        if (join_info.has_bls_pk()) {
            auto agg_bls_pk_proto = std::make_shared<elect::protobuf::BlsPublicKey>(join_info.bls_pk());
            id_agg_bls_pk_map[join_addr] = agg_bls_pk_proto;
        }

        if (join_info.has_bls_proof()) {
            auto proof_proto = std::make_shared<elect::protobuf::BlsPopProof>(join_info.bls_proof());
            id_agg_bls_pk_proof_map[join_addr] = proof_proto;
        }                    

        {
            auto shard_iter = join_elect_shard_map.find(view_block_ptr->qc().elect_height());
            if (shard_iter == join_elect_shard_map.end()) {
                join_elect_shard_map[view_block_ptr->qc().elect_height()] =
                    std::unordered_map<std::string, uint32_t>();
            }

            auto& elect_shard_map = join_elect_shard_map[view_block_ptr->qc().elect_height()];
            elect_shard_map[join_addr] = join_info.shard_id();
            ZJC_INFO("kJoinElect add new elect node: %s, shard: %u, pool: %u, "
                "height: %lu, elect height: %lu, tm height: %lu",
                common::Encode::HexEncode(join_addr).c_str(),
                join_info.shard_id(),
                view_block_ptr->qc().pool_index(),
                block.height(),
                view_block_ptr->qc().elect_height(),
                block.timeblock_height());
        }
    };

    for (uint32_t i = 0; i < block.joins_size(); ++i) {
        handle_joins_func(block.joins(i));
    }

    if (block.has_elect_block()) {
        auto& elect_block = block.elect_block();
        if (elect_block.gas_for_root() > 0) {
            statistic_info_ptr->root_all_gas_amount += elect_block.gas_for_root();
        }

        for(auto node : elect_block.in()) {
            auto addr = secptr_->GetAddress(node.pubkey());
            auto acc_iter = accout_poce_info_map_.find(addr);
            if (acc_iter == accout_poce_info_map_.end()) {
                accout_poce_info_map_[addr] = std::make_shared<AccoutPoceInfoItem>();
                CHECK_MEMORY_SIZE(accout_poce_info_map_);
                acc_iter = accout_poce_info_map_.find(addr);
            }

            auto& accoutPoceInfoIterm = acc_iter->second;
            accoutPoceInfoIterm->consensus_gap += 1;
            accoutPoceInfoIterm->credit += node.fts_value();;
            ZJC_INFO("HandleElectStatistic addr: %s, consensus_gap: %lu, credit: %lu",
                common::Encode::HexEncode(addr).c_str(), 
                accoutPoceInfoIterm->consensus_gap, 
                accoutPoceInfoIterm->credit);
        }
    }

    if (block.has_elect_statistic()) {
        auto& elect_statistic = block.elect_statistic();
        auto& heights = elect_statistic.height_info();
        auto st_iter = statistic_pool_info_.begin();
        while (st_iter != statistic_pool_info_.end()) {
            if (st_iter->first >= latest_statisticed_height_) {
                break;
            }
                
            ZJC_INFO("erase statistic height: %lu", st_iter->first);
            st_iter = statistic_pool_info_.erase(st_iter);
            CHECK_MEMORY_SIZE(statistic_pool_info_);
        }

        latest_statisticed_height_ = elect_statistic.statistic_height();
        latest_statistic_item_ = std::make_shared<pools::protobuf::StatisticTxItem>(heights);
        for (int32_t node_idx = 0;
                node_idx < elect_statistic.join_elect_nodes_size(); ++node_idx) {
            ZJC_INFO("success get shard election: %lu, %lu, "
                "join nodes size: %u, shard: %u",
                elect_statistic.sharding_id(),
                elect_statistic.statistic_height(),
                elect_statistic.join_elect_nodes_size(),
                elect_statistic.join_elect_nodes(node_idx).shard());
            if (elect_statistic.join_elect_nodes(node_idx).shard() == network::kRootCongressNetworkId) {
                auto eiter = join_elect_stoke_map.find(view_block_ptr->qc().elect_height());
                if (eiter == join_elect_stoke_map.end()) {
                    join_elect_stoke_map[view_block_ptr->qc().elect_height()] =
                        std::unordered_map<std::string, uint64_t>();
                }

                auto& elect_stoke_map = join_elect_stoke_map[view_block_ptr->qc().elect_height()];
                elect_stoke_map[elect_statistic.join_elect_nodes(node_idx).pubkey()] =
                    elect_statistic.join_elect_nodes(node_idx).stoke();
                auto shard_iter = join_elect_shard_map.find(view_block_ptr->qc().elect_height());
                if (shard_iter == join_elect_shard_map.end()) {
                    join_elect_shard_map[view_block_ptr->qc().elect_height()] = 
                        std::unordered_map<std::string, uint32_t>();
                }

                auto& elect_shard_map = join_elect_shard_map[view_block_ptr->qc().elect_height()];
                elect_shard_map[elect_statistic.join_elect_nodes(node_idx).pubkey()] = 
                    network::kRootCongressNetworkId;
                ZJC_INFO("root sharding kJoinElect add new elect node: %s, "
                    "stoke: %lu, elect height: %lu",
                    common::Encode::HexEncode(
                        elect_statistic.join_elect_nodes(node_idx).pubkey()).c_str(),
                    elect_statistic.join_elect_nodes(node_idx).stoke(),
                    view_block_ptr->qc().elect_height());
            }
        }
    }

    statistic_info_ptr->all_gas_amount += block.all_gas();
    std::string leader_id = getLeaderIdFromBlock(*view_block_ptr);
    if (leader_id.empty()) {
        assert(false);
        return;
    }

    auto elect_height_iter = height_node_collect_info_map.find(
        view_block_ptr->qc().elect_height());
    if (elect_height_iter == height_node_collect_info_map.end()) {
        height_node_collect_info_map[view_block_ptr->qc().elect_height()] = 
            std::unordered_map<std::string, StatisticMemberInfoItem>();
        elect_height_iter = height_node_collect_info_map.find(
            view_block_ptr->qc().elect_height());
    }

    auto& node_info_map = elect_height_iter->second;
    // 聚合每个选举高度，每个节点在各个pool 中完成交易的gas总和
    auto node_iter = node_info_map.find(leader_id);
    if (node_iter == node_info_map.end()) {
        node_info_map[leader_id] = StatisticMemberInfoItem();
        node_iter = node_info_map.find(leader_id);
    }

    auto& node_info = node_iter->second;
    node_info.gas_sum += block.all_gas();
    node_info.tx_count += block.tx_list_size();
    std::string debug_str = ", height_node_collect_info_map height: ";
    for (auto titer = statistic_info_ptr->height_node_collect_info_map.begin(); 
            titer != statistic_info_ptr->height_node_collect_info_map.end(); ++titer) {
        for (auto siter = titer->second.begin(); siter != titer->second.end(); ++siter) {
            debug_str += ", leader id: " + common::Encode::HexEncode(siter->first) + 
            ", block_gas: " + std::to_string(siter->second.gas_sum) + 
            ", tx count: " + std::to_string(siter->second.tx_count) + ", ";
        }
    }

    ZJC_INFO("statistic height: %lu, success handle block pool: %u, height: %lu, "
        "tm height: %lu, leader_id: %s, tx_count: %u, tx size: %u, "
        "debug_str: %s, statistic_pool_debug_str: %s",
        pool_statistic_riter->first,
        view_block_ptr->qc().pool_index(), block.height(), 
        block.timeblock_height(), 
        common::Encode::HexEncode(leader_id).c_str(),
        node_info.tx_count,
        block.tx_list_size(),
        debug_str.c_str(),
        statistic_pool_debug_str.c_str());

}

std::string ShardStatistic::getLeaderIdFromBlock(
        const shardora::view_block::protobuf::ViewBlockItem &view_block) {
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        view_block.qc().elect_height(),
        view_block.qc().network_id(),
        nullptr,
        nullptr);
    if (members == nullptr) {
        ZJC_INFO("block leader not exit block.hash %s block.electHeight:%d, network_id:%d ",
                  common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                  view_block.qc().elect_height(),
                  view_block.qc().network_id());
        return "";
    }

    auto leader_id = (*members)[view_block.qc().leader_idx()]->id;
    return leader_id;
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
    ZJC_INFO("new elect block: %lu, %lu, prepare_elect_height_: %lu",
        prev_elect_height_, now_elect_height_, prepare_elect_height_);
}

void ShardStatistic::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    ZJC_INFO("new timeblcok coming and should statistic new tx %lu, %lu.", 
        latest_timeblock_height_, latest_time_block_height);
    prev_timeblock_height_ = latest_timeblock_height_;
    latest_timeblock_height_ = latest_time_block_height;
}

int ShardStatistic::StatisticWithHeights(
        pools::protobuf::ElectStatistic& elect_statistic,
        uint64_t statisticed_timeblock_height) {
#ifdef TEST_NO_CROSS
        return kPoolsError;
#endif

    auto iter = statistic_pool_info_.rbegin();
    auto piter = statistic_pool_info_.rend();
    while (iter != statistic_pool_info_.rend() && iter->first > latest_statisticed_height_) {
        ZJC_INFO("iter->first: %lu, size: %u, piter->first: %lu, size: %u, latest_statisticed_height_: %lu", 
            iter->first, 
            iter->second.size(), 
            piter !=  statistic_pool_info_.rend() ? piter->first : 0lu, 
            piter !=  statistic_pool_info_.rend() ? piter->second.size() : 0lu, 
            latest_statisticed_height_);
        piter = iter;
        ++iter;
    }

    if (piter == statistic_pool_info_.rend() || iter == statistic_pool_info_.rend()) {
        std::string piter_debug_str;
        if (piter != statistic_pool_info_.rend())
        for (auto test_iter = piter->second.begin(); test_iter != piter->second.end(); ++test_iter) {
            piter_debug_str += std::to_string(test_iter->first) + ", ";
        }

        std::string iter_debug_str;
        if (iter != statistic_pool_info_.rend())
        for (auto test_iter = iter->second.begin(); test_iter != iter->second.end(); ++test_iter) {
            iter_debug_str += std::to_string(test_iter->first) + ", ";
        }

        ZJC_INFO("failed iter == statistic_pool_info_.end() piter: %d, iter: %d, "
            "piter_debug_str: %s, iter_debug_str: %s, iter->first: %lu, latest_statisticed_height_: %lu",
            (piter == statistic_pool_info_.rend()), 
            (iter == statistic_pool_info_.rend()),
            piter_debug_str.c_str(),
            iter_debug_str.c_str(),
            iter->first,
            latest_statisticed_height_);
        return kPoolsError;
    }

    if (piter->second.size() != common::kInvalidPoolIndex ||
            iter->second.size() != common::kInvalidPoolIndex) {
        std::string valid_pools = "";
        for (auto titer = piter->second.begin(); titer != piter->second.end(); ++titer) {
            valid_pools += std::to_string(titer->first) + ":" + 
                std::to_string(titer->second.statistic_min_height) + ":" + 
                std::to_string(titer->second.statistic_max_height) + ",";
        }
        ZJC_INFO("pool not full statistic height: %lu, now: %u, all: %u, "
            "now_size: %u, %s, latest_statisticed_height_: %lu", 
            piter->first,
            piter->second.size(), 
            common::kInvalidPoolIndex, 
            iter->second.size(),
            valid_pools.c_str(),
            latest_statisticed_height_);
        return kPoolsError;
    }

    for (uint32_t tmp_pool_idx = 0; tmp_pool_idx < common::kInvalidPoolIndex; ++tmp_pool_idx) {
        if (iter->second[tmp_pool_idx].statistic_max_height == 0llu) {
            for (auto tmp_iter = pool_statistic_height_with_block_height_map_.begin(); 
                    tmp_iter != pool_statistic_height_with_block_height_map_.end(); ++tmp_iter) {
                if (tmp_iter->first > iter->first) {
                    auto tmp_pool_iter = tmp_iter->second.find(tmp_pool_idx);
                    if (tmp_pool_iter != tmp_iter->second.end()) {
                        iter->second[tmp_pool_idx].statistic_max_height = tmp_pool_iter->second;
                    }
                     
                    break;
                }
            }
        }
        
        if (iter->second[tmp_pool_idx].statistic_min_height >
                iter->second[tmp_pool_idx].statistic_max_height) {
            ZJC_INFO("pool: %d, statistic height: %lu, min height: %lu, max height: %lu",
                tmp_pool_idx,
                iter->first,
                iter->second[tmp_pool_idx].statistic_min_height,
                iter->second[tmp_pool_idx].statistic_max_height);
            for (auto tmp_iter = pool_statistic_height_with_block_height_map_.begin(); 
                    tmp_iter != pool_statistic_height_with_block_height_map_.end(); ++tmp_iter) {
                auto tmp_pool_iter = tmp_iter->second.find(tmp_pool_idx);
                if (tmp_pool_iter != tmp_iter->second.end()) {
                    ZJC_INFO("pool: %u, statistic height: %lu, block height: %lu",
                        tmp_pool_idx, tmp_iter->first, tmp_pool_iter->second);
                }
            }

            return kPoolsError;
        }
    }

    auto exist_iter = statistic_height_map_.find(iter->first);
    if (exist_iter != statistic_height_map_.end()) {
        elect_statistic = exist_iter->second;
        ZJC_INFO("success get exists statistic message "
            "prev_timeblock_height_: %lu, statisticed_timeblock_height: %lu, "
            "now tm height: %lu, statistic: %s",
            prev_timeblock_height_,
            statisticed_timeblock_height,
            latest_timeblock_height_,
            ProtobufToJson(elect_statistic).c_str());
        return kPoolsSuccess;
    }
    
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    uint32_t local_net_id = common::GlobalInfo::Instance()->network_id();
    if (local_net_id >= network::kConsensusShardEndNetworkId) {
        local_net_id -= network::kConsensusWaitingShardOffset;
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
        ZJC_INFO("now_elect_members == nullptr.");
        return kPoolsError;
    }

    std::unordered_set<std::string> added_id_set;
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
            added_id_set.insert((*now_elect_members)[i]->id);
            added_id_set.insert((*now_elect_members)[i]->pubkey);
        }
    } else {
        ZJC_INFO("failed get prepare members prepare_elect_height_: %lu", prepare_elect_height_);
    }

    uint64_t all_gas_amount = 0;
    uint64_t root_all_gas_amount = 0;
    auto pool_iter = iter->second.begin();
    auto* statistic_info_ptr = &pool_iter->second;
    all_gas_amount += statistic_info_ptr->all_gas_amount;
    root_all_gas_amount += statistic_info_ptr->root_all_gas_amount;
    auto join_elect_stoke_map = statistic_info_ptr->join_elect_stoke_map;
    auto join_elect_shard_map = statistic_info_ptr->join_elect_shard_map;
    auto height_node_collect_info_map = statistic_info_ptr->height_node_collect_info_map;
    auto id_pk_map = statistic_info_ptr->id_pk_map;
    auto& id_agg_bls_pk_map = statistic_info_ptr->id_agg_bls_pk_map;
    auto& id_agg_bls_pk_proof_map = statistic_info_ptr->id_agg_bls_pk_proof_map;
    std::string debug_str;
    ++pool_iter;
    std::string tx_count_debug_str;
    for (; pool_iter != iter->second.end(); ++pool_iter) {
        tx_count_debug_str += "pool idx: " + std::to_string(pool_iter->first) + ", ";
        auto* statistic_info_ptr = &pool_iter->second;
        all_gas_amount += statistic_info_ptr->all_gas_amount;
        root_all_gas_amount += statistic_info_ptr->root_all_gas_amount;
        debug_str += "pool: " + std::to_string(pool_iter->first) + 
            ", gas_amount: " + std::to_string(statistic_info_ptr->root_all_gas_amount) + 
            ", root_all_gas_amount: " + std::to_string(statistic_info_ptr->root_all_gas_amount) +
            ", join_elect_stoke_map height: ";
        for (auto titer = statistic_info_ptr->join_elect_stoke_map.begin(); 
                titer != statistic_info_ptr->join_elect_stoke_map.end(); ++titer) {
            debug_str += std::to_string(titer->first) + ",";
        }

        debug_str += ", join_elect_shard_map height: ";
        for (auto titer = statistic_info_ptr->join_elect_shard_map.begin(); 
                titer != statistic_info_ptr->join_elect_shard_map.end(); ++titer) {
            debug_str += std::to_string(titer->first) + ",";
        }

        debug_str += ", height_node_collect_info_map height: ";
        for (auto titer = statistic_info_ptr->height_node_collect_info_map.begin(); 
                titer != statistic_info_ptr->height_node_collect_info_map.end(); ++titer) {
            debug_str += std::to_string(titer->first) + ",";
        }

        for (auto h_join_elect_stoke_iter = statistic_info_ptr->join_elect_stoke_map.begin();
                h_join_elect_stoke_iter != statistic_info_ptr->join_elect_stoke_map.end(); 
                ++h_join_elect_stoke_iter) {
            auto tmp_iter = join_elect_stoke_map.find(h_join_elect_stoke_iter->first);
            if (tmp_iter == join_elect_stoke_map.end()) {
                join_elect_stoke_map[h_join_elect_stoke_iter->first] = h_join_elect_stoke_iter->second;
            } else {
                for (auto join_elect_stoke_iter = h_join_elect_stoke_iter->second.begin();
                        join_elect_stoke_iter != h_join_elect_stoke_iter->second.end();
                        ++join_elect_stoke_iter) {
                    auto stoke_iter = tmp_iter->second.find(join_elect_stoke_iter->first);
                    if (stoke_iter == tmp_iter->second.end()) {
                        tmp_iter->second[join_elect_stoke_iter->first] = join_elect_stoke_iter->second;
                    } else {
                        stoke_iter->second += join_elect_stoke_iter->second;
                    }
                }
            }
        }

        // join_elect_stoke_map.insert(
        //     statistic_info_ptr->join_elect_stoke_map.begin(), 
        //     statistic_info_ptr->join_elect_stoke_map.end());
        for (auto h_join_elect_shard_iter = statistic_info_ptr->join_elect_shard_map.begin();
                h_join_elect_shard_iter != statistic_info_ptr->join_elect_shard_map.end(); 
                ++h_join_elect_shard_iter) {
            auto tmp_iter = join_elect_shard_map.find(h_join_elect_shard_iter->first);
            if (tmp_iter == join_elect_shard_map.end()) {
                join_elect_shard_map[h_join_elect_shard_iter->first] = h_join_elect_shard_iter->second;
            } else {
                for (auto join_elect_shard_iter = h_join_elect_shard_iter->second.begin();
                        join_elect_shard_iter != h_join_elect_shard_iter->second.end();
                        ++join_elect_shard_iter) {
                    auto stoke_iter = tmp_iter->second.find(join_elect_shard_iter->first);
                    if (stoke_iter == tmp_iter->second.end()) {
                        tmp_iter->second[join_elect_shard_iter->first] = join_elect_shard_iter->second;
                    } else {
                        ZJC_ERROR("invalid pk and shard id: %s, %u",
                            common::Encode::HexEncode(join_elect_shard_iter->first).c_str(), 
                            join_elect_shard_iter->second);
                        assert(false);
                    }
                }
            }
        }

        // join_elect_shard_map.insert(
        //     statistic_info_ptr->join_elect_shard_map.begin(), 
        //     statistic_info_ptr->join_elect_shard_map.end());
        for (auto h_height_node_collect_info_iter = statistic_info_ptr->height_node_collect_info_map.begin();
                h_height_node_collect_info_iter != statistic_info_ptr->height_node_collect_info_map.end(); 
                ++h_height_node_collect_info_iter) {
            tx_count_debug_str += "height: " + std::to_string(h_height_node_collect_info_iter->first) + ", ";
            auto tmp_iter = height_node_collect_info_map.find(h_height_node_collect_info_iter->first);
            if (tmp_iter == height_node_collect_info_map.end()) {
                height_node_collect_info_map[h_height_node_collect_info_iter->first] = h_height_node_collect_info_iter->second;
            } else {
                for (auto height_node_collect_info_iter = h_height_node_collect_info_iter->second.begin();
                        height_node_collect_info_iter != h_height_node_collect_info_iter->second.end();
                        ++height_node_collect_info_iter) {
                    tx_count_debug_str += "id: " + common::Encode::HexEncode(height_node_collect_info_iter->first) + 
                        ": " + std::to_string(height_node_collect_info_iter->second.tx_count) + ", ";
                    auto stoke_iter = tmp_iter->second.find(height_node_collect_info_iter->first);
                    if (stoke_iter == tmp_iter->second.end()) {
                        tmp_iter->second[height_node_collect_info_iter->first] = height_node_collect_info_iter->second;
                    } else {
                        stoke_iter->second.tx_count += height_node_collect_info_iter->second.tx_count;
                        stoke_iter->second.gas_sum += height_node_collect_info_iter->second.gas_sum;
                    }
                }
            }
        }
        // height_node_collect_info_map.insert(
        //     statistic_info_ptr->height_node_collect_info_map.begin(), 
        //     statistic_info_ptr->height_node_collect_info_map.end());
        id_pk_map.insert(
            statistic_info_ptr->id_pk_map.begin(), 
            statistic_info_ptr->id_pk_map.end());
    }

    // 为当前委员会的节点填充共识工作的奖励信息
    setElectStatistics(height_node_collect_info_map, now_elect_members, elect_statistic, is_root);
    addNewNode2JoinStatics(
        join_elect_stoke_map,
        join_elect_shard_map,
        added_id_set,
        id_pk_map,
        id_agg_bls_pk_map,
        id_agg_bls_pk_proof_map,
        elect_statistic);
    addPrepareMembers2JoinStastics(
        prepare_members,
        added_id_set,
        elect_statistic,
        now_elect_members);
    if (is_root) {
        elect_statistic.set_gas_amount(root_all_gas_amount);
    } else {
        elect_statistic.set_gas_amount(all_gas_amount);
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    elect_statistic.set_sharding_id(net_id);
    elect_statistic.set_statistic_height(piter->first);
    auto *heights_info = elect_statistic.mutable_height_info();
    heights_info->set_tm_height(piter->first);
    for (uint32_t tmp_pool_idx = 0; tmp_pool_idx < common::kInvalidPoolIndex; ++tmp_pool_idx) {
        auto* height_item = heights_info->add_heights();
        height_item->set_min_height(iter->second[tmp_pool_idx].statistic_min_height);
        height_item->set_max_height(iter->second[tmp_pool_idx].statistic_max_height);
    }

    ZJC_INFO("success create statistic message prev_timeblock_height_: %lu, "
        "statisticed_timeblock_height: %lu, "
        "now tm height: %lu, statistic: %s, new statistic height: %lu, "
        "now satistic height: %lu, statistic with height now: %s, tx_count_debug_str: %s",
        prev_timeblock_height_,
        statisticed_timeblock_height,
        latest_timeblock_height_,
        ProtobufToJson(elect_statistic).c_str(),
        piter->first,
        iter->first, 
        debug_str.c_str(), 
        tx_count_debug_str.c_str());
    assert(piter->first > iter->first);
    statistic_height_map_[iter->first] = elect_statistic;
    CHECK_MEMORY_SIZE(statistic_height_map_);
    auto handled_height = iter->first;
    auto eiter = statistic_pool_info_.find(handled_height);
    statistic_pool_info_.erase(eiter);
    return kPoolsSuccess;
}

void ShardStatistic::addHeightInfo2Statics(
        shardora::pools::protobuf::ElectStatistic &elect_statistic, 
        uint64_t max_tm_height) {
    auto *heights_info = elect_statistic.mutable_height_info();
    heights_info->set_tm_height(max_tm_height);
}

void ShardStatistic::addPrepareMembers2JoinStastics(
        shardora::common::MembersPtr &prepare_members,
        std::unordered_set<std::string> &added_id_set,
        shardora::pools::protobuf::ElectStatistic &elect_statistic,
        shardora::common::MembersPtr &now_elect_members) {
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < prepare_members->size(); ++i) {
            auto inc_iter = added_id_set.find((*prepare_members)[i]->pubkey);
            if (inc_iter != added_id_set.end()) {
                // ZJC_INFO("id is in elect: %s", common::Encode::HexEncode(
                //     secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str());
                continue;
            }

            uint32_t shard = common::GlobalInfo::Instance()->network_id();
            uint64_t stoke = 0;
            auto join_elect_node = elect_statistic.add_join_elect_nodes();
            join_elect_node->set_consensus_gap(0);
            join_elect_node->set_credit(0);
            join_elect_node->set_pubkey((*prepare_members)[i]->pubkey);
            // agg bls pk
            auto agg_bls_pk_proto = bls::BlsPublicKey2Proto((*prepare_members)[i]->agg_bls_pk);
            if (agg_bls_pk_proto) {
                join_elect_node->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk_proto);
            }
            auto proof_proto = bls::BlsPopProof2Proto((*prepare_members)[i]->agg_bls_pk_proof);
            if (proof_proto) {
                join_elect_node->mutable_agg_bls_pk_proof()->CopyFrom(*proof_proto);
            }            

            join_elect_node->set_elect_pos(0);
            join_elect_node->set_stoke(stoke);
            join_elect_node->set_shard(shard);
            ZJC_INFO("add node to election prepare member: %s, %s, stoke: %lu, shard: %u, elect pos: %d",
                      common::Encode::HexEncode((*prepare_members)[i]->pubkey).c_str(),
                      common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str(),
                      stoke, shard,
                      0);
        }
    }

    if (prepare_members != nullptr) {
        ZJC_INFO("kJoinElect add new elect node now elect_height: %lu, prepare elect height: %lu, "
            "new nodes size: %u, now members size: %u, prepare members size: %u",
            now_elect_height_,
            prepare_elect_height_,
            elect_statistic.join_elect_nodes_size(),
            now_elect_members->size(),
            prepare_members->size());
    }
}

void ShardStatistic::addNewNode2JoinStatics(
        std::map<uint64_t, std::unordered_map<std::string, uint64_t>> &join_elect_stoke_map,
        std::map<uint64_t, std::unordered_map<std::string, uint32_t>> &join_elect_shard_map,
        std::unordered_set<std::string> &added_id_set,
        std::unordered_map<std::string, std::string> &id_pk_map,
        std::unordered_map<std::string, std::shared_ptr<elect::protobuf::BlsPublicKey>> &id_agg_bls_pk_map,
        std::unordered_map<std::string, std::shared_ptr<elect::protobuf::BlsPopProof>> &id_agg_bls_pk_proof_map,
        shardora::pools::protobuf::ElectStatistic &elect_statistic) {
#ifndef NDEBUG
    for (auto iter = join_elect_stoke_map.begin(); iter != join_elect_stoke_map.end(); ++iter) {
        for (auto id_iter = iter->second.begin(); id_iter != iter->second.end(); ++id_iter) {
            ZJC_INFO("stoke map eh: %lu, id: %s, stoke: %lu",
                iter->first,
                common::Encode::HexEncode(id_iter->first).c_str(),
                id_iter->second);
        }
    }

    for (auto iter = join_elect_shard_map.begin(); iter != join_elect_shard_map.end(); ++iter) {
        for (auto id_iter = iter->second.begin(); id_iter != iter->second.end(); ++id_iter) {
            ZJC_INFO("shard map eh: %lu, id: %s, shard: %u",
                iter->first,
                common::Encode::HexEncode(id_iter->first).c_str(),
                id_iter->second);
        }
    }
#endif
    std::vector<std::string> elect_nodes; // collect new ndoe
    auto r_eiter = join_elect_stoke_map.rbegin();
    auto r_siter = join_elect_shard_map.rbegin();
    if (r_eiter != join_elect_stoke_map.rend() &&
            r_siter != join_elect_shard_map.rend() &&
            r_eiter->first == r_siter->first) {
        const auto &stoke_map = r_eiter->second;
        const auto &shard_map = r_siter->second;
        for (auto &[node_id, stoke] : stoke_map) {
            if (shard_map.count(node_id) == 0) {
                ZJC_INFO("failed get shard: %s", common::Encode::HexEncode(node_id).c_str());
                continue;
            }

            if (added_id_set.count(node_id) > 0) {
                // 说明节点是之前的委员会成员 。
                ZJC_INFO("not added id: %s", common::Encode::HexEncode(node_id).c_str());
                continue;
            }

            elect_nodes.push_back(node_id);
            ZJC_INFO("elect nodes add: %s, %lu",
                      common::Encode::HexEncode(node_id).c_str(), stoke);
            added_id_set.insert(node_id);
        }
    }

    for (uint32_t i = 0; i < elect_nodes.size() && i < kWaitingElectNodesMaxCount; ++i) {
        std::string node_id = elect_nodes[i];
        std::string pubkey;
        std::shared_ptr<elect::protobuf::BlsPublicKey> agg_bls_pk;
        std::shared_ptr<elect::protobuf::BlsPopProof> agg_bls_pk_proof;
        if (node_id.size() == common::kUnicastAddressLength) {
            auto iter = id_pk_map.find(node_id);
            if (iter == id_pk_map.end()) {
                assert(false);
                continue;
            }

            auto iter2 = id_agg_bls_pk_map.find(node_id);
            if (iter2 == id_agg_bls_pk_map.end()) {
                assert(false);
                continue;
            }

            auto iter3 = id_agg_bls_pk_proof_map.find(node_id);
            if (iter3 == id_agg_bls_pk_proof_map.end()) {
                assert(false);
                continue;
            }            

            pubkey = iter->second;
            agg_bls_pk = iter2->second;
            agg_bls_pk_proof = iter3->second; 
        }

        auto shard_iter = r_siter->second.find(elect_nodes[i]);
        auto shard_id = shard_iter->second;
        auto stoke = 0;
        auto join_elect_node = elect_statistic.add_join_elect_nodes();
        auto iter = r_eiter->second.find(elect_nodes[i]);
        join_elect_node->set_consensus_gap(0);
        join_elect_node->set_credit(0);
        join_elect_node->set_pubkey(pubkey);
        if (agg_bls_pk) {
            join_elect_node->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk);
        }
        
        if (agg_bls_pk_proof) {
            join_elect_node->mutable_agg_bls_pk_proof()->CopyFrom(*agg_bls_pk_proof);
        }

        join_elect_node->set_stoke(stoke);
        join_elect_node->set_shard(shard_id);
        join_elect_node->set_elect_pos(0);
        ZJC_INFO("add node to election new member: %s, %s, stoke: %lu, shard: %u, elect pos: %d",
                  common::Encode::HexEncode(pubkey).c_str(),
                  common::Encode::HexEncode(secptr_->GetAddress(pubkey)).c_str(),
                  iter->second, shard_iter->second,
                  0);
        ZJC_INFO("add new elect node: %s, stoke: %lu, shard: %u",
            common::Encode::HexEncode(pubkey).c_str(), iter->second, shard_iter->second);
    }
}

void ShardStatistic::setElectStatistics(
        std::map<uint64_t, std::unordered_map<std::string, shardora::pools::StatisticMemberInfoItem>> &height_node_collect_info_map,
        shardora::common::MembersPtr &now_elect_members,
        shardora::pools::protobuf::ElectStatistic &elect_statistic,
        bool is_root) {
    if (height_node_collect_info_map.empty() || height_node_collect_info_map.rbegin()->first < now_elect_height_) {
        height_node_collect_info_map[now_elect_height_] = std::unordered_map<std::string, StatisticMemberInfoItem>();
        auto &node_info_map = height_node_collect_info_map[now_elect_height_];
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
            node_info_map[(*now_elect_members)[i]->id] = StatisticMemberInfoItem();
        }
    }

    for (auto hiter = height_node_collect_info_map.begin();
            hiter != height_node_collect_info_map.end(); ++hiter) {
        auto &node_info_map = hiter->second;
        auto &statistic_item = *elect_statistic.add_statistics();
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            hiter->first,
            common::GlobalInfo::Instance()->network_id(),
            nullptr,
            nullptr);
        if (members == nullptr) {
            continue;
        }

        for (uint32_t midx = 0; midx < members->size(); ++midx) {
            auto &id = (*members)[midx]->id;
            auto node_info = node_info_map.emplace(id, StatisticMemberInfoItem()).first->second;
            auto node_poce_info = accout_poce_info_map_.try_emplace(
                id, std::make_shared<AccoutPoceInfoItem>()).first->second;
            CHECK_MEMORY_SIZE(accout_poce_info_map_);
            statistic_item.add_credit(0);  // (node_poce_info->credit);
            statistic_item.add_consensus_gap(0);  // (node_poce_info->consensus_gap);
            statistic_item.add_tx_count(node_info.tx_count);
            statistic_item.add_gas_sum(node_info.gas_sum);
            uint64_t stoke = 0;
            if (!is_root) {
                prefix_db_->GetElectNodeMinStoke(
                    common::GlobalInfo::Instance()->network_id(), id, &stoke);
            }
            auto from = common::Encode::HexEncode(id);
            stoke = 0;
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
        }

        statistic_item.set_elect_height(hiter->first);
    }
}

}  // namespace pools

}  // namespace shardora
