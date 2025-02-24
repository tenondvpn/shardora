#include "pools/tx_pool.h"
#include <cassert>
#include <common/log.h>

#include "common/encode.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace pools {
    
TxPool::TxPool() {}

TxPool::~TxPool() {}

void TxPool::Init(
        TxPoolManager* pools_mgr,
        std::shared_ptr<security::Security> security,
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    pools_mgr_ = pools_mgr;
    security_ = security;
    kv_sync_ = kv_sync;
    pool_index_ = pool_idx;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    InitLatestInfo();
    InitHeightTree();
}

void TxPool::InitHeightTree() {
    CheckThreadIdValid();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    auto height_tree_ptr = std::make_shared<HeightTreeLevel>(
        net_id,
        pool_index_,
        latest_height_,
        db_);
    height_tree_ptr->Set(0);
    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr->Valid(synced_height_ + 1)) {
            break;
        }
    }

    height_tree_ptr_ = height_tree_ptr;
}

uint32_t TxPool::SyncMissingBlocks(uint64_t now_tm_ms) {
    if (!height_tree_ptr_) {
        ZJC_DEBUG("get invalid height_tree_ptr_ size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        ZJC_DEBUG("get invalid heights size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    ZJC_DEBUG("%u get invalid heights size: %u, latest_height_: %lu", pool_index_, invalid_heights.size(), latest_height_);
    if (invalid_heights.size() > 0) {
        auto net_id = common::GlobalInfo::Instance()->network_id();
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
            return 0;
        }

        for (uint32_t i = 0; i < invalid_heights.size(); ++i) {
            if (prefix_db_->BlockExists(net_id, pool_index_, invalid_heights[i])) {
                height_tree_ptr_->Set(invalid_heights[i]);
//                 ZJC_DEBUG("pool exists des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
//                     "invaid heights size: %u, height: %lu",
//                     net_id, pool_index_, latest_height_,
//                     invalid_heights.size(), invalid_heights[i]);
                continue;
            }

//             ZJC_DEBUG("pool des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
//                 "invaid heights size: %u, height: %lu",
//                 net_id, pool_index_, latest_height_,
//                 invalid_heights.size(), invalid_heights[i]);
            ZJC_DEBUG("add sync block height net: %u, pool: %u, height: %lu",
                net_id,
                pool_index_,
                invalid_heights[i]);
            ZJC_INFO("kvsync add sync block height net: %u, pool: %u, height: %lu",
                net_id,
                pool_index_,
                invalid_heights[i]);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                invalid_heights[i],
                sync::kSyncHigh);
        }
    }

    return invalid_heights.size();
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
    CheckThreadIdValid();
    if (added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_WARN("add failed extend %u, %u, all valid: %u", 
            added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), tx_size());
        return kPoolsError;
    }

    if (tx_ptr->unique_tx_hash.empty()) {
        ZJC_WARN("add failed unique hash empty: %d", tx_ptr->tx_info->step());
        tx_ptr->unique_tx_hash = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    added_txs_.push(tx_ptr);
    return kPoolsSuccess;

    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    assert(tx_ptr != nullptr);
    if (tx_ptr->tx_info->step() == pools::protobuf::kCreateLibrary) {
        universal_prio_map_[tx_ptr->prio_key] = tx_ptr;
        CHECK_MEMORY_SIZE(universal_prio_map_);
    } else {
        prio_map_[tx_ptr->prio_key] = tx_ptr;
        CHECK_MEMORY_SIZE(prio_map_);
    }

    gid_map_[tx_ptr->tx_info->gid()] = tx_ptr;
    CHECK_MEMORY_SIZE_WITH_MESSAGE(gid_map_, (std::string("pool index: ") + std::to_string(pool_index_)).c_str());
#ifdef LATENCY
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (prev_tx_count_tm_us_ == 0) {
        prev_tx_count_tm_us_ = now_tm_us;
    }

    if (now_tm_us > prev_tx_count_tm_us_ + 3000000lu) {
        ZJC_INFO("waiting_tx_count pool: %d: tx: %llu", pool_index_, gid_map_.size());
        prev_tx_count_tm_us_ = now_tm_us;
    }

    gid_start_time_map_[tx_ptr->tx_info->gid()] = common::TimeUtils::TimestampUs();
    CHECK_MEMORY_SIZE(gid_start_time_map_);
    oldest_timestamp_ = prio_map_.begin()->second->time_valid;
#endif
    // timeout_txs_.push(tx_ptr->tx_info->gid());
    // CHECK_MEMORY_SIZE_WITH_MESSAGE(timeout_txs_, "timeout txs push");
    if (pool_index_ == common::kImmutablePoolSize) {
        ZJC_DEBUG("pool: %d, success add tx step: %d, gid: %s", 
            pool_index_, 
            tx_ptr->tx_info->step(),
            common::Encode::HexEncode(tx_ptr->tx_info->gid()).c_str());
    }
    
    ZJC_DEBUG("success add tx pool: %d, gid: %s, tx size: %u, gid: %u, cons: %u, prio: %u, uni: %u", 
        pool_index_, 
        common::Encode::HexEncode(tx_ptr->tx_info->gid()).c_str(),
        tx_size(),
        gid_map_.size(),
        consensus_tx_map_.size(), prio_map_.size(), universal_prio_map_.size());
    assert(gid_map_.size() == tx_size());
    ADD_TX_DEBUG_INFO((tx_ptr->tx_info));
    return kPoolsSuccess;
}

