#pragma once

#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <limits>
#include <protos/prefix_db.h>
#include <queue>

namespace shardora {

namespace hotstuff {

struct CompareViewBlock {
    bool operator()(const std::shared_ptr<ViewBlock>& lhs, const std::shared_ptr<ViewBlock>& rhs) const {
        return lhs->view > rhs->view;
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

        ViewBlockInfo() : view_block(nullptr), status(ViewBlockStatus::Unknown), qc(nullptr) {}
    };
    
    ViewBlockChain(std::shared_ptr<db::Db>& db);
    ~ViewBlockChain();
    
    ViewBlockChain(const ViewBlockChain&) = delete;
    ViewBlockChain& operator=(const ViewBlockChain&) = delete;

    // Add Node
    Status Store(const std::shared_ptr<ViewBlock>& view_block);
    // Get Block by hash value, fetch from neighbor nodes if necessary
    Status Get(const HashStr& hash, std::shared_ptr<ViewBlock>& view_block);
    // If has block
    bool Has(const HashStr& hash);
    // if in the same branch
    bool Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    
    // prune from last prune height to target view block
    Status PruneTo(const HashStr& target_hash, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes, bool include_history);

    Status GetAll(std::vector<std::shared_ptr<ViewBlock>>&);

    Status GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>&);

    inline std::shared_ptr<ViewBlock> LatestCommittedBlock() const {
        return std::make_shared<ViewBlock>(latest_committed_block_);
    }

    inline std::shared_ptr<ViewBlock> LatestLockedBlock() const {
        return std::make_shared<ViewBlock>(latest_locked_block_);
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        latest_committed_block_ = *view_block;
        view_blocks_info_[view_block->hash]->status = ViewBlockStatus::Committed;        
    }

    inline void SetLatestLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        auto view_block_status = GetViewBlockStatus(view_block);
        if (view_block_status != ViewBlockStatus::Committed) {
            latest_locked_block_ = *view_block;
            view_blocks_info_[view_block->hash]->status = ViewBlockStatus::Locked;
        }        
    }

    inline ViewBlockStatus GetViewBlockStatus(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->hash);
        if (it == view_blocks_info_.end()) {
            return ViewBlockStatus::Unknown;
        }
        return it->second->status;        
    } 

    // 获取 view_block 的 QC
    std::shared_ptr<QC> GetQcOf(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->hash);
        if (it == view_blocks_info_.end()) {
            return nullptr;
        }
        return it->second->qc;        
    }

    void SetQcOf(const HashStr& view_block_hash, const std::shared_ptr<QC>& qc) {
        SetQcToMap(view_block_hash, qc);
        auto view_block = std::make_shared<ViewBlock>();
        Status s = Get(view_block_hash, view_block);
        if (s == Status::kSuccess) {
            StoreToDb(view_block, qc);
        }
    }

    void SetQcOf(const std::shared_ptr<ViewBlock>& view_block, const std::shared_ptr<QC>& qc) {
        SetQcToMap(view_block->hash, qc);
        StoreToDb(view_block, qc);
    }

    Status StoreToDb(const std::shared_ptr<ViewBlock>& v_block, const std::shared_ptr<QC>& qc) {
        // 持久化已经生成 qc 的 ViewBlock
        if (v_block == nullptr) {
            return Status::kInvalidArgument;
        }
        if (qc == nullptr) {
            return Status::kInvalidArgument;
        }
        auto pb_v_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
        ViewBlock2Proto(v_block, pb_v_block.get());
        // 不存储 block 部分，block 已经单独存过了
        pb_v_block->clear_block_str();
        // 保存 v_block 对应的 qc 到 db
        pb_v_block->set_self_qc_str(qc->Serialize());
        auto db_batch = std::make_shared<db::DbWriteBatch>();
        prefix_db_->SaveViewBlockInfo(v_block->block->network_id(),
            v_block->block->pool_index(),
            v_block->block->height(),
            *pb_v_block,
            *db_batch);
        return Status::kSuccess;
    }
    
    // If a chain is valid
    bool IsValid();

    View GetMinHeight() const {
        View min = std::numeric_limits<View>::max();
        for (auto it = view_blocks_at_height_.begin(); it != view_blocks_at_height_.end(); it++) {
            if (it->first < min) {
                min = it->first;
            }
        }
        return min;
    }    

    View GetMaxHeight() const {
        View max = 0;
        for (auto it = view_blocks_at_height_.begin(); it != view_blocks_at_height_.end(); it++) {
            if (it->first > max) {
                max = it->first;
            }
        }
        return max;
    }

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

    std::shared_ptr<ViewBlock> QCRef(const std::shared_ptr<ViewBlock>& view_block) {
        if (view_block->qc) {
            auto it2 = view_blocks_info_.find(view_block->qc->view_block_hash);
            if (it2 == view_blocks_info_.end()) {
                return nullptr;
            }
            return it2->second->view_block;
        }
        return nullptr;
    }
    
private:
    // prune the branch starting from view_block
    Status PruneFromBlockToTargetHash(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& hashes_of_branch, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks, const HashStr& target_hash);
    Status PruneHistoryTo(const std::shared_ptr<ViewBlock>&);
    Status GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children);
    Status DeleteViewBlock(const std::shared_ptr<ViewBlock>& view_block);
    
    
    View prune_height_;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<View, std::vector<std::shared_ptr<ViewBlock>>> view_blocks_at_height_; // 一般一个 view 只有一个块
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    ViewBlock latest_committed_block_; // 使用指针在同步时会被删掉，导致无法选出 leader，最新 committed block
    ViewBlock latest_locked_block_; // locked_block_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    void SetViewBlockToMap(const HashStr& hash, const std::shared_ptr<ViewBlock>& view_block) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            view_blocks_info_[hash] = std::make_shared<ViewBlockInfo>();
        }
        view_blocks_info_[hash]->view_block = view_block;
    }

    void SetStatusToMap(const HashStr& hash, const ViewBlockStatus& status) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            return;
        }
        view_blocks_info_[hash]->status = status;        
    }

    void AddChildrenToMap(const HashStr& parent_hash, const std::shared_ptr<ViewBlock>& view_block) {
        auto it = view_blocks_info_.find(parent_hash);
        if (it == view_blocks_info_.end()) {
            return;
        }
        view_blocks_info_[parent_hash]->children.push_back(view_block);        
    }

    void SetQcToMap(const HashStr& hash, const std::shared_ptr<QC>& qc) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            return;
        }
        view_blocks_info_[hash]->qc = qc;        
    }
};

// from db
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block,
        std::shared_ptr<QC>& self_qc);
std::shared_ptr<QC> GetQCWrappedByGenesis();
std::shared_ptr<QC> GetGenesisQC(const HashStr& genesis_view_block_hash);
        
} // namespace consensus
    
} // namespace shardora


