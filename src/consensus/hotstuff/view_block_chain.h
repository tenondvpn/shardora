#pragma once

#include <limits>
#include <queue>

#include "block/account_manager.h"
#include "common/range_set.h"
#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <protos/prefix_db.h>
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace hotstuff {

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:    
    ViewBlockChain(uint32_t pool_index, std::shared_ptr<db::Db>& db, std::shared_ptr<block::AccountManager> account_mgr);
    ~ViewBlockChain();
    
    ViewBlockChain(const ViewBlockChain&) = delete;
    ViewBlockChain& operator=(const ViewBlockChain&) = delete;
    // Add Node
    Status Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceMapPtr balane_map_ptr,
        std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr);
    // Get Block by hash value, fetch from neighbor nodes if necessary
    std::shared_ptr<ViewBlockInfo> Get(const HashStr& hash);
    std::shared_ptr<ViewBlock> GetViewBlock(const HashStr& hash);
    // std::shared_ptr<ViewBlock> Get(uint64_t view);
    // If has block
    bool Has(const HashStr& hash);
    // if in the same branch
    bool Extends(const ViewBlock& block, const ViewBlock& target);
    // prune from last prune height to target view block
    Status PruneTo(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);
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
    bool GetPrevAddressBalance(const std::string& phash, const std::string& address, int64_t* balance);
    void MergeAllPrevBalanceMap(
            const std::string& parent_hash, 
            BalanceMap& acc_balance_map);
    bool CheckTxGidValid(const std::string& gid, const std::string& parent_hash);
    // If a chain is valid
    bool IsValid();
    void Print() const;
    void PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent = "") const;
    std::string String() const;
    void UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item);
    bool ViewBlockIsCheckedParentHash(const std::string& hash);
    void SaveBlockCheckedParentHash(const std::string& hash, uint64_t view);
    
    void UpdateStoredToDbView(View view) {
        if (stored_to_db_view_ < view) {
            stored_to_db_view_ = view;
        }
    }

    uint64_t GetMaxHeight() {
        if (latest_committed_block_->has_block_info()) {
            return latest_committed_block_->block_info().height();
        }

        return 0;
    }

    inline void Clear() {
        view_blocks_info_.clear();
        prune_height_ = View(1);
        // latest_committed_block_ = nullptr;
        // latest_locked_block_ = nullptr;
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

    inline std::shared_ptr<ViewBlock> LatestLockedBlock() const {
        return latest_locked_block_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        if (latest_committed_block_ &&
                (view_block->qc().network_id() !=
                latest_committed_block_->qc().network_id() ||
                latest_committed_block_->qc().view() >= 
                view_block->qc().view())) {
            return;
        }

        // 允许设置旧的 view block
        ZJC_DEBUG("changed latest commited block %u_%u_%lu, new view: %lu",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height(),
            view_block->qc().view());
        latest_committed_block_ = view_block;
        auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end()) {
            it->second->status = ViewBlockStatus::Committed;
        }
    }

    inline void SetLatestLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        auto view_block_status = GetViewBlockStatus(view_block);
        if (view_block_status != ViewBlockStatus::Committed) {
            latest_locked_block_ = view_block;
            auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
            if (it != view_blocks_info_.end()) {
                view_blocks_info_[view_block->qc().view_block_hash()]->status = ViewBlockStatus::Locked;
            }
        }        
    }

    inline ViewBlockStatus GetViewBlockStatus(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
        if (it == view_blocks_info_.end()) {
            return ViewBlockStatus::Unknown;
        }
        return it->second->status;        
    } 

private:
    void SetViewBlockToMap(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        assert(!view_block_info->view_block->qc().view_block_hash().empty());
        auto it = view_blocks_info_.find(view_block_info->view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
            ZJC_DEBUG("exists, failed add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
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

        if (it != view_blocks_info_.end()) {
            view_block_info->children = it->second->children;
        }
        
        view_blocks_info_[view_block_info->view_block->qc().view_block_hash()] = view_block_info;
        CHECK_MEMORY_SIZE(view_blocks_info_);
        ZJC_DEBUG("success add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
            common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
            view_block_info->view_block->qc().network_id(),
            view_block_info->view_block->qc().pool_index(),
            view_block_info->view_block->qc().view(),
            view_block_info->view_block->block_info().height(),
            common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
            view_block_info->view_block->block_info().tx_list_size(),
            String().c_str());
    }

    std::shared_ptr<ViewBlockInfo> GetViewBlockInfo(
            std::shared_ptr<ViewBlock> view_block, 
            BalanceMapPtr acc_balance_map_ptr,
            std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr) {
        auto view_block_info_ptr = std::make_shared<ViewBlockInfo>();
        if (view_block) {
            ZJC_DEBUG("add new view block %s, leader: %d",
                common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view());
            for (uint32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
                view_block_info_ptr->added_txs.insert(view_block->block_info().tx_list(i).gid());
            }
        }

        view_block_info_ptr->view_block = view_block;
        view_block_info_ptr->acc_balance_map_ptr = acc_balance_map_ptr;
        view_block_info_ptr->zjc_host_ptr = zjc_host_ptr;
        return view_block_info_ptr;
    }

    void SetStatusToMap(const HashStr& hash, const ViewBlockStatus& status) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            return;
        }

        view_blocks_info_[hash]->status = status;        
    }

    // prune the branch starting from view_block
    Status GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children);
    
    std::shared_ptr<ViewBlock> high_view_block_ = nullptr;
    View prune_height_ = 0;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::shared_ptr<ViewBlock> latest_locked_block_; // locked_block_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    volatile View stored_to_db_view_ = 0llu;
    std::unordered_map<std::string, uint64_t> valid_parent_block_hash_;
};

// from db
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block);
void GetQCWrappedByGenesis(uint32_t pool_index, QC* qc);
        
} // namespace consensus
    
} // namespace shardora