void TxPool::GetTxSyncToLeader(
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckGidValidFunction gid_vlid_func) {
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    auto iter = prio_map_.begin();
    while (iter != prio_map_.end() && txs->size() < (int32_t)count) {
        if (gid_vlid_func != nullptr && !gid_vlid_func(iter->second->tx_info->gid())) {
            ZJC_DEBUG("gid invalid: %s", common::Encode::HexEncode(iter->second->tx_info->gid()).c_str());
        } else {
            ZJC_DEBUG("gid valid: %s", common::Encode::HexEncode(iter->second->tx_info->gid()).c_str());
            auto* tx = txs->Add();
            *tx = *iter->second->tx_info;
            ADD_TX_DEBUG_INFO(tx);
            assert(!iter->second->unique_tx_hash.empty());
        }

        auto tmp_iter = gid_map_.find(iter->second->tx_info->gid());
        assert(tmp_iter != gid_map_.end());
        gid_map_.erase(tmp_iter);
        ZJC_INFO("1 remove tx now: %u", gid_map_.size());
        iter = prio_map_.erase(iter);
    }
    assert(gid_map_.size() == tx_size());
}

void TxPool::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::map<std::string, TxItemPtr>& res_map, 
        uint32_t count, 
        pools::CheckGidValidFunction gid_vlid_func) {
    CheckThreadIdValid();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("now get tx universal_prio_map_ size: %u, prio_map_: %u, consensus_tx_map_: %u",
        universal_prio_map_.size(),
        prio_map_.size(),
        consensus_tx_map_.size());
    GetTxIdempotently(msg_ptr, universal_prio_map_, res_map, count, gid_vlid_func);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!res_map.empty()) {
        ZJC_DEBUG("pool index: %u, success get tx size: %d", pool_index_, res_map.size());
        return;
    }

    GetTxIdempotently(msg_ptr, prio_map_, res_map, count, gid_vlid_func);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    GetTxIdempotently(msg_ptr, consensus_tx_map_, res_map, count, gid_vlid_func);    
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

void TxPool::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckGidValidFunction gid_vlid_func) {
    TxItemPtr tx_ptr;
    while (added_txs_.pop(&tx_ptr) && res_map.size() < count) {
        if (gid_vlid_func != nullptr && !gid_vlid_func(tx_ptr->tx_info->gid())) {
            ZJC_DEBUG("gid invalid: %s", common::Encode::HexEncode(tx_ptr->tx_info->gid()).c_str());
            continue;
        }

        res_map[tx_ptr->unique_tx_hash] = tx_ptr;
    }

    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    auto iter = src_prio_map.begin();
    while (iter != src_prio_map.end() && res_map.size() < count) {
        if (gid_vlid_func != nullptr && !gid_vlid_func(iter->second->tx_info->gid())) {
            ZJC_DEBUG("gid invalid: %s", common::Encode::HexEncode(iter->second->tx_info->gid()).c_str());
            ++iter;
            continue;
        }

        res_map[iter->second->unique_tx_hash] = iter->second;
        assert(!iter->second->unique_tx_hash.empty());
        if (pool_index_ == common::kImmutablePoolSize) {
            ZJC_DEBUG("gid valid: %s, now size: %d", 
                common::Encode::HexEncode(iter->second->tx_info->gid()).c_str(),
                src_prio_map.size());
        }

        ADD_TX_DEBUG_INFO((iter->second->tx_info));
        auto tmp_iter = gid_map_.find(iter->second->tx_info->gid());
        assert(tmp_iter != gid_map_.end());
        gid_map_.erase(tmp_iter);
        ZJC_INFO("2 remove tx now: %u", gid_map_.size());
        iter = src_prio_map.erase(iter);
    }
    
    ZJC_DEBUG("success get tx pool: %u, count: %u, get count: %u, "
        "exists count: %u, gid: %u, cons: %u, prio: %u, uni: %u",
        pool_index_, count, res_map.size(), src_prio_map.size(), gid_map_.size(),
        consensus_tx_map_.size(), prio_map_.size(), universal_prio_map_.size());
    assert(gid_map_.size() == tx_size());
}

