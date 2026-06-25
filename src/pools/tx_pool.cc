#include "pools/tx_pool.h"
#include <cassert>
#include <common/log.h>

#include "common/encode.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "consensus/consensus_utils.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace pools {

// Ensure msg_ptr->status_notify_cb is populated before calling set_status.
// Txs that arrived via network sync (not HTTP) never passed FirewallCheckMessage,
// so their status_notify_cb is empty. Fill it from the pool manager's callback.
static inline void SetTxStatus(
        TxPoolManager* mgr,
        const transport::MessagePtr& msg_ptr,
        transport::MessageHandleStatus status) {
    if (msg_ptr == nullptr) return;
    if (!msg_ptr->status_notify_cb && mgr != nullptr) {
        auto cb = mgr->GetTxStatusCallback();
        if (cb && !msg_ptr->msg_hash.empty()) {
            msg_ptr->status_notify_cb = [cb](const std::string& hash,
                                              transport::MessageHandleStatus s) {
                cb(common::Encode::HexEncode(hash), s);
            };
        }
    }

    SHARDORA_DEBUG("set tx status: %d, hash: %s, msg_ptr->status_notify_cb: %d", 
        (int32_t)status, common::Encode::HexEncode(msg_ptr->msg_hash).c_str(), (msg_ptr->status_notify_cb != nullptr));
    msg_ptr->set_status(status);
}
    
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
    // // CheckThreadIdValid();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        SHARDORA_DEBUG("get invalid network id: %u, latest_height_: %lu", 
            common::GlobalInfo::Instance()->network_id(), latest_height_);
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        SHARDORA_DEBUG("get invalid network id: %u, latest_height_: %lu", 
            common::GlobalInfo::Instance()->network_id(), latest_height_);
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

    SHARDORA_DEBUG("init height tree success, net_id: %u, pool_index_: %u, latest_height_: %lu, synced_height_: %lu", 
        net_id, pool_index_, latest_height_, synced_height_);
    height_tree_ptr_ = height_tree_ptr;
    SyncMissingBlocks(common::TimeUtils::TimestampMs());
}

