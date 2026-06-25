#pragma once

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <functional>

#ifdef SHARDORA_UNITTEST
#include "common/node_members.h"
#endif

#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/unique_map.h"
#include "network/network_utils.h"
#include "pools/account_qps_lru_map.h"
#include "pools/cross_block_manager.h"
#include "pools/cross_pool.h"
#include "pools/root_cross_pool.h"
#include "pools/to_confirm_latency_tracker.h"
#include "pools/tx_pool.h"
#include "protos/address.pb.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "sync/key_value_sync.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace shardora {


namespace block {
    class AccountManager;
};

namespace consensus {
    class HotstuffManager;
}

namespace pools {
    
class TxPoolManager {
public:
    TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager>& acc_mgr,
        std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr);
    ~TxPoolManager();
    void TxPoolHandleMessage(const transport::MessagePtr& msg);
    void GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index,
        uint32_t count,
        std::vector<pools::TxItemPtr>& res_map,
        pools::CheckAddrNonceValidFunction tx_valid_func);
    void GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t pool_index,
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func,
        const std::unordered_map<std::string, uint64_t>& leader_nonce_map);
    void InitCrossPools();
    void BftCheckInvalidGids(
        uint32_t pool_index, 
        std::vector<std::shared_ptr<InvalidGidItem>>& items);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    int BackupConsensusAddTxs(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index, 
        const pools::TxItemPtr& valid_tx);
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& address);
    void PoolTimerMessage();
    void OnTxPoolAddTx(
        int32_t step,
        const std::string& to,
        const std::string& tx_value = "");
    void OnCrossShardToStart(const std::string& des, uint64_t start_timestamp_us);

    uint64_t ToConfirmAvgLatencyUs() const {
        return to_confirm_latency_tracker_.avg_latency_us();
    }

    // Callback invoked whenever a tx reaches a terminal status
    // (anything other than kMessageHandle / kTxAccept).
    using TxStatusCallback = std::function<void(const std::string&, transport::MessageHandleStatus)>;
    void SetTxStatusCallback(TxStatusCallback cb) { tx_status_cb_ = std::move(cb); }
    const TxStatusCallback& GetTxStatusCallback() const { return tx_status_cb_; }

#ifdef SHARDORA_UNITTEST
    // pools_test: mock HotstuffManager::is_other_leader without linking consensus.
    static void SetIsOtherLeaderHookForTest(std::function<common::BftMemberPtr(uint32_t pool_index)> fn);
    static void ClearIsOtherLeaderHookForTest();
#endif

    bool NewTxValid(uint32_t pool_index, const std::string& addr, uint64_t nonce) {
        return tx_pool_[pool_index].NewTxValid(addr, nonce);
    }

    void TxOver(uint32_t pool_index, view_block::protobuf::ViewBlockItem& view_block) {
        tx_pool_[pool_index].TxOver(view_block);
    }

    uint32_t all_tx_size(uint32_t pool_index) const {
        return tx_pool_[pool_index].all_tx_size();
    }

    void OnNewCrossBlock(
            const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block) {
        auto* block_item = &view_block->block_info();
        SHARDORA_DEBUG("new cross block coming net: %u, pool: %u, height: %lu",
            view_block->qc().network_id(), view_block->qc().pool_index(), block_item->height());
        if (view_block->qc().network_id() == network::kRootCongressNetworkId) {
            root_cross_pools_[view_block->qc().pool_index()].UpdateLatestInfo(block_item->height());
            SHARDORA_DEBUG("root cross succcess update cross block latest info net: %u, pool: %u, height: %lu",
                view_block->qc().network_id(), view_block->qc().pool_index(), block_item->height());
            return;
        }

        if (view_block->qc().network_id() >= network::kConsensusShardEndNetworkId ||
                view_block->qc().pool_index() != common::kGlobalPoolIndex) {
            return;
        }

        cross_pools_[view_block->qc().network_id()].UpdateLatestInfo(block_item->height());
        SHARDORA_DEBUG("succcess update cross block latest info net: %u, pool: %u, height: %lu",
            view_block->qc().network_id(), view_block->qc().pool_index(), block_item->height());
    }

    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, const common::MembersPtr& members) {
        if (sharding_id == common::GlobalInfo::Instance()->network_id() ||
                sharding_id + network::kConsensusWaitingShardOffset ==
                common::GlobalInfo::Instance()->network_id()) {
            if (latest_elect_height_ < elect_height) {
                latest_elect_height_ = elect_height;
                latest_leader_count_ = 0;
                member_index_ = common::kInvalidUint32;
                for (uint32_t i = 0; i < members->size(); ++i) {
                    if ((*members)[i]->pool_index_mod_num >= 0) {
                        ++latest_leader_count_;
                    }

                    if ((*members)[i]->id == security_->GetAddress()) {
                        member_index_ = i;
                        SHARDORA_DEBUG("local member index is: %lu", member_index_);
                    }
                }

                latest_members_ = members;
            }
        }

        SHARDORA_DEBUG("succcess set elect max sharding id: %u", sharding_id);
        if (sharding_id > now_max_sharding_id_) {
            now_max_sharding_id_ = sharding_id;
        }
        
        cross_block_mgr_->UpdateMaxShardingId(sharding_id);
    }

    void RegisterCreateTxFunction(uint32_t type, CreateConsensusItemFunction func) {
        //assert(type < pools::protobuf::StepType_ARRAYSIZE);
        item_functions_[type] = func;
    }

    TxItemPtr CreateTxPtr(transport::MessagePtr& msg_ptr) {
        return item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    }

    uint64_t latest_height(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_height();
    }

    uint64_t root_latest_height(uint32_t pool_index) const {
        return root_cross_pools_[pool_index].latest_height();
    }

    uint64_t cross_latest_height(uint32_t network_id) const {
        if (network_id > common::GlobalInfo::Instance()->now_valid_end_shard()) {
            return common::kInvalidUint64;
        }

        return cross_pools_[network_id].latest_height();
    }
    
    std::string latest_hash(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_hash();
    }

    uint64_t latest_timestamp(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_timestamp();
    }

