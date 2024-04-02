#pragma once

#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <protos/prefix_db.h>
#include <queue>

namespace shardora {

namespace consensus {

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
    // if in the same branch
    bool Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    // prune from last prune height to target view block
    Status PruneTo(const HashStr& target_hash, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);

    inline ViewBlockMinHeap OrphanBlocks() const {
        return orphan_blocks_;
    }

    void AddOrphanBlock(const std::shared_ptr<ViewBlock>& view_block);
    std::shared_ptr<ViewBlock> PopOrphanBlock();
    bool IsOrphanBlockTimeout(const std::shared_ptr<ViewBlock> view_block) const;
    
private:
    // prune the branch starting from view_block
    Status PruneFrom(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& hashes_of_branch, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks);
    Status GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children);
    Status DeleteViewBlock(const std::shared_ptr<ViewBlock>& view_block);
    
    View prune_height_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlock>> view_blocks_;
    std::unordered_map<View, std::vector<std::shared_ptr<ViewBlock>>> view_blocks_at_height_;
    std::unordered_map<HashStr, std::vector<std::shared_ptr<ViewBlock>>> view_block_children_;

    ViewBlockMinHeap orphan_blocks_; // 已经获得但没有父块, 按照 view 排序
    std::unordered_map<HashStr, uint64_t> orphan_added_us_;
};

// from db
std::shared_ptr<ViewBlock> GetGenesisViewBlock();
    
        
} // namespace consensus
    
} // namespace shardora