void TxPool::GetTxByIds(
        const std::vector<std::string>& gids,
        std::map<std::string, TxItemPtr>& res_map) {
    assert(false);
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    CheckThreadIdValid();
    for (const auto& gid : gids) {
        auto it = gid_map_.find(gid);
        if (it == gid_map_.end()) {
            continue;
        }
        auto hash = it->second->unique_tx_hash;
        TxItemPtr tx = nullptr;
        GetTxByHash(universal_prio_map_, hash, tx);
        if (tx) {
            res_map[tx->unique_tx_hash] = tx;
            continue;
        }
        GetTxByHash(prio_map_, hash, tx);
        if (tx) {
            res_map[tx->unique_tx_hash] = tx;
            continue;
        }
        GetTxByHash(consensus_tx_map_, hash, tx);
        if (tx) {
            res_map[tx->unique_tx_hash] = tx;
            continue;
        }
    }
}

void TxPool::GetTxByHash(
        std::map<std::string, TxItemPtr>& src_prio_map,
        const std::string& hash,
        TxItemPtr& tx) {
    assert(false);
    return;
    auto iter = src_prio_map.find(hash);
    if (iter == src_prio_map.end()) {
        return;
    }
    tx = iter->second;
    assert(!iter->second->unique_tx_hash.empty());
    auto miter = gid_map_.find(iter->second->tx_info->gid());
    assert(miter != gid_map_.end());
    gid_map_.erase(miter);
    iter = src_prio_map.erase(iter);
    assert(gid_map_.size() == tx_size());
    ZJC_INFO("0 remove tx now: %u", gid_map_.size());
}

void TxPool::CheckTimeoutTx() {
    // auto now_tm = common::TimeUtils::TimestampUs();
    // if (prev_check_tx_timeout_tm_ > now_tm) {
    //     return;
    // }

    // prev_check_tx_timeout_tm_ = now_tm + 1000000lu;
    // uint32_t count = 0;
    // while (!timeout_txs_.empty() && count++ < 1024) {
    //     auto& gid = timeout_txs_.front();
    //     auto iter = gid_map_.find(gid);
    //     if (iter == gid_map_.end()) {
    //         timeout_txs_.pop();
    //         CHECK_MEMORY_SIZE_WITH_MESSAGE(timeout_txs_, "timeout txs pop");
    //         continue;
    //     }

    //     // ZJC_DEBUG("check tx timeout gid: %s, size: %u, iter->second->timeout: %lu now_tm: %lu", 
    //     //     common::Encode::HexEncode(gid).c_str(),
    //     //     gid_map_.size(), iter->second->timeout, now_tm);
    //     if (iter->second->timeout > now_tm) {
    //         break;
    //     }

    //     timeout_txs_.pop();
    //     CHECK_MEMORY_SIZE_WITH_MESSAGE(timeout_txs_, "timeout txs pop");
    //     timeout_remove_txs_.push(gid);
    //     CHECK_MEMORY_SIZE(timeout_remove_txs_);
    // }

    // count = 0;
    // while (!timeout_remove_txs_.empty() && count++ < 64) {
    //     auto& gid = timeout_remove_txs_.front();
    //     auto iter = gid_map_.find(gid);
    //     if (iter == gid_map_.end()) {
    //         timeout_remove_txs_.pop();
    //         continue;
    //     }

    //     ZJC_DEBUG("remove tx timeout size: %u, iter->second->timeout: %lu now_tm: %lu", 
    //         gid_map_.size(), iter->second->remove_timeout, now_tm);
    //     if (iter->second->remove_timeout > now_tm) {
    //         break;
    //     }

    //     RemoveTx(gid);
    //     timeout_remove_txs_.pop();
    //     CHECK_MEMORY_SIZE(timeout_remove_txs_);
    //     ZJC_DEBUG("timeout remove gid: %s, tx hash: %s",
    //         common::Encode::HexEncode(gid).c_str(),
    //         common::Encode::HexEncode(iter->second->unique_tx_hash).c_str());
    // }
}

