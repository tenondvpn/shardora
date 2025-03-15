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
#include <elect/elect_pledge.h>
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
        ZJC_DEBUG("failed load latest pool statistic tag.");
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

    auto* block_ptr = &view_block_ptr->block_info();
    ZJC_DEBUG("new block coming net: %u, pool: %u, height: %lu, timeblock height: %lu",
        view_block_ptr->qc().network_id(),
        view_block_ptr->qc().pool_index(),
        block_ptr->height(),
        block_ptr->timeblock_height());
    auto& block = *block_ptr;
    if (!network::IsSameToLocalShard(view_block_ptr->qc().network_id())) {
        return;
    }
    
    const auto& tx_list = block.tx_list();
    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() != consensus::kConsensusSuccess) {
            continue;
        }

        // ZJC_DEBUG("handle statsitic block %u_%u_%lu, "
        //     "block height: %lu, tm height: %lu, gid: %s, step: %d", 
        //     view_block_ptr->qc().network_id(),
        //     view_block_ptr->qc().pool_index(),
        //     view_block_ptr->qc().view(),
        //     view_block_ptr->block_info().height(),
        //     view_block_ptr->block_info().timeblock_height(),
        //     common::Encode::HexEncode(tx_list[i].gid()).c_str(),
        //     tx_list[i].step());
        if (tx_list[i].step() == pools::protobuf::kStatistic) {
            HandleStatisticBlock(block, tx_list[i]);
        }
    }

    auto& pool_blocks_info = pools_consensus_blocks_[view_block_ptr->qc().pool_index()];
    if (block_ptr->height() != pool_blocks_info->latest_consensus_height_ + 1) {
        pool_blocks_info->blocks[block_ptr->height()] = view_block_ptr;
        ZJC_DEBUG("pool latest height not continus: %lu, %lu",
            block_ptr->height(), pool_blocks_info->latest_consensus_height_);
    } else {
        HandleStatistic(view_block_ptr);
        pool_blocks_info->latest_consensus_height_ = block_ptr->height();
        cleanUpBlocks(*pool_blocks_info);
    }

    {
        uint64_t first_block_tm_height = common::kInvalidUint64;
        uint64_t first_block_elect_height = common::kInvalidUint64;
        auto& block_map = pool_blocks_info->blocks;
        if (!block_map.empty()) {
            first_block_tm_height = block_map.begin()->second->block_info().timeblock_height();
            first_block_elect_height = block_map.begin()->second->qc().elect_height();
        }

        auto latest_elect_item = elect_mgr_->GetLatestElectBlock(common::GlobalInfo::Instance()->network_id());
        ZJC_DEBUG(
            "block coming pool: %u, height: %lu, latest height: %lu, "
            "block map size: %u, first_block_tm_height: %lu, "
            "first_block_elect_height: %lu, now elect height: %lu, "
            "block_map.empty(): %d, block tm height: %lu, block elect height: %lu",
            view_block_ptr->qc().pool_index(),
            block_ptr->height(),
            pool_blocks_info->latest_consensus_height_,
            block_map.size(),
            first_block_tm_height,
            first_block_elect_height,
            latest_elect_item->elect_height(),
            block_map.empty(),
            block_ptr->timeblock_height(),
            view_block_ptr->qc().elect_height());
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

void ShardStatistic::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    ZJC_DEBUG("handle statistic block now size: %u", tx.storages_size());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("handle statistic block now key: %s", tx.storages(i).key().c_str());
        if (tx.storages(i).key() == protos::kShardStatistic) {
            pools::protobuf::ElectStatistic elect_statistic;
            if (!elect_statistic.ParseFromString(tx.storages(i).value())) {
                ZJC_ERROR("get statistic val failed: %s",
                    common::Encode::HexEncode(tx.storages(i).value()).c_str());
                assert(false);
                return;
            }

            ZJC_DEBUG("success handle statistic block: %s, latest_statisticed_height_: %lu",
                ProtobufToJson(elect_statistic).c_str(), latest_statisticed_height_);
            auto& heights = elect_statistic.height_info();
            auto st_iter = statistic_pool_info_.begin();
            while (st_iter != statistic_pool_info_.end()) {
                if (st_iter->first >= latest_statisticed_height_) {
                    break;
                }
                    
                ZJC_DEBUG("erase statistic height: %lu", st_iter->first);
                st_iter = statistic_pool_info_.erase(st_iter);
                CHECK_MEMORY_SIZE(statistic_pool_info_);
            }

            latest_statisticed_height_ = elect_statistic.statistic_height();
            // auto iter = tm_height_with_statistic_info_.find(heights.tm_height());
            // if (iter != tm_height_with_statistic_info_.end()) {
            //     tm_height_with_statistic_info_.erase(iter);
            // }

            // prefix_db_->SaveStatisticLatestHeihgts(
            //     common::GlobalInfo::Instance()->network_id(),
            //     heights);
            latest_statistic_item_ = std::make_shared<pools::protobuf::StatisticTxItem>(heights);
            break;
        }
    }
}

