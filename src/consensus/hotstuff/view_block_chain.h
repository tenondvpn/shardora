#pragma once

#include <limits>
#include <queue>

#include "block/account_manager.h"
#include "block/account_lru_map.h"
#include "common/time_utils.h"
#include "consensus/hotstuff/types.h"
#include "consensus/hotstuff/hotstuff_utils.h"
#include "consensus/hotstuff/storage_lru_map.h"
#include "protos/prefix_db.h"
#include "zjcvm/zjc_host.h"

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
        std::shared_ptr<db::Db>& db, 
        std::shared_ptr<block::BlockManager>& block_mgr,
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
        std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr,
        bool init);
    // Get Block by hash value, fetch from neighbor nodes if necessary
    std::shared_ptr<ViewBlockInfo> Get(const HashStr& hash);
    std::shared_ptr<ViewBlock> GetViewBlockWithHash(const HashStr& hash);
    std::shared_ptr<ViewBlock> GetViewBlockWithHeight(uint32_t network_id, uint64_t height);
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
        const std::string& parent_hash);
    // If a chain is valid
    bool IsValid();
    std::string String() const;
    void UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item);
    bool ViewBlockIsCheckedParentHash(const std::string& hash);
    // void SaveBlockCheckedParentHash(const std::string& hash, uint64_t view);
    protos::AddressInfoPtr ChainGetAccountInfo(const std::string& acc_id);
    protos::AddressInfoPtr ChainGetPoolAccountInfo(uint32_t pool_index);
    void Commit(const std::shared_ptr<ViewBlockInfo>& queue_item_ptr);
    void CommitSynced(std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    bool view_commited(uint32_t network_id, View view) const {
        if (network_id == common::GlobalInfo::Instance()->network_id()) {
            if (commited_view_.find(view) != commited_view_.end()) {
                return true;
            }
        }

        if (prefix_db_->ViewBlockIsValidView(network_id, pool_index_, view)) {
            return true;
        }

        return false;
    }

    uint64_t GetMaxHeight() {
        if (latest_committed_block_->has_block_info()) {
            return latest_committed_block_->block_info().height();
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

    void ResetViewBlock(const HashStr& hash) {
        auto it = view_blocks_info_.find(hash);
        if (it != view_blocks_info_.end() && 
                it->second->view_block != nullptr && 
                it->second->view_block->qc().sign_x().empty()) {
            it->second->view_block = nullptr;
        }
    }

    
    inline std::shared_ptr<ViewBlock> LatestCommittedBlock() const {
        return latest_committed_block_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        auto view_block = view_block_info->view_block;
        if (latest_committed_block_ &&
                (view_block->qc().network_id() !=
                latest_committed_block_->qc().network_id() ||
                latest_committed_block_->qc().view() >= 
                view_block->qc().view())) {
            return;
        }

        // 允许设置旧的 view block
        SHARDORA_DEBUG("changed latest commited block %u_%u_%lu, new view: %lu, sign x: %s",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height(),
            view_block->qc().view(),
            common::Encode::HexEncode(view_block->qc().sign_x()).c_str());
        assert(!view_block->qc().sign_x().empty());
        latest_committed_block_ = view_block;
        commited_block_queue_.push(view_block_info);
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

#ifdef TEST_FORKING_ATTACK
    std::shared_ptr<ViewBlockInfo> GetViewBlockVithView(uint64_t view) {
        auto iter = view_with_blocks_.find(view);
        if (iter == view_with_blocks_.end()) {
            return nullptr;
        }

        auto rand_idx = rand() % iter->second.size();
        return iter->second[rand_idx];
    }
#endif

private:
    void AddPoolStatisticTag(uint64_t height);
    void SetViewBlockToMap(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        assert(!view_block_info->view_block->qc().view_block_hash().empty());
        auto it = view_blocks_info_.find(view_block_info->view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
            auto strings = String();
            if (strings.empty()) {
                SHARDORA_DEBUG("exists, failed add view block: %s, %u_%u_%lu, height: %lu, "
                    "parent hash: %s, tx size: %u",
                    common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
                    view_block_info->view_block->qc().network_id(),
                    view_block_info->view_block->qc().pool_index(),
                    view_block_info->view_block->qc().view(),
                    view_block_info->view_block->block_info().height(),
                    common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
                    view_block_info->view_block->block_info().tx_list_size());
            } else {
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
            }

            return;
        }
        
        view_blocks_info_[view_block_info->view_block->qc().view_block_hash()] = view_block_info;
#ifdef TEST_FORKING_ATTACK
        view_with_blocks_[view_block_info->view_block->qc().view()].push_back(view_block_info);
#endif
    }

    std::shared_ptr<ViewBlockInfo> GetViewBlockInfo(
            std::shared_ptr<ViewBlock> view_block, 
            BalanceAndNonceMapPtr acc_balance_map_ptr,
            std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr) {
        auto view_block_info_ptr = std::make_shared<ViewBlockInfo>();
        view_block_info_ptr->view_block = view_block;
        view_block_info_ptr->acc_balance_map_ptr = acc_balance_map_ptr;
        view_block_info_ptr->zjc_host_ptr = zjc_host_ptr;
        return view_block_info_ptr;
    }

    void AddNewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_item,
        db::DbWriteBatch& db_batch);
        
    static const uint32_t kCachedViewBlockCount = 16u;

    std::shared_ptr<ViewBlock> high_view_block_ = nullptr;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::atomic<View> stored_to_db_view_ = 0llu;
    std::atomic<View> commited_max_view_ = 0llu;
    common::ThreadSafeQueue<View> stored_view_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockInfo>> cached_block_queue_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> cached_block_map_;
    std::priority_queue<
        std::shared_ptr<ViewBlockInfo>, 
        std::vector<std::shared_ptr<ViewBlockInfo>>,
        ViewBlockInfoCmp> cached_pri_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockInfo>> commited_block_queue_;
    std::unordered_map<uint64_t, std::shared_ptr<ViewBlockInfo>> commited_block_map_;
    std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> commited_pri_queue_;
    block::AccountLruMap<102400> account_lru_map_;
    std::shared_ptr<sync::KeyValueSync> kv_sync_;
    std::shared_ptr<IBlockAcceptor> block_acceptor_;
    consensus::BlockCacheCallback new_block_cache_callback_ = nullptr;
    StorageLruMap<10240> storage_lru_map_;
    std::shared_ptr<block::BlockManager> block_mgr_;
    std::atomic<uint64_t> latest_timeblock_height_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::set<uint64_t> commited_view_;
    uint64_t prev_check_timeout_blocks_ms_ = 0;
    ChainType chain_type_ = kInvalidChain;
#ifdef TEST_FORKING_ATTACK
    std::map<uint64_t, std::vector<std::shared_ptr<ViewBlockInfo>>> view_with_blocks_;
#endif

};

// from db
Status GetLatestViewBlockFromDb(
    uint32_t network_id,
    const std::shared_ptr<db::Db>& db,
    const uint32_t& pool_index,
    std::shared_ptr<ViewBlock>& view_block);
        
} // namespace consensus
    
} // namespace shardora