void TxPool::TxRecover(std::map<std::string, TxItemPtr>& txs) {
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        auto miter = gid_map_.find(iter->first);
        if (miter != gid_map_.end()) {
            if (miter->second->is_consensus_add_tx) {
                consensus_tx_map_[miter->second->unique_tx_hash] = miter->second;
                CHECK_MEMORY_SIZE(consensus_tx_map_);
                continue;
            }

            if (iter->second->tx_info->step() == pools::protobuf::kCreateLibrary) {
                universal_prio_map_[miter->second->prio_key] = miter->second;
                CHECK_MEMORY_SIZE(universal_prio_map_);
            } else {
                prio_map_[miter->second->prio_key] = miter->second;
                CHECK_MEMORY_SIZE(prio_map_);
            }
        }
    }
}

void TxPool::RecoverTx(const std::string& gid) {
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    auto miter = gid_map_.find(gid);
    if (miter != gid_map_.end()) {
        if (miter->second->is_consensus_add_tx) {
            consensus_tx_map_[miter->second->unique_tx_hash] = miter->second;
            CHECK_MEMORY_SIZE(consensus_tx_map_);
            return;
        }
        if (miter->second->tx_info->step() == pools::protobuf::kCreateLibrary) {
            universal_prio_map_[miter->second->prio_key] = miter->second;
            CHECK_MEMORY_SIZE(universal_prio_map_);
        } else {
            prio_map_[miter->second->prio_key] = miter->second;
            CHECK_MEMORY_SIZE(prio_map_);
        }        
    }
}

bool TxPool::GidValid(const std::string& gid) {
    return true;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    CheckThreadIdValid();
    auto tmp_res = added_gids_.insert(gid);
    ZJC_DEBUG("check gid valid called: %s", common::Encode::HexEncode(gid).c_str());
    CHECK_MEMORY_SIZE(added_gids_);
    if (tmp_res.second) {
        if (prefix_db_->CheckAndSaveGidExists(gid)) {
            return false;
        }

        std::string key = protos::kGidPrefix + gid;
        added_gids_batch_.Put(key, "1");
        if (added_gids_.size() >= 1024) {
            auto st = db_->Put(added_gids_batch_);
            if (!st.ok()) {
                ZJC_FATAL("write data to db failed!");
            }

            added_gids_batch_ = db::DbWriteBatch();
            added_gids_.clear();
        }

        return true;
    }
   
    return false;
}

void TxPool::RemoveTx(const std::string& gid) {
    // CheckThreadIdValid();
    // auto added_gid_iter = added_gids_.find(gid);
    // if (added_gid_iter != added_gids_.end()) {
    //     added_gids_.erase(added_gid_iter);
    // }

    // auto giter = gid_map_.find(gid);
    // if (giter == gid_map_.end()) {
    //     return;
    // }

    // auto prio_iter = prio_map_.find(giter->second->prio_key);
    // if (prio_iter != prio_map_.end()) {
    //     prio_map_.erase(prio_iter);
    // }

    // auto universal_prio_iter = universal_prio_map_.find(giter->second->prio_key);
    // if (universal_prio_iter != universal_prio_map_.end()) {
    //     universal_prio_map_.erase(universal_prio_iter);
    // }

    // auto cons_iter = consensus_tx_map_.find(giter->second->unique_tx_hash);
    // if (cons_iter != consensus_tx_map_.end()) {
    //     consensus_tx_map_.erase(cons_iter);
    // }

    // // ZJC_DEBUG("remove tx success gid: %s, tx hash: %s",
    // //     common::Encode::HexEncode(giter->second->tx_info.gid()).c_str(),
    // //     common::Encode::HexEncode(giter->second->unique_tx_hash).c_str());
    // gid_map_.erase(giter);
#ifdef LATENCY    
    if (!prio_map_.empty()) {
        oldest_timestamp_ = prio_map_.begin()->second->time_valid;
    } else {
        oldest_timestamp_ = 0;
    }
#endif
}

