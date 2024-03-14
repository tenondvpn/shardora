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
#include "pools/cross_block_manager.h"
#include "pools/cross_pool.h"
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
    class AccountManager;
};

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager>& acc_mgr,
        RotationLeaderCallback rotatition_leader_cb);
    ~TxPoolManager();
    void HandleMessage(const transport::MessagePtr& msg);
    void GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map);
    void TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void RemoveTx(uint32_t pool_index, const std::string& gid) {
        tx_pool_[pool_index].RemoveTx(gid);
    }

    void TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs);
    void PopTxs(uint32_t pool_index);
    void InitCrossPools();
    void BftCheckInvalidGids(uint32_t pool_index, std::vector<std::shared_ptr<InvalidGidItem>>& items);

    void OnNewCrossBlock(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item) {
        ZJC_DEBUG("new cross block coming net: %u, pool: %u, height: %lu",
            block_item->network_id(), block_item->pool_index(), block_item->height());
        if (block_item->pool_index() != common::kRootChainPoolIndex) {
            return;
        }

        if (cross_pools_ == nullptr) {
            return;
        }

        uint32_t index = 0;
        if (max_cross_pools_size_ > 1 && block_item->network_id() >= network::kConsensusShardBeginNetworkId) {
            index = block_item->network_id() - network::kConsensusShardBeginNetworkId;
        } else {
            if (block_item->network_id() != network::kRootCongressNetworkId) {
                return;
            }
        }

        if (index >= max_cross_pools_size_) {
            assert(false);
            return;
        }

        cross_pools_[index].UpdateLatestInfo(thread_idx, block_item->height());
        ZJC_DEBUG("succcess update cross block latest info net: %u, pool: %u, height: %lu",
            block_item->network_id(), block_item->pool_index(), block_item->height());
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

        if (sharding_id - network::kRootCongressNetworkId > now_sharding_count_) {
            now_sharding_count_ = sharding_id - network::kRootCongressNetworkId;
        }

        cross_block_mgr_->UpdateMaxShardingId(sharding_id);
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            uint32_t pool_index,
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
            std::vector<uint8_t>* invalid_txs) {
        return tx_pool_[pool_index].GetTx(tx_hash_list, invalid_txs);
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

    // UpdateLatestInfo 当某个 pool 出块后，更新此 shard 的 pool_mgr 状态
    void UpdateLatestInfo(
            uint8_t thread_idx,
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& hash,
            const std::string& prehash,
            db::DbWriteBatch& db_batch) {
        assert(height >= 0);
        ZJC_DEBUG("sharding_id: %u, pool index: %u, update height: %lu", sharding_id, pool_index, height);
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        // 更新 pool_mgr 全局状态
        if (height > synced_max_heights_[pool_index]) {
            synced_max_heights_[pool_index] = height;
        }

        // 更新对应 pool 的最新状态，主要是高度信息和哈希值
        uint64_t synced_height = tx_pool_[pool_index].UpdateLatestInfo(thread_idx, height, hash, prehash);
        
        // 获取更新后的 poollatestinfo(不能直接吧 height 和 hash 当作 latest 因为同步的时候有可能是乱序的)
        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(tx_pool_[pool_index].latest_height());
        pool_info.set_hash(tx_pool_[pool_index].latest_hash());        
        // 更新对应 pool 的最新状态，主要是高度信息和哈希值
        
        pool_info.set_synced_height(synced_height);
        prefix_db_->SaveLatestPoolInfo(sharding_id, pool_index, pool_info, db_batch);
    }

    void CheckTimeoutTx(uint32_t pool_index) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        tx_pool_[pool_index].CheckTimeoutTx();
    }

    void AddChangeLeaderInvalidHash(uint32_t pool_index, uint64_t height, const std::string& hash) {
        tx_pool_[pool_index].AddChangeLeaderInvalidHash(height, hash);
    }

    void GetHeightInvalidChangeLeaderHashs(
            uint32_t pool_index,
            uint64_t height,
            std::vector<std::string>& hashs) {
        tx_pool_[pool_index].GetHeightInvalidChangeLeaderHashs(height, hashs);
    }

    bool is_next_block_checked(uint32_t pool_index, uint64_t height, const std::string& hash) {
        return tx_pool_[pool_index].is_next_block_checked(height, hash);
    }