void ShardStatistic::HandleStatistic(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
    auto& block = view_block_ptr->block_info();
    bool is_root = (
        common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
        common::GlobalInfo::Instance()->network_id() ==
        network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset);
    if (block.timeblock_height() < latest_timeblock_height_) {
        ZJC_DEBUG("timeblock not less than latest timeblock: %lu, %lu", 
            block.timeblock_height(), latest_timeblock_height_);
    }

    auto pool_idx = view_block_ptr->qc().pool_index();
    std::string statistic_pool_debug_str;
    for (auto riter = statistic_pool_info_.rbegin();
            riter != statistic_pool_info_.rend(); ++riter) {
        statistic_pool_debug_str += "statistic height: " + std::to_string(riter->first);
        for (auto pool_iter = riter->second.begin(); pool_iter != riter->second.end(); ++pool_iter) {
            statistic_pool_debug_str += ", " + 
                std::to_string(pool_iter->first) + ":" + 
                std::to_string(pool_iter->second.statistic_max_height) + ",";
        }
    }

    auto pool_statistic_riter = statistic_pool_info_.rbegin();
    while (pool_statistic_riter != statistic_pool_info_.rend()) {
        auto pool_iter = pool_statistic_riter->second.find(pool_idx);
        ZJC_DEBUG("check elect height: %lu, pool: %u, block height: %lu, find: %d",
            pool_iter->first, pool_idx, block.height(), 
            (pool_iter != pool_statistic_riter->second.end()));
        if (pool_iter != pool_statistic_riter->second.end()) {
            ZJC_DEBUG("pool: %u, get block height and statistic height: %lu, max_height: %lu",
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
        assert(false);
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
    auto& join_elect_stoke_map = statistic_info_ptr->join_elect_stoke_map;
    auto& join_elect_shard_map = statistic_info_ptr->join_elect_shard_map;
    auto& height_node_collect_info_map = statistic_info_ptr->height_node_collect_info_map;
    auto& id_pk_map = statistic_info_ptr->id_pk_map;
    auto& id_agg_bls_pk_map = statistic_info_ptr->id_agg_bls_pk_map;
    auto& id_agg_bls_pk_proof_map = statistic_info_ptr->id_agg_bls_pk_proof_map;
    uint64_t block_gas = 0;
    auto callback = [&](const block::protobuf::Block& block) {
        for (int32_t i = 0; i < block.tx_list_size(); ++i) {
            auto& tx = block.tx_list(i);
            if (tx.step() == pools::protobuf::kPoolStatisticTag) {
                uint64_t statistic_height = 0;
                for (int32_t storage_idx = 0; storage_idx < tx.storages_size(); ++storage_idx) {
                    if (tx.storages(storage_idx).key() == protos::kPoolStatisticTag) {
                        uint64_t* udata = (uint64_t*)tx.storages(storage_idx).value().c_str();
                        statistic_height = udata[0];
                        break;
                    }
                }

                auto exist_iter = statistic_pool_info_.find(statistic_height);
                if (exist_iter == statistic_pool_info_.end()) {
                    StatisticInfoItem statistic_item;
                    statistic_item.statistic_min_height = block.height() + 1;
                    std::map<uint32_t, StatisticInfoItem> pool_map;
                    pool_map[pool_idx] = statistic_item;
                    statistic_pool_info_[statistic_height] = pool_map;
                    CHECK_MEMORY_SIZE(statistic_pool_info_);
                    ZJC_DEBUG(
                        "new success handle kPoolStatisticTag tx statistic_height: %lu, "
                        "pool: %u, height: %lu, statistic_max_height: %lu, gid: %s", 
                        statistic_height, 
                        pool_idx, 
                        block.height(), 
                        statistic_item.statistic_min_height,
                        common::Encode::HexEncode(tx.gid()).c_str());
                } else {
                    StatisticInfoItem statistic_item;
                    statistic_item.statistic_min_height = block.height() + 1;
                    exist_iter->second[pool_idx] = statistic_item;
                    ZJC_DEBUG(
                        "exists success handle kPoolStatisticTag tx statistic_height: %lu, "
                        "pool: %u, height: %lu, statistic_max_height: %lu, gid: %s", 
                        statistic_height, 
                        pool_idx, 
                        block.height(), 
                        statistic_item.statistic_min_height,
                        common::Encode::HexEncode(tx.gid()).c_str());
                }

                statistic_info_ptr->statistic_max_height = block.height();
            }

            if (tx.step() == pools::protobuf::kNormalFrom ||
                    tx.step() == pools::protobuf::kCreateLibrary || 
                    tx.step() == pools::protobuf::kContractCreate ||
                    tx.step() == pools::protobuf::kContractCreateByRootFrom ||
                    tx.step() == pools::protobuf::kContractExcute ||
                    tx.step() == pools::protobuf::kJoinElect ||
                    tx.step() == pools::protobuf::kContractGasPrepayment) {
                block_gas += tx.gas_price() * tx.gas_used();
            }

            if (tx.status() != consensus::kConsensusSuccess) {
                ZJC_DEBUG("success handle block pool: %u, height: %lu, "
                    "tm height: %lu, tx: %d, status: %d", 
                    view_block_ptr->qc().pool_index(), block.height(), 
                    block.timeblock_height(), i, tx.status());
                continue;
            }

            if (tx.step() == pools::protobuf::kJoinElect) {
                ZJC_DEBUG("join elect tx comming.");
                for (int32_t storage_idx = 0;
                        storage_idx < tx.storages_size(); ++storage_idx) {
                    if (tx.storages(storage_idx).key() == protos::kElectNodeStoke) {
                        auto eiter = join_elect_stoke_map.find(view_block_ptr->qc().elect_height());
                        if (eiter == join_elect_stoke_map.end()) {
                            join_elect_stoke_map[view_block_ptr->qc().elect_height()] = 
                                std::unordered_map<std::string, uint64_t>();
                        }

                        auto& elect_stoke_map = join_elect_stoke_map[view_block_ptr->qc().elect_height()];
                        uint64_t* tmp_stoke = (uint64_t*)tx.storages(
                            storage_idx).value().c_str();
                        elect_stoke_map[tx.from()] = tmp_stoke[0];
                        ZJC_DEBUG("success add elect node stoke %s, %lu, "
                            "elect height: %lu, tm height: %lu",
                            common::Encode::HexEncode(tx.from()).c_str(), 
                            tmp_stoke[0],
                            view_block_ptr->qc().elect_height(),
                            block.timeblock_height());
                    }

                    if (tx.storages(storage_idx).key() == protos::kNodePublicKey) {
                        auto tmp_id = secptr_->GetAddress(
                            tx.storages(storage_idx).value());
                        if (tmp_id != tx.from()) {
                            assert(false);
                            continue;
                        }

                        id_pk_map[tx.from()] = tx.storages(storage_idx).value();
                    }

                    if (block.tx_list(i).storages(storage_idx).key() == protos::kAggBlsPublicKey) {
                        auto agg_bls_pk_proto_str = block.tx_list(i).storages(storage_idx).value();
                        auto agg_bls_pk_proto = std::make_shared<elect::protobuf::BlsPublicKey>();
                        if (agg_bls_pk_proto->ParseFromString(agg_bls_pk_proto_str)) {
                            id_agg_bls_pk_map[block.tx_list(i).from()] = agg_bls_pk_proto.get();
                        }
                    }

                    if (block.tx_list(i).storages(storage_idx).key() == protos::kAggBlsPopProof) {
                        auto proof_proto_str = block.tx_list(i).storages(storage_idx).value();
                        auto proof_proto = std::make_shared<elect::protobuf::BlsPopProof>();
                        if (proof_proto->ParseFromString(proof_proto_str)) {
                            id_agg_bls_pk_proof_map[block.tx_list(i).from()] = proof_proto.get();
                        }
                    }                    

                    if (tx.storages(storage_idx).key() == protos::kJoinElectVerifyG2) {
                        bls::protobuf::JoinElectInfo join_info;
                        if (!join_info.ParseFromString(
                                tx.storages(storage_idx).value())) {
                            assert(false);
                            break;
                        }

                    auto shard_iter = join_elect_shard_map.find(view_block_ptr->qc().elect_height());
                    if (shard_iter == join_elect_shard_map.end()) {
                        join_elect_shard_map[view_block_ptr->qc().elect_height()] =
                            std::unordered_map<std::string, uint32_t>();
                    }

                    auto& elect_shard_map = join_elect_shard_map[view_block_ptr->qc().elect_height()];
                    elect_shard_map[tx.from()] = join_info.shard_id();
                    ZJC_DEBUG("kJoinElect add new elect node: %s, shard: %u, pool: %u, "
                        "height: %lu, elect height: %lu, tm height: %lu",
                        common::Encode::HexEncode(tx.from()).c_str(),
                        join_info.shard_id(),
                        view_block_ptr->qc().pool_index(),
                        block.height(),
                        view_block_ptr->qc().elect_height(),
                        block.timeblock_height());
                    }
                }
            }

            if (tx.step() == pools::protobuf::kConsensusRootElectShard && is_root) {
                ZJC_DEBUG("success handle kConsensusRootElectShard");
                for (int32_t storage_idx = 0;
                        storage_idx < tx.storages_size(); ++storage_idx) {
                    if (tx.storages(storage_idx).key() ==
                            protos::kElectNodeAttrElectBlock) {
                        elect::protobuf::ElectBlock elect_block;
                        if (!elect_block.ParseFromString(
                                tx.storages(storage_idx).value())) {
                            assert(false);
                            break;
                        }

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
                            ZJC_DEBUG("HandleElectStatistic addr: %s, consensus_gap: %lu, credit: %lu",
                                common::Encode::HexEncode(addr).c_str(), 
                                accoutPoceInfoIterm->consensus_gap, 
                                accoutPoceInfoIterm->credit);
                        }
                    }

                    if (tx.storages(storage_idx).key() == protos::kShardElection) {
                        uint64_t* tmp = (uint64_t*)tx.storages(storage_idx).value().c_str();
                        pools::protobuf::ElectStatistic elect_statistic;
                        if (!prefix_db_->GetStatisticedShardingHeight(
                                tmp[0],
                                tmp[1],
                                &elect_statistic)) {
                            ZJC_DEBUG("get statistic elect statistic failed! net: %u, height: %lu",
                                tmp[0],
                                tmp[1]);
                            break;
                        }

                        for (int32_t node_idx = 0;
                                node_idx < elect_statistic.join_elect_nodes_size(); ++node_idx) {
                            ZJC_DEBUG("success get shard election: %lu, %lu, "
                                "join nodes size: %u, shard: %u",
                                tmp[0], tmp[1], elect_statistic.join_elect_nodes_size(),
                                elect_statistic.join_elect_nodes(node_idx).shard());
                            if (elect_statistic.join_elect_nodes(node_idx).shard() ==
                                    network::kRootCongressNetworkId) {
                                auto eiter = join_elect_stoke_map.find(view_block_ptr->qc().elect_height());
                                if (eiter == join_elect_stoke_map.end()) {
                                    join_elect_stoke_map[view_block_ptr->qc().elect_height()] =
                                        std::unordered_map<std::string, uint64_t>();
                                }

                                auto& elect_stoke_map = join_elect_stoke_map[view_block_ptr->qc().elect_height()];
                                uint64_t* tmp_stoke = (uint64_t*)tx.storages(
                                        storage_idx).value().c_str();
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
                                ZJC_DEBUG("root sharding kJoinElect add new elect node: %s, "
                                    "stoke: %lu, elect height: %lu",
                                    common::Encode::HexEncode(
                                        elect_statistic.join_elect_nodes(node_idx).pubkey()).c_str(),
                                    elect_statistic.join_elect_nodes(node_idx).stoke(),
                                    view_block_ptr->qc().elect_height());
                            }
                        }
                    }
                }
            }
        }
    };
    
    callback(block);
    statistic_info_ptr->all_gas_amount += block_gas;
    std::string leader_id = getLeaderIdFromBlock(*view_block_ptr);
    if (leader_id.empty()) {
        // assert(false);
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
    node_info.gas_sum += block_gas;
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

    ZJC_DEBUG("statistic height: %lu, success handle block pool: %u, height: %lu, "
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
        ZJC_DEBUG("block leader not exit block.hash %s block.electHeight:%d, network_id:%d ",
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

    ZJC_DEBUG("new timeblcok coming and should statistic new tx %lu, %lu.", 
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

        // ZJC_DEBUG("failed iter == statistic_pool_info_.end() piter: %d, iter: %d, "
        //     "piter_debug_str: %s, iter_debug_str: %s, iter->first: %lu, latest_statisticed_height_: %lu",
        //     (piter == statistic_pool_info_.rend()), 
        //     (iter == statistic_pool_info_.rend()),
        //     piter_debug_str.c_str(),
        //     iter_debug_str.c_str(),
        //     iter->first,
        //     latest_statisticed_height_);
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
        ZJC_DEBUG("pool not full: %u, %u, now_size: %u, %s", 
            piter->second.size(), 
            common::kInvalidPoolIndex, 
            iter->second.size(),
            valid_pools.c_str());
        return kPoolsError;
    }

    for (uint32_t tmp_pool_idx = 0; tmp_pool_idx < common::kInvalidPoolIndex; ++tmp_pool_idx) {
        if (iter->second[tmp_pool_idx].statistic_min_height >
                iter->second[tmp_pool_idx].statistic_max_height) {
            ZJC_DEBUG("pool min height: %lu, max height: %lu",
                iter->second[tmp_pool_idx].statistic_min_height,
                iter->second[tmp_pool_idx].statistic_max_height);
            return kPoolsError;
        }
    }

    auto exist_iter = statistic_height_map_.find(iter->first);
    if (exist_iter != statistic_height_map_.end()) {
        elect_statistic = exist_iter->second;
        ZJC_DEBUG("success get exists statistic message "
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
        ZJC_DEBUG("now_elect_members == nullptr.");
        return kPoolsError;
    }

    std::unordered_set<std::string> added_id_set;
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < now_elect_members->size(); ++i) {
            added_id_set.insert((*now_elect_members)[i]->id);
            added_id_set.insert((*now_elect_members)[i]->pubkey);
        }
    } else {
        ZJC_DEBUG("failed get prepare members prepare_elect_height_: %lu", prepare_elect_height_);
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
// tx_count_debug_str: pool idx: 1, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 2, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 3, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 4, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 5, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 6, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 3, pool idx: 7, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 8, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 3, pool idx: 9, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 122, pool idx: 10, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 11, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 12, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 4, pool idx: 13, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 11, pool idx: 14, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 15, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 16, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 9, 
// tx_count_debug_str: pool idx: 1, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 2, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 3, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 4, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 5, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 6, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 7, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 8, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 2, pool idx: 9, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 120, pool idx: 10, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 11, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 12, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 3, pool idx: 13, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 6, pool idx: 14, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 15, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 1, pool idx: 16, height: 1, id: 8c99304613266afcef9b0188701fa0ebdbf23999: 8, 
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

    ZJC_DEBUG("success create statistic message prev_timeblock_height_: %lu, "
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
    auto eiter = statistic_pool_info_.find(iter->first);
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
                // ZJC_DEBUG("id is in elect: %s", common::Encode::HexEncode(
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
            ZJC_DEBUG("add node to election prepare member: %s, %s, stoke: %lu, shard: %u, elect pos: %d",
                      common::Encode::HexEncode((*prepare_members)[i]->pubkey).c_str(),
                      common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str(),
                      stoke, shard,
                      0);
        }
    }

    if (prepare_members != nullptr) {
        ZJC_DEBUG("kJoinElect add new elect node now elect_height: %lu, prepare elect height: %lu, "
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
        std::unordered_map<std::string, elect::protobuf::BlsPublicKey*> &id_agg_bls_pk_map,
        std::unordered_map<std::string, elect::protobuf::BlsPopProof*> &id_agg_bls_pk_proof_map,
        shardora::pools::protobuf::ElectStatistic &elect_statistic) {
#ifndef NDEBUG
    for (auto iter = join_elect_stoke_map.begin(); iter != join_elect_stoke_map.end(); ++iter) {
        for (auto id_iter = iter->second.begin(); id_iter != iter->second.end(); ++id_iter) {
            ZJC_DEBUG("stoke map eh: %lu, id: %s, stoke: %lu",
                iter->first,
                common::Encode::HexEncode(id_iter->first).c_str(),
                id_iter->second);
        }
    }

    for (auto iter = join_elect_shard_map.begin(); iter != join_elect_shard_map.end(); ++iter) {
        for (auto id_iter = iter->second.begin(); id_iter != iter->second.end(); ++id_iter) {
            ZJC_DEBUG("shard map eh: %lu, id: %s, shard: %u",
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
                ZJC_DEBUG("failed get shard: %s", common::Encode::HexEncode(node_id).c_str());
                continue;
            }

            if (added_id_set.count(node_id) > 0) {
                // 说明节点是之前的委员会成员 。
                ZJC_DEBUG("not added id: %s", common::Encode::HexEncode(node_id).c_str());
                continue;
            }

            elect_nodes.push_back(node_id);
            ZJC_DEBUG("elect nodes add: %s, %lu",
                      common::Encode::HexEncode(node_id).c_str(), stoke);
            added_id_set.insert(node_id);
        }
    }

    for (uint32_t i = 0; i < elect_nodes.size() && i < kWaitingElectNodesMaxCount; ++i) {
        std::string node_id = elect_nodes[i];
        std::string pubkey;
        elect::protobuf::BlsPublicKey* agg_bls_pk;
        elect::protobuf::BlsPopProof* agg_bls_pk_proof;
        if (node_id.size() == security::kUnicastAddressLength) {
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
        join_elect_node->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk);
        join_elect_node->mutable_agg_bls_pk_proof()->CopyFrom(*agg_bls_pk_proof);
        join_elect_node->set_stoke(stoke);
        join_elect_node->set_shard(shard_id);
        join_elect_node->set_elect_pos(0);
        ZJC_DEBUG("add node to election new member: %s, %s, stoke: %lu, shard: %u, elect pos: %d",
                  common::Encode::HexEncode(pubkey).c_str(),
                  common::Encode::HexEncode(secptr_->GetAddress(pubkey)).c_str(),
                  iter->second, shard_iter->second,
                  0);
        ZJC_DEBUG("add new elect node: %s, stoke: %lu, shard: %u",
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
