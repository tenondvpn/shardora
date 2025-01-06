#pragma once

#include <limits>
#include <queue>

#include "block/account_manager.h"
#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <protos/prefix_db.h>
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace hotstuff {

struct CompareViewBlock {
    bool operator()(const std::shared_ptr<ViewBlock>& lhs, const std::shared_ptr<ViewBlock>& rhs) const {
        return lhs->view() > rhs->view();
    }
};

using ViewBlockMinHeap =
    std::priority_queue<std::shared_ptr<ViewBlock>,
                        std::vector<std::shared_ptr<ViewBlock>>,
                        CompareViewBlock>;


static const int MaxBlockNumForView = 7;
enum class ViewBlockStatus : int {
    Unknown = 0,
    Proposed = 1,
    Locked = 2,
    Committed = 3,
};

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:
    struct ViewBlockInfo {
        std::shared_ptr<ViewBlock> view_block;
        ViewBlockStatus status;
        std::vector<std::shared_ptr<ViewBlock>> children;
        std::shared_ptr<QC> qc;
        std::unordered_set<std::string> added_txs;
        BalanceMapPtr acc_balance_map_ptr;
        std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr;

        ViewBlockInfo() : 
            view_block(nullptr), 
            status(ViewBlockStatus::Unknown), 
            qc(nullptr) {}
    };
    
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
    std::shared_ptr<ViewBlock> Get(const HashStr& hash);
    std::shared_ptr<ViewBlock> Get(uint64_t view);

    View GetMaxHeight() const {
        View max = 0;
        for (auto it = view_blocks_at_height_.begin(); it != view_blocks_at_height_.end(); it++) {
            if (it->first > max) {
                max = it->first;
            }
        }
        return max;
    }    

    // If has block
    bool Has(const HashStr& hash);
    // if in the same branch
    bool Extends(const ViewBlock& block, const ViewBlock& target);
    
    // prune from last prune height to target view block
    Status PruneTo(const HashStr& target_hash, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes, bool include_history);

    Status GetAll(std::vector<std::shared_ptr<ViewBlock>>&);
    
    Status GetAllVerified(std::vector<std::shared_ptr<ViewBlock>>&);

    Status GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>&);

    void MergeAllPrevStorageMap(
            const std::string& parent_hash, 
            zjcvm::ZjchainHost& zjc_host) {
        std::string phash = parent_hash;
        uint32_t count = 0;
        while (true) {
            if (phash.empty()) {
                break;
            }

            ZJC_DEBUG("now merge prev storage map: %s", common::Encode::HexEncode(phash).c_str());
            auto it = view_blocks_info_.find(phash);
            if (it == view_blocks_info_.end()) {
                break;
            }

            if (it->second->zjc_host_ptr) {
                auto& prev_storages_map = it->second->zjc_host_ptr->prev_storages_map();
                for (auto iter = prev_storages_map.begin(); iter != prev_storages_map.end(); ++iter) {
                    zjc_host.SavePrevStorages(iter->first, iter->second, false);
                    if (iter->first.size() > 40)
                    ZJC_DEBUG("%s, merge success prev storage key: %s, value: %s",
                        common::Encode::HexEncode(phash).c_str(), 
                        common::Encode::HexEncode(iter->first).c_str(),
                        common::Encode::HexEncode(iter->second).c_str());
                }
            }

            if (!it->second->view_block) {
                break;
            }
            
            phash = it->second->view_block->parent_hash();
        }
    }

    void MergeAllPrevBalanceMap(
            const std::string& parent_hash, 
            BalanceMap& acc_balance_map) {
        std::string phash = parent_hash;
        uint32_t count = 0;
        while (true) {
            if (phash.empty()) {
                break;
            }

            auto it = view_blocks_info_.find(phash);
            if (it == view_blocks_info_.end()) {
                break;
            }

            if (it->second->acc_balance_map_ptr) {
                auto& prev_acc_balance_map = *it->second->acc_balance_map_ptr;
                for (auto iter = prev_acc_balance_map.begin(); iter != prev_acc_balance_map.end(); ++iter) {
                    auto fiter = acc_balance_map.find(iter->first);
                    if (fiter == acc_balance_map.end()) {
                        acc_balance_map[iter->first] = iter->second;
                        ZJC_DEBUG("merge prev all balance merge prev account balance %s: %lu, %u_%u_%lu, block height: %lu",
                            common::Encode::HexEncode(iter->first).c_str(), iter->second, 
                            it->second->view_block->network_id(), 
                            it->second->view_block->pool_index(),
                            it->second->view_block->view(),
                            it->second->view_block->block_info().height());
                    }
                }
            }

            if (!it->second->view_block) {
                break;
            }
            
            phash = it->second->view_block->parent_hash();
        }
    }

    bool CheckTxGidValid(const std::string& gid, const std::string& parent_hash) {
        std::string phash = parent_hash;
        uint32_t count = 0;
        while (true) {
            if (phash.empty()) {
                break;
            }

            auto it = view_blocks_info_.find(phash);
            if (it == view_blocks_info_.end()) {
                break;
            }

            auto iter = it->second->added_txs.find(gid);
            if (iter != it->second->added_txs.end()) {
                ZJC_DEBUG("failed check tx gid: %s, phash: %s",
                    common::Encode::HexEncode(gid).c_str(),
                    common::Encode::HexEncode(phash).c_str());
                return false;
            }

            ++count;
            if (count >= AllowedEmptyBlockCnt) {
                break;
            }

            if (!it->second->view_block) {
                return false;
            }
            
            phash = it->second->view_block->parent_hash();
        }

        if (prefix_db_->JustCheckCommitedGidExists(gid)) {
            ZJC_DEBUG("failed check tx gid exists in db: %s", 
                common::Encode::HexEncode(gid).c_str());
            return false;
        }

        ZJC_DEBUG("success check tx gid not exists in db: %s, phash: %s", 
            common::Encode::HexEncode(gid).c_str(), 
            common::Encode::HexEncode(parent_hash).c_str());
        return true;
    }

    Status GetRecursiveChildren(HashStr, std::vector<std::shared_ptr<ViewBlock>>&);

    inline std::shared_ptr<ViewBlock> LatestCommittedBlock() const {
        assert(!latest_committed_block_ || latest_committed_block_->has_self_commit_qc());
        return latest_committed_block_;
    }

    inline std::shared_ptr<ViewBlock> LatestLockedBlock() const {
        return latest_locked_block_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        if (latest_committed_block_ &&
                (view_block->network_id() !=
                latest_committed_block_->network_id() ||
                latest_committed_block_->view() >= 
                view_block->view())) {
            return;
        }

        if (!view_block->has_self_commit_qc()) {
            assert(false);
            return;
        } 

        // 允许设置旧的 view block
        ZJC_DEBUG("changed latest commited block %u_%u_%lu, new view: %lu",
            view_block->network_id(), 
            view_block->pool_index(), 
            view_block->block_info().height(),
            view_block->view());
        latest_committed_block_ = view_block;
        auto it = view_blocks_info_.find(view_block->hash());
        if (it != view_blocks_info_.end()) {
            it->second->status = ViewBlockStatus::Committed;
        }
    }

    inline void SetLatestLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        auto view_block_status = GetViewBlockStatus(view_block);
        if (view_block_status != ViewBlockStatus::Committed) {
            latest_locked_block_ = view_block;
            auto it = view_blocks_info_.find(view_block->hash());
            if (it != view_blocks_info_.end()) {
                view_blocks_info_[view_block->hash()]->status = ViewBlockStatus::Locked;
            }
        }        
    }

    inline ViewBlockStatus GetViewBlockStatus(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->hash());
        if (it == view_blocks_info_.end()) {
            return ViewBlockStatus::Unknown;
        }
        return it->second->status;        
    }

    // 获取 view_block 的 QC
    std::shared_ptr<QC> GetQcOf(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->hash());
        if (it == view_blocks_info_.end()) {
            return nullptr;
        }
        return it->second->qc;        
    }

    void SetQcOf(const HashStr& view_block_hash, const std::shared_ptr<QC>& qc) {
        SetQcToMap(view_block_hash, qc);
    }

    void SetQcOf(const std::shared_ptr<ViewBlock>& view_block, const std::shared_ptr<QC>& qc) {
        SetQcToMap(view_block->hash(), qc);
    }    

    // Only Store committed vblocks
    Status StoreToDb(
            const std::shared_ptr<ViewBlock>& v_block,
            uint64_t test_index,
            std::shared_ptr<db::DbWriteBatch>& db_batch) {        
        // 持久化已经生成 qc 的 ViewBlock
        if (v_block == nullptr) {
            return Status::kInvalidArgument;
        }

        if (!v_block->has_self_commit_qc()) {
            return Status::kInvalidArgument;
        }

        if (!IsQcTcValid(v_block->qc())) {            
            ZJC_DEBUG("not has signature, pool: %u, StoreToDb 0, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
                pool_index_, test_index, v_block->block_info().tx_list_size(),
                v_block->network_id(),
                v_block->pool_index(),
                v_block->view(),
                common::Encode::HexEncode(v_block->hash()).c_str());
            return Status::kSuccess;
        }

        if (prefix_db_->HasViewBlockInfo(v_block->hash())) {
            ZJC_DEBUG("has in db, pool: %u, StoreToDb 0, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
                pool_index_, test_index, v_block->block_info().tx_list_size(),
                v_block->network_id(),
                v_block->pool_index(),
                v_block->view(),
                common::Encode::HexEncode(v_block->hash()).c_str());
            return Status::kSuccess;
        }        
        
        prefix_db_->SaveViewBlockInfo(*v_block, db_batch);
        ZJC_DEBUG("success pool: %u, StoreToDb 3, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
            pool_index_, test_index, v_block->block_info().tx_list_size(),
            v_block->network_id(),
            v_block->pool_index(),
            v_block->view(),
            common::Encode::HexEncode(v_block->hash()).c_str());
        return Status::kSuccess;
    }
    
    // If a chain is valid
    bool IsValid();
    inline void Clear() {
        view_blocks_info_.clear();
        view_blocks_at_height_.clear();
        prune_height_ = View(1);
        // latest_committed_block_ = nullptr;
        // latest_locked_block_ = nullptr;
        start_block_ = nullptr;        
    }

    inline uint32_t Size() const {
        return view_blocks_info_.size();
    }

    void Print() const;
    void PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent = "") const;
    std::string String() const;

    std::shared_ptr<ViewBlock> QCRef(const std::shared_ptr<ViewBlock>& view_block) {
        if (view_block->has_qc()) {
            auto it2 = view_blocks_info_.find(view_block->qc().view_block_hash());
            if (it2 == view_blocks_info_.end()) {
                return nullptr;
            }
            return it2->second->view_block;
        }
        return nullptr;
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

    void ResetViewBlock(const HashStr& hash) {
        auto it = view_blocks_info_.find(hash);
        if (it != view_blocks_info_.end() && 
                it->second->view_block != nullptr && 
                it->second->view_block->qc().sign_x().empty()) {
            it->second->view_block = nullptr;
        }
    }

private:
    void SetViewBlockToMap(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        assert(!view_block_info->view_block->hash().empty());
        auto it = view_blocks_info_.find(view_block_info->view_block->hash());
        if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
            ZJC_DEBUG("exists, failed add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
                common::Encode::HexEncode(view_block_info->view_block->hash()).c_str(),
                view_block_info->view_block->network_id(),
                view_block_info->view_block->pool_index(),
                view_block_info->view_block->view(),
                view_block_info->view_block->block_info().height(),
                common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
                view_block_info->view_block->block_info().tx_list_size(),
                String().c_str());
            return;
        }

        if (it != view_blocks_info_.end()) {
            view_block_info->children = it->second->children;
        }
        
        view_blocks_info_[view_block_info->view_block->hash()] = view_block_info;
        CHECK_MEMORY_SIZE(view_blocks_info_);
        ZJC_DEBUG("success add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
            common::Encode::HexEncode(view_block_info->view_block->hash()).c_str(),
            view_block_info->view_block->network_id(),
            view_block_info->view_block->pool_index(),
            view_block_info->view_block->view(),
            view_block_info->view_block->block_info().height(),
            common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
            view_block_info->view_block->block_info().tx_list_size(),
            String().c_str());
        auto& zjc_host_prev_storages = view_block_info->zjc_host_ptr->prev_storages_map();
        for (auto iter = zjc_host_prev_storages.begin(); iter != zjc_host_prev_storages.end(); ++iter) {
            ZJC_DEBUG("success add prev storage key: %s",
                common::Encode::HexEncode(iter->first).c_str());
        }
    }

    std::shared_ptr<ViewBlockInfo> GetViewBlockInfo(
            std::shared_ptr<ViewBlock> view_block, 
            BalanceMapPtr acc_balance_map_ptr,
            std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr) {
        auto view_block_info_ptr = std::make_shared<ViewBlockInfo>();
        if (view_block) {
            ZJC_DEBUG("add new view block %s, leader: %d",
                common::Encode::HexEncode(view_block->hash()).c_str(), view_block->view());
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

    void SetQcToMap(const HashStr& hash, const std::shared_ptr<QC>& qc) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            return;
        }
        view_blocks_info_[hash]->qc = qc;        
    }    

    void AddChildrenToMap(const std::shared_ptr<ViewBlock>& view_block) {
        const HashStr& parent_hash = view_block->parent_hash();
        if (latest_committed_block_ != nullptr && 
                view_block->view() <= latest_committed_block_->view()) {
            return;
        }

        auto it = view_blocks_info_.find(parent_hash);
        if (it == view_blocks_info_.end()) {
            auto block_info_ptr = GetViewBlockInfo(nullptr, nullptr, nullptr);
            view_blocks_info_[parent_hash] = block_info_ptr;
            CHECK_MEMORY_SIZE(view_blocks_info_);
            ZJC_DEBUG("add null parent view block: %u_%u_%lu, height: %lu",
                view_block->network_id(), 
                view_block->pool_index(), 
                view_block->view(), 
                view_block->block_info().height());
        }
        view_blocks_info_[parent_hash]->children.push_back(view_block);
    }

    // prune the branch starting from view_block
    Status PruneFromBlockToTargetHash(
        const std::shared_ptr<ViewBlock>& view_block, 
        const std::unordered_set<HashStr>& hashes_of_branch, 
        std::vector<std::shared_ptr<ViewBlock>>& forked_blocks, 
        const HashStr& target_hash);
    Status PruneHistoryTo(const std::shared_ptr<ViewBlock>&);    
    Status GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children);
    Status DeleteViewBlock(const std::shared_ptr<ViewBlock>& view_block);
    
    View prune_height_ = 0;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<View, std::vector<std::shared_ptr<ViewBlock>>> view_blocks_at_height_; // 一般一个 view 只有一个块
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::shared_ptr<ViewBlock> latest_locked_block_; // locked_block_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
};

// from db
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block);
void GetQCWrappedByGenesis(uint32_t pool_index, QC *qc);
std::shared_ptr<QC> GetGenesisQC(uint32_t pool_index, const HashStr& genesis_view_block_hash);
        
} // namespace consensus
    
} // namespace shardora


