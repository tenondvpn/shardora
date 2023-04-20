#pragma once

#include <deque>

#include "block/block_manager.h"
#include "consensus/zbft/waiting_txs.h"
#include "timeblock/time_block_manager.h"

namespace zjchain {

namespace consensus {

class Zbft;
class WaitingTxsPools {
public:
    WaitingTxsPools(
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& timeblock_mgr);
    ~WaitingTxsPools();
    void TxOver(std::shared_ptr<Zbft>& zbft_ptr) {}
    void TxRecover(std::shared_ptr<Zbft>& zbft_ptr);
    void UpdateLatestInfo(
            uint8_t thread_idx,
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& hash,
            db::DbWriteBatch& db_batch) {
        assert(height > 0);
        pool_mgr_->UpdateLatestInfo(thread_idx, sharding_id, pool_index, height, hash, db_batch);
    }

    uint64_t latest_height(uint32_t pool_index) const;
    std::string latest_hash(uint32_t pool_index) const;
    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(uint32_t pool_index);
    std::shared_ptr<WaitingTxsItem> GetToTxs(uint32_t pool_index, bool leader);
    std::shared_ptr<WaitingTxsItem> GetStatisticTx(uint32_t pool_index, bool leader);
    std::shared_ptr<WaitingTxsItem> GetTimeblockTx(uint32_t pool_index, bool leader);
    std::shared_ptr<WaitingTxsItem> GetElectTx(uint32_t pool_index, bool leader);
    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const common::BloomFilter& bloom_filter,
        uint8_t thread_idx);
    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
        uint8_t thread_idx);

private:
    std::shared_ptr<WaitingTxsItem> GetSingleTx(uint32_t pool_index);

    WaitingTxs wtxs[common::kInvalidPoolIndex];
    std::shared_ptr<pools::TxPoolManager> pool_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> timeblock_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(WaitingTxsPools);
};


};  // namespace consensus

};  // namespace zjchain