#pragma once

#include <atomic>
#include <limits>
#include <mutex>
#include <queue>

#include "block/account_manager.h"
#include "block/account_lru_map.h"
#include "common/lru_map.h"
#include "common/time_utils.h"
#include "consensus/hotstuff/types.h"
#include "consensus/hotstuff/hotstuff_utils.h"
#include "consensus/hotstuff/storage_lru_map.h"
#include "protos/prefix_db.h"
#include "shardoravm/shardora_host.h"

namespace shardora {

namespace pools {
    class TxPoolManager;
}
namespace hotstuff {

class IBlockAcceptor;

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:    
    ViewBlockChain();
    ~ViewBlockChain();
    void Init(
        ChainType chain_type,
        uint32_t pool_index, 
        std::shared_ptr<db::Db> db, 
        std::shared_ptr<block::BlockManager> block_mgr,
        std::shared_ptr<block::AccountManager> account_mgr, 
        std::shared_ptr<sync::KeyValueSync> kv_sync,
        std::shared_ptr<IBlockAcceptor> block_acceptor,
        std::shared_ptr<pools::TxPoolManager> pools_mgr,
        consensus::BlockCacheCallback new_block_cache_callback);
    ViewBlockChain(const ViewBlockChain&) = delete;
    ViewBlockChain& operator=(const ViewBlockChain&) = delete;
    // Add Node
    Status Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceAndNonceMapPtr balane_map_ptr,
        std::shared_ptr<shardoravm::ShardorahainHost> shardora_host_ptr,
        bool init);
    // Get Block by hash value, fetch from neighbor nodes if necessary
    std::shared_ptr<ViewBlockInfo> Get(const HashStr& hash) const;
    std::shared_ptr<ViewBlockInfo> GetViewBlockWithHash(const HashStr& hash, bool remove);
    std::shared_ptr<ViewBlock> GetViewBlockWithView(uint32_t network_id, uint64_t height);
    std::shared_ptr<ViewBlock> GetViewBlockWithHeight(uint32_t network_id, uint64_t height);
    // Drain cached_block_queue_ into cached_block_map_/LRU maps.
    // Must be called from a single thread (the sync timer thread) before
    // calling GetViewBlockWithHeight/GetViewBlockWithView, because the
    // underlying ReaderWriterQueue is SPSC and the drain touches
    // non-thread-safe maps.
    void DrainCachedBlockQueue();
    // Lightweight height lookup: cached_view_with_blocks_ + DB, no queue drain.
    std::shared_ptr<ViewBlock> GetWithHeight(uint32_t network_id, uint64_t height);
    // std::shared_ptr<ViewBlock> Get(uint64_t view);
    // If has block
    bool Has(const HashStr& hash);
    bool ReplaceWithSyncedBlock(std::shared_ptr<ViewBlock>&);
    // if in the same branch
    bool Extends(const ViewBlock& block, const ViewBlock& target);
    // prune from last prune height to target view block
    Status GetAll(std::vector<std::shared_ptr<ViewBlock>>&);
    Status GetAllVerified(std::vector<std::shared_ptr<ViewBlock>>&);
    Status GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>&);
    bool GetPrevStorageKeyValue(
            const std::string& parent_hash, 
            const std::string& id, 
            const std::string& key, 
            std::string* val);
    evmc::bytes32 GetPrevStorageBytes32KeyValue(
            const std::string& parent_hash, 
            const evmc::address& addr,
            const evmc::bytes32& key);
    void MergeAllPrevBalanceMap(
            const std::string& parent_hash, 
            BalanceAndNonceMap& acc_balance_map);
    int CheckTxNonceValid(
        const std::string& addr, 
        uint64_t nonce, 
        const std::string& parent_hash,
        uint64_t* now_nonce,
        const BalanceAndNonceMap* merged_balance_map = nullptr);
    // If a chain is valid
    bool IsValid();
    std::string String() const;
    void UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item);
    void RecoverHighViewBlock();
    // void SaveBlockCheckedParentHash(const std::string& hash, uint64_t view);
    protos::AddressInfoPtr ChainGetAccountInfo(const std::string& acc_id);
    void Commit(const std::shared_ptr<ViewBlockInfo>& queue_item_ptr);
    void CommitSynced(std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random,
        uint64_t timeblock_addr_nonce);
    void HandleTimerMessage();
    std::shared_ptr<ViewBlockInfo> CheckCommit(const QC& qc);
    
    uint64_t GetMaxHeight() {
        auto latest_committed_block = LatestCommittedBlock();
        if (latest_committed_block && latest_committed_block->has_block_info()) {
            return latest_committed_block->block_info().height();
        }

        return 0;
    }

    inline void Clear() {
        view_blocks_info_.clear();
        start_block_ = nullptr;        
    }

    inline uint32_t Size() const {
        return view_blocks_info_.size();
    }

    std::shared_ptr<ViewBlock> ParentBlock(const ViewBlock& view_block) {
        if (view_block.has_parent_hash()) {
            auto it2 = view_blocks_info_.find(view_block.parent_hash());
            if (it2 == view_blocks_info_.end()) {
                return nullptr;
            }

            return it2->second->view_block;
        }

        return nullptr;
    }

    inline std::shared_ptr<ViewBlock> HighViewBlock() const {
        return high_view_block_;
    }

    inline const QC& HighQC() const {
        return high_view_block_->qc();
    }

    inline View HighView() const {
        return high_view_block_view_;
    }

    inline std::shared_ptr<ViewBlock> LatestCommittedBlock() const {
        std::lock_guard<std::mutex> lock(latest_committed_block_mutex_);
        return latest_committed_block_;
    }
    // Set the latest committed block
    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        auto view_block = view_block_info->view_block;
        auto latest_committed_block = LatestCommittedBlock();
        if (latest_committed_block &&
                (view_block->qc().network_id() !=
                latest_committed_block->qc().network_id() ||
                latest_committed_block->qc().view() >= 
                view_block->qc().view())) {
            return;
        }

        // Allow setting old view blocks
        SHARDORA_DEBUG("changed latest commited block %u_%u_%lu, new view: %lu, sign x: %s",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height(),
            view_block->qc().view(),
            common::Encode::HexEncode(view_block->qc().sign_x()).c_str());
        //assert(!view_block->qc().sign_x().empty());
        {
            std::lock_guard<std::mutex> lock(latest_committed_block_mutex_);
            latest_committed_block_ = view_block;
        }
    }

    inline ViewBlockStatus GetViewBlockStatus(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
        if (it == view_blocks_info_.end()) {
            return ViewBlockStatus::Unknown;
        }
        return it->second->status;        
    } 

    uint32_t pool_index() const {
        return pool_index_;
    }

    bool ChainIsFull() const {
        if (!IsQcTcValid(high_view_block_->qc())) {
            SHARDORA_DEBUG("pool: %d, check pool chain is full failed, invalid qc: %s", 
                pool_index_, ProtobufToJson(high_view_block_->qc()).c_str());
            return false;
        }

        if (!high_view_block_->has_block_info()) {
            // High view block may lack block_info if it was created from a TC
            // (timeout). Fall back to LatestCommittedBlock for the check.
            auto latest_committed_block = LatestCommittedBlock();
            if (latest_committed_block && latest_committed_block->has_block_info()) {
                return pools_mgr_->PoolChainIsFull(
                    pool_index_, 
                    latest_committed_block->block_info().height());
            }
            SHARDORA_DEBUG("pool: %d, check pool chain is full failed, high view block has no block info, view: %lu", 
                pool_index_, high_view_block_->qc().view());
            return false;
        }

        if (high_view_block_->block_info().height() == 0) {
            return pools_mgr_->PoolChainIsFull(pool_index_, 0);
        }

        auto latest_committed_block = LatestCommittedBlock();
        if (latest_committed_block && 
                latest_committed_block->block_info().height() == high_view_block_->block_info().height()) {
            SHARDORA_DEBUG("pool: %d, check pool chain is full, latest committed block height: %lu, high view block height: %lu", 
                pool_index_, latest_committed_block->block_info().height(), high_view_block_->block_info().height());
            return pools_mgr_->PoolChainIsFull(
                pool_index_, 
                high_view_block_->block_info().height());
        }

        auto tmp_block = high_view_block_;
        while (true) {
            auto pre_block = Get(tmp_block->parent_hash());
            if (pre_block && pre_block->view_block && IsQcTcValid(pre_block->view_block->qc())) {
                tmp_block = pre_block->view_block;
                continue;
            }

            break;
        }

        if (tmp_block->block_info().height() <= 0) {
            return pools_mgr_->PoolChainIsFull(pool_index_, 0);
        }

        return pools_mgr_->PoolChainIsFull(
            pool_index_, 
            tmp_block->block_info().height() - 1);
    }

