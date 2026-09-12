#include "pools/to_txs_pools.h"

#include "block/account_manager.h"
#include "consensus/consensus_utils.h"
#include "common/global_info.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "pools/tx_pool_manager.h"
#include "protos/get_proto_hash.h"
#include <protos/pools.pb.h>
#include "shardoravm/reversible_feistel_address.h"

namespace shardora {

namespace pools {

// Add two 32-byte big-endian uint256 values in-place: *a += b.
// Both strings must be exactly 32 bytes; if either is wrong-sized the
// operation is a no-op (caller falls back to uint64 path).
static void AddAmount256(std::string& a, const std::string& b) {
    if (a.size() != 32 || b.size() != 32) return;
    uint32_t carry = 0;
    for (int i = 31; i >= 0; --i) {
        uint32_t sum = static_cast<uint8_t>(a[i])
                     + static_cast<uint8_t>(b[i])
                     + carry;
        a[i] = static_cast<char>(sum & 0xFF);
        carry = sum >> 8;
    }
}

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::AccountManager>& acc_mgr)
        : db_(db), local_id_(local_id), pools_mgr_(pools_mgr), acc_mgr_(acc_mgr),
          max_sharding_id_(max_sharding_id) {
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
    if (!network::IsSameToLocalShard(view_block_ptr->qc().network_id())) {
        SHARDORA_DEBUG("network invalid: %d, local: %d", 
            view_block_ptr->qc().network_id(), 
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    auto pool_idx = view_block_ptr->qc().pool_index();
   
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(view_block_ptr->debug());
    SHARDORA_DEBUG("to txs new block coming %u_%u_%lu, "
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
        for (uint32_t i = 0; i < (uint32_t)block.cross_shard_to_array_size(); ++i) {
            auto& to = block.cross_shard_to_array(i);
            tx_map[to.des()] = to;
            if (to.has_base_root_address() && !to.base_root_address().empty()) {
                SHARDORA_INFO("to_txs_pools store CrossShardBase: block=%u_%u_%lu base=%s user=%s des_shard=%u pool=%u",
                    view_block_ptr->qc().network_id(),
                    view_block_ptr->qc().pool_index(),
                    view_block_ptr->block_info().height(),
                    common::Encode::HexEncode(to.base_root_address()).c_str(),
                    common::Encode::HexEncode(to.des()).c_str(),
                    to.des_sharding_id(), to.pool_index());
            } else {
                SHARDORA_DEBUG("success add to item: %s, %lu",
                    common::Encode::HexEncode(to.des()).c_str(), to.amount());
            }
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        auto height_iter = height_map.find(view_block_ptr->block_info().height());
        if (height_iter == height_map.end()) {
            height_map[view_block_ptr->block_info().height()] = tx_map;
            SHARDORA_DEBUG("height_map: %u_%u_%lu, size: %lu", 
                view_block_ptr->qc().network_id(),
                view_block_ptr->qc().pool_index(),
                view_block_ptr->block_info().height(),
                height_map.size());
        }
    }

    if (block.has_normal_to()) {
        SHARDORA_DEBUG("success update to heights: %s", ProtobufToJson(block.normal_to()).c_str());
        StoreLeaderToHeights(nullptr);
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights_ = std::make_shared<pools::protobuf::ShardToTxItem>(
            block.normal_to().to_heights());
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto committed_height = block.normal_to().to_heights().heights(i);
            auto iter = added_heights_[i].begin();
            while (iter != added_heights_[i].end()) {
                if (iter->first >= committed_height) {
                    break;
                }

                SHARDORA_DEBUG("sucess remove pool: %d, height: %lu", i, iter->first);
                if (pool_consensus_heihgts_[i] <= iter->first) {
                    pool_consensus_heihgts_[i] = iter->first + 1;
                }

                iter = added_heights_[i].erase(iter);
            }

            // Ensure pool_consensus_heihgts_ is at least committed_height
            // so that LeaderCreateToHeights won't reference already-cleaned data
            uint64_t old_cons = pool_consensus_heihgts_[i];
            if (pool_consensus_heihgts_[i] < committed_height) {
                pool_consensus_heihgts_[i] = committed_height;
            }
            SHARDORA_DEBUG("normal_to commit pool %u: committed=%lu, cons_height: %lu->%lu",
                i, committed_height, old_cons, pool_consensus_heihgts_[i]);

            // Clean up network_txs_pools_ entries that have been committed
            {
                common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
                auto& height_map = network_txs_pools_[i];
                auto hiter = height_map.begin();
                while (hiter != height_map.end()) {
                    if (hiter->first >= committed_height) {
                        break;  // map is ordered, no need to continue
                    }
                    hiter = height_map.erase(hiter);
                }
            }

            // Clean up valided_heights_ entries strictly below committed height
            // Keep committed_height itself so LeaderCreateToHeights can validate it
            {
                auto viter = valided_heights_[i].begin();
                while (viter != valided_heights_[i].end()) {
                    if (*viter < committed_height) {
                        viter = valided_heights_[i].erase(viter);
                    } else {
                        ++viter;
                    }
                }
                // Ensure committed_height is always present in valided_heights_
                valided_heights_[i].insert(committed_height);
            }

            // Update erased_max_heights_ so future added_heights_ cleanup works
            if (committed_height > 0 && committed_height - 1 > erased_max_heights_[i]) {
                erased_max_heights_[i] = committed_height - 1;
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

    // Update max height BEFORE the advancement loop so the loop upper bound is correct
    valided_heights_[pool_idx].insert(block.height());
    if (block.height() > pool_max_heihgts_[pool_idx]) {
        pool_max_heihgts_[pool_idx] = block.height();
    }

    // Try to advance pool_consensus_heihgts_ as far as possible through consecutive heights
    if (pool_consensus_heihgts_[pool_idx] + 1 == block.height() ||
            (pool_consensus_heihgts_[pool_idx] < block.height() &&
             added_heights_[pool_idx].find(pool_consensus_heihgts_[pool_idx] + 1) != added_heights_[pool_idx].end())) {
        uint64_t old_cons_height = pool_consensus_heihgts_[pool_idx];
        // Advance through all consecutive heights present in added_heights_
        while (pool_consensus_heihgts_[pool_idx] < pool_max_heihgts_[pool_idx]) {
            auto iter = added_heights_[pool_idx].find(
                    pool_consensus_heihgts_[pool_idx] + 1);
            if (iter == added_heights_[pool_idx].end()) {
                break;
            }
            ++pool_consensus_heihgts_[pool_idx];
        }
        SHARDORA_DEBUG("pool %u cons_height advanced: %lu -> %lu, max: %lu, block: %lu",
            pool_idx, old_cons_height, pool_consensus_heihgts_[pool_idx],
            pool_max_heihgts_[pool_idx], block.height());
    } else {
        SHARDORA_DEBUG("pool %u cons_height NOT advanced: cons=%lu, block=%lu, max=%lu, "
            "added_has_next=%d",
            pool_idx, pool_consensus_heihgts_[pool_idx], block.height(),
            pool_max_heihgts_[pool_idx],
            (added_heights_[pool_idx].find(pool_consensus_heihgts_[pool_idx] + 1) 
                != added_heights_[pool_idx].end()));
    }
}

void ToTxsPools::LoadLatestHeights() {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        // //assert(false);
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ShardToTxItem>();
    pools::protobuf::ShardToTxItem& to_heights = *heights_ptr;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id = net_id - network::kConsensusWaitingShardOffset;
    }

    if (!prefix_db_->GetLatestToTxsHeights(net_id, &to_heights)) {
        // Fresh network: no stored heights yet. Initialize to zeros so
        // LeaderCreateToHeights can proceed instead of failing with nullptr.
        to_heights.set_sharding_id(net_id);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            to_heights.add_heights(0);
        }
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
            SHARDORA_DEBUG("set consensus height: %u, height: %lu", i, this_net_heights[i]);
        }
    }

    for (uint32_t i = 0; i <= max_pool_index; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        SHARDORA_DEBUG("pool latest height: %u, %lu", i, pool_latest_height);
        if (pool_latest_height == common::kInvalidUint64) {
            continue;
        }

        valided_heights_[i].insert(pool_latest_height);
        bool consensus_stop = false;
        for (uint64_t height = pool_consensus_heihgts_[i];
                height <= pool_latest_height; ++height) {
            auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
            auto& view_block = *view_block_ptr;
            if (!prefix_db_->GetBlockWithHeight(net_id, i, height, &view_block)) {
                consensus_stop = true;
            } else {
                NewBlock(view_block_ptr);
            }

            if (!consensus_stop) {
                pool_consensus_heihgts_[i] = height;
            }
        }
    }

    SHARDORA_DEBUG("to txs get consensus heights: %s", ProtobufToJson(to_heights).c_str());
}

void ToTxsPools::HandleElectJoinVerifyVec(
        const std::string& g2_value,
        std::vector<bls::protobuf::JoinElectInfo>& verify_reqs) {
    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(g2_value)) {
        //assert(false);
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
        SHARDORA_DEBUG("prev_to_heights_ == nullptr");
        return kPoolsError;
    }

    bool valid = false;
    auto leader_to_heights_ptr = LoadLeaderToHeights();
    if (leader_to_heights_ptr != nullptr) {
        auto now_ms = common::TimeUtils::TimestampMs();
        auto in_flight_age = now_ms - leader_to_heights_set_tm_;
        if (leader_to_heights_set_tm_ + 30000lu > now_ms) {
            // Prior to_tx proposal may have failed; recompute from the latest pool
            // consensus heights instead of reusing a stale snapshot.
            SHARDORA_DEBUG("LeaderCreateToHeights: recomputing with in-flight to_tx, age: %lu ms",
                in_flight_age);
        } else {
            SHARDORA_DEBUG("LeaderCreateToHeights: in-flight tx timed out after %lu ms, recomputing",
                in_flight_age);
            StoreLeaderToHeights(nullptr);
        }
    }
    
    {
        std::shared_ptr<pools::protobuf::ShardToTxItem> prev_heights_snap = nullptr;
        {
            common::AutoSpinLock lock(prev_to_heights_mutex_);
            prev_heights_snap = prev_to_heights_;
        }

        auto timeout = common::TimeUtils::TimestampMs();
        size_t total_size_bytes = 0;
        common::AutoSpinLock pools_lock(network_txs_pools_mutex_);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            uint64_t cons_height = pool_consensus_heihgts_[i];
            // Floor: never go below the already-committed height
            uint64_t floor_height = 0;
            if (prev_heights_snap != nullptr && i < (uint32_t)prev_heights_snap->heights_size()) {
                floor_height = prev_heights_snap->heights(i);
            }

            while (cons_height > 0 && cons_height >= floor_height) {
                auto exist_iter = added_heights_[i].find(cons_height);
                if (exist_iter != added_heights_[i].end()) {
                    if (exist_iter->second + 1000lu > timeout) {
                        --cons_height;
                        continue;
                    }
                }

                if (valided_heights_[i].find(cons_height) == valided_heights_[i].end()) {
                    // If cons_height equals the committed floor, it's safe to use
                    if (cons_height == floor_height) {
                        valid = true;
                        break;
                    }
                    SHARDORA_DEBUG("leader get to heights error, pool: %u, height: %lu, "
                        "floor: %lu, valided_size: %lu",
                        i, cons_height, floor_height, valided_heights_[i].size());
                    return kPoolsError;
                }

                valid = true;
                break;
            }

            // If decremented below floor, use floor height
            if (cons_height < floor_height) {
                cons_height = floor_height;
            }

            // Cap cons_height by accumulated serialized to_txs size across all pools.
            // Trim from the top height down until total_size_bytes + this pool stays
            // within half of kMaxProposeMsgBytes, preventing oversized propose messages.
            {
                auto& hmap = network_txs_pools_[i];
                size_t pool_size = 0;
                for (uint64_t h = floor_height + 1; h <= cons_height; ++h) {
                    auto it = hmap.find(h);
                    if (it == hmap.end()) continue;
                    for (auto& kv : it->second) {
                        pool_size += kv.second.ByteSizeLong();
                    }
                }
                while (cons_height > floor_height &&
                       total_size_bytes + pool_size > (size_t)(common::kMaxProposeMsgBytes / 2)) {
                    auto it = hmap.find(cons_height);
                    if (it != hmap.end()) {
                        for (auto& kv : it->second) {
                            pool_size -= kv.second.ByteSizeLong();
                        }
                    }
                    --cons_height;
                }
                total_size_bytes += pool_size;
                has_statistic_height_[i] = cons_height;
            }

            to_heights.add_heights(cons_height);
            SHARDORA_DEBUG("pool: %u, success add cons height: %lu, floor: %lu, total_size: %zu",
                i, cons_height, floor_height, total_size_bytes);
        }
    }
    

    std::shared_ptr<pools::protobuf::ShardToTxItem> prev_to_heights = nullptr;
    {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights = prev_to_heights_;
    }

    if (!valid) {
        SHARDORA_DEBUG("final leader get to heights error, pool: %u, height: %lu", 0, 0);
        return kPoolsError;
    }

    leader_to_heights_ptr = std::make_shared<pools::protobuf::ShardToTxItem>(to_heights);
    StoreLeaderToHeights(leader_to_heights_ptr);
    leader_to_heights_set_tm_ = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < (uint32_t)to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) > to_heights.heights(i)) {
            SHARDORA_DEBUG("prev heights invalid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), to_heights.heights(i));
            return kPoolsError;
        }
    }

    for (uint32_t i = 0; i < (uint32_t)to_heights.heights_size(); ++i) {
        SHARDORA_DEBUG("test prev heights valid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), to_heights.heights(i));
        if (prev_to_heights->heights(i) < to_heights.heights(i)) {
            SHARDORA_DEBUG("prev heights valid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), to_heights.heights(i));
            return kPoolsSuccess;
        }
    }

    SHARDORA_DEBUG("leader get to heights unchanged, no cross-shard to txs to include");
    return kPoolsError;
}

int ToTxsPools::CreateToTxWithHeights(
        pools::protobuf::ShardToTxItem* prev_to_heights,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        pools::protobuf::ToTxMessage& to_tx,
        uint32_t des_shard_id) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    if (leader_to_heights.heights_size() != common::kInvalidPoolIndex) {
        SHARDORA_DEBUG("leader_to_heights.heights_size() != common::kInvalidPoolIndex: %u, %u", 
            leader_to_heights.heights_size(), common::kInvalidPoolIndex);
        //assert(false);
        return kPoolsError;
    }

    std::map<std::string, pools::protobuf::ToTxMessageItem> acc_amount_map;
    if (prev_to_heights->heights_size() <= 0) {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        *prev_to_heights = *prev_to_heights_;
    }

    for (int32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) > leader_to_heights.heights(i)) {
            SHARDORA_DEBUG("prev heights invalid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), leader_to_heights.heights(i));
            return kPoolsError;
        }
    }

    bool heights_valid = false;
    for (int32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) < leader_to_heights.heights(i)) {
            SHARDORA_DEBUG("prev heights valid, pool: %u, prev height: %lu, now: %lu",
                i, prev_to_heights->heights(i), leader_to_heights.heights(i));
            heights_valid = true;
            break;
        }
    }

    SHARDORA_DEBUG("%d, statistic to txs prev_to_heights: %s, leader_to_heights: %s", 
        heights_valid,
        ProtobufToJson(*prev_to_heights).c_str(), 
        ProtobufToJson(leader_to_heights).c_str());
    if (!heights_valid) {
        SHARDORA_DEBUG("CreateToTxWithHeights: heights unchanged, skip empty to_tx");
        return kPoolsError;
    }

    for (uint32_t pool_idx = 0; pool_idx < (uint32_t)leader_to_heights.heights_size(); ++pool_idx) {
        uint64_t min_height = 1llu;
        if (prev_to_heights != nullptr) {
            min_height = prev_to_heights->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        SHARDORA_DEBUG("pool %u, now statistic to height: %lu, consensus height: %lu",
            pool_idx,
            max_height,
            pool_consensus_heihgts_[pool_idx]);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            SHARDORA_DEBUG("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx,
                max_height,
                pool_consensus_heihgts_[pool_idx]);
            // //assert(false);
            return kPoolsError;
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        // SHARDORA_DEBUG("find pool index: %u min_height: %lu, max height: %lu", 
        //     pool_idx, min_height, max_height);
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = height_map.find(height);
            if (hiter == height_map.end()) {
                // Height may have no cross-shard transactions (empty cross_shard_to_array),
                // or data was already cleaned up by a committed normal_to block.
                // This is normal - skip missing heights instead of aborting.
                SHARDORA_DEBUG("find pool index: %u height: %lu not found, skipping", pool_idx, height);
                continue;
            }

            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                if (to_iter->second.des_sharding_id() == network::kWaitingToCheckNetworkId) {
                    auto addr_info = acc_mgr_->GetAccountInfo(to_iter->second.des().substr(0, common::kUnicastAddressLength));
                    if (addr_info) {
                        to_iter->second.set_des_sharding_id(addr_info->sharding_id());
                        SHARDORA_DEBUG("get des sharding id: %u for des: %s, height: %lu, pool index: %u, addr: %s",
                            addr_info->sharding_id(), 
                            common::Encode::HexEncode(to_iter->second.des()).c_str(), 
                            height, 
                            pool_idx,
                            common::Encode::HexEncode(addr_info->addr()).c_str());
                    } else {
                        if (to_iter->second.prefund() > 0) {
                            to_iter->second.set_des_sharding_id(network::kUniversalNetworkId);
                        } else {
                            to_iter->second.set_des_sharding_id(network::kRootCongressNetworkId);
                        }
                        SHARDORA_DEBUG("new account: addr=%s des shard=%u",
                            common::Encode::HexEncode(to_iter->second.des()).c_str(), 
                            network::kRootCongressNetworkId);
                    }
                }

                if ((uint32_t)to_iter->second.des_sharding_id() != des_shard_id) {
                    continue;
                }

                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    SHARDORA_DEBUG("len: %u, addr: %s",
                        to_iter->first.size(), common::Encode::HexEncode(to_iter->first).c_str());
                    acc_amount_map[to_iter->first] = to_iter->second;
                    if (to_iter->second.has_base_root_address() && !to_iter->second.base_root_address().empty()) {
                        SHARDORA_INFO("acc_amount_map add CrossShardBase: pool=%u h=%lu base=%s user=%s des_shard=%u item_pool=%u",
                            pool_idx, height,
                            common::Encode::HexEncode(to_iter->second.base_root_address()).c_str(),
                            common::Encode::HexEncode(to_iter->second.des()).c_str(),
                            to_iter->second.des_sharding_id(), to_iter->second.pool_index());
                    } else {
                        SHARDORA_DEBUG("to block pool: %u, height: %lu, success add account "
                            "transfer amount height: %lu, id: %s, amount: %lu, to info: %s, "
                            "des_sharding_id: %u",
                            pool_idx, height,
                            height, common::Encode::HexEncode(to_iter->first).c_str(),
                            to_iter->second.amount(),
                            ProtobufToJson(to_iter->second).c_str(),
                            to_iter->second.des_sharding_id());
                    }
                } else {
                    amount_iter->second.set_amount(amount_iter->second.amount() + to_iter->second.amount());
                    // Accumulate full uint256 amount when both items carry it.
                    if (amount_iter->second.has_amount256() && to_iter->second.has_amount256()) {
                        std::string acc = amount_iter->second.amount256();
                        AddAmount256(acc, to_iter->second.amount256());
                        amount_iter->second.set_amount256(acc);
                    } else if (to_iter->second.has_amount256()) {
                        // Incoming has amount256 but accumulated doesn't yet — adopt it.
                        amount_iter->second.set_amount256(to_iter->second.amount256());
                    }
                    if (to_iter->second.has_library_bytes()) {
                        amount_iter->second.set_library_bytes(to_iter->second.library_bytes());
                    }

                    if (to_iter->second.has_runtime_bytecode()) {
                        amount_iter->second.set_runtime_bytecode(to_iter->second.runtime_bytecode());
                    }

                    if (to_iter->second.has_base_root_address()) {
                        amount_iter->second.set_base_root_address(to_iter->second.base_root_address());
                    }

                    if (to_iter->second.prefund() > 0) {
                        amount_iter->second.set_prefund(amount_iter->second.prefund() + to_iter->second.prefund());
                    }

                    if (amount_iter->second.des_sharding_id() != to_iter->second.des_sharding_id()) {
                        SHARDORA_INFO("CrossShardBase acc_amount_map MERGE des_sharding_id conflict: "
                            "des=%s old_shard=%u new_shard=%u base=%s pool=%u height=%lu",
                            common::Encode::HexEncode(to_iter->second.des()).c_str(),
                            amount_iter->second.des_sharding_id(),
                            to_iter->second.des_sharding_id(),
                            to_iter->second.has_base_root_address()
                                ? common::Encode::HexEncode(to_iter->second.base_root_address()).c_str()
                                : "(none)",
                            pool_idx, height);
                        amount_iter->second.set_des_sharding_id(to_iter->second.des_sharding_id());
                    }

                    if (to_iter->second.has_base_root_address() && !to_iter->second.base_root_address().empty()) {
                        auto base_evmc2 = shardoravm::StrToEvmcAddr(to_iter->second.base_root_address());
                        auto shad_evmc2 = shardoravm::DeriveShardAddress(base_evmc2, to_iter->second.des_sharding_id(), to_iter->second.pool_index());
                        std::string shad_str2(reinterpret_cast<const char*>(shad_evmc2.bytes), 20);
                        SHARDORA_INFO("to block pool MERGE CrossShardBase: pool=%u h=%lu base=%s shadow=%s des_shard=%u item_pool=%u amount=%lu acc_amount=%lu",
                            pool_idx, height,
                            common::Encode::HexEncode(to_iter->second.base_root_address()).c_str(),
                            common::Encode::HexEncode(shad_str2).c_str(),
                            to_iter->second.des_sharding_id(), to_iter->second.pool_index(),
                            to_iter->second.amount(), amount_iter->second.amount());
                    } else {
                        SHARDORA_DEBUG("to block pool: %u, height: %lu, success add account "
                            "transfer amount height: %lu, id: %s, amount: %lu, prefundement: %lu, "
                            "all: %lu, to info: %s",
                            pool_idx, height,
                            height, common::Encode::HexEncode(to_iter->first).c_str(),
                            to_iter->second.amount(),
                            amount_iter->second.amount(),
                            amount_iter->second.prefund(),
                            ProtobufToJson(to_iter->second).c_str());
                    }
                }
            }
        }
    }

    if (acc_amount_map.empty()) {
        SHARDORA_DEBUG("acc amount map empty, no cross-shard to txs");
        return kPoolsSuccess;
    }

    SHARDORA_DEBUG("success statistic to txs prev_to_heights: %s, leader_to_heights: %s", 
        ProtobufToJson(*prev_to_heights).c_str(), 
        ProtobufToJson(leader_to_heights).c_str());
    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        auto to_item = to_tx.add_tos();
        *to_item = iter->second;
        if (to_item->has_base_root_address() && !to_item->base_root_address().empty()) {
            auto base_evmc3 = shardoravm::StrToEvmcAddr(to_item->base_root_address());
            auto shad_evmc3 = shardoravm::DeriveShardAddress(base_evmc3, to_item->des_sharding_id(), to_item->pool_index());
            std::string shad_str3(reinterpret_cast<const char*>(shad_evmc3.bytes), 20);
            SHARDORA_INFO("set to CrossShardBase: des=%s amount=%lu des_shard=%u pool=%u base=%s shadow=%s prefund=%lu",
                common::Encode::HexEncode(to_item->des()).c_str(),
                iter->second.amount(), to_item->des_sharding_id(), to_item->pool_index(),
                common::Encode::HexEncode(to_item->base_root_address()).c_str(),
                common::Encode::HexEncode(shad_str3).c_str(),
                to_item->prefund());
        } else {
            SHARDORA_DEBUG("set to %s amount %lu, sharding id: %u, des sharding id: %d, pool index: %d, prefund: %lu",
                common::Encode::HexEncode(to_item->des()).c_str(),
                iter->second.amount(), to_item->des_sharding_id(),
                to_item->des_sharding_id(), to_item->pool_index(), to_item->prefund());
        }
    }

    // to_tx.set_elect_height(elect_height);
    // to_tx.set_des_shard(sharding_id);
    return kPoolsSuccess;
}