void TxPool::TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
//     CheckThreadIdValid();
//     for (int32_t i = 0; i < tx_list.size(); ++i) {
//         auto& gid = tx_list[i].gid(); 
//         RemoveTx(gid);
// #ifdef LATENCY
//         // 统计交易确认延迟
//         auto now_tm = common::TimeUtils::TimestampUs();
//         auto start_tm_iter = gid_start_time_map_.find(gid);
//         if (start_tm_iter != gid_start_time_map_.end()) {
//             latencys_us_.push_back(now_tm - start_tm_iter->second);
//             // ZJC_INFO("tx latency gid: %s, us: %llu",
//             //     common::Encode::HexEncode(gid).c_str(), now_tm - start_tm_iter->second);
//             gid_start_time_map_.erase(gid);
//             CHECK_MEMORY_SIZE(gid_start_time_map_);
//         }

//         if (latencys_us_.size() > 10) {
//             uint64_t p50 = common::GetNthElement(latencys_us_, 0.5);
//             latencys_us_.clear();
        
//             ZJC_DEBUG("tx latency p50: %llu", p50);
//         }
// #endif
//     }

//     finish_tx_count_ += tx_list.size();
}

 bool TxPool::CheckJoinElectTxInfo(pools::protobuf::TxMessage& tx_msg) {
    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(tx_msg.value())) {
        return false;
    }

    auto address_info = pools_mgr_->GetAddressInfo(security_->GetAddress(tx_msg.pubkey()));
    if (address_info == nullptr) {
        return false;
    }
    uint32_t tmp_shard = join_info.shard_id();
    if (tmp_shard != network::kRootCongressNetworkId) {
        if (tmp_shard != address_info->sharding_id()) {
            ZJC_DEBUG("join des shard error: %d,  %d.",
                tmp_shard, address_info->sharding_id());
        return false;
        }
    }

    std::string new_hash;
    if (!SaveNodeVerfiyVec(address_info->addr(), join_info, &new_hash)) {
        assert(false);
        return false;
    }
    tx_msg.set_key(protos::kJoinElectVerifyG2);
    tx_msg.set_value(new_hash);
    return true;
}

bool TxPool::SaveNodeVerfiyVec(
        const std::string& id,
        const bls::protobuf::JoinElectInfo& join_info,
        std::string* new_hash) {
    int32_t t = common::GetSignerCount(common::GlobalInfo::Instance()->each_shard_max_members());
    if (join_info.g2_req().verify_vec_size() > 0 && join_info.g2_req().verify_vec_size() != t) {
        return false;
    }

    std::string str_for_hash;
    str_for_hash.reserve(join_info.g2_req().verify_vec_size() * 4 * 64 + 8);
    uint32_t shard_id = join_info.shard_id();
    uint32_t mem_idx = join_info.member_idx();
    str_for_hash.append((char*)&shard_id, sizeof(shard_id));
    str_for_hash.append((char*)&mem_idx, sizeof(mem_idx));
    for (int32_t i = 0; i < join_info.g2_req().verify_vec_size(); ++i) {
        auto& item = join_info.g2_req().verify_vec(i);
        str_for_hash.append(item.x_c0());
        str_for_hash.append(item.x_c1());
        str_for_hash.append(item.y_c0());
        str_for_hash.append(item.y_c1());
        str_for_hash.append(item.z_c0());
        str_for_hash.append(item.z_c1());
    }

    *new_hash = common::Hash::keccak256(str_for_hash);
    auto str = join_info.SerializeAsString();
    prefix_db_->SaveTemporaryKv(*new_hash, str);
    return true;
}

void TxPool::InitLatestInfo() {
    pools::protobuf::PoolLatestInfo pool_info;
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }

    if (prefix_db_->GetLatestPoolInfo(
            network_id,
            pool_index_,
            &pool_info)) {
        // 根据数据库更新内存中的 tx_pool 状态
        if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
            latest_height_ = pool_info.height();
            latest_hash_ = pool_info.hash();
            synced_height_ = pool_info.synced_height();
            latest_timestamp_ = pool_info.timestamp();
            prev_synced_height_ = synced_height_;
            to_sync_max_height_ = latest_height_;
            InitGetTempBftInvalidHashs();
            ZJC_DEBUG("init latest pool info shard: %u, pool %lu, init height: %lu",
                network_id, pool_index_, latest_height_);
        }
    }
}

