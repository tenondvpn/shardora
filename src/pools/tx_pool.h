#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>
#include <queue>

#include "common/bloom_filter.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/spin_mutex.h"
#include "common/time_utils.h"
#include "common/limit_hash_set.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"
#include "pools/height_tree_level.h"
#include "sync/key_value_sync.h"

#ifndef NDEBUG
#define CheckThreadIdValid()
// #define CheckThreadIdValid() { \
//     auto now_thread_id = std::this_thread::get_id(); \
//      \
//     if (local_thread_id_count_ >= 1) { \
//         assert(local_thread_id_ == now_thread_id); \
//     } else { \
//         local_thread_id_ = now_thread_id; \
//     } \
//     if (local_thread_id_ != now_thread_id) { ++local_thread_id_count_; } \
// }
#else
#define CheckThreadIdValid()
#endif


namespace shardora {

namespace pools {

class TxPoolManager;
struct TxItemPriOper {
    bool operator() (TxItemPtr& a, TxItemPtr& b) {
        return a->tx_info->gas_price() < b->tx_info->gas_price();
    }
};

class TxPool {
public:
    TxPool();
    ~TxPool();
    void Init(
        TxPoolManager* pools_mgr,
        std::shared_ptr<security::Security> security,
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync);
    int AddTx(TxItemPtr& tx_ptr);
    void GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::map<std::string, TxItemPtr>& res_map, 
        uint32_t count, 
        pools::CheckGidValidFunction gid_vlid_func);
    void GetTxSyncToLeader(
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckGidValidFunction gid_vlid_func);
    void GetTxByIds(
            const std::vector<std::string>& gids,
            std::map<std::string, TxItemPtr>& res_map);    
    void TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
    void CheckTimeoutTx();
    uint32_t SyncMissingBlocks(uint64_t now_tm_ms);
    void RemoveTx(const std::string& gid);
    void ConsensusAddTxs(const pools::TxItemPtr& tx);
    void GetHeightInvalidChangeLeaderHashs(uint64_t height, std::vector<std::string>&hashs);
    void AddChangeLeaderInvalidHash(uint64_t height, const std::string& hash);
    void SaveTempBftInvalidHashs(uint64_t height, const std::set<std::string>& hashs);
    void InitGetTempBftInvalidHashs();
    uint64_t UpdateLatestInfo(
            uint64_t height,
            const std::string& hash,
            const std::string& prehash,
            const uint64_t timestamp);
    bool is_next_block_checked(uint64_t height, const std::string& hash);
    void SyncBlock();
    double CheckLeaderValid(bool get_factor, uint32_t* finished_count, uint32_t* tx_count);
    void RecoverTx(const std::string& gid);
    bool GidValid(const std::string& gid);

    uint32_t all_tx_size() const {
        return added_txs_.size();
        // return gid_map_.size();
    }

    uint32_t tx_size() const {        
        return added_txs_.size();
        // return prio_map_.size() + consensus_tx_map_.size() + universal_prio_map_.size();
    }

    uint64_t oldest_timestamp() const {
        return oldest_timestamp_;
    }

    void FlushHeightTree(db::DbWriteBatch& db_batch) {
        CheckThreadIdValid();
        // TODO: fix bug
        if (height_tree_ptr_ != nullptr) {
            auto tmp_tree_ptr = height_tree_ptr_;
            tmp_tree_ptr->FlushToDb(db_batch);
            // 清理内存
            height_tree_ptr_ = nullptr;
        }
    }

    uint64_t latest_height() {
        if (latest_height_ == common::kInvalidUint64) {
            InitLatestInfo();
        }

//         assert(latest_height_ != common::kInvalidUint64);
        return latest_height_;
    }

    std::string& latest_hash() {
        if (latest_hash_.empty()) {
            InitLatestInfo();
        }

//         assert(!latest_hash_.empty());
        return latest_hash_;
    }
    uint64_t latest_timestamp() {
        if (latest_timestamp_ == 0) {
            InitLatestInfo();
        }
        
        return latest_timestamp_;
    }

private:
    bool CheckJoinElectTxInfo(pools::protobuf::TxMessage& tx_msg);
    bool SaveNodeVerfiyVec(
            const std::string& id,
            const bls::protobuf::JoinElectInfo& join_info,
            std::string* new_hash);
    void GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckGidValidFunction gid_vlid_func);    
    void GetTxByHash(
        std::map<std::string, TxItemPtr>& src_prio_map,
        const std::string& hash,
        pools::TxItemPtr& tx);
    void InitHeightTree();
    void InitLatestInfo();
    void UpdateSyncedHeight();

    static const uint64_t kSyncBlockPeriodMs = 3000lu;

    std::unordered_map<std::string, TxItemPtr> gid_map_;
    std::unordered_map<std::string, uint64_t> gid_start_time_map_;
    std::vector<uint64_t> latencys_us_;
    std::queue<std::string> timeout_txs_;
    std::queue<std::string> timeout_remove_txs_;
    std::map<std::string, TxItemPtr> prio_map_;
    std::map<std::string, TxItemPtr> universal_prio_map_;
    uint64_t latest_height_ = common::kInvalidUint64;
    std::string latest_hash_;
    uint64_t latest_timestamp_ = 0U;
    std::unordered_map<uint64_t, std::set<std::string>> change_leader_invalid_hashs_;
    std::shared_ptr<HeightTreeLevel> height_tree_ptr_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint64_t synced_height_ = 0;
    uint64_t prev_synced_height_ = 0;
    uint64_t to_sync_max_height_ = 0;
    uint64_t prev_synced_time_ms_ = 0;
    std::shared_ptr<db::Db> db_ = nullptr;
    uint32_t all_finish_tx_count_ = 0;
    uint32_t all_tx_count_ = 0;
    uint32_t checked_count_ = 0;
    volatile uint32_t finish_tx_count_ = 0;
    std::map<uint64_t, std::string> checked_height_with_prehash_;
    volatile uint64_t oldest_timestamp_ = 0;
    uint64_t prev_tx_count_tm_us_ = 0;
    std::map<std::string, TxItemPtr> consensus_tx_map_;
    TxPoolManager* pools_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    uint64_t prev_check_tx_timeout_tm_ = 0;
    std::thread::id local_thread_id_;
    uint64_t local_thread_id_count_ = 0;
    common::ThreadSafeQueue<TxItemPtr, 1024 * 256> added_txs_;
    common::ThreadSafeQueue<TxItemPtr, 1024 * 256> consensys_added_txs_;

    // TODO: check it
    common::SpinMutex tx_pool_mutex_;

// TODO: just test
    std::unordered_set<std::string> added_gids_;
    db::DbWriteBatch added_gids_batch_;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace shardora
