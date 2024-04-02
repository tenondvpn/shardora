#pragma once

#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain_manager.h>
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
    // if in the same branch
    bool Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    // prune to latest committed block and return the dirty blocks
    Status PruneToLatestCommitted(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);

    inline View LatestCommittedHeight() const {
        return latest_committed_height_;
    }

    inline ViewBlockMinHeap OrphanBlocks() const {
        return orphan_blocks_;
    }

    void AddOrphanBlock(const std::shared_ptr<ViewBlock>& view_block);
    std::shared_ptr<ViewBlock> PopOrphanBlock();
    bool IsOrphanBlockTimeout(const std::shared_ptr<ViewBlock> view_block) const;
    
private:
    // prune the branch starting from view_block
    Status PruneFrom(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& committed_hashes, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks);
    
    View latest_committed_height_;
    View prune_height_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlock>> view_blocks_;
    std::unordered_map<View, std::vector<std::shared_ptr<ViewBlock>>> view_blocks_at_height_;
    std::unordered_map<HashStr, std::vector<std::shared_ptr<ViewBlock>>> view_block_children_;

    ViewBlockMinHeap orphan_blocks_; // 已经获得但没有父块, 按照 view 排序
    std::unordered_map<HashStr, uint64_t> orphan_added_us_;
};

std::shared_ptr<ViewBlock> GetGenesisViewBlock() {}
    
        
} // namespace consensus
    
} // namespace shardora


