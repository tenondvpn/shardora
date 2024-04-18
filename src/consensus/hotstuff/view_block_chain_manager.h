#pragma once

#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <db/db.h>

namespace shardora {

namespace hotstuff {

// 用于构造 ViewBlockChainSyncer 和 HotStuffManager
class ViewBlockChainManager {
public:
    explicit ViewBlockChainManager(const std::shared_ptr<db::Db>&);
    ~ViewBlockChainManager();

    ViewBlockChainManager(const ViewBlockChainManager&) = delete;
    ViewBlockChainManager& operator=(const ViewBlockChainManager&) = delete;

    Status Init();
    
    inline std::shared_ptr<ViewBlockChain> Chain(const uint32_t& pool_idx) const {
        auto it = pool_chain_map_.find(pool_idx);
        if (it == pool_chain_map_.end()) {
            return nullptr;
        }
        return it->second;
    }

    inline uint32_t ChainNum() const {
        return pool_chain_map_.size();
    }
    
private:
    std::unordered_map<uint32_t, std::shared_ptr<ViewBlockChain>> pool_chain_map_;
    std::shared_ptr<db::Db> db_;
};

}

} // namespace shardora