void TxPool::UpdateSyncedHeight() {
    if (!height_tree_ptr_) {
        return;
    }

    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
            break;
        }
    }
}

void TxPool::GetHeightInvalidChangeLeaderHashs(uint64_t height, std::vector<std::string>&hashs) {
    auto iter = change_leader_invalid_hashs_.find(height);
    if (iter == change_leader_invalid_hashs_.end()) {
        return;
    }

    for (auto hiter = iter->second.begin(); hiter != iter->second.end(); ++hiter) {
        hashs.push_back(*hiter);
        ZJC_DEBUG("success get invalid hash pool: %u, height: %lu, hash: %s",
            pool_index_, height, common::Encode::HexEncode(*hiter).c_str());
    }
}

void TxPool::AddChangeLeaderInvalidHash(uint64_t height, const std::string& hash) {
    if (height != latest_height_) {
        return;
    }

    auto iter = change_leader_invalid_hashs_.find(height);
    if (iter == change_leader_invalid_hashs_.end()) {
        change_leader_invalid_hashs_[height] = std::set<std::string>();
        CHECK_MEMORY_SIZE(change_leader_invalid_hashs_);
        iter = change_leader_invalid_hashs_.find(height);
    } else {
        auto exists_iter = iter->second.find(hash);
        if (exists_iter != iter->second.end()) {
            return;
        }
    }

    ZJC_DEBUG("success add invalid hash pool: %u, height: %lu, hash: %s",
        pool_index_, height, common::Encode::HexEncode(hash).c_str());
    iter->second.insert(hash);
    SaveTempBftInvalidHashs(height, iter->second);
}

void TxPool::SaveTempBftInvalidHashs(uint64_t height, const std::set<std::string>& hashs) {
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
        network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }
    
    prefix_db_->SaveTempHeightInvalidHashs(network_id, pool_index_, height, hashs);
}

void TxPool::InitGetTempBftInvalidHashs() {
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
        network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }

    std::set<std::string> hashs;
    prefix_db_->GetTempHeightInvalidHashs(network_id, pool_index_, latest_height_ + 1, &hashs);
    if (!hashs.empty()) {
        change_leader_invalid_hashs_[latest_height_ + 1] = hashs;
        CHECK_MEMORY_SIZE(change_leader_invalid_hashs_);
    }
}

