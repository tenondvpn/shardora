#pragma once

#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/view_block_chain_syncer.h>

namespace shardora {

namespace consensus {

class ViewBlockChainManager {
public:
    ViewBlockChainManager(std::shared_ptr<ViewBlockChainSyncer> syncer_ptr);
    ~ViewBlockChainManager();

    ViewBlockChainManager(const ViewBlockChainManager&) = delete;
    ViewBlockChainManager& operator=(const ViewBlockChainManager&) = delete;
    
    Status Store(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& view_block);
    Status Get(const uint32_t& pool_idx, const HashStr& hash, std::shared_ptr<ViewBlock>& view_block);
    bool Extends(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    Status PruneToLatestCommitted(const uint32_t& pool_idx, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);
    
private:
    void ConsumeOrphanBlocks();
    std::shared_ptr<ViewBlockChain> Chain(const uint32_t& pool_idx);

    std::unordered_map<uint32_t, std::shared_ptr<ViewBlockChain>> pool_chain_map_;
    std::shared_ptr<ViewBlockChainSyncer> syncer_ptr_;
    common::Tick tick_;
};

}

} // namespace shardora

