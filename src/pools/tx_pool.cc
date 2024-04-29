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
    if (height_tree_ptr_ == nullptr) {
        return 0;
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
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
//     common::AutoSpinLock auto_lock(mutex_);
    if (removed_gid_.DataExists(tx_ptr->tx_info.gid())) {
        return kPoolsTxAdded;
    }

    if (gid_map_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        // ZJC_WARN("add failed extend 1024");
        return kPoolsError;
    }

    if (tx_ptr->unique_tx_hash.empty()) {
        ZJC_WARN("add failed unique hash empty: %d", tx_ptr->tx_info.step());
        tx_ptr->unique_tx_hash = pools::GetTxMessageHash(tx_ptr->tx_info);
    }

    assert(tx_ptr != nullptr);
    auto iter = gid_map_.find(tx_ptr->tx_info.gid());
    if (iter != gid_map_.end()) {
        return kPoolsTxAdded;
    }

    if (tx_ptr->step == pools::protobuf::kCreateLibrary) {
        universal_prio_map_[tx_ptr->prio_key] = tx_ptr;
    } else {
        prio_map_[tx_ptr->prio_key] = tx_ptr;
    }

    gid_map_[tx_ptr->tx_info.gid()] = tx_ptr;
#ifdef LATENCY
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (prev_tx_count_tm_us_ == 0) {
        prev_tx_count_tm_us_ = now_tm_us;
    }

    if (now_tm_us > prev_tx_count_tm_us_ + 3000000lu) {
        ZJC_INFO("waiting_tx_count pool: %d: tx: %llu", pool_index_, gid_map_.size());
        prev_tx_count_tm_us_ = now_tm_us;
    }

    gid_start_time_map_[tx_ptr->tx_info.gid()] = common::TimeUtils::TimestampUs(); 
    oldest_timestamp_ = prio_map_.begin()->second->time_valid;
#endif
    timeout_txs_.push(tx_ptr->tx_info.gid());
    return kPoolsSuccess;
}

void TxPool::GetTx(
        const std::map<std::string, pools::TxItemPtr>& invalid_txs, 
        transport::protobuf::Header& header,
        uint32_t count) {
    std::vector<TxItemPtr> recover_txs;
    zbft::protobuf::TxBft* txbft = header.mutable_zbft()->mutable_tx_bft();
    auto iter = prio_map_.begin();
    auto* kv_sync = header.mutable_sync();
    while (iter != prio_map_.end() && txbft->txs_size() < count) {
        auto invalid_iter = invalid_txs.find(iter->second->unique_tx_hash);
        if (invalid_iter != invalid_txs.end()) {
            ++iter;
            continue;
        }

        auto* tx = txbft->add_txs();
        *tx = iter->second->tx_info;
        ZJC_DEBUG("backup success get local transfer to tx %u, %s, step: %d",
            pool_index_, 
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str(),
            iter->second->tx_info.step());
        assert(!iter->second->unique_tx_hash.empty());
        ++iter;
    }
}

void TxPool::GetTx(
        std::map<std::string, TxItemPtr>& res_map, 
        uint32_t count, 
        std::unordered_map<std::string, std::string>& kvs) {
    GetTx(universal_prio_map_, res_map, count, kvs);
    if (!res_map.empty()) {
        return;
    }

    GetTx(prio_map_, res_map, count, kvs);
    GetTx(consensus_tx_map_, res_map, count, kvs);
}

void TxPool::GetTx(
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count,
        std::unordered_map<std::string, std::string>& kvs) {
    auto timestamp_now = common::TimeUtils::TimestampUs();
    std::vector<TxItemPtr> recover_txs;
    auto iter = src_prio_map.begin();
    while (iter != src_prio_map.end() && res_map.size() < count) {
        res_map[iter->second->unique_tx_hash] = iter->second;
        ZJC_DEBUG("leader success get local transfer to tx %u, %s, step: %d",
            pool_index_, 
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str(),
            iter->second->tx_info.step());
        assert(!iter->second->unique_tx_hash.empty());
        iter = src_prio_map.erase(iter);
    }
}

void TxPool::CheckTimeoutTx() {
//     common::AutoSpinLock auto_lock(mutex_);
    auto now_tm = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    while (!timeout_txs_.empty() && count++ < 64) {
        auto& gid = timeout_txs_.front();
        auto iter = gid_map_.find(gid);
        if (iter == gid_map_.end()) {
            timeout_txs_.pop();
            continue;
        }

        if (iter->second->timeout > now_tm) {
            break;
        }

        timeout_txs_.pop();
        timeout_remove_txs_.push(gid);
    }

    count = 0;
    while (!timeout_remove_txs_.empty() && count++ < 64) {
        auto& gid = timeout_remove_txs_.front();
        auto iter = gid_map_.find(gid);
        if (iter == gid_map_.end()) {
            timeout_remove_txs_.pop();
            continue;
        }

        if (iter->second->remove_timeout > now_tm) {
            break;
        }

        RemoveTx(gid);
        timeout_remove_txs_.pop();
        ZJC_DEBUG("timeout remove gid: %s, tx hash: %s",
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str());
    }
}

void TxPool::TxRecover(std::map<std::string, TxItemPtr>& txs) {
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        iter->second->in_consensus = false;
        auto miter = gid_map_.find(iter->first);
        if (miter != gid_map_.end()) {
            if (miter->second->is_consensus_add_tx) {
                consensus_tx_map_[miter->second->unique_tx_hash] = miter->second;
                continue;
            }

            if (iter->second->step == pools::protobuf::kCreateLibrary) {
                universal_prio_map_[miter->second->prio_key] = miter->second;
            } else {
                prio_map_[miter->second->prio_key] = miter->second;
            }
        }
    }
}

