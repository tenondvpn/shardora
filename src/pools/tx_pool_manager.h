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

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(
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
    void TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs);
    void PopTxs(uint32_t pool_index);
    void SetTimeout(uint32_t pool_index) {}

    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, const common::MembersPtr& members) {
        if (sharding_id == common::GlobalInfo::Instance()->network_id() ||
                sharding_id + network::kConsensusWaitingShardOffset ==
                common::GlobalInfo::Instance()->network_id()) {
            if (latest_elect_height_ > elect_height) {
                latest_elect_height_ = elect_height;
                latest_leader_count_ = 0;
                for (uint32_t i = 0; i < members->size(); ++i) {
                    if ((*members)[i]->pool_index_mod_num >= 0) {
                        ++latest_leader_count_;
                    }
                }
            }
        }
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            uint32_t pool_index,
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        return tx_pool_[pool_index].GetTx(tx_hash_list);
    }

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

    void UpdateLatestInfo(
            uint8_t thread_idx,
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& hash,
            db::DbWriteBatch& db_batch) {
        assert(height >= 0);
//         ZJC_DEBUG("pool index: %u, update height: %lu", pool_index, height);
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        if (height > synced_max_heights_[pool_index]) {
            synced_max_heights_[pool_index] = height;
        }

        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        uint64_t synced_height = tx_pool_[pool_index].UpdateLatestInfo(thread_idx, height, hash);
        pool_info.set_synced_height(synced_height);
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

    void CheckTimeoutTx(uint32_t pool_index) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        tx_pool_[pool_index].CheckTimeoutTx();
    }

private:
    void DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr);
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr);
    void HandleCreateContractTx(const transport::MessagePtr& msg_ptr);
    void HandleUserCallContractTx(const transport::MessagePtr& msg_ptr);
    void HandleNormalFromTx(const transport::MessagePtr& msg_ptr);
    void HandleContractExcute(const transport::MessagePtr& msg_ptr);
    void HandleElectTx(const transport::MessagePtr& msg_ptr);
    bool UserTxValid(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    void SyncPoolsMaxHeight(uint8_t thread_idx);
    void HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr);
    void SyncMinssingHeights(uint8_t thread_idx, uint64_t now_tm_ms);
    void SyncBlockWithMaxHeights(uint8_t thread_idx, uint32_t pool_idx, uint64_t height);
    void CheckLeaderValid(const std::vector<double>& factors, std::vector<int32_t>* invalid_pools);
    void BroadcastInvalidPools(
        uint8_t thread_idx,
        const std::vector<int32_t>& invalid_pools);

    static const uint32_t kPopMessageCountEachTime = 320u;
    static const uint64_t kFlushHeightTreePeriod = 60000lu;
    static const uint64_t kSyncPoolsMaxHeightsPeriod = 30000lu;
    static const uint64_t kSyncMissingBlockPeriod = 3000lu;
    static const uint64_t kCheckLeaderLofPeriod = 3000lu;
    static const uint64_t kCaculateLeaderLofPeriod = 30000lu;
    double kGrubbsValidFactor = 3.217;  // 90%
    const double kInvalidLeaderRatio = 0.85;

    TxPool* tx_pool_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::ThreadSafeQueue<transport::MessagePtr> msg_queues_[common::kInvalidPoolIndex];
    CreateConsensusItemFunction item_functions_[pools::protobuf::StepType_ARRAYSIZE] = { nullptr };
    common::UniqueMap<std::string, protos::AddressInfoPtr, 10240, 32> address_map_;
    uint32_t prev_count_[257] = { 0 };
    uint64_t prev_timestamp_us_ = 0;
    uint64_t prev_sync_check_ms_ = 0;
    uint64_t prev_sync_heights_ms_ = 0;
    uint64_t prev_check_leader_valid_ms_ = 0;
    uint64_t prev_cacultate_leader_valid_ms_ = 0;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint32_t prev_synced_pool_index_ = 0;
    uint32_t prev_sync_height_tree_tm_ms_ = 0;
    volatile uint64_t synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    uint64_t latest_elect_height_ = 0;
    uint32_t latest_leader_count_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace zjchain