int ToTxsPools::CreateToTxForAllShards(
        pools::protobuf::ShardToTxItem* prev_to_heights,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        pools::protobuf::AllToTxMessage& all_to_txs) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    if (leader_to_heights.heights_size() != common::kInvalidPoolIndex) {
        return kPoolsError;
    }

    using AccAmountMap = std::map<std::string, pools::protobuf::ToTxMessageItem>;
    std::unordered_map<uint32_t, AccAmountMap> per_shard_acc;

    if (prev_to_heights->heights_size() <= 0) {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        *prev_to_heights = *prev_to_heights_;
    }

    for (int32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) > leader_to_heights.heights(i)) {
            return kPoolsError;
        }
    }

    bool heights_valid = false;
    for (int32_t i = 0; i < leader_to_heights.heights_size(); ++i) {
        if (prev_to_heights->heights(i) < leader_to_heights.heights(i)) {
            heights_valid = true;
            break;
        }
    }
    if (!heights_valid) {
        return kPoolsError;
    }

    for (uint32_t pool_idx = 0; pool_idx < (uint32_t)leader_to_heights.heights_size(); ++pool_idx) {
        uint64_t min_height = prev_to_heights->heights(pool_idx) + 1;
        uint64_t max_height = leader_to_heights.heights(pool_idx);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            return kPoolsError;
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = height_map.find(height);
            if (hiter == height_map.end()) continue;

            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                if (to_iter->second.des_sharding_id() == network::kWaitingToCheckNetworkId) {
                    auto addr_info = acc_mgr_->GetAccountInfo(
                        to_iter->second.des().substr(0, common::kUnicastAddressLength));
                    if (addr_info) {
                        to_iter->second.set_des_sharding_id(addr_info->sharding_id());
                    } else {
                        to_iter->second.set_des_sharding_id(
                            to_iter->second.prefund() > 0
                                ? network::kUniversalNetworkId
                                : network::kRootCongressNetworkId);
                    }
                }

                uint32_t shard = (uint32_t)to_iter->second.des_sharding_id();
                auto& acc_map = per_shard_acc[shard];
                auto amount_iter = acc_map.find(to_iter->first);
                if (amount_iter == acc_map.end()) {
                    acc_map[to_iter->first] = to_iter->second;
                    if (to_iter->second.has_base_root_address() && !to_iter->second.base_root_address().empty()) {
                        SHARDORA_INFO("acc_amount_map add CrossShardBase: pool=%u h=%lu base=%s user=%s des_shard=%u item_pool=%u",
                            pool_idx, height,
                            common::Encode::HexEncode(to_iter->second.base_root_address()).c_str(),
                            common::Encode::HexEncode(to_iter->second.des()).c_str(),
                            shard, to_iter->second.pool_index());
                    }
                } else {
                    amount_iter->second.set_amount(amount_iter->second.amount() + to_iter->second.amount());
                    if (amount_iter->second.has_amount256() && to_iter->second.has_amount256()) {
                        std::string acc = amount_iter->second.amount256();
                        AddAmount256(acc, to_iter->second.amount256());
                        amount_iter->second.set_amount256(acc);
                    } else if (to_iter->second.has_amount256()) {
                        amount_iter->second.set_amount256(to_iter->second.amount256());
                    }
                    if (to_iter->second.has_library_bytes())
                        amount_iter->second.set_library_bytes(to_iter->second.library_bytes());
                    if (to_iter->second.has_runtime_bytecode())
                        amount_iter->second.set_runtime_bytecode(to_iter->second.runtime_bytecode());
                    if (to_iter->second.has_base_root_address())
                        amount_iter->second.set_base_root_address(to_iter->second.base_root_address());
                    if (to_iter->second.prefund() > 0)
                        amount_iter->second.set_prefund(amount_iter->second.prefund() + to_iter->second.prefund());
                    if (amount_iter->second.des_sharding_id() != to_iter->second.des_sharding_id()) {
                        SHARDORA_INFO("CrossShardBase acc_amount_map MERGE des_sharding_id conflict: "
                            "des=%s old_shard=%u new_shard=%u base=%s pool=%u height=%lu",
                            common::Encode::HexEncode(to_iter->second.des()).c_str(),
                            amount_iter->second.des_sharding_id(), shard,
                            to_iter->second.has_base_root_address()
                                ? common::Encode::HexEncode(to_iter->second.base_root_address()).c_str()
                                : "(none)",
                            pool_idx, height);
                        amount_iter->second.set_des_sharding_id(shard);
                    }
                    if (to_iter->second.has_base_root_address() && !to_iter->second.base_root_address().empty()) {
                        auto base_evmc2 = shardoravm::StrToEvmcAddr(to_iter->second.base_root_address());
                        auto shad_evmc2 = shardoravm::DeriveShardAddress(base_evmc2, shard, to_iter->second.pool_index());
                        std::string shad_str2(reinterpret_cast<const char*>(shad_evmc2.bytes), 20);
                        SHARDORA_INFO("to block pool MERGE CrossShardBase: pool=%u h=%lu base=%s shadow=%s des_shard=%u item_pool=%u amount=%lu acc_amount=%lu",
                            pool_idx, height,
                            common::Encode::HexEncode(to_iter->second.base_root_address()).c_str(),
                            common::Encode::HexEncode(shad_str2).c_str(),
                            shard, to_iter->second.pool_index(),
                            to_iter->second.amount(), amount_iter->second.amount());
                    }
                }
            }
        }
    }

    if (per_shard_acc.empty()) {
        return kPoolsSuccess;
    }

    for (auto& shard_entry : per_shard_acc) {
        auto& acc_map = shard_entry.second;
        if (acc_map.empty()) continue;
        auto& to_tx = *all_to_txs.add_to_tx_arr();
        to_tx.set_des_shard(shard_entry.first);
        for (auto& kv : acc_map) {
            auto* to_item = to_tx.add_tos();
            *to_item = kv.second;
            if (to_item->has_base_root_address() && !to_item->base_root_address().empty()) {
                auto base_evmc3 = shardoravm::StrToEvmcAddr(to_item->base_root_address());
                auto shad_evmc3 = shardoravm::DeriveShardAddress(base_evmc3, to_item->des_sharding_id(), to_item->pool_index());
                std::string shad_str3(reinterpret_cast<const char*>(shad_evmc3.bytes), 20);
                SHARDORA_INFO("set to CrossShardBase: des=%s amount=%lu des_shard=%u pool=%u base=%s shadow=%s prefund=%lu",
                    common::Encode::HexEncode(to_item->des()).c_str(),
                    to_item->amount(), to_item->des_sharding_id(), to_item->pool_index(),
                    common::Encode::HexEncode(to_item->base_root_address()).c_str(),
                    common::Encode::HexEncode(shad_str3).c_str(),
                    to_item->prefund());
            } else {
                SHARDORA_DEBUG("set to %s amount %lu, des_shard=%u pool=%d prefund=%lu",
                    common::Encode::HexEncode(to_item->des()).c_str(),
                    to_item->amount(), to_item->des_sharding_id(),
                    to_item->pool_index(), to_item->prefund());
            }
        }
    }
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace shardora