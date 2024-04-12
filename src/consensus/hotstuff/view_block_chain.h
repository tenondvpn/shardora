#pragma once

#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
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

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:
    explicit ViewBlockChain(const std::shared_ptr<ViewBlock>&);
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

    inline std::unordered_set<std::shared_ptr<ViewBlock>> LatestLockedBlocks() const {
        return latest_locked_blocks_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        latest_locked_blocks_.erase(view_block);
        latest_committed_block_ = view_block;
    }

    inline void AddLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        latest_locked_blocks_.insert(view_block);
    }

    // inline ViewBlockMinHeap OrphanBlocks() const {
    //     return orphan_blocks_;
    // }

    // void AddOrphanBlock(const std::shared_ptr<ViewBlock>& view_block);
    // std::shared_ptr<ViewBlock> PopOrphanBlock();
    // bool IsOrphanBlockTimeout(const std::shared_ptr<ViewBlock> view_block) const;
    // If a chain is valid
    bool IsValid();

    View GetMinHeight() const {
        View min = 0;
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
        prune_height_ = View(1);

        latest_committed_block_ = nullptr;
        latest_locked_blocks_.clear();
        start_block_ = nullptr;
    }

    inline uint32_t Size() const {
        return view_blocks_.size();
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

    // ViewBlockMinHeap orphan_blocks_; // 已经获得但没有父块, 按照 view 排序
    // std::unordered_map<HashStr, uint64_t> orphan_added_us_;

    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::unordered_set<std::shared_ptr<ViewBlock>> latest_locked_blocks_; // locked_blocks_;
};

// from db
std::shared_ptr<ViewBlock> GetGenesisViewBlock(const std::shared_ptr<db::Db>& db, uint32_t pool_index);
std::shared_ptr<QC> GetGenesisQC();    
        
} // namespace consensus
    
} // namespace shardora


