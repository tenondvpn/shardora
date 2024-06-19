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
#include <elect/elect_pledge.h>
// #include <iostream>
// #include "shard_statistic.h"


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

    ZJC_DEBUG("new block coming net: %u, pool: %u, height: %lu, timeblock height: %lu",
        block_ptr->network_id(),
        block_ptr->pool_index(), block_ptr->height(), block_ptr->timeblock_height());
    HandleElectStatistic(block_ptr);
    block::protobuf::Block& block = *block_ptr;
    if (!checkBlockValid(block)) return;


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

    auto& pool_blocks_info = pools_consensus_blocks_[block.pool_index()];
    if (block_ptr->height() != pool_blocks_info->latest_consensus_height_ + 1) {
        pool_blocks_info->blocks[block_ptr->height()] = block_ptr;
    } else {
        HandleStatistic(block_ptr);
        pool_blocks_info->latest_consensus_height_ = block_ptr->height();
        cleanUpBlocks(*pool_blocks_info);
    }

    {
        uint64_t first_block_tm_height = common::kInvalidUint64;
        uint64_t first_block_elect_height = common::kInvalidUint64;
        auto& block_map = pool_blocks_info->blocks;
        if (!block_map.empty()) {
            first_block_tm_height = block_map.begin()->second->timeblock_height();
            first_block_elect_height = block_map.begin()->second->electblock_height();
        }

        auto latest_elect_item = elect_mgr_->GetLatestElectBlock(common::GlobalInfo::Instance()->network_id());
        ZJC_DEBUG(
            "block coming pool: %u, height: %lu, latest height: %lu, "
            "block map size: %u, first_block_tm_height: %lu, "
            "first_block_elect_height: %lu, now elect height: %lu, "
            "block_map.empty(): %d, block tm height: %lu, block elect height: %lu",
            block_ptr->pool_index(),
            block_ptr->height(),
            pool_blocks_info->latest_consensus_height_,
            block_map.size(),
            first_block_tm_height,
            first_block_elect_height,
            latest_elect_item->elect_height(),
            block_map.empty(),
            block_ptr->timeblock_height(),
            block_ptr->electblock_height());
    }
}

bool ShardStatistic::checkBlockValid(shardora::block::protobuf::Block &block)
{
    if (block.network_id() != common::GlobalInfo::Instance()->network_id() &&
        block.network_id() + network::kConsensusWaitingShardOffset != common::GlobalInfo::Instance()->network_id())
    {
        ZJC_DEBUG("network invalid %u, %u",
                  block.network_id(), common::GlobalInfo::Instance()->network_id());
        return false;
    }

    // if (block.tx_list().empty())
    // {
    //     ZJC_DEBUG("tx list empty!");
    //     assert(false);
    //     return false;
    // }
    return true;
}

