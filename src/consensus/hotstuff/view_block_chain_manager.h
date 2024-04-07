#pragma once

#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace consensus {

class ViewBlockChainManager {
public:
    explicit ViewBlockChainManager(const std::shared_ptr<ViewBlock>&);
    ~ViewBlockChainManager();

    ViewBlockChainManager(const ViewBlockChainManager&) = delete;
    ViewBlockChainManager& operator=(const ViewBlockChainManager&) = delete;
    
    // Status Store(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& view_block);
    // Status Get(const uint32_t& pool_idx, const HashStr& hash, std::shared_ptr<ViewBlock>& view_block);
    // bool Extends(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target);
    // Status PruneTo(const uint32_t& pool_idx, const HashStr& target, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);

    // void AddOrphanBlock(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& view_block);
    // std::shared_ptr<ViewBlock> PopOrphanBlock(const uint32_t& pool_idx);
    // bool IsOrphanBlockTimeout(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock> view_block) const;
    inline std::shared_ptr<ViewBlockChain> Chain(const uint32_t& pool_idx) const {
        return pool_chain_map_.at(pool_idx);
    }

    inline uint32_t ChainNum() const {
        return pool_chain_map_.size();
    }
    
private:
    // void ConsumeOrphanBlocks();


    std::unordered_map<uint32_t, std::shared_ptr<ViewBlockChain>> pool_chain_map_;
    // common::Tick tick_;
};

}

} // namespace shardora