uint32_t TxPool::SyncMissingBlocks(uint64_t now_tm_ms) {
    if (!height_tree_ptr_) {
        if (has_missing_height_) {
            InitHeightTree();
        }

        if (!height_tree_ptr_) {
            SHARDORA_DEBUG("pool: %u, get invalid height_tree_ptr_ size: %u, latest_height_: %lu", 
                pool_index_, 0, latest_height_);
            return 0;
        }
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        SHARDORA_DEBUG("get invalid heights size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    SHARDORA_DEBUG("%u get invalid heights size: %u, latest_height_: %lu", 
        pool_index_, invalid_heights.size(), latest_height_);
    uint32_t synced_count = 0;
    if (invalid_heights.size() > 0) {
        auto net_id = common::GlobalInfo::Instance()->network_id();
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
            return 0;
        }

        uint64_t min_height = invalid_heights[0];
        for (uint64_t i = min_height; i < latest_height_; ++i) {
            if (prefix_db_->BlockExists(net_id, pool_index_, i)) {
                SHARDORA_DEBUG("block exists now add sync height 1, %u_%u_%lu", 
                    net_id,
                    pool_index_,
                    i);
                height_tree_ptr_->Set(i);
                std::erase(invalid_heights, i);
                continue;
            }

            SHARDORA_DEBUG("now add sync height 1, %u_%u_%lu", 
                net_id,
                pool_index_,
                i);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                i,
                sync::kSyncHigh);
            ++synced_count;
            if (synced_count >= 128u) {
                break;
            }
        }
    }

    if (synced_count > 0) {
        has_missing_height_ = true;
    } else {
        has_missing_height_ = false;
    }

    return invalid_heights.size();
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
    if (tx_ptr->tx_info->step() == pools::protobuf::kContractExcute || 
            tx_ptr->tx_info->step() == pools::protobuf::kContractRefund) {
        if (tx_ptr->address_info->addr().size() != common::kPreypamentAddressLength) {
            SHARDORA_DEBUG("trace tx pool: %d, kContractExcute "
                "failed add tx %s, key: %s, nonce: %lu, step: %d", 
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
                tx_ptr->tx_info->nonce(),
                (int32_t)tx_ptr->tx_info->step());
            SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, shardora::transport::MessageHandleStatus::kRequestInvalid);
            return kPoolsError;
        }
    }

    if (tx_ptr->tx_key.empty()) {
        SHARDORA_DEBUG("add failed unique hash empty: %d", (int32_t)tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    tx_ptr->elect_height = latest_elect_height_;
    added_txs_.push(tx_ptr);
    tx_pool_dirty_ = true;
    if (pools_mgr_ != nullptr) {
        pools_mgr_->OnTxPoolAddTx(
            tx_ptr->tx_info->step(),
            tx_ptr->tx_info->to(),
            tx_ptr->tx_info->value());
    }
    SHARDORA_DEBUG("trace tx pool: %d, success add tx %s, key: %s, nonce: %lu, step: %d", 
        pool_index_,
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
        tx_ptr->tx_info->nonce(),
        (int32_t)tx_ptr->tx_info->step());
    if (tx_ptr->tx_info->step() == pools::protobuf::kContractExcute || 
            tx_ptr->tx_info->step() == pools::protobuf::kContractRefund) {
        //assert(tx_ptr->address_info->addr().size() == common::kPreypamentAddressLength);
    }
    
    return kPoolsSuccess;
}

void TxPool::RecordNormalToDelay(uint64_t now_tm_us, const TxItemPtr& tx_ptr) {
    if (tx_ptr == nullptr || tx_ptr->tx_info == nullptr ||
            tx_ptr->tx_info->step() != pools::protobuf::kNormalTo) {
        return;
    }

    ++normal_to_delay_tx_count_;
    normal_to_delay_tm_us_ += now_tm_us - tx_ptr->receive_tm_us;
}

void TxPool::MaybeReportNormalToDelay(uint64_t now_tm_us) {
    if (prev_normal_to_delay_tm_timeout_ + 3000lu > (now_tm_us / 1000lu)) {
        return;
    }

    const uint64_t avg = normal_to_delay_tx_count_ > 0 ?
        normal_to_delay_tm_us_ / normal_to_delay_tx_count_ : 0;
    SHARDORA_WARN("[NormalToLatency] average delay pool: %d, normal_to avg=%lu us, count=%lu",
        pool_index_,
        avg,
        normal_to_delay_tx_count_);
    normal_to_delay_tm_us_ = 0;
    normal_to_delay_tx_count_ = 0;
    prev_normal_to_delay_tm_timeout_ = now_tm_us / 1000lu;
}

void TxPool::TxOver(view_block::protobuf::ViewBlockItem& view_block) {
    // CheckThreadIdValid();
    auto now_tm_us = common::TimeUtils::TimestampUs();
    SHARDORA_DEBUG("0 now tx size: %u, now tx size: %u", all_tx_size(), view_block.block_info().tx_list_size());
    auto over_addr_nonce_ptr = std::make_shared<std::unordered_map<std::string, uint64_t>>();
    for (uint32_t i = 0; i < (uint32_t)view_block.block_info().tx_list_size(); ++i) {
        auto& tx_info = view_block.block_info().tx_list(i);
        auto addr = IsTxUseFromAddress(tx_info.step()) ? 
            tx_info.from() : 
            tx_info.to();
        if (tx_info.step() == pools::protobuf::kContractExcute || 
                tx_info.step() == pools::protobuf::kContractRefund) {
            addr = tx_info.to() + tx_info.from();
        }

        if (addr.empty()) {
            SHARDORA_DEBUG("pool: %d, addr is empty: %s",
                pool_index_,
                ProtobufToJson(tx_info).c_str());
            //assert(false);
            continue;
        }

        auto remove_tx_func = [&](std::map<std::string, std::map<uint64_t, TxItemPtr>>& tx_map) {
            auto tx_iter = tx_map.find(addr);
            if (tx_iter != tx_map.end()) {
                for (auto nonce_iter = tx_iter->second.begin(); nonce_iter != tx_iter->second.end(); ) {
                    SHARDORA_DEBUG("pool: %d, find tx addr success: %s, unique hash: %s, "
                        "step: %lu, nonce: %lu, consensus nonce: %lu, key: %s", 
                        pool_index_,
                        common::Encode::HexEncode(addr).c_str(),
                        common::Encode::HexEncode(tx_info.unique_hash()).c_str(),
                        (int32_t)tx_info.step(),
                        tx_info.nonce(),
                        nonce_iter->second->tx_info->nonce(),
                        common::Encode::HexEncode(nonce_iter->second->tx_info->key()).c_str());
                    if (!IsUserTransaction(tx_info.step())) {
                        if (nonce_iter->second->tx_info->key() != tx_info.unique_hash()) {
                            ++nonce_iter;
                            continue;
                        }

                        // SHARDORA_DEBUG("trace tx pool: %d, success add unique tx %s, key: %s, "
                        //     "nonce: %lu, step: %d, unique hash exists: %s", 
                        //     pool_index_,
                        //     common::Encode::HexEncode(tx_info.to()).c_str(), 
                        //     common::Encode::HexEncode(tx_info.unique_hash()).c_str(), 
                        //     tx_info.nonce(),
                        //     (int32_t)tx_info.step(),
                        //     common::Encode::HexEncode(addr).c_str());
                    } else {
                        if (nonce_iter->first > tx_info.nonce()) {
                            break;
                        }
                    }

                    if (IsUserTransaction(tx_info.step())) {
                        ++all_delay_tx_count_;
                        all_delay_tm_us_ += now_tm_us - nonce_iter->second->receive_tm_us;
                    } else if (tx_info.step() == pools::protobuf::kNormalTo) {
                        RecordNormalToDelay(now_tm_us, nonce_iter->second);
                    }

                    // SHARDORA_DEBUG("trace tx pool: %d, over tx addr: %s, nonce: %lu", 
                    //     pool_index_,
                    //     common::Encode::HexEncode(addr).c_str(), 
                    //     nonce_iter->first);
                    auto tx_ptr = nonce_iter->second;
                    // SHARDORA_DEBUG("pool: %d, over pop success add system tx nonce addr: %s, "
                    //     "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %d",
                    //     pool_index_,
                    //     common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    //     tx_ptr->address_info->nonce(), 
                    //     tx_ptr->tx_info->nonce(),
                    //     common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                    //     (int32_t)tx_ptr->tx_info->step());
                    nonce_iter = tx_iter->second.erase(nonce_iter);
                }

                if (tx_iter->second.empty()) {
                    tx_map.erase(tx_iter);
                }
            } else {
                SHARDORA_DEBUG("pool: %d, find tx addr failed: %s",
                    pool_index_, common::Encode::HexEncode(addr).c_str());
            }
        };
        
        remove_tx_func(tx_map_);
        remove_tx_func(consensus_tx_map_);
        SHARDORA_DEBUG("trace tx pool: %d, step: %d, from: %s, to: %s, unique hash: %s, over tx addr: %s, nonce: %lu", 
            pool_index_,
            (int32_t)tx_info.step(),
            common::Encode::HexEncode(tx_info.from()).c_str(), 
            common::Encode::HexEncode(tx_info.to()).c_str(), 
            common::Encode::HexEncode(tx_info.unique_hash()).c_str(), 
            common::Encode::HexEncode(addr).c_str(), 
            tx_info.nonce());
        (*over_addr_nonce_ptr)[addr] = tx_info.nonce();
    }
        
    SHARDORA_DEBUG("pool: %d, all now tx size: %u, now tx size: %u, all_delay_tx_count_: %u", 
        pool_index_, all_tx_size(), view_block.block_info().tx_list_size(), all_delay_tx_count_);
    if (prev_delay_tm_timeout_ + 3000lu <= (now_tm_us / 1000lu) && all_delay_tx_count_ > 0) {
        SHARDORA_WARN("pool: %d, average delay us: %lu",
            pool_index_, (all_delay_tm_us_ / all_delay_tx_count_));
        all_delay_tm_us_ = 0;
        all_delay_tx_count_ = 0;
        prev_delay_tm_timeout_ = now_tm_us / 1000lu;
    }
    MaybeReportNormalToDelay(now_tm_us);

    over_addr_map_queue_.push(over_addr_nonce_ptr);
    tx_pool_dirty_ = true;  // nonces advanced, previously stuck txs may now be valid
}

void TxPool::GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func,
        const std::unordered_map<std::string, uint64_t>& leader_nonce_map) {
    // CheckThreadIdValid();
    TxItemPtr tx_ptr;
    while (added_txs_.pop(&tx_ptr)) {
        SHARDORA_DEBUG("pool: %d, pop success add system tx nonce addr: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
         if (!IsUserTransaction(tx_ptr->tx_info->step())) {
            if (prefix_db_->ExistsOverUniqueHash(tx_ptr->tx_info->key())) {
                SHARDORA_DEBUG("overed tx pool: %d, success add system tx nonce addr: %s, "
                    "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %u",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce(),
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                    (uint32_t)tx_ptr->tx_info->step());
                continue;
            }

            if (tx_ptr->tx_info->to() != tx_ptr->address_info->addr()) {
                SHARDORA_WARN("invalid address pool: %u, success add system tx nonce addr: %s, to: %s, "
                    "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %u",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce(),
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                    (int32_t)tx_ptr->tx_info->step());
                continue;
            }

            tx_map_[tx_ptr->tx_info->to()][tx_ptr->tx_info->nonce()] = tx_ptr;
            SHARDORA_DEBUG("pool: %u, success add system tx nonce addr: %s, to: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %u",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                (int32_t)tx_ptr->tx_info->step());
            continue;
        }

        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            SHARDORA_DEBUG("pool: %d, failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce());
            if (tx_ptr->msg_ptr) {
                SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
            }
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                SHARDORA_DEBUG("pool: %d, exists failed add tx nonce invalid "
                    "addr: %s, addr nonce: %lu, tx nonce: %lu",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce());
                if (tx_ptr->msg_ptr) {
                    SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
                }
                continue;
            }
        }

        tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        // consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        SHARDORA_DEBUG("pool: %d, success add tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
            pool_index_,
            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
            tx_ptr->address_info->nonce(), 
            tx_ptr->tx_info->nonce());
    }

    // SHARDORA_WARN("now tx size: %u", all_tx_size());
    // Budget: vote messages must fit within the network max packet size.
    // Reserve 256 KB for the vote message envelope (QC, signatures, etc.);
    // the remaining budget is available for piggybacked tx data.
    static const uint32_t kMaxVoteMsgTxBytes = 768 * 1024;  // 768 KB for tx payload
    static const uint32_t kMaxTxPerAddr      = 256;         // per-address tx cap
    uint32_t accumulated_bytes = 0;
    for (auto iter = tx_map_.begin(); iter != tx_map_.end(); ++iter) {
        if ((uint32_t)txs->size() >= count) {
            break;
        }

        if (accumulated_bytes >= kMaxVoteMsgTxBytes) {
            break;
        }

        uint64_t valid_nonce = common::kInvalidUint64;
        uint32_t addr_tx_count = 0;  // per-address counter, capped at kMaxTxPerAddr

        // Check if the leader already has txs for this address.
        // If so, start searching from the leader's max nonce.
        auto nonce_iter = iter->second.begin();
        uint64_t leader_known_nonce = 0;
        auto leader_it = leader_nonce_map.find(iter->first);
        if (leader_it != leader_nonce_map.end()) {
            leader_known_nonce = leader_it->second;
            auto tmp_iter = iter->second.lower_bound(leader_known_nonce);
            if (tmp_iter != iter->second.end()) {
                nonce_iter = tmp_iter;
            }
        }

        while (nonce_iter != iter->second.end()) {
            auto tx_ptr = nonce_iter->second;
            if (!IsUserTransaction(tx_ptr->tx_info->step())) {
                break;
            }

            // Skip nonces the leader already has
            if (leader_known_nonce > 0 && tx_ptr->tx_info->nonce() < leader_known_nonce) {
                ++nonce_iter;
                continue;
            }

            // if (tx_ptr->synced_leaders_.Valid(leader_idx)) {
            //     if (tx_ptr->elect_height == latest_elect_height_) {
            //         // If leader has a nonce map for this addr, don't break — keep searching forward
            //         if (leader_known_nonce > 0) {
            //             continue;
            //         }
                    
            //         SHARDORA_DEBUG("trace tx pool: %d, already synced to leader: %u, tx_key: %s, from: %s, to: %s, nonce: %lu, step: %d", 
            //             pool_index_, leader_idx, common::Encode::HexEncode(tx_ptr->tx_key).c_str(), 
            //             (tx_ptr->tx_info->pubkey().size() == (security::kPublicKeyUncompressSize - 1)) ? 
            //                 common::Encode::HexEncode(security_->GetAddress(tx_ptr->tx_info->pubkey())).c_str() : "",
            //             common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
            //             tx_ptr->tx_info->nonce(), (int32_t)tx_ptr->tx_info->step());
            //         break;
            //     }

            //     tx_ptr->synced_leaders_.clear();
            //     tx_ptr->elect_height = latest_elect_height_;
            // }

            if (valid_nonce == common::kInvalidUint64) {
                uint64_t now_nonce = 0ll;
                int res = tx_valid_func(
                        *tx_ptr->address_info, 
                        *tx_ptr->tx_info,
                        &now_nonce);
                if (res != 0) {
                    if (res == 3) {
                        SHARDORA_DEBUG("trace tx invalid tx, pool: %d, tx_key invalid: %s, res: %d, from: %s, to: %s, nonce: %lu, step: %u",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
                            res,
                            (tx_ptr->tx_info->pubkey().size() == (security::kPublicKeyUncompressSize - 1)) ? 
                                common::Encode::HexEncode(security_->GetAddress(tx_ptr->tx_info->pubkey())).c_str() : "",
                            common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                            tx_ptr->tx_info->nonce(),
                            (int32_t)tx_ptr->tx_info->step());
                        {
                            const auto now_tm_us = common::TimeUtils::TimestampUs();
                            ++all_delay_tx_count_;
                            all_delay_tm_us_ += now_tm_us - tx_ptr->receive_tm_us;
                        }
                        if (tx_ptr->msg_ptr) {
                            SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
                        }
                        nonce_iter = iter->second.erase(nonce_iter);
                        continue;
                    }

                    if (res > 0) {
                        ++nonce_iter;
                        continue;
                    }
                    
                    SHARDORA_DEBUG("break trace tx invalid tx, pool: %d, tx_key invalid: %s, res: %d, from: %s, to: %s, nonce: %lu, step: %u",
                        pool_index_,
                        common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
                        res,
                        (tx_ptr->tx_info->pubkey().size() == (security::kPublicKeyUncompressSize - 1)) ? 
                            common::Encode::HexEncode(security_->GetAddress(tx_ptr->tx_info->pubkey())).c_str() : "",
                        common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                        tx_ptr->tx_info->nonce(),
                        (int32_t)tx_ptr->tx_info->step());
                    break;
                }
            } else {
                if (valid_nonce + 1 != tx_ptr->tx_info->nonce()) {
                    break;
                }
            }

            valid_nonce = tx_ptr->tx_info->nonce();
            tx_ptr->synced_leaders_.Set(leader_idx);
            // Per-address cap: at most kMaxTxPerAddr txs per address per sync round.
            if (addr_tx_count >= kMaxTxPerAddr) {
                SHARDORA_DEBUG("trace tx pool: %d, addr tx cap reached addr: %s, count: %u",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    addr_tx_count);
                break;
            }

            // Byte budget: estimate serialized size and stop if over limit.
            uint32_t tx_bytes = static_cast<uint32_t>(tx_ptr->tx_info->ByteSizeLong());
            if (accumulated_bytes + tx_bytes > kMaxVoteMsgTxBytes) {
                SHARDORA_DEBUG("trace tx pool: %d, byte budget exhausted: accumulated=%u tx_bytes=%u limit=%u",
                    pool_index_, accumulated_bytes, tx_bytes, kMaxVoteMsgTxBytes);
                break;
            }

            SHARDORA_DEBUG("trace tx pool: %d, success sync to leader: %u, tx_key: %s, from: %s, to: %s, nonce: %lu, step: %d", 
                pool_index_, leader_idx, common::Encode::HexEncode(tx_ptr->tx_key).c_str(), 
                (tx_ptr->tx_info->pubkey().size() == (security::kPublicKeyUncompressSize - 1)) ? 
                    common::Encode::HexEncode(security_->GetAddress(tx_ptr->tx_info->pubkey())).c_str() : "",
                common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                tx_ptr->tx_info->nonce(), (int32_t)tx_ptr->tx_info->step());
            auto* tx = txs->Add();
            *tx = *tx_ptr->tx_info;
            accumulated_bytes += tx_bytes;
            ++addr_tx_count;
            ++nonce_iter;
            if ((uint32_t)txs->size() >= count) {
                break;
            }
        }

        if ((uint32_t)txs->size() >= count || accumulated_bytes >= kMaxVoteMsgTxBytes) {
            break;
        }
    }
}

