#include "pools/to_txs_pools.h"

#include "block/account_manager.h"
#include "consensus/consensus_utils.h"
#include "common/global_info.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "pools/tx_pool_manager.h"
#include "protos/get_proto_hash.h"
#include <protos/pools.pb.h>

namespace shardora {

namespace pools {

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::AccountManager>& acc_mgr)
        : db_(db), local_id_(local_id), pools_mgr_(pools_mgr), acc_mgr_(acc_mgr) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    if (pools_mgr_ != nullptr) {
        LoadLatestHeights();
    }
}

ToTxsPools::~ToTxsPools() {
}

void ToTxsPools::NewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
#ifdef TEST_NO_CROSS
    return;
#endif
    ThreadToStatistic(view_block_ptr);
}

void ToTxsPools::ThreadToStatistic(
    const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
#ifdef TEST_NO_CROSS
    return;
#endif
    auto& block = view_block_ptr->block_info();
    if (!network::IsSameToLocalShard(common::GlobalInfo::Instance()->network_id())) {
        ZJC_DEBUG("network invalid: %d, local: %d", 
            view_block_ptr->qc().network_id(), 
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    auto pool_idx = view_block_ptr->qc().pool_index();
   
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(view_block_ptr->debug());
    ZJC_DEBUG("to txs new block coming %u_%u_%lu, "
        "cons height: %lu, tx size: %d, propose_debug: %s, step: %d, tx status: %d, block: %s",
        view_block_ptr->qc().network_id(),
        view_block_ptr->qc().pool_index(),
        block.height(), 
        pool_consensus_heihgts_[pool_idx], 
        view_block_ptr->block_info().tx_list_size(),
        ProtobufToJson(cons_debug).c_str(),
        (view_block_ptr->block_info().tx_list_size() > 0 ? view_block_ptr->block_info().tx_list(0).step() : -1),
        (view_block_ptr->block_info().tx_list_size() > 0 ? view_block_ptr->block_info().tx_list(0).status() : -1),
        ProtobufToJson(block).c_str());
#endif

    {
        TxMap tx_map;
        for (uint32_t i = 0; i < block.cross_shard_to_array_size(); ++i) {
            auto& to = block.cross_shard_to_array(i);
            tx_map[to.des()] = to;
            ZJC_DEBUG("success add to item: %s, %lu",
                common::Encode::HexEncode(to.des()).c_str(), to.amount());
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        auto height_iter = height_map.find(view_block_ptr->block_info().height());
        if (height_iter == height_map.end()) {
            height_map[view_block_ptr->block_info().height()] = tx_map;
        }
    }

    if (block.has_normal_to()) {
        ZJC_INFO("success update to heights: %s", ProtobufToJson(block.normal_to()).c_str());
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights_ = std::make_shared<pools::protobuf::ShardToTxItem>(
            block.normal_to().to_heights());
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto iter = added_heights_[i].begin();
            while (iter != added_heights_[i].end()) {
                if (iter->first >= block.normal_to().to_heights().heights(i)) {
                    break;
                }

                ZJC_DEBUG("sucess remove pool: %d, height: %lu", i, iter->first);
                iter = added_heights_[i].erase(iter);
            }
        }
    }

    added_heights_[pool_idx].insert(std::make_pair<>(
        block.height(), 
        view_block_ptr->block_info().timestamp()));
    auto added_heights_iter = added_heights_[pool_idx].begin();
    while (added_heights_iter != added_heights_[pool_idx].end()) {
        if (added_heights_iter->first > erased_max_heights_[pool_idx]) {
            break;
        }

        added_heights_iter = added_heights_[pool_idx].erase(added_heights_iter);
    }

    CHECK_MEMORY_SIZE_WITH_MESSAGE(added_heights_[pool_idx], std::to_string(pool_idx).c_str());    
    if (pool_consensus_heihgts_[pool_idx] + 1 == block.height()) {
        ++pool_consensus_heihgts_[pool_idx];
        for (; pool_consensus_heihgts_[pool_idx] <= pool_max_heihgts_[pool_idx];
                ++pool_consensus_heihgts_[pool_idx]) {
            auto iter = added_heights_[pool_idx].find(
                    pool_consensus_heihgts_[pool_idx] + 1);
            if (iter == added_heights_[pool_idx].end()) {
                break;
            }
        }
    }

    valided_heights_[pool_idx].insert(block.height());
    if (block.height() > pool_max_heihgts_[pool_idx]) {
        pool_max_heihgts_[pool_idx] = block.height();
    }
}

void ToTxsPools::LoadLatestHeights() {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        // assert(false);
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ShardToTxItem>();
    pools::protobuf::ShardToTxItem& to_heights = *heights_ptr;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id = net_id - network::kConsensusWaitingShardOffset;
    }

    if (!prefix_db_->GetLatestToTxsHeights(net_id, &to_heights)) {
        // assert(false);
        return;
    }

    {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights_ = heights_ptr;
    }
    uint32_t max_pool_index = common::kImmutablePoolSize;
    // if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
    //     ++max_pool_index;
    // }

    if (heights_ptr != nullptr) {
        auto& this_net_heights = heights_ptr->heights();
        for (int32_t i = 0; i < this_net_heights.size(); ++i) {
            pool_consensus_heihgts_[i] = this_net_heights[i];
            has_statistic_height_[i] = this_net_heights[i];
            ZJC_DEBUG("set consensus height: %u, height: %lu", i, this_net_heights[i]);
        }
    }

    for (uint32_t i = 0; i <= max_pool_index; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        ZJC_DEBUG("pool latest height: %u, %lu", i, pool_latest_height);
        if (pool_latest_height == common::kInvalidUint64) {
            continue;
        }

        bool consensus_stop = false;
        for (uint64_t height = pool_consensus_heihgts_[i];
                height <= pool_latest_height; ++height) {
            auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
            auto& view_block = *view_block_ptr;
            if (!prefix_db_->GetBlockWithHeight(
                    common::GlobalInfo::Instance()->network_id(), i, height, &view_block)) {
                consensus_stop = true;
            } else {
                NewBlock(view_block_ptr);
            }

            if (!consensus_stop) {
                pool_consensus_heihgts_[i] = height;
            }
        }
    }

    ZJC_DEBUG("to txs get consensus heights: %s", ProtobufToJson(to_heights).c_str());
}

void ToTxsPools::HandleElectJoinVerifyVec(
        const std::string& g2_value,
        std::vector<bls::protobuf::JoinElectInfo>& verify_reqs) {
    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(g2_value)) {
        assert(false);
        return;
    }

    if (join_info.shard_id() != network::kRootCongressNetworkId) {
        return;
    }

    verify_reqs.push_back(join_info);
}

