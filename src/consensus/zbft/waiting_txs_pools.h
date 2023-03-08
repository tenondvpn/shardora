#pragma once

#include <deque>

#include "block/block_manager.h"
#include "consensus/zbft/waiting_txs.h"

namespace zjchain {

namespace consensus {

class Zbft;
class WaitingTxsPools {
public:
    WaitingTxsPools(
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr);
    ~WaitingTxsPools();
    void TxOver(std::shared_ptr<Zbft>& zbft_ptr);
    void TxRecover(std::shared_ptr<Zbft>& zbft_ptr);
    void UpdateLatestInfo(uint32_t pool_index, uint64_t height, const std::string& hash) {
        pool_mgr_->UpdateLatestInfo(pool_index, height, hash);
    }

    uint64_t latest_height(uint32_t pool_index) const;
    std::string latest_hash(uint32_t pool_index) const;
    void LockPool(std::shared_ptr<Zbft>& zbft_ptr);
    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(bool direct, uint32_t pool_index);
    std::shared_ptr<WaitingTxsItem> GetToTxs(uint32_t pool_index);
    std::shared_ptr<WaitingTxsItem> GetTimeblockTx();
    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const common::BloomFilter& bloom_filter,
        uint8_t thread_idx);
    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
        uint8_t thread_idx);

private:
    void FilterInvalidTx(
        uint32_t pool_index,
        std::map<std::string, pools::TxItemPtr>& txs);
    std::shared_ptr<WaitingTxsItem> GetTimeblockTx();

    WaitingTxs wtxs[common::kInvalidPoolIndex];
    std::shared_ptr<pools::TxPoolManager> pool_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::deque<std::shared_ptr<Zbft>> pipeline_pools_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(WaitingTxsPools);
};


};  // namespace consensus

};  // namespace zjchain