void ShardStatistic::cleanUpBlocks(PoolBlocksInfo& pool_blocks_info) {
    auto& block_map = pool_blocks_info.blocks;
    for (auto iter = block_map.begin(); iter != block_map.end(); ) {
        if (iter->second->height() <= pool_blocks_info.latest_consensus_height_) {
            iter = block_map.erase(iter);
        } else if (iter->first == pool_blocks_info.latest_consensus_height_ + 1) {
            HandleStatistic(iter->second);
            pool_blocks_info.latest_consensus_height_ = iter->second->height();
            iter = block_map.erase(iter);
        } else {
            ++iter;
        }
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

<<<<<<< HEAD
=======
void ShardStatistic::HandleCrossShard(
        bool is_root,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        pools::protobuf::CrossShardStatistic& cross_statistic) {
    if (tx.status() != consensus::kConsensusSuccess) {
        ZJC_DEBUG("success handle block pool: %u, height: %lu, tm height: %lu, status: %d, step: %d",
            block.pool_index(), block.height(), block.timeblock_height(), tx.status(), tx.step());
        return;
    }

    CrossStatisticItem cross_item;
    switch (tx.step()) {
    case pools::protobuf::kNormalTo: {
        if (!is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kNormalToShards) {
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
        ZJC_DEBUG("succcess add cross statistic shard: %u, pool: %u, height: %lu, des: %u",
            src_shard, block.pool_index(), block.height(), cross_item.des_net);
    } else if (cross_item.cross_ptr != nullptr) {
        for (int32_t i = 0; i < cross_item.cross_ptr->crosses_size(); ++i) {
            auto* proto_cross_item = cross_statistic.add_crosses();
            proto_cross_item->set_src_shard(cross_item.cross_ptr->crosses(i).src_shard());
            proto_cross_item->set_src_pool(cross_item.cross_ptr->crosses(i).src_pool());
            proto_cross_item->set_height(cross_item.cross_ptr->crosses(i).height());
            proto_cross_item->set_des_shard(cross_item.cross_ptr->crosses(i).des_shard());
            ZJC_DEBUG("succcess add cross statistic shard: %u, pool: %u, height: %lu, des: %u",
                cross_item.cross_ptr->crosses(i).src_shard(),
                cross_item.cross_ptr->crosses(i).src_pool(),
                cross_item.cross_ptr->crosses(i).height(),
                cross_item.cross_ptr->crosses(i).des_shard());
        }
    }
}


void ShardStatistic::HandleElectStatistic(const std::shared_ptr<block::protobuf::Block>& block_ptr) {
    auto& block = *block_ptr;
    elect::protobuf::ElectBlock elect_block;
    bool isElectBlock = false;
    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        if (block.tx_list(i).step() == pools::protobuf::kConsensusRootElectShard) { 
           auto block_tx = block.tx_list(i);
           for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                if (block_tx.storages(i).key() == protos::kElectNodeAttrElectBlock) {
                   
                    if (!elect_block.ParseFromString(block_tx.storages(i).value())) {
                        assert(false);
                        return;
                    }
                    isElectBlock = true;
                    break;                   
                } 
            }
        } 
    }
    if (!isElectBlock) {
       return;
    }

    if(!elect_block.has_elect_height() || elect_block.elect_height() <= least_elect_height_for_statistic_ ) {
        ZJC_DEBUG("HandleElectStatistic have processed elect height: %lu, least_elect_height_: %lu", 
            elect_block.elect_height(), least_elect_height_for_statistic_);
       return;
    }
    least_elect_height_for_statistic_ = elect_block.elect_height();

    for(auto node : elect_block.in()) {
        auto addr = secptr_->GetAddress(node.pubkey());
        auto& accoutPoceInfoIterm = accout_poce_info_map_.try_emplace(addr, std::make_shared<AccoutPoceInfoItem>())
                                    .first->second;
        accoutPoceInfoIterm->consensus_gap +=1;
        accoutPoceInfoIterm->credit += node.fts_value();;
        ZJC_DEBUG("HandleElectStatistic addr: %s, consensus_gap: %lu, credit: %lu", common::Encode::HexEncode(addr).c_str(), 
            accoutPoceInfoIterm->consensus_gap, accoutPoceInfoIterm->credit);
    }

}

>>>>>>> main
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

    uint64_t block_gas = 0;
    auto callback = [&](const block::protobuf::Block& block) {
        for (int32_t i = 0; i < block.tx_list_size(); ++i) {
            if (tm_statistic_ptr->max_height < block.height()) {
                tm_statistic_ptr->max_height = block.height();
                ZJC_DEBUG("pool index: %d, set max height: %lu", block.pool_index(), block.height());
            }

            if (block.tx_list(i).step() == pools::protobuf::kNormalFrom ||
                    block.tx_list(i).step() == pools::protobuf::kContractCreate ||
                    block.tx_list(i).step() == pools::protobuf::kContractCreateByRootFrom ||
                    block.tx_list(i).step() == pools::protobuf::kContractExcute ||
                    block.tx_list(i).step() == pools::protobuf::kJoinElect ||
                    block.tx_list(i).step() == pools::protobuf::kContractGasPrepayment) {
                block_gas += block.tx_list(i).gas_price() * block.tx_list(i).gas_used();
            }

            if (block.tx_list(i).status() != consensus::kConsensusSuccess) {
                ZJC_DEBUG("success handle block pool: %u, height: %lu, tm height: %lu, tx: %d, status: %d", 
                    block.pool_index(), block.height(), block.timeblock_height(), i, block.tx_list(i).status());
                continue;
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
                                tmp[0], tmp[1], elect_statistic.join_elect_nodes_size(), elect_statistic.join_elect_nodes(node_idx).shard());
                            if (elect_statistic.join_elect_nodes(node_idx).shard() == network::kRootCongressNetworkId) {
                                elect_static_info_item->node_stoke_map[elect_statistic.join_elect_nodes(node_idx).pubkey()] =
                                    elect_statistic.join_elect_nodes(node_idx).stoke();
                                ZJC_DEBUG("root sharding kJoinElect add new elect node: %s, stoke: %lu, elect height: %lu",
                                    common::Encode::HexEncode(elect_statistic.join_elect_nodes(node_idx).pubkey()).c_str(),
                                    elect_statistic.join_elect_nodes(node_idx).stoke(),
                                    block.electblock_height());
                                elect_static_info_item->node_shard_map[elect_statistic.join_elect_nodes(node_idx).pubkey()] = network::kRootCongressNetworkId;
                            }
                        }
                    }
                }
            }
        }
    };
    
    callback(block);
    elect_static_info_item->all_gas_amount += block_gas;
    std::string leader_id = getLeaderIdFromBlock(block);
    if (leader_id.empty()) {
        assert(false);
        return;
    }

    auto [it, inserted] = elect_static_info_item->node_tx_count_map.try_emplace(leader_id, pools::StatisticMemberInfoItem());
    auto& staticMemberInfo = it->second;
    staticMemberInfo.tx_count += block.tx_list_size();
    staticMemberInfo.gas_sum += block_gas;

    ZJC_DEBUG("success handle block pool: %u, height: %lu, tm height: %lu",
            block.pool_index(), block.height(), block.timeblock_height());
}