int ToTxsPools::LeaderCreateToHeights(pools::protobuf::ShardToTxItem& to_heights) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    if (prev_to_heights_ == nullptr) {
        return kPoolsError;
    }

    bool valid = false;
    auto timeout = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        uint64_t cons_height = pool_consensus_heihgts_[i];
        while (cons_height > 0) {
            auto exist_iter = added_heights_[i].find(cons_height);
            if (exist_iter != added_heights_[i].end()) {
                if (exist_iter->second + 300lu > timeout) {
                    --cons_height;
                    continue;
                }
            }

            if (valided_heights_[i].find(cons_height) == valided_heights_[i].end()) {
                ZJC_DEBUG("leader get to heights error, pool: %u, height: %lu", i, cons_height);
                return kPoolsError;
            }

            valid = true;
            break;
        }

        to_heights.add_heights(cons_height);
    }

    std::shared_ptr<pools::protobuf::ShardToTxItem> prev_to_heights = nullptr;
    {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights = prev_to_heights_;
    }

    if (!valid) {
        ZJC_DEBUG("final leader get to heights error, pool: %u, height: %lu", 0, 0);
        return kPoolsError;
    }

    for (uint32_t i = 0; i < to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) > to_heights.heights(i)) {
            ZJC_DEBUG("prev heights invalid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), to_heights.heights(i));
            return kPoolsError;
        }
    }

    for (uint32_t i = 0; i < to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) < to_heights.heights(i)) {
            ZJC_DEBUG("prev heights valid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), to_heights.heights(i));
            return kPoolsSuccess;
        }
    }

    return kPoolsError;
}

