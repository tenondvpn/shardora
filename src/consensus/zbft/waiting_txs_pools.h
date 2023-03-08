#pragma once

#include <deque>

#include "block/block_manager.h"
#include "consensus/zbft/waiting_txs.h"

namespace zjchain {

namespace consensus {

class Zbft;
class WaitingTxsPools {
public:
    virtual void TxOver(std::shared_ptr<Zbft>& zbft_ptr) = 0;
    virtual void TxRecover(std::shared_ptr<Zbft>& zbft_ptr) = 0;
    virtual void UpdateLatestInfo(uint32_t pool_index, uint64_t height, const std::string& hash) = 0;
    virtual uint64_t latest_height(uint32_t pool_index) const = 0;
    virtual std::string latest_hash(uint32_t pool_index) const = 0;
    virtual void LockPool(std::shared_ptr<Zbft>& zbft_ptr) = 0;
    virtual std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(bool direct, uint32_t pool_index) = 0;
    virtual std::shared_ptr<WaitingTxsItem> FollowerGetToTxs(
        uint32_t pool_index,
        const std::string& tx_hash,
        uint8_t thread_idx) = 0;
    virtual std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const common::BloomFilter& bloom_filter,
        uint8_t thread_idx) = 0;
    virtual std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
        uint8_t thread_idx) = 0;

protected:
    WaitingTxsPools() {}
    virtual ~WaitingTxsPools() {}
};


};  // namespace consensus

};  // namespace zjchain