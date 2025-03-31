#pragma once

#include <bitset>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/unique_map.h"
#include "network/network_utils.h"
#include "pools/account_qps_lru_map.h"
#include "pools/cross_block_manager.h"
#include "pools/cross_pool.h"
#include "pools/root_cross_pool.h"
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

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager>& acc_mgr);
    ~TxPoolManager();
    void HandleMessage(const transport::MessagePtr& msg);
    void GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map,
        pools::CheckGidValidFunction gid_vlid_func);
    void GetTxSyncToLeader(
        uint32_t pool_index,
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckGidValidFunction gid_vlid_func);
    void PopTxs(
        uint32_t pool_index, 
        bool pop_all, 
        bool* has_user_tx, 
        bool* has_system_tx);
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

    void TxOver(uint32_t pool_index, view_block::protobuf::ViewBlockItem& view_block) {
        tx_pool_[pool_index].TxOver(view_block);
    }

    uint32_t all_tx_size(uint32_t pool_index) const {
        return tx_pool_[pool_index].all_tx_size();
    }

    uint32_t tx_size(uint32_t pool_index) const {
        return tx_pool_[pool_index].tx_size();
    }

    void OnNewCrossBlock(
            const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block) {
        auto* block_item = &view_block->block_info();
        ZJC_DEBUG("new cross block coming net: %u, pool: %u, height: %lu",
            view_block->qc().network_id(), view_block->qc().pool_index(), block_item->height());
        if (view_block->qc().network_id() == network::kRootCongressNetworkId) {
            root_cross_pools_[view_block->qc().pool_index()].UpdateLatestInfo(block_item->height());
            ZJC_DEBUG("root cross succcess update cross block latest info net: %u, pool: %u, height: %lu",
                view_block->qc().network_id(), view_block->qc().pool_index(), block_item->height());
            return;
        }

        if (view_block->qc().pool_index() != common::kRootChainPoolIndex) {
            return;
        }

        cross_pools_[view_block->qc().network_id()].UpdateLatestInfo(block_item->height());
        ZJC_DEBUG("succcess update cross block latest info net: %u, pool: %u, height: %lu",
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
                        ZJC_DEBUG("local member index is: %lu", member_index_);
                    }
                }

                latest_members_ = members;
            }
        }

        ZJC_DEBUG("succcess set elect max sharding id: %u", sharding_id);
        if (sharding_id > now_max_sharding_id_) {
            now_max_sharding_id_ = sharding_id;
        }
        
        cross_block_mgr_->UpdateMaxShardingId(sharding_id);
    }

    void RegisterCreateTxFunction(uint32_t type, CreateConsensusItemFunction func) {
        assert(type < pools::protobuf::StepType_ARRAYSIZE);
        item_functions_[type] = func;
    }

    TxItemPtr CreateTxPtr(transport::MessagePtr& msg_ptr) {
        return item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    }

    uint64_t latest_height(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_height();
    }

    std::string latest_hash(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_hash();
    }

    uint64_t latest_timestamp(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_timestamp();
    }

#ifdef ZJC_UNITTEST
    // just for test
    int AddTx(uint32_t pool_index, TxItemPtr& tx_ptr) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return kPoolsError;
        }

        return tx_pool_[pool_index].AddTx(tx_ptr);
    }
#endif

    // UpdateLatestInfo 当某个 pool 出块后，更新此 shard 的 pool_mgr 状态
    void UpdateLatestInfo(
            std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
            db::DbWriteBatch& db_batch) {
        auto* block = &view_block->block_info();
        uint64_t height = block->height();
        assert(height >= 0);
        uint32_t sharding_id = view_block->qc().network_id();
        uint32_t pool_index = view_block->qc().pool_index();
        const std::string& hash = view_block->qc().view_block_hash();
        ZJC_DEBUG("sharding_id: %u, pool index: %u, update height: %lu", sharding_id, pool_index, height);
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        // 更新 pool_mgr 全局状态
        if (height > synced_max_heights_[pool_index]) {
            synced_max_heights_[pool_index] = height;
        }

        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        pool_info.set_timestamp(block->timestamp());
        // 更新对应 pool 的最新状态，主要是高度信息和哈希值
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
        cross_block_mgr_->UpdateMaxHeight(view_block->qc().network_id(), block->height());
        uint64_t height = block->height();
        assert(height >= 0);
        uint32_t sharding_id = view_block->qc().network_id();
        const std::string& hash = view_block->qc().view_block_hash();
        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        pool_info.set_timestamp(block->timestamp());
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

private:
    void DispatchTx(uint32_t pool_index, const transport::MessagePtr& msg_ptr);
    void HandleCreateContractTx(const transport::MessagePtr& msg_ptr);
    void HandleSetContractPrepayment(const transport::MessagePtr& msg_ptr);
    void HandleNormalFromTx(const transport::MessagePtr& msg_ptr);
    void HandleContractExcute(const transport::MessagePtr& msg_ptr);
    void HandleElectTx(const transport::MessagePtr& msg_ptr);
    bool UserTxValid(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage();
    void SyncPoolsMaxHeight();
    void HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr);
    void SyncMinssingHeights(uint64_t now_tm_ms);
    void SyncMinssingRootHeights(uint64_t now_tm_ms);
    void SyncBlockWithMaxHeights(uint32_t pool_idx, uint64_t height);
    void SyncRootBlockWithMaxHeights(uint32_t pool_idx, uint64_t height);
    void SyncCrossPool();
    void FlushHeightTree();
    void HandlePoolsMessage(const transport::MessagePtr& msg_ptr);
    void GetMinValidTxCount();
    uint32_t GetTxPoolIndex(const transport::MessagePtr& msg_ptr);

    static const uint32_t kPopMessageCountEachTime = 64000u;
    static const uint64_t kFlushHeightTreePeriod = 60000lu;
    static const uint64_t kSyncPoolsMaxHeightsPeriod = 3000lu;
    static const uint64_t kSyncMissingBlockPeriod = 3000lu;
    static const uint64_t kSyncCrossPeriod = 3000lu;
    static const uint64_t kGetMinPeriod = 3000lu;
    static const uint32_t kMinPoolsValidCount = common::kInvalidPoolIndex / 3;
    double kGrubbsValidFactor = 3.217;  // 90%
    const double kInvalidLeaderRatio = 0.85;

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
    volatile uint64_t synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    volatile uint64_t root_synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    volatile uint64_t cross_synced_max_heights_[network::kConsensusShardEndNetworkId] = { 0 };
    common::MembersPtr latest_members_;
    uint64_t latest_elect_height_ = 0;
    uint32_t latest_leader_count_ = 0;
    uint32_t member_index_ = common::kInvalidUint32;
    CrossPool* cross_pools_ = nullptr;
    RootCrossPool* root_cross_pools_ = nullptr;
    uint32_t now_max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    uint32_t prev_cross_sync_index_ = 0;
    std::shared_ptr<CrossBlockManager> cross_block_mgr_ = nullptr;
    common::Tick tools_tick_;
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> pools_msg_queue_;
    uint64_t prev_elect_height_ = common::kInvalidUint64;
    volatile bool destroy_ = false;
    common::ThreadSafeQueue<std::shared_ptr<InvalidGidItem>> invalid_gid_queues_[common::kInvalidPoolIndex];
    uint32_t min_valid_tx_count_ = 1;
    uint64_t min_valid_timestamp_ = 0;
    uint64_t min_timestamp_ = common::kInvalidUint64;
    uint64_t prev_get_valid_tm_ms_ = 0;
    uint64_t prev_show_tm_ms_ = 0;
    uint64_t prev_msgs_show_tm_ms_ = 0;
    std::weak_ptr<block::AccountManager> acc_mgr_;
    volatile uint32_t now_max_tx_count_ = 0;
    AccountQpsLruMap<10240> account_tx_qps_check_;

    // tps received
    uint64_t prev_tps_count_ = 0;

    // tps add tps
    uint64_t add_prev_tps_time_ms_  = 0;
    uint64_t add_prev_tps_count_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace shardora