#ifdef SHARDORA_UNITTEST
    // just for test
    int AddTx(uint32_t pool_index, TxItemPtr& tx_ptr) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return kPoolsError;
        }

        return tx_pool_[pool_index].AddTx(tx_ptr);
    }
#endif

    void UpdateLatestInfo(
            std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
            db::DbWriteBatch& db_batch) {
        auto* block = &view_block->block_info();
        uint64_t height = block->height();
        //assert(height >= 0);
        uint32_t sharding_id = view_block->qc().network_id();
        uint32_t pool_index = view_block->qc().pool_index();
        const std::string& hash = view_block->qc().view_block_hash();
        SHARDORA_DEBUG("sharding_id: %u, pool index: %u, update height: %lu", sharding_id, pool_index, height);
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        if (height > synced_max_heights_[pool_index]) {
            synced_max_heights_[pool_index] = height;
        }

        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        pool_info.set_timestamp(block->timestamp());
        uint64_t synced_height = tx_pool_[pool_index].UpdateLatestInfo(
            height, hash, view_block->parent_hash(), block->timestamp());
        pool_info.set_synced_height(synced_height);
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

    void UpdateCrossLatestInfo(
            std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
            db::DbWriteBatch& db_batch) {
        auto* block = &view_block->block_info();
        uint32_t pool_index = view_block->qc().pool_index();
        if (view_block->qc().network_id() != network::kRootCongressNetworkId) {
            if (view_block->qc().network_id() >= network::kConsensusShardEndNetworkId ||
                    pool_index != common::kGlobalPoolIndex) {
                return;
            }

            cross_block_mgr_->UpdateMaxHeight(view_block->qc().network_id(), block->height());
        }

        if (view_block->qc().network_id() == network::kRootCongressNetworkId) {
            root_cross_pools_[pool_index].UpdateLatestInfo(block->height());
        }

        uint64_t height = block->height();
        //assert(height >= 0);
        uint32_t sharding_id = view_block->qc().network_id();
        const std::string& hash = view_block->qc().view_block_hash();
        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        pool_info.set_timestamp(block->timestamp());
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

    void AddPoolMessage(const transport::MessagePtr& msg_ptr) {
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        msg_ptr->system_message = true;
        pools_msg_queue_[thread_idx].push(msg_ptr);
    }

    bool TxKeyExists(uint32_t pool_index, const std::string& addr, uint64_t nonce, const std::string& key) {
        return tx_pool_[pool_index].TxKeyExists(addr, nonce, key);
    }

    bool PoolChainIsFull(uint32_t pool_index, uint64_t height) const {
        return tx_pool_[pool_index].PoolChainIsFull(height);
    }
    
private:
    int TmpFirewallCheckMessage(const transport::MessagePtr& msg_ptr);
    void DispatchTx(uint32_t pool_index, const transport::MessagePtr& msg_ptr);
    int32_t HandleCreateContractTx(const transport::MessagePtr& msg_ptr);
    int32_t HandleSetContractPrefund(const transport::MessagePtr& msg_ptr);
    int32_t HandleContractRefund(const transport::MessagePtr& msg_ptr);
    int32_t HandleNormalFromTx(const transport::MessagePtr& msg_ptr);
    int32_t HandleContractExcute(const transport::MessagePtr& msg_ptr);
    int32_t HandleElectTx(const transport::MessagePtr& msg_ptr);
    bool UserTxValid(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage();
    void OnConsensusTimerEnter();
    void OnConsensusTimerLeave();
    void SyncPoolsMaxHeight();
    void HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr);
    void SyncMinssingHeights(uint64_t now_tm_ms);
    void SyncMinssingRootHeights(uint64_t now_tm_ms);
    void SyncBlockWithMaxHeights(uint32_t pool_idx, uint64_t height);
    void SyncRootBlockWithMaxHeights(uint32_t pool_idx, uint64_t height);
    void SyncCrossPool();
    void FlushHeightTree();
    void HandlePoolsMessage(const transport::MessagePtr& msg_ptr);
    bool ShouldRecordToConfirmStart(const std::string& to) const;
    void GetMinValidTxCount();
    void SendTxToOtherNodes(const transport::MessagePtr& msg_ptr);

    uint32_t GetTxPoolIndex(const transport::MessagePtr& msg_ptr);
    // void CreateTestTxs(uint32_t pool_begin, uint32_t pool_end, uint32_t tps);
    static const uint64_t kFlushHeightTreePeriod = 60000lu;
    static const uint64_t kSyncPoolsMaxHeightsPeriod = 3000lu;
    static const uint64_t kSyncMissingBlockPeriod = 3000lu;
    static const uint64_t kSyncCrossPeriod = 3000lu;
    static const uint64_t kGetMinPeriod = 3000lu;
    static const uint32_t kMinPoolsValidCount = common::kInvalidPoolIndex / 3;
    double kGrubbsValidFactor = 3.217;  // 90%
    const double kInvalidLeaderRatio = 0.85;

    static const uint32_t kPopMessageCountEachTime = 64000u;
    TxStatusCallback tx_status_cb_;
    TxPool* tx_pool_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::ThreadSafeQueue<transport::MessagePtr> msg_queues_[common::kInvalidPoolIndex];
    CreateConsensusItemFunction item_functions_[pools::protobuf::StepType_ARRAYSIZE] = { nullptr };
    uint32_t prev_count_[257] = { 0 };
    uint64_t prev_timestamp_us_ = 0;
    uint64_t prev_sync_check_ms_ = 0;
    uint64_t prev_sync_heights_ms_ = 0;
    uint64_t prev_sync_cross_ms_ = 0;
    uint64_t prev_check_leader_valid_ms_ = 0;
    uint64_t prev_cacultate_leader_valid_ms_ = 0;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint64_t prev_synced_pool_index_ = 0;
    uint64_t root_prev_synced_pool_index_ = 0;
    uint64_t prev_sync_height_tree_tm_ms_ = 0;
    std::atomic<uint64_t> synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    std::atomic<uint64_t> root_synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    std::atomic<uint64_t> cross_synced_max_heights_[network::kConsensusShardEndNetworkId] = { 0 };
    common::MembersPtr latest_members_;
    uint64_t latest_elect_height_ = 0;
    uint32_t latest_leader_count_ = 0;
    uint32_t member_index_ = common::kInvalidUint32;
    CrossPool* cross_pools_ = nullptr;
    RootCrossPool* root_cross_pools_ = nullptr;
    uint32_t now_max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    uint32_t prev_cross_sync_index_ = 0;
    std::shared_ptr<CrossBlockManager> cross_block_mgr_ = nullptr;
    std::atomic<uint32_t> consensus_timer_in_flight_{ 0 };
    std::mutex consensus_timer_shutdown_mutex_;
    std::condition_variable consensus_timer_cv_;
    common::Tick tools_tick_;
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> pools_msg_queue_[common::kMaxThreadCount];
    uint64_t prev_elect_height_ = common::kInvalidUint64;
    std::atomic<bool> destroy_ = false;
    common::ThreadSafeQueue<std::shared_ptr<InvalidGidItem>> invalid_gid_queues_[common::kInvalidPoolIndex];
    ToConfirmLatencyTracker to_confirm_latency_tracker_;
    uint32_t min_valid_tx_count_ = 1;
    uint64_t min_valid_timestamp_ = 0;
    uint64_t min_timestamp_ = common::kInvalidUint64;
    uint64_t prev_get_valid_tm_ms_ = 0;
    uint64_t prev_show_tm_ms_ = 0;
    uint64_t prev_msgs_show_tm_ms_ = 0;
    std::weak_ptr<block::AccountManager> acc_mgr_;
    std::atomic<uint32_t> now_max_tx_count_ = 0;
    AccountQpsLruMap<102400> account_tx_qps_check_;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_;
#ifdef USE_SERVER_TEST_TRANSACTION
    std::shared_ptr<std::thread> test_tx_thread_ = nullptr;
#endif

    // tps received
    uint64_t prev_tps_count_ = 0;

    // tps add tps
    uint64_t add_prev_tps_time_ms_  = 0;
    uint64_t add_prev_tps_count_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace shardora