void TxPool::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::vector<pools::TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    int32_t try_times = 0;
    while (res_map.size() < count && try_times++ < 10) {
        res_map.clear();
        TempGetTxIdempotently(msg_ptr, res_map, count, tx_valid_func);
        if (count == 1) {
            break;
        }

        if (added_txs_.size() == 0) {
            break;
        }
    }
}

void TxPool::TempGetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::vector<pools::TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    // CheckThreadIdValid();
    TxItemPtr tx_ptr;
    while (added_txs_.pop(&tx_ptr)) {
        SHARDORA_DEBUG("pool: %d, pop success add system tx nonce addr: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
        if (!IsUserTransaction(tx_ptr->tx_info->step())) {
            if (prefix_db_->ExistsOverUniqueHash(tx_ptr->tx_info->key())) {
                SHARDORA_DEBUG("overed tx pool: %d, success add system tx nonce addr: %s, "
                    "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %u",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce(),
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                    (uint32_t)tx_ptr->tx_info->step());
                continue;
            }

            //assert(tx_ptr->tx_info->to() == tx_ptr->address_info->addr());
            tx_map_[tx_ptr->tx_info->to()][tx_ptr->tx_info->nonce()] = tx_ptr;
            SHARDORA_DEBUG("pool: %d, success add system tx nonce addr: %s, to: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s, step: %u",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                (uint32_t)tx_ptr->tx_info->step());
            continue;
        }

        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            SHARDORA_DEBUG("pool: %d, failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce());
            if (tx_ptr->msg_ptr) {
                SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
            }
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                SHARDORA_DEBUG("pool: %d, exists failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce());
                if (tx_ptr->msg_ptr) {
                    SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
                }
                continue;
            }
        }

        tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        // consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        SHARDORA_DEBUG("pool: %d, success add tx nonce addr: %s, addr nonce: %lu, tx nonce: %lu, step: %d",
            pool_index_,
            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
            tx_ptr->address_info->nonce(), 
            tx_ptr->tx_info->nonce(),
            (int32_t)tx_ptr->tx_info->step());
    }

    while (consensus_added_txs_.pop(&tx_ptr)) {
        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                continue;
            }
        }

        auto tx_map_iter = tx_map_.find(tx_ptr->address_info->addr());
        if (tx_map_iter != tx_map_.end()) {
            auto nonce_iter = tx_map_iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != tx_map_iter->second.end()) {
                continue;
            }
        }

        consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        SHARDORA_DEBUG("pool: %d, consensus_added_txs_ success add tx nonce addr: %s, "
            "addr nonce: %lu, tx nonce: %lu, step: %d",
            pool_index_,
            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
            tx_ptr->address_info->nonce(), 
            tx_ptr->tx_info->nonce(),
            (int32_t)tx_ptr->tx_info->step());
    }

    // Optimization: if no new txs arrived (both queues were empty) and the pool
    // is not dirty (no state change since last empty scan), skip the expensive
    // full scan of tx_map_ / consensus_tx_map_.
    if (!tx_pool_dirty_) {
        SHARDORA_DEBUG("pool: %d, skip scan: pool not dirty, tx_map: %u, cons_map: %u",
            pool_index_, tx_map_.size(), consensus_tx_map_.size());
        return;
    }

    std::set<uint32_t> system_added_step;
    auto get_tx_func = [&](std::map<std::string, std::map<uint64_t, TxItemPtr>>& tx_map) {
        for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
            uint64_t valid_nonce = common::kInvalidUint64;
            for (auto nonce_iter = iter->second.begin(); nonce_iter != iter->second.end(); ) {
                auto tx_ptr = nonce_iter->second;
                if (!IsUserTransaction(tx_ptr->tx_info->step())) {
                    SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, "
                        "nonce: %lu, step: %d, unique hash: %s",
                        pool_index_,
                        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                        tx_ptr->tx_info->nonce(), 
                        (int32_t)tx_ptr->tx_info->step(),
                        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                    if (prefix_db_->ExistsOverUniqueHash(tx_ptr->tx_info->key())) {
                        SHARDORA_DEBUG("unique hash exists trace tx pool: %d, tx_key invalid addr: %s, "
                            "nonce: %lu, step: %d, unique hash: %s",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(), 
                            (int32_t)tx_ptr->tx_info->step(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        RecordNormalToDelay(common::TimeUtils::TimestampUs(), tx_ptr);
                        nonce_iter = iter->second.erase(nonce_iter);
                        continue;
                    }
                }

                ++nonce_iter;
                if (valid_nonce == common::kInvalidUint64) {
                    uint64_t now_nonce = 0llu;
                    int res = tx_valid_func(
                        *tx_ptr->address_info, 
                        *tx_ptr->tx_info,
                        &now_nonce);
                    SHARDORA_DEBUG("begin nonce, trace tx pool: %d, tx_key invalid addr: %s, "
                        "nonce: %lu, unique hash: %s, "
                        "now_nonce: %u, tx_ptr->tx_info->nonce() + iter->second.size(): %u, res: %d", 
                        pool_index_,
                        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                        tx_ptr->tx_info->nonce(),
                        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                        now_nonce,
                        (tx_ptr->tx_info->nonce() + iter->second.size()),
                        res);
                    if (res != 0) {
                        if (res == 3) {
                            SHARDORA_DEBUG("trace tx invalid tx, pool: %d, tx_key invalid: %s, res: %d, from: %s, to: %s, nonce: %lu, step: %u",
                                pool_index_,
                                common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
                                res,
                                common::Encode::HexEncode(tx_ptr->tx_info->pubkey()).c_str(),
                                common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                                tx_ptr->tx_info->nonce(),
                                (int32_t)tx_ptr->tx_info->step());
                            if (IsUserTransaction(tx_ptr->tx_info->step())) {
                                const auto now_tm_us = common::TimeUtils::TimestampUs();
                                ++all_delay_tx_count_;
                                all_delay_tm_us_ += now_tm_us - tx_ptr->receive_tm_us;
                            }
                            if (tx_ptr->msg_ptr) {
                                SetTxStatus(pools_mgr_, tx_ptr->msg_ptr, transport::kTxUserNonceInvalid);
                            }
                            auto erase_iter = iter->second.find(tx_ptr->tx_info->nonce());
                            if (erase_iter != iter->second.end()) {
                                nonce_iter = iter->second.erase(erase_iter);
                            }
                            continue;
                        }
                        
                        if (!IsUserTransaction(tx_ptr->tx_info->step())) {
                            if (nonce_iter == iter->second.end()) {
                                SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s, "
                                    "now_nonce: %u, tx_ptr->tx_info->nonce() + iter->second.size(): %u", 
                                    pool_index_,
                                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                    tx_ptr->tx_info->nonce(),
                                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                                    now_nonce,
                                    (tx_ptr->tx_info->nonce() + iter->second.size()));
                                break;
                            }

                            if (nonce_iter->second->tx_info->nonce() != tx_ptr->tx_info->nonce() + 1) {
                                SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s, "
                                    "now_nonce: %u, nonce_iter->second->tx_info->nonce(): %u", 
                                    pool_index_,
                                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                    tx_ptr->tx_info->nonce(),
                                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                                    now_nonce,
                                    nonce_iter->second->tx_info->nonce());
                                break;
                            }

                            continue;
                        } else {
                            if (res > 0) {
                                if (now_nonce >= tx_ptr->tx_info->nonce() + iter->second.size()) {
                                    SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s, "
                                        "now_nonce: %u, tx_ptr->tx_info->nonce() + iter->second.size(): %u", 
                                        pool_index_,
                                        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                        tx_ptr->tx_info->nonce(),
                                        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                                        now_nonce,
                                        (tx_ptr->tx_info->nonce() + iter->second.size()));
                                    break;
                                }

                                nonce_iter = iter->second.lower_bound(now_nonce);
                                if (nonce_iter->second->tx_info->nonce() != now_nonce) {
                                    SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s, "
                                        "now_nonce: %u, nonce_iter->second->tx_info->nonce(): %u", 
                                        pool_index_,
                                        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                        tx_ptr->tx_info->nonce(),
                                        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(),
                                        now_nonce,
                                        nonce_iter->second->tx_info->nonce());
                                    break;
                                }
                                
                                SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                                    pool_index_,
                                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                    tx_ptr->tx_info->nonce(),
                                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                                ++nonce_iter;
                                continue;
                            }
                        }
                        
                        
                        SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        break;
                    }
                } else {
                    if (tx_ptr->tx_info->nonce() != valid_nonce + 1) {
                        SHARDORA_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        break;
                    }
                }

                if (!IsUserTransaction(tx_ptr->tx_info->step()) &&
                        tx_ptr->tx_info->step() != pools::protobuf::kConsensusLocalTos && 
                        tx_ptr->tx_info->step() != pools::protobuf::kRootCreateAddress) {
                    auto iter = system_added_step.find(tx_ptr->tx_info->step());
                    if (iter != system_added_step.end()) {
                        SHARDORA_DEBUG("trace tx pool: %d, failed add tx addr: %s, nonce: %lu, step: %d, unique hash: %s", 
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(), 
                            (int32_t)tx_ptr->tx_info->step(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        // //assert(false);
                        continue;
                    }

                    system_added_step.insert(tx_ptr->tx_info->step());
                }

                valid_nonce = tx_ptr->tx_info->nonce();
                tx_ptr->receive_tm_us = common::TimeUtils::TimestampUs();
                res_map.push_back(tx_ptr);
                SHARDORA_DEBUG("iter addr: %s, trace tx pool: %d, "
                    "consensus leader tx addr: %s, key: %s, nonce: %lu, "
                    "res count: %u, count: %u, tx_map size: %u, addr tx size: %u", 
                    common::Encode::HexEncode(iter->first).c_str(), 
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
                    tx_ptr->tx_info->nonce(),
                    res_map.size(),
                    count,
                    tx_map.size(),
                    iter->second.size());
                if (res_map.size() >= count) {
                    break;
                }
            }

            if (res_map.size() >= count) {
                break;
            }
        }
    };

    get_tx_func(tx_map_);
    SHARDORA_DEBUG("pool: %d, now get tx by leader all: %u, added tx size: %u, "
        "get: %u, count: %u", 
        pool_index_, all_tx_size(), added_txs_.size(),
        res_map.size(), count);
    get_tx_func(consensus_tx_map_);
    SHARDORA_DEBUG("pool: %d, now get tx by leader all: %u, added tx size: %u, "
        "get: %u, count: %u", 
        pool_index_, all_tx_size(), added_txs_.size(),
        res_map.size(), count);
    // If the full scan yielded 0 valid txs, mark pool as clean so the next
    // call can skip the scan unless new txs arrive or nonces advance.
    if (res_map.empty()) {
        tx_pool_dirty_ = false;
    }
    MaybeReportNormalToDelay(common::TimeUtils::TimestampUs());
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
        // Update the status of tx_pool in memory according to the database
        if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
            latest_height_ = pool_info.height();
            latest_hash_ = pool_info.hash();
            synced_height_ = pool_info.synced_height();
            latest_timestamp_ = pool_info.timestamp();
            prev_synced_height_ = synced_height_;
            to_sync_max_height_ = latest_height_;
            SHARDORA_DEBUG("init latest pool info shard: %u, pool %lu, init height: %lu",
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

uint64_t TxPool::UpdateLatestInfo(
        uint64_t height,
        const std::string& hash,
        const std::string& prehash,
        const uint64_t timestamp) {
    if (!kv_sync_) {
        return common::kInvalidUint64;
    }
    
    // // CheckThreadIdValid();
    auto tmp_height_tree_ptr = height_tree_ptr_;
    if (!tmp_height_tree_ptr) {
        InitHeightTree();
    }

    tmp_height_tree_ptr = height_tree_ptr_;
    if (tmp_height_tree_ptr) {
        SHARDORA_DEBUG("success set height, net: %u, pool: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), pool_index_, height);
        tmp_height_tree_ptr->Set(height);
    }

    if (latest_height_ == common::kInvalidUint64 || latest_height_ < height) {
        latest_height_ = height;
        latest_hash_ = hash;
        latest_timestamp_ = timestamp;
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

    SHARDORA_DEBUG("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
        pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
    return synced_height_;
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
            SHARDORA_DEBUG("now add sync height 1, %u_%u_%lu", 
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

void TxPool::ConsensusAddTxs(const pools::TxItemPtr& tx_ptr) {
    if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
        return;
    }

    if (!IsUserTransaction(tx_ptr->tx_info->step())) {
        return;
    }

    // // CheckThreadIdValid();
    if (consensus_added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        SHARDORA_WARN("add failed extend %u, %u, all valid: %u", 
            consensus_added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), all_tx_size());
        return;
    }

    if (tx_ptr->tx_key.empty()) {
        SHARDORA_WARN("add failed unique hash empty: %d", (int32_t)tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    SHARDORA_DEBUG("trace tx pool: %d, sync add tx addr: %s, nonce: %lu", 
        pool_index_,
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
        tx_ptr->tx_info->nonce());
    consensus_added_txs_.push(tx_ptr);
    tx_pool_dirty_ = true;
}

}  // namespace pools

}  // namespace shardora
