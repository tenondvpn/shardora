#include <common/utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/view_block_chain_manager.h>
#include <db/db.h>

namespace shardora {

namespace hotstuff {

ViewBlockChainManager::ViewBlockChainManager(const std::shared_ptr<db::Db>& db) : db_(db) {}

ViewBlockChainManager::~ViewBlockChainManager() {}

Status ViewBlockChainManager::Init(InitBlockFunc init_block_func) {
    // Always start from genesis block, waiting for syncs if the view is too old
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        pool_chain_map_[pool_idx] = std::make_shared<ViewBlockChain>();
        if (!init_block_func) {
            continue;
        }
        auto start_block = init_block_func(db_, pool_idx);
        if (!start_block) {
            continue;
        }
    }
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

