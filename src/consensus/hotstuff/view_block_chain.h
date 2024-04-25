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

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:
    ViewBlockChain();
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
        return latest_committed_block_;
    }

    inline std::shared_ptr<ViewBlock> LatestLockedBlock() const {
        return latest_locked_block_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        latest_locked_block_ = nullptr;
        latest_committed_block_ = view_block;
    }

    inline void SetLatestLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        latest_locked_block_ = view_block;
    }

    // 获取 view_block 的 QC
    std::shared_ptr<QC> GetQcOf(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_block_qc_map_.find(view_block->hash);
        if (it == view_block_qc_map_.end()) {
            return nullptr;
        }
        return it->second;
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
        view_blocks_.clear();
        view_blocks_at_height_.clear();
        view_block_children_.clear();
        view_block_qc_map_.clear();
        prune_height_ = View(1);

        latest_committed_block_ = nullptr;
        latest_locked_block_ = nullptr;
        start_block_ = nullptr;
    }

    inline uint32_t Size() const {
        return view_blocks_.size();
    }

    void Print() const;
    void PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent = "") const;

    std::shared_ptr<ViewBlock> QCRef(const std::shared_ptr<ViewBlock>& view_block) {
        if (view_block->qc) {
            auto it2 = view_blocks_.find(view_block->qc->view_block_hash);
            if (it2 == view_blocks_.end()) {
                return nullptr;
            }
            return it2->second;
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
    std::unordered_map<HashStr, std::shared_ptr<ViewBlock>> view_blocks_;
    std::unordered_map<View, std::vector<std::shared_ptr<ViewBlock>>> view_blocks_at_height_;
    std::unordered_map<HashStr, std::vector<std::shared_ptr<ViewBlock>>> view_block_children_;
    std::unordered_map<HashStr, std::shared_ptr<QC>> view_block_qc_map_; // 存放 view_block 及它的 QC

    // ViewBlockMinHeap orphan_blocks_; // 已经获得但没有父块, 按照 view 排序
    // std::unordered_map<HashStr, uint64_t> orphan_added_us_;

    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::shared_ptr<ViewBlock> latest_locked_block_; // locked_block_;
};

// from db

        
} // namespace consensus
    
} // namespace shardora


