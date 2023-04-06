#pragma once

#include <bitset>
#include <memory>

#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/unique_map.h"
#include "pools/tx_pool.h"
#include "protos/address.pb.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "sync/key_value_sync.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace block {
    class BlockManager;
}

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync);
    ~TxPoolManager();
    void HandleMessage(const transport::MessagePtr& msg);
    void GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map);
    void GetTx(
        const common::BloomFilter& bloom_filter,
        uint32_t pool_index,
        std::map<std::string, TxItemPtr>& res_map);
    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            uint32_t pool_index,
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        return tx_pool_[pool_index].GetTx(tx_hash_list);
    }

    void TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs);
    void PopTxs(uint32_t pool_index);
    void SetTimeout(uint32_t pool_index) {}
    void RegisterCreateTxFunction(uint32_t type, CreateConsensusItemFunction func) {
        assert(type < pools::protobuf::StepType_ARRAYSIZE);
        item_functions_[type] = func;
    }

    uint64_t latest_height(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_height();
    }

    std::string latest_hash(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_hash();
    }

    // just for test
    int AddTx(uint32_t pool_index, TxItemPtr& tx_ptr) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return kPoolsError;
        }

        return tx_pool_[pool_index].AddTx(tx_ptr);
    }

    void UpdateToSyncHeight(uint32_t pool_index, uint8_t thread_idx, uint64_t to_sync_max_height) {
        tx_pool_[pool_index].UpdateToSyncHeight(thread_idx, to_sync_max_height);
    }

    void CheckSync(uint8_t thread_idx) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (prev_sync_check_us_ + 100000lu >= now_tm) {
            return;
        }

        prev_sync_check_us_ = now_tm;
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            tx_pool_[i].SyncBlock(thread_idx);
        }
    }

    void UpdateLatestInfo(
            uint8_t thread_idx,
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& hash,
            db::DbWriteBatch& db_batch) {
        assert(height > 0);
//         ZJC_DEBUG("pool index: %u, update height: %lu", pool_index, height);
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        uint64_t synced_height = tx_pool_[pool_index].UpdateLatestInfo(thread_idx, height, hash);
        pool_info.set_synced_height(synced_height);
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

    void NewBlockWithTx(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        return;
    }

    void CheckTimeoutTx(uint32_t pool_index) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        tx_pool_[pool_index].CheckTimeoutTx();
    }

private:
    void SaveStorageToDb(const transport::protobuf::Header& msg);
    void DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr);
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr);
    void HandleCreateContractTx(const transport::MessagePtr& msg_ptr);
    void HandleNormalFromTx(const transport::MessagePtr& msg_ptr);
    void HandleCrossShardingToTxs(const transport::MessagePtr& msg_ptr);

    static const uint32_t kPopMessageCountEachTime = 320u;

    TxPool* tx_pool_{ nullptr };
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::ThreadSafeQueue<transport::MessagePtr> msg_queues_[common::kInvalidPoolIndex];
    CreateConsensusItemFunction item_functions_[pools::protobuf::StepType_ARRAYSIZE] = { nullptr };
    common::UniqueMap<std::string, protos::AddressInfoPtr, 10240, 32> address_map_;
    uint32_t prev_count_[257] = { 0 };
    uint64_t prev_timestamp_us_ = 0;
    uint64_t prev_sync_check_us_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace zjchain
