#include "pools/tx_pool.h"
#include <cassert>

#include "common/encode.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"

namespace zjchain {

namespace pools {
    
TxPool::TxPool() {}

TxPool::~TxPool() {}

void TxPool::Init(uint32_t pool_idx) {
    pool_index_ = pool_idx;
    added_tx_map_.reserve(10240);
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
//     common::AutoSpinLock lock(mutex_);
    assert(tx_ptr != nullptr);
    auto iter = added_tx_map_.find(tx_ptr->tx_hash);
    if (iter != added_tx_map_.end()) {
        return kPoolsTxAdded;
    }

    added_tx_map_[tx_ptr->tx_hash] = tx_ptr;
    prio_map_[tx_ptr->prio_key] = tx_ptr;
    gid_map_[tx_ptr->gid] = tx_ptr;
//     ZJC_DEBUG("success add tx %u, %u, %u", pool_index_, added_tx_map_.size(), prio_map_.size());
    return kPoolsSuccess;
}

void TxPool::GetTx(std::map<std::string, TxItemPtr>& res_map, uint32_t count) {
    auto timestamp_now = common::TimeUtils::TimestampUs();
    std::vector<TxItemPtr> recover_txs;
//     common::AutoSpinLock lock(mutex_);
//     if (prio_map_.size() < 2 * count) {
//         return;
//     }

    auto iter = prio_map_.begin();
    while (iter != prio_map_.end()) {
        if (iter->second->time_valid >= timestamp_now) {
            break;
        }

        res_map[iter->second->tx_hash] = iter->second;
        prio_map_.erase(iter++);
        if (res_map.size() >= count) {
//             ZJC_INFO("1 get tx mem size: %d, get: %d", prio_map_.size(), res_map.size());
            return;
        }
    }
}

bool TxPool::CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now) {
    if (tx_ptr->timeout > timestamp_now) {
        return false;
    }

    while (!timeout_txs_.empty()) {
        auto& item = timeout_txs_.front();
        auto miter = added_tx_map_.find(item->tx_hash);
        if (miter == added_tx_map_.end()) {
            timeout_txs_.pop_front();
            continue;
        }

        if (item->remove_timeout < timestamp_now) {
            if (miter != added_tx_map_.end()) {
//                 ZJC_DEBUG("timeout remove tx %s", common::Encode::HexEncode(miter->second->tx_hash).c_str());
                added_tx_map_.erase(miter);
            }

            timeout_txs_.pop_front();
            continue;
        }

        break;
    }

    timeout_txs_.push_back(tx_ptr);
    return true;
}

void TxPool::GetTx(
        const common::BloomFilter& bloom_filter,
        std::map<std::string, TxItemPtr>& res_map) {
//     common::AutoSpinLock lock(mutex_);
    for (auto iter = added_tx_map_.begin(); iter != added_tx_map_.end(); ++iter) {
        if (bloom_filter.Contain(common::Hash::Hash64(iter->second->tx_hash))) {
            res_map[iter->second->tx_hash] = iter->second;
//             ZJC_DEBUG("bloom filter success get tx %u, %s",
//                 pool_index_, common::Encode::HexEncode(iter->second->tx_hash).c_str());
        }
    }
}

void TxPool::TxRecover(std::map<std::string, TxItemPtr>& txs) {
//     common::AutoSpinLock lock(mutex_);
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        iter->second->in_consensus = false;
        if (iter->second->step == pools::protobuf::kNormalTo) {
            ZJC_DEBUG("pools::protobuf::kNormalTo recover and can get.");
        }

        auto miter = added_tx_map_.find(iter->first);
        if (miter != added_tx_map_.end()) {
            prio_map_[miter->second->prio_key] = miter->second;
        }
    }
}

void TxPool::TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
//     common::AutoSpinLock lock(mutex_);
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto giter = gid_map_.find(tx_list[i].gid());
        if (giter == gid_map_.end()) {
            continue;
        }

        giter->second->in_consensus = false;
        auto miter = added_tx_map_.find(giter->second->tx_hash);
        if (miter != added_tx_map_.end()) {
            added_tx_map_.erase(miter);
        }

        auto prio_iter = prio_map_.find(giter->second->prio_key);
        if (prio_iter != prio_map_.end()) {
            prio_map_.erase(prio_iter);
        }

        gid_map_.erase(giter);
    }

    ZJC_DEBUG("0 tx over %u, map: %u, prio_map: %u, gid map: %u",
        tx_list.size(), added_tx_map_.size(), prio_map_.size(), gid_map_.size());
}

void TxPool::TxOver(std::map<std::string, TxItemPtr>& txs) {
//     common::AutoSpinLock lock(mutex_);
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        auto miter = added_tx_map_.find(iter->first);
        if (miter != added_tx_map_.end()) {
            added_tx_map_.erase(miter);
//             ZJC_DEBUG("remove tx %s", common::Encode::HexEncode(iter->second->tx_hash).c_str());
        }

        auto prio_iter = prio_map_.find(iter->second->prio_key);
        if (prio_iter != prio_map_.end()) {
            prio_map_.erase(prio_iter);
        }

        auto giter = gid_map_.find(iter->second->gid);
        if (giter != gid_map_.end()) {
            gid_map_.erase(giter);
        }
    }

    ZJC_DEBUG("tx over %u, map: %u, prio_map: %u, gid map: %u",
        txs.size(), added_tx_map_.size(), prio_map_.size(), gid_map_.size());
}

}  // namespace pools

}  // namespace zjchain