int ToTxsPools::CreateToTxWithHeights(
        uint32_t sharding_id,
        uint64_t elect_height,
        pools::protobuf::ShardToTxItem* prev_to_heights,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        pools::protobuf::ToTxMessage& to_tx) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    if (leader_to_heights.heights_size() != common::kInvalidPoolIndex) {
        ZJC_DEBUG("leader_to_heights.heights_size() != common::kInvalidPoolIndex: %u, %u", 
            leader_to_heights.heights_size(), common::kInvalidPoolIndex);
        assert(false);
        return kPoolsError;
    }

    std::map<std::string, pools::protobuf::ToTxMessageItem> acc_amount_map;
    if (prev_to_heights->heights_size() <= 0) {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        *prev_to_heights = *prev_to_heights_;
    }

    for (uint32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) > leader_to_heights.heights(i)) {
            ZJC_DEBUG("prev heights invalid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), leader_to_heights.heights(i));
            return kPoolsError;
        }
    }

    bool heights_valid = false;
    for (uint32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) < leader_to_heights.heights(i)) {
            ZJC_DEBUG("prev heights valid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), leader_to_heights.heights(i));
            heights_valid = true;
            break;
        }
    }

    ZJC_DEBUG("sharding id valid: %d, %d, statistic to txs prev_to_heights: %s, leader_to_heights: %s", 
        sharding_id, 
        heights_valid,
        ProtobufToJson(*prev_to_heights).c_str(), 
        ProtobufToJson(leader_to_heights).c_str());
    if (!heights_valid) {
        return kPoolsError;
    }

    for (int32_t pool_idx = 0; pool_idx < leader_to_heights.heights_size(); ++pool_idx) {
        uint64_t min_height = 1llu;
        if (prev_to_heights != nullptr) {
            min_height = prev_to_heights->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        ZJC_DEBUG("pool %u, now statistic to height: %lu, consensus height: %lu",
            pool_idx,
            max_height,
            pool_consensus_heihgts_[pool_idx]);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            ZJC_DEBUG("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx,
                max_height,
                pool_consensus_heihgts_[pool_idx]);
            // assert(false);
            return kPoolsError;
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        // ZJC_DEBUG("find pool index: %u min_height: %lu, max height: %lu", 
        //     pool_idx, min_height, max_height);
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = height_map.find(height);
            if (hiter == height_map.end()) {
                ZJC_DEBUG("find pool index: %u height: %lu failed!", pool_idx, height);
                // assert(false);
                return kPoolsError;
            }

            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto des_sharding_id = to_iter->second.des_sharding_id();
                if (des_sharding_id != sharding_id) {
                    ZJC_DEBUG("statistic des shard: %u, %u_%u_%lu, find pool index: %u "
                        "height: %lu sharding: %u, %u failed id: %s, amount: %lu",
                        sharding_id,
                        common::GlobalInfo::Instance()->network_id(),
                        pool_idx,
                        height,
                        pool_idx, height, des_sharding_id,
                        sharding_id, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount());
                    continue;
                }
                
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    ZJC_DEBUG("len: %u, addr: %s",
                        to_iter->first.size(), common::Encode::HexEncode(to_iter->first).c_str());
                    acc_amount_map[to_iter->first] = to_iter->second;
                    ZJC_DEBUG("to block pool: %u, height: %lu, success add account "
                        "transfer amount height: %lu, id: %s, amount: %lu, to info: %s",
                        pool_idx, height,
                        height, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount(),
                        ProtobufToJson(to_iter->second).c_str());
                } else {
                    amount_iter->second.set_amount(amount_iter->second.amount() + to_iter->second.amount());
                    if (to_iter->second.has_library_bytes()) {
                        amount_iter->second.set_library_bytes(to_iter->second.library_bytes());
                    }

                    if (to_iter->second.prepayment() > 0) {
                        amount_iter->second.set_prepayment(amount_iter->second.prepayment() + to_iter->second.prepayment());
                    }
                    
                    ZJC_DEBUG("to block pool: %u, height: %lu, success add account "
                        "transfer amount height: %lu, id: %s, amount: %lu, all: %lu, to info: %s",
                        pool_idx, height,
                        height, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount(),
                        amount_iter->second.amount(),
                        ProtobufToJson(to_iter->second).c_str());
                }
            }
        }
    }

    // if (acc_amount_map.empty() && cross_set.empty()) {
    if (acc_amount_map.empty()) {
//         assert(false);
        ZJC_DEBUG("acc amount map empty.");
        return kPoolsError;
    }

    ZJC_INFO("success statistic to txs prev_to_heights: %s, leader_to_heights: %s", 
        ProtobufToJson(*prev_to_heights).c_str(), 
        ProtobufToJson(leader_to_heights).c_str());
    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        auto to_item = to_tx.add_tos();
        *to_item = iter->second;
        ZJC_DEBUG("set to %s amount %lu, sharding id: %u, des sharding id: %d, pool index: %d, prepayment: %lu",
            common::Encode::HexEncode(to_item->des()).c_str(),
            iter->second.amount(), to_item->sharding_id(), 
            sharding_id, to_item->pool_index(), to_item->prepayment());
    }

    to_tx.set_elect_height(elect_height);
    to_tx.set_des_shard(sharding_id);
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace shardora