private:
    void DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr);
    void HandleCreateContractTx(const transport::MessagePtr& msg_ptr);
    void HandleSetContractPrepayment(const transport::MessagePtr& msg_ptr);
    void HandleNormalFromTx(const transport::MessagePtr& msg_ptr);
    void HandleContractExcute(const transport::MessagePtr& msg_ptr);
    void HandleElectTx(const transport::MessagePtr& msg_ptr);
    bool UserTxValid(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(uint8_t thread_idx);
    void SyncPoolsMaxHeight(uint8_t thread_idx);
    void HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr);
    void SyncMinssingHeights(uint8_t thread_idx, uint64_t now_tm_ms);
    void SyncBlockWithMaxHeights(uint8_t thread_idx, uint32_t pool_idx, uint64_t height);
    void CheckLeaderValid(const std::vector<double>& factors, std::vector<int32_t>* invalid_pools);
    bool SaveNodeVerfiyVec(
        const std::string& id,
        const bls::protobuf::JoinElectInfo& join_info,
        std::string* new_hash);
    void SyncCrossPool(uint8_t thread_idx);
    void FlushHeightTree();
    void PopPoolsMessage(uint8_t thread_idx);
    void HandlePoolsMessage(const transport::MessagePtr& msg_ptr);
    void HandleInvalidGids(const transport::MessagePtr& msg_ptr);
    void GetMinValidTxCount();

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
    uint64_t prev_sync_height_tree_tm_ms_ = 0;
    volatile uint64_t synced_max_heights_[common::kInvalidPoolIndex] = { 0 };
    volatile uint64_t cross_synced_max_heights_[network::kConsensusWaitingShardOffset] = { 0 };
    common::MembersPtr latest_members_;
    uint64_t latest_elect_height_ = 0;
    uint32_t latest_leader_count_ = 0;
    uint32_t member_index_ = common::kInvalidUint32;
    CrossPool* cross_pools_ = nullptr;
    uint32_t max_cross_pools_size_ = 1;
    uint32_t now_sharding_count_ = 1;
    uint32_t prev_cross_sync_index_ = 0;
    std::shared_ptr<CrossBlockManager> cross_block_mgr_ = nullptr;
    common::Tick tick_;
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> pools_msg_queue_[common::kMaxThreadCount];
    RotationLeaderCallback rotatition_leader_cb_ = nullptr;
    std::deque<std::shared_ptr<std::vector<std::pair<uint32_t, uint32_t>>>> invalid_pools_;
    uint64_t prev_elect_height_ = common::kInvalidUint64;
    std::shared_ptr<std::thread> pop_message_thread_ = nullptr;
    std::condition_variable pop_tx_con_;
    std::mutex pop_tx_mu_;
    volatile bool destroy_ = false;
    std::unordered_map<std::string, std::shared_ptr<InvalidGidItem>> invalid_gids_;
    common::ThreadSafeQueue<std::shared_ptr<InvalidGidItem>> invalid_gid_queues_[common::kInvalidPoolIndex];
    uint32_t min_valid_tx_count_ = 1;
    uint64_t min_valid_timestamp_ = 0;
    uint64_t min_timestamp_ = common::kInvalidUint64;
    uint64_t prev_get_valid_tm_ms_ = 0;
    uint64_t prev_show_tm_ms_ = 0;
    uint64_t prev_msgs_show_tm_ms_ = 0;
    std::weak_ptr<block::AccountManager> acc_mgr_;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace zjchain
