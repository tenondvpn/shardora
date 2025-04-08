#pragma once

#include <deque>

#include "block/block_manager.h"
#include "consensus/zbft/waiting_txs.h"
#include "timeblock/time_block_manager.h"

namespace shardora {

namespace consensus {

class Zbft;
class WaitingTxsPools {
public:
    WaitingTxsPools(
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& timeblock_mgr);
    ~WaitingTxsPools();
    uint64_t latest_height(uint32_t pool_index) const {
        return pool_mgr_->latest_height(pool_index);
    }

    std::string latest_hash(uint32_t pool_index) const {
        return pool_mgr_->latest_hash(pool_index);
    }

    uint64_t latest_timestamp(uint32_t pool_index) const {
        return pool_mgr_->latest_timestamp(pool_index);
    }

    void GetTxSyncToLeader(
            uint32_t leader_idx, 
            uint32_t pool_index,
            uint32_t count,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
            pools::CheckAddrNonceValidFunction tx_valid_func) {
        pool_mgr_->GetTxSyncToLeader(leader_idx, pool_index, count, txs, tx_valid_func);
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxsIdempotently(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func);
    std::shared_ptr<WaitingTxsItem> GetToTxs(uint32_t pool_index, const std::string& tx_hash);
    std::shared_ptr<WaitingTxsItem> GetStatisticTx(uint32_t pool_index, const std::string& tx_hash);
    std::shared_ptr<WaitingTxsItem> GetTimeblockTx(uint32_t pool_index, bool leader);
    std::shared_ptr<WaitingTxsItem> GetElectTx(uint32_t pool_index, const std::string& tx_hash);
    bool HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func);

private:
    std::shared_ptr<WaitingTxsItem> GetSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func);

    WaitingTxs wtxs[common::kInvalidPoolIndex];
    std::shared_ptr<pools::TxPoolManager> pool_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> timeblock_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(WaitingTxsPools);
};


};  // namespace consensus

};  // namespace shardora