void TxPool::RemoveTx(const std::string& gid) {
    removed_gid_.Push(gid);
    auto giter = gid_map_.find(gid);
    if (giter == gid_map_.end()) {
        return;
    }

    giter->second->in_consensus = false;
    auto prio_iter = prio_map_.find(giter->second->prio_key);
    if (prio_iter != prio_map_.end()) {
        prio_map_.erase(prio_iter);
    }

    auto universal_prio_iter = universal_prio_map_.find(giter->second->prio_key);
    if (universal_prio_iter != universal_prio_map_.end()) {
        universal_prio_map_.erase(universal_prio_iter);
    }

    auto cons_iter = consensus_tx_map_.find(giter->second->unique_tx_hash);
    if (cons_iter != consensus_tx_map_.end()) {
        consensus_tx_map_.erase(cons_iter);
    }

//     ZJC_DEBUG("remove tx success gid: %s, tx hash: %s",
//         common::Encode::HexEncode(giter->second->gid).c_str(),
//         common::Encode::HexEncode(giter->second->tx_hash).c_str());
    gid_map_.erase(giter);
#ifdef LATENCY    
    if (!prio_map_.empty()) {
        oldest_timestamp_ = prio_map_.begin()->second->time_valid;
    } else {
        oldest_timestamp_ = 0;
    }
#endif
}

void TxPool::TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto& gid = tx_list[i].gid(); 
        RemoveTx(gid);
#ifdef LATENCY
        // 统计交易确认延迟
        auto now_tm = common::TimeUtils::TimestampUs();
        auto start_tm_iter = gid_start_time_map_.find(gid);
        if (start_tm_iter != gid_start_time_map_.end()) {
            latencys_us_.push_back(now_tm - start_tm_iter->second);
            // ZJC_INFO("tx latency gid: %s, us: %llu",
            //     common::Encode::HexEncode(gid).c_str(), now_tm - start_tm_iter->second);
            gid_start_time_map_.erase(gid);
        }

        if (latencys_us_.size() > 10) {
            uint64_t p50 = common::GetNthElement(latencys_us_, 0.5);
            latencys_us_.clear();
        
            ZJC_INFO("tx latency p50: %llu", p50);
        }
#endif
    }

    finish_tx_count_ += tx_list.size();
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
    if (height_tree_ptr_ == nullptr) {
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
    }
}

uint64_t TxPool::UpdateLatestInfo(
        uint64_t height,
        const std::string& hash,
        const std::string& prehash,
        const uint64_t timestamp) {
    if (height_tree_ptr_ == nullptr) {
        InitHeightTree();
    }

    if (height_tree_ptr_ != nullptr) {
        height_tree_ptr_->Set(height);
        ZJC_DEBUG("success set height, net: %u, pool: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), pool_index_, height);
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
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                prev_synced_height_ + 1,
                sync::kSyncHighest);
        }
    }
}

double TxPool::CheckLeaderValid(bool get_factor, uint32_t* finished_count, uint32_t* tx_count) {
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


std::shared_ptr<consensus::WaitingTxsItem> TxPool::GetTx(
        const google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>& txs,
        std::vector<uint8_t>* invalid_txs) {
    auto txs_items = std::make_shared<consensus::WaitingTxsItem>();
    auto& tx_map = txs_items->txs;
    for (int32_t i = 0; i < txs.size(); ++i) {
        auto txhash = "";  // txs[i].txhash();
        auto iter = gid_map_.find(txhash);
        if (iter == gid_map_.end()) {
            ZJC_INFO("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(txhash).c_str());
            if (invalid_txs == nullptr) {
                return nullptr;
            }

            invalid_txs->push_back(i);
            continue;
        }

        if (invalid_txs != nullptr && !invalid_txs->empty()) {
            continue;
        }

        ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(txhash).c_str());
        tx_map[txhash] = iter->second;
    }
    
    if (invalid_txs != nullptr && !invalid_txs->empty()) {
        return nullptr;
    }

    return txs_items;
}

void TxPool::ConsensusAddTxs(const std::vector<pools::TxItemPtr>& txs) {
    for (uint32_t i = 0; i < txs.size(); ++i) {
        auto iter = gid_map_.find(txs[i]->tx_info.gid());
        if (iter != gid_map_.end()) {
            ZJC_DEBUG("tx already exist, gid: %s", txs[i]->tx_info.gid().c_str());
            continue;
        }

        if (removed_gid_.DataExists(txs[i]->tx_info.gid())) {
            ZJC_DEBUG("tx already removed, gid: %s", txs[i]->tx_info.gid().c_str());
            continue;
        }   

        // bool valid = true;
        // switch (txs[i]->tx_info.step()) {
        //     case pools::protobuf::kJoinElect:
        //         if (!CheckJoinElectTxInfo(txs[i]->tx_info)) {
        //             valid = false;
        //         }

        //         break;
        //     default:
        //         break;
        // }

        // if (!valid) {
        //     ZJC_FATAL("tx invalid!");
        //     continue;
        // }

        gid_map_[txs[i]->tx_info.gid()] = txs[i];
        txs[i]->is_consensus_add_tx = true;
        consensus_tx_map_[txs[i]->unique_tx_hash] = txs[i];
    }
}

}  // namespace pools

}  // namespace shardora
