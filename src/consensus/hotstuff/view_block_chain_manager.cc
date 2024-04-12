#include <common/utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/view_block_chain_manager.h>
#include <db/db.h>

namespace shardora {

namespace hotstuff {

ViewBlockChainManager::ViewBlockChainManager(const std::shared_ptr<db::Db>& db) : db_(db) {}

ViewBlockChainManager::~ViewBlockChainManager() {}

Status ViewBlockChainManager::Init() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        auto start_block = GetGenesisViewBlock(db_, pool_idx);
        if (!start_block) {
            return Status::kNotFound;
        }
        pool_chain_map_[pool_idx] = std::make_shared<ViewBlockChain>(start_block);;
    }
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