std::string ShardStatistic::getLeaderIdFromBlock(shardora::block::protobuf::Block &block)
{
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        block.network_id(),
        nullptr,
        nullptr);
    if (members == nullptr)
    {
        ZJC_DEBUG("block leader not exit block.hash %s block.electHeight:%d, network_id:%d ",
                  common::Encode::HexEncode(block.hash()).c_str(),
                  block.electblock_height(),
                  block.network_id());
        return "";
    }

    auto leader_id = (*members)[block.leader_index()]->id;
    return leader_id;
}

uint64_t ShardStatistic::getStoke(uint32_t shard_id, std::string contractId, std::string temp_addr, uint64_t elect_height) {
    auto default_stoke = 0;

   std::string contract_addr;
    if (shard_id < 1) {
        contract_addr = contractId;
    } else {
        contract_addr = elect::ElectPlege::gen_elect_plege_contract_addr(shard_id);
        ZJC_DEBUG("contract addr: %s", contract_addr.c_str());

    }
    contract_addr = common::Encode::HexDecode(contract_addr);
    auto contract_addr_info = prefix_db_->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        ZJC_ERROR("get contract addr info failed! contract: %s", common::Encode::HexEncode(contract_addr).c_str());
       return default_stoke;
    }

    std::string tmp_input;

    {
        const std::string input = "c78cd339000000000000000000000000";
        // std::ostringstream oss;
        // // 将数字转换为16进制，并且填充0直到长度为16个字符
        // oss << std::setw(64) << std::setfill('0') << std::hex << elect_height;

        // // 获取结果字符串
        // std::string result = oss.str();
        // std::string temp_elect_height = result;
        tmp_input = input + temp_addr ;
        ZJC_DEBUG("tmp input: %s", tmp_input.c_str());
    }



    std::string input = common::Encode::HexDecode(tmp_input);
    std::string addr = common::Encode::HexDecode(temp_addr);
    zjcvm::ZjchainHost zjc_host;
    uint64_t prepayment = 1000000000000;
    uint64_t height = 0;
    auto res = prefix_db_->GetContractUserPrepayment(contract_addr, addr, &height, &prepayment);
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = 0;
    zjc_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = 0;
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.contract_mgr_ = contract_mgr_;
    zjc_host.acc_mgr_ = nullptr;
    zjc_host.my_address_ = contract_addr;
    zjc_host.tx_context_.block_gas_limit = prepayment;
    // user caller prepayment 's gas
    uint64_t from_balance = prepayment;
    uint64_t to_balance = contract_addr_info->balance();
    zjc_host.AddTmpAccountBalance(
        addr,
        from_balance);
    zjc_host.AddTmpAccountBalance(
        contract_addr,
        to_balance);
    evmc_result evmc_res = {};
    evmc::Result result{ evmc_res };
    int exec_res = zjcvm::Execution::Instance()->execute(
        contract_addr_info->bytes_code(),
        input,
        addr,
        contract_addr,
        addr,
        0,
        prepayment,
        0,
        zjcvm::kJustCall,
        zjc_host,
        &result);
    if (exec_res != zjcvm::kZjcvmSuccess || result.status_code != EVMC_SUCCESS) {
        ZJC_DEBUG("query contract error");
           return default_stoke;
    }

    std::string qdata((char*)result.output_data, result.output_size);
    // 32bytes type, 32 bytes len, 32 bytes stoke.
    evmc_bytes32 len_bytes;
    memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    uint64_t len = zjcvm::EvmcBytes32ToUint64(len_bytes);

    evmc_bytes32 stoke_bytes;
    std::string stoke_hex(qdata.c_str() + 64, len);
    std::string stoke_dec = common::Encode::HexDecode(stoke_hex);
    memcpy(stoke_bytes.bytes, stoke_dec.c_str(), stoke_dec.size());
    uint64_t stoke = zjcvm::EvmcBytes32ToUint64(stoke_bytes);
    return stoke;
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
    } else {
        ZJC_DEBUG("failed get prepare members prepare_elect_height_: %lu", prepare_elect_height_);
    }

    uint64_t all_gas_amount = 0;
    uint64_t root_all_gas_amount = 0;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> height_node_count_map;
    std::map<uint64_t, std::unordered_map<std::string, uint64_t>> height_node_gas_map;
    std::map<uint64_t, std::unordered_map<std::string, uint64_t>> join_elect_stoke_map;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> join_elect_shard_map;
    std::map<uint64_t, std::unordered_map<std::string, StatisticMemberInfoItem>> height_node_collect_info_map;
    std::unordered_map<std::string, std::string> id_pk_map;
    uint64_t pools_max_height[common::kInvalidPoolIndex] = {0};
    uint64_t max_tm_height = 0;
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        for (auto tm_iter = node_height_count_map_[pool_idx].begin(); 
                tm_iter != node_height_count_map_[pool_idx].end(); ++tm_iter) {
            ZJC_DEBUG("0 pool: %u, elect height: %lu, tm height: %lu, latest tm height: %lu, "
                "statisticed_timeblock_height_: %lu", 
                pool_idx, 0, tm_iter->first, latest_timeblock_height_, statisticed_timeblock_height_);
            if (tm_iter->first >= latest_timeblock_height_) {
                ZJC_DEBUG("failed statistic %lu, %lu", tm_iter->first, latest_timeblock_height_);
                // assert(false);
                break;
            }

            if (tm_iter->first <= statisticed_timeblock_height_) {
                continue;
            }

            if (tm_iter->first > max_tm_height) {
                max_tm_height = tm_iter->first;
            }
            
            for (auto elect_iter = tm_iter->second->elect_node_info_map.begin(); 
                    elect_iter != tm_iter->second->elect_node_info_map.end(); ++elect_iter) {
                auto elect_height = elect_iter->first;
                ZJC_DEBUG("1 pool: %u, elect height: %lu, tm height: %lu, latest tm height: %lu", 
                    pool_idx, elect_height, tm_iter->first, latest_timeblock_height_);
                auto& node_info_map = height_node_collect_info_map
                                        .try_emplace(elect_height, std::unordered_map<std::string, StatisticMemberInfoItem>())
                                        .first->second;
                // 聚合每个选举高度，每个节点在各个pool 中完成交易的gas总和
                for (auto node_count_iter = elect_iter->second->node_tx_count_map.begin();
                    node_count_iter != elect_iter->second->node_tx_count_map.end(); ++node_count_iter) {
                    auto& node_info = node_info_map.try_emplace(node_count_iter->first, StatisticMemberInfoItem())
                                        .first->second;
                    node_info.gas_sum += node_count_iter->second.gas_sum;
                    node_info.tx_count += node_count_iter->second.tx_count;
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

    std::string debug_for_str;
    // 为当前委员会的节点填充共识工作的奖励信息
    setElectStatistics(height_node_collect_info_map, now_elect_members, elect_statistic, is_root);
    addNewNode2JoinStatics(join_elect_stoke_map,
                          join_elect_shard_map,
                          added_id_set,
                          debug_for_str,
                          id_pk_map,
                          elect_statistic);
    addPrepareMembers2JoinStastics(prepare_members,
                                  added_id_set,
                                  elect_statistic,
                                  debug_for_str,
                                  now_elect_members);
    if (is_root) {
        elect_statistic.set_gas_amount(root_all_gas_amount);
    } else {
        elect_statistic.set_gas_amount(all_gas_amount);
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    elect_statistic.set_sharding_id(net_id);
    addHeightInfo2Statics(elect_statistic, max_tm_height);

    {
        debug_for_str += std::to_string(all_gas_amount) + ",";
        debug_for_str += std::to_string(net_id) + ",";
        ZJC_DEBUG("LLLLLL statistic :%s", ProtobufToJson(elect_statistic).c_str());
        ZJC_DEBUG("success create statistic message: %s, heights: %s, prev_timeblock_height_: %lu",
            debug_for_str.c_str(),
            "heights.c_str()",
            prev_timeblock_height_);
    }

    new_block_changed_ = false;
    *statisticed_timeblock_height = prev_timeblock_height_;
    return kPoolsSuccess;
}

void ShardStatistic::addHeightInfo2Statics(shardora::pools::protobuf::ElectStatistic &elect_statistic, uint64_t max_tm_height) {
    auto *heights_ptr = elect_statistic.mutable_height_info();
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
}

void ShardStatistic::addPrepareMembers2JoinStastics(shardora::common::MembersPtr &prepare_members,
                                                 std::unordered_set<std::string> &added_id_set,
                                                 shardora::pools::protobuf::ElectStatistic &elect_statistic,
                                                 std::string &debug_for_str,
                                                 shardora::common::MembersPtr &now_elect_members) {
    if (prepare_members != nullptr) {
        for (uint32_t i = 0; i < prepare_members->size(); ++i) {
            auto inc_iter = added_id_set.find((*prepare_members)[i]->pubkey);
            if (inc_iter != added_id_set.end()) {
                ZJC_DEBUG("id is in elect: %s", common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str());
                continue;
            }

            auto addr_info = pools_mgr_->GetAddressInfo(secptr_->GetAddress((*prepare_members)[i]->pubkey));
            if (addr_info == nullptr) {
                ZJC_DEBUG("id is in elect get addr failed: %s", common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str());
                continue;
            }

            if (addr_info->elect_pos() != common::kInvalidUint32) {
                if (addr_info->elect_pos() < 0 ||
                    addr_info->elect_pos() >= common::GlobalInfo::Instance()->each_shard_max_members()) {
                    ZJC_DEBUG("id is in elect get pos failed: %s", common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str());
                    continue;
                }
            }
            uint32_t shard = common::GlobalInfo::Instance()->network_id();
            uint64_t stoke = getStoke(shard, "", addr_info->addr(), now_elect_height_);

            auto join_elect_node = elect_statistic.add_join_elect_nodes();
            join_elect_node->set_consensus_gap(0);
            join_elect_node->set_credit(0);
            join_elect_node->set_pubkey((*prepare_members)[i]->pubkey);
            join_elect_node->set_elect_pos(addr_info->elect_pos());
            join_elect_node->set_stoke(stoke);
            join_elect_node->set_shard(shard);
            debug_for_str += common::Encode::HexEncode((*prepare_members)[i]->pubkey) + "," +
                             std::to_string(stoke) + "," + std::to_string(shard) + ",";
            ZJC_DEBUG("add node to election prepare member: %s, %s, stoke: %lu, shard: %u",
                      common::Encode::HexEncode((*prepare_members)[i]->pubkey).c_str(),
                      common::Encode::HexEncode(secptr_->GetAddress((*prepare_members)[i]->pubkey)).c_str(),
                      stoke, shard);
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

void ShardStatistic::addNewNode2JoinStatics(std::map<uint64_t, std::unordered_map<std::string, uint64_t>> &join_elect_stoke_map,
                                            std::map<uint64_t, std::unordered_map<std::string, uint32_t>> &join_elect_shard_map,
                                            std::unordered_set<std::string> &added_id_set,
                                            std::string &debug_for_str,
                                            std::unordered_map<std::string, std::string> &id_pk_map,
                                            shardora::pools::protobuf::ElectStatistic &elect_statistic) {
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
        auto shard_iter = r_siter->second.find(elect_nodes[i]);
        auto shard_id = shard_iter->second;
        auto stoke = getStoke(shard_id, "", addr_info->addr(), now_elect_height_);

        auto join_elect_node = elect_statistic.add_join_elect_nodes();
        auto iter = r_eiter->second.find(elect_nodes[i]);
        join_elect_node->set_consensus_gap(0);
        join_elect_node->set_credit(0);
        join_elect_node->set_pubkey(pubkey);
        join_elect_node->set_stoke(stoke);
        join_elect_node->set_shard(shard_id);
        join_elect_node->set_elect_pos(addr_info->elect_pos());
        debug_for_str += common::Encode::HexEncode(elect_nodes[i]) + "," + std::to_string(iter->second) + "," + std::to_string(shard_iter->second) + ",";
        ZJC_DEBUG("add node to election new member: %s, %s, stoke: %lu, shard: %u",
                  common::Encode::HexEncode(pubkey).c_str(),
                  common::Encode::HexEncode(secptr_->GetAddress(pubkey)).c_str(),
                  iter->second, shard_iter->second);
        ZJC_DEBUG("add new elect node: %s, stoke: %lu, shard: %u", common::Encode::HexEncode(pubkey).c_str(), iter->second, shard_iter->second);
    }
}

// uint64_t getStoke(uint32_t shard_id,  std::string addr) {
//     return 1;
// }

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
            auto node_poce_info = accout_poce_info_map_.try_emplace(id, std::make_shared<AccoutPoceInfoItem>()).first->second;
            statistic_item.add_credit(node_poce_info->credit);
            statistic_item.add_consensus_gap(node_poce_info->consensus_gap);
            statistic_item.add_tx_count(node_info.tx_count);
            statistic_item.add_gas_sum(node_info.gas_sum);
            uint64_t stoke = 0;
            if (!is_root) {
                prefix_db_->GetElectNodeMinStoke(
                    common::GlobalInfo::Instance()->network_id(), id, &stoke);
            }
            auto from = common::Encode::HexEncode(id);
            stoke =getStoke((*members)[midx]->net_id, "", from, hiter->first);
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
