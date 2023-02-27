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
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
    assert(tx_ptr != nullptr);
    auto iter = added_tx_map_.find(tx_ptr->sgid);
    if (iter != added_tx_map_.end()) {
        return kPoolsTxAdded;
    }

    added_tx_map_.insert(std::make_pair(tx_ptr->sgid, tx_ptr));
    mem_queue_.push(tx_ptr);
    return kPoolsSuccess;
}

TxItemPtr TxPool::GetTx() {
    auto timestamp_now = common::TimeUtils::TimestampUs();
    std::vector<TxItemPtr> recover_txs;
    TxItemPtr got_tx = nullptr;
    while (!mem_queue_.empty()) {
        auto item = mem_queue_.top();
        mem_queue_.pop();
        if (CheckTimeoutTx(item, timestamp_now)) {
            continue;
        }

        if (item->time_valid <= timestamp_now) {
            got_tx = item;
            break;
        } else {
            recover_txs.push_back(item);
        }
    }

    for (auto iter = recover_txs.begin(); iter != recover_txs.end(); ++iter) {
        mem_queue_.push(*iter);
    }

    return got_tx;
}

bool TxPool::CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now) {
    if (tx_ptr->timeout > timestamp_now) {
        return false;
    }

    while (!timeout_txs_.empty()) {
        auto& item = timeout_txs_.front();
        auto miter = added_tx_map_.find(item->sgid);
        if (miter == added_tx_map_.end()) {
            timeout_txs_.pop_front();
            continue;
        }

        if (item->remove_timeout < timestamp_now) {
            if (miter != added_tx_map_.end()) {
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

TxItemPtr TxPool::GetTx(const std::string& sgid) {
    auto iter = added_tx_map_.find(sgid);
    if (iter != added_tx_map_.end()) {
        return iter->second;
    }

    return nullptr;
}

void TxPool::GetTx(
        const common::BloomFilter& bloom_filter,
        std::vector<TxItemPtr>& res_vec) {
    for (auto iter = added_tx_map_.begin(); iter != added_tx_map_.end(); ++iter) {
        if (bloom_filter.Contain(common::Hash::Hash64(iter->second->tx_hash))) {
            res_vec.push_back(iter->second);
        }
    }
}

void TxPool::TxRecover(std::vector<TxItemPtr>& txs) {
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        auto miter = added_tx_map_.find((*iter)->sgid);
        if (miter != added_tx_map_.end()) {
            mem_queue_.push(miter->second);
        }
    }
}

void TxPool::TxOver(std::vector<TxItemPtr>& txs) {
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        auto miter = added_tx_map_.find((*iter)->sgid);
        if (miter != added_tx_map_.end()) {
            added_tx_map_.erase(miter);
        }
    }
}

}  // namespace pools

}  // namespace zjchain
