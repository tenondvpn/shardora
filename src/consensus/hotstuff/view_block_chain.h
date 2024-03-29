#pragma once

#include <consensus/hotstuff/types.h>
#include <protos/prefix_db.h>
namespace shardora {
    
namespace consensus {

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
    // Get Block locally
    Status LocalGet(const HashStr& hash, std::shared_ptr<ViewBlock>& view_block);
    // if in the same branch
    Status Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    // prune to latest committed block and return the dirty blocks
    Status PruneToLatestCommitted(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);
    // flush to db
    Status FlushToDb();
private:
    // TODO mutex_?
    std::mutex mutex_;
    View latest_committed_height_;
    View prune_height_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlock>> view_blocks_;
    std::unordered_map<View, std::shared_ptr<ViewBlock>> view_blocks_at_height_;
    std::unordered_set<HashStr> pending_fetch_; // the view blocks waiting to be fetch from remote nodes
    std::unordered_set<std::shared_ptr<ViewBlock>> orphan_blocks_; // 已经获得但没有父块
    
    // std::shared_ptr<protos::PrefixDb> prefix_db_;

    void ConsumeOrphanBlocks();
};

std::shared_ptr<ViewBlock> GetGenesisViewBlock() {
    
        
} // namespace consensus
    
} // namespace shardora