private:
    void AddPoolStatisticTag(uint64_t height, uint64_t timeblock_addr_nonce);
    void SetViewBlockToMap(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        //assert(!view_block_info->view_block->qc().view_block_hash().empty());
        auto it = view_blocks_info_.find(view_block_info->view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
            SHARDORA_DEBUG("exists, failed add view block: %s, %u_%u_%lu, height: %lu, "
                "parent hash: %s, tx size: %u, strings: %s",
                common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
                view_block_info->view_block->qc().network_id(),
                view_block_info->view_block->qc().pool_index(),
                view_block_info->view_block->qc().view(),
                view_block_info->view_block->block_info().height(),
                common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
                view_block_info->view_block->block_info().tx_list_size(),
                String().c_str());
            return;
        }
        
        view_blocks_info_[view_block_info->view_block->qc().view_block_hash()] = view_block_info;
        SHARDORA_DEBUG("store now add view block: %s, %u_%u_%lu, height: %lu, "
            "parent hash: %s, tx size: %u, strings: %s",
            common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
            view_block_info->view_block->qc().network_id(),
            view_block_info->view_block->qc().pool_index(),
            view_block_info->view_block->qc().view(),
            view_block_info->view_block->block_info().height(),
            common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
            view_block_info->view_block->block_info().tx_list_size(),
            String().c_str());
        // //assert(view_with_blocks_.find(view_block_info->view_block->qc().view()) == view_with_blocks_.end());
        view_with_blocks_[view_block_info->view_block->qc().view()] = view_block_info;
        SHARDORA_DEBUG("success add view block info now size: %u", view_blocks_info_.size());
    }

    std::shared_ptr<ViewBlockInfo> GetViewBlockInfo(
            std::shared_ptr<ViewBlock> view_block, 
            BalanceAndNonceMapPtr acc_balance_map_ptr,
            std::shared_ptr<shardoravm::ShardorahainHost> shardora_host_ptr) {
        auto view_block_info_ptr = std::make_shared<ViewBlockInfo>();
        SHARDORA_DEBUG("2 success add view block remove add %u_%u_%lu", 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view());
        view_block_info_ptr->view_block = view_block;
        view_block_info_ptr->acc_balance_map_ptr = acc_balance_map_ptr;
        view_block_info_ptr->shardora_host_ptr = shardora_host_ptr;
        return view_block_info_ptr;
    }

    void AddNewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_item,
        db::DbWriteBatch& db_batch);
        
    static const uint32_t kCachedViewBlockCount = 16u;

    std::shared_ptr<ViewBlock> high_view_block_ = nullptr;
    std::atomic<View> high_view_block_view_ = 0llu;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    std::shared_ptr<ViewBlock> latest_committed_block_; // latest committed block
    mutable std::mutex latest_committed_block_mutex_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::atomic<View> stored_to_db_view_ = 0llu;
    std::atomic<View> commited_max_view_ = 0llu;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockInfo>> cached_block_queue_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> cached_block_map_;
    std::map<uint64_t, std::vector<std::shared_ptr<ViewBlockInfo>>> cached_view_with_blocks_;
    std::priority_queue<
        std::shared_ptr<ViewBlockInfo>, 
        std::vector<std::shared_ptr<ViewBlockInfo>>,
        ViewBlockInfoCmp> cached_pri_queue_;
    block::AccountLruMap<102400> account_lru_map_;
    std::shared_ptr<sync::KeyValueSync> kv_sync_;
    std::shared_ptr<IBlockAcceptor> block_acceptor_;
    consensus::BlockCacheCallback new_block_cache_callback_ = nullptr;
    StorageLruMap<10240> storage_lru_map_;

    // Per-pool storage cache for GetPrevStorageBytes32KeyValue.
    // Avoids repeated view chain traversal for the same (addr, key).
    // Invalidated when stored_to_db_view_ advances (new block committed).
    struct Bytes32StorageCache {
        std::unordered_map<std::string, evmc::bytes32> cache;  // key = addr(20) + key(32)

        evmc::bytes32* get(const evmc::address& addr, const evmc::bytes32& key) {
            std::string k((char*)addr.bytes, 20);
            k.append((char*)key.bytes, 32);
            auto it = cache.find(k);
            return (it != cache.end()) ? &it->second : nullptr;
        }

        void put(const evmc::address& addr, const evmc::bytes32& key, const evmc::bytes32& val) {
            std::string k((char*)addr.bytes, 20);
            k.append((char*)key.bytes, 32);
            cache[k] = val;
            // Limit cache size to prevent unbounded memory growth
            if (cache.size() > 131072) {
                cache.clear();
            }
        }
    };
    Bytes32StorageCache bytes32_storage_cache_;
    std::shared_ptr<block::BlockManager> block_mgr_;
    std::atomic<uint64_t> latest_timeblock_height_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::set<uint64_t> commited_view_;
    uint64_t prev_check_timeout_blocks_ms_ = 0;
    ChainType chain_type_ = kInvalidChain;
    std::map<uint64_t, std::shared_ptr<ViewBlockInfo>> view_with_blocks_;
    common::LRUMap<BlockViewKey, std::shared_ptr<ViewBlockInfo>> latest_commited_view_lru_map_{ 16 };
    common::LRUMap<std::string, std::shared_ptr<ViewBlockInfo>> latest_commited_hash_lru_map_{ 16 };
    common::LRUMap<BlockViewKey, std::shared_ptr<ViewBlockInfo>> latest_commited_height_lru_map_{ 16 };
    std::thread::id local_thread_id_;
    uint64_t local_thread_id_count_ = 0;

};

// from db
Status GetLatestViewBlockFromDb(
    uint32_t network_id,
    const std::shared_ptr<db::Db>& db,
    const uint32_t& pool_index,
    std::shared_ptr<ViewBlock>& view_block);
        
} // namespace consensus
    
} // namespace shardora