uint64_t TxPool::UpdateLatestInfo(
        uint64_t height,
        const std::string& hash,
        const std::string& prehash,
        const uint64_t timestamp) {
    CheckThreadIdValid();
    auto tmp_height_tree_ptr = height_tree_ptr_;
    if (!tmp_height_tree_ptr) {
        InitHeightTree();
    }

    tmp_height_tree_ptr = height_tree_ptr_;
    if (tmp_height_tree_ptr) {
        ZJC_DEBUG("success set height, net: %u, pool: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), pool_index_, height);
        tmp_height_tree_ptr->Set(height);
    }

    if (latest_height_ == common::kInvalidUint64 || latest_height_ < height) {
        latest_height_ = height;
        latest_hash_ = hash;
        latest_timestamp_ = timestamp;
        InitGetTempBftInvalidHashs();
    }

    if (to_sync_max_height_ == common::kInvalidUint64 || to_sync_max_height_ < latest_height_) {
        to_sync_max_height_ = latest_height_;
    }

    if (height > synced_height_) {
        checked_height_with_prehash_[height] = prehash;
        CHECK_MEMORY_SIZE(checked_height_with_prehash_);
    }

    if (synced_height_ + 1 == height) {
        synced_height_ = height;
        auto iter = checked_height_with_prehash_.begin();
        while (iter != checked_height_with_prehash_.end()) {
            if (iter->first < synced_height_) {
                iter = checked_height_with_prehash_.erase(iter);
            } else {
                ++iter;
            }
        }

        UpdateSyncedHeight();
        if (prev_synced_height_ < synced_height_) {
            prev_synced_height_ = synced_height_;
        }
    } else {
        SyncBlock();
    }

    ZJC_DEBUG("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
        pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
    return synced_height_;
}

bool TxPool::is_next_block_checked(uint64_t height, const std::string& hash) {
    auto iter = checked_height_with_prehash_.find(height + 1);
    if (iter != checked_height_with_prehash_.end()) {
        return iter->second == hash;
    }

    return false;
}

void TxPool::SyncBlock() {
    if (height_tree_ptr_ == nullptr) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    for (; prev_synced_height_ < to_sync_max_height_ &&
            (prev_synced_height_ < synced_height_ + 64);
            ++prev_synced_height_) {
        if (!height_tree_ptr_->Valid(prev_synced_height_ + 1)) {
            ZJC_DEBUG("add sync block height net: %u, pool: %u, height: %lu",
                net_id,
                pool_index_,
                prev_synced_height_ + 1);
            ZJC_INFO("kvsync add sync block height net: %u, pool: %u, height: %lu",
                net_id,
                pool_index_,
                prev_synced_height_ + 1);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                prev_synced_height_ + 1,
                sync::kSyncHighest);
        }
    }
}

double TxPool::CheckLeaderValid(bool get_factor, uint32_t* finished_count, uint32_t* tx_count) {
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    all_finish_tx_count_ += finish_tx_count_;
    double factor = 0.0;
    if (get_factor) {
        if (all_tx_count_ > 0) {
            factor = (double)all_finish_tx_count_ / (double)all_tx_count_;
        } else {
            factor = 1.0;
        }

        *finished_count = all_finish_tx_count_;
        *tx_count = all_tx_count_;
        all_tx_count_ = 0;
        all_finish_tx_count_ = 0;
    }

    all_tx_count_ += gid_map_.size();
    finish_tx_count_ = 0;
    return factor;
}

void TxPool::ConsensusAddTxs(const pools::TxItemPtr& tx) {
    assert(false);
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    if (!pools::IsUserTransaction(tx->tx_info->step())) {
        ZJC_DEBUG("invalid tx add to consensus tx map: %d, gid: %s",
            tx->tx_info->step(),
            common::Encode::HexEncode(tx->tx_info->gid()).c_str());
        return;
    }

    gid_map_[tx->tx_info->gid()] = tx;
    CHECK_MEMORY_SIZE_WITH_MESSAGE(
        gid_map_, 
        (std::string("pool index: ") + std::to_string(pool_index_)).c_str());
    tx->is_consensus_add_tx = true;
    consensus_tx_map_[tx->unique_tx_hash] = tx;
    CHECK_MEMORY_SIZE_WITH_MESSAGE(
        consensus_tx_map_, 
        (std::string("pool index: ") + std::to_string(pool_index_)).c_str());
    ZJC_DEBUG("success add consensus tx gid: %s",
        common::Encode::HexEncode(tx->tx_info->gid()).c_str());
}

void TxPool::ConsensusAddTxs(const std::vector<pools::TxItemPtr>& txs) {
    assert(false);
    return;
    common::AutoSpinLock auto_lock(tx_pool_mutex_);
    for (uint32_t i = 0; i < txs.size(); ++i) {
        if (!pools::IsUserTransaction(txs[i]->tx_info->step())) {
            ZJC_DEBUG("invalid tx add to consensus tx map: %d, gid: %s",
                txs[i]->tx_info->step(),
                common::Encode::HexEncode(txs[i]->tx_info->gid()).c_str());
            continue;
        }

        gid_map_[txs[i]->tx_info->gid()] = txs[i];
        CHECK_MEMORY_SIZE_WITH_MESSAGE(
            gid_map_, 
            (std::string("pool index: ") + std::to_string(pool_index_)).c_str());
        txs[i]->is_consensus_add_tx = true;
        consensus_tx_map_[txs[i]->unique_tx_hash] = txs[i];
        CHECK_MEMORY_SIZE_WITH_MESSAGE(
            consensus_tx_map_, 
            (std::string("pool index: ") + std::to_string(pool_index_)).c_str());
        ZJC_DEBUG("pool: %d, success add tx step: %d, to: %s, gid: %s, txhash: %s", 
            pool_index_,
            txs[i]->tx_info->step(), 
            common::Encode::HexEncode(txs[i]->tx_info->to()).c_str(),
            common::Encode::HexEncode(txs[i]->tx_info->gid()).c_str(),
            common::Encode::HexEncode(txs[i]->unique_tx_hash).c_str());
    }
}

}  // namespace pools

}  // namespace shardora
