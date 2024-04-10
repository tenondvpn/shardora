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
    
    inline std::shared_ptr<ViewBlockChain> Chain(const uint32_t& pool_idx) const {
        return pool_chain_map_.at(pool_idx);
    }

    inline uint32_t ChainNum() const {
        return pool_chain_map_.size();
    }
    
private:
    std::unordered_map<uint32_t, std::shared_ptr<ViewBlockChain>> pool_chain_map_;
    // common::Tick tick_;
};

}

} // namespace shardora

