#include "consensus/hotstuff/view_block_chain.h"
#include "common/defer.h"
#include <consensus/hotstuff/types.h>

namespace shardora {
namespace consensus {

ViewBlockChain::ViewBlockChain() : latest_committed_height_(View(0)), prune_height_(View(0)) {
    // TODO genesis block
    Store(GetGenesisViewBlock());
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(const std::shared_ptr<ViewBlock>& view_block) {
    if (!view_block) {
        return Status::kError;
    }

    // 检查是否有父块，没有则放入 orphans
    auto it = view_blocks_.find(view_block->parent_hash);
    if (it == view_blocks_.end()) {
        orphan_blocks_.insert(view_block);
        return Status::kError;
    }
    
    
    view_blocks_[view_block->hash] = view_block;
    view_blocks_at_height_[view_block->view].push_back(view_block);

    auto iter = pending_fetch_.find(view_block->hash);
    if (iter != pending_fetch_.end()) {
        pending_fetch_.erase(iter);
    }
}

Status ViewBlockChain::Get(const HashStr &hash,
                           std::shared_ptr<ViewBlock> &view_block) {
    auto it = view_blocks_.find(hash);
    if (it != view_blocks_.end()) {
        view_block = it->second;
        return Status::kSuccess;
    }
    
    pending_fetch_.insert(hash);
    // TODO fetch from neighbors
    
    auto fetch_it = pending_fetch_.find(hash);
    if (fetch_it != pending_fetch_.end()) {
        pending_fetch_.erase(fetch_it);
    }

    if (!view_block || view_block->hash != hash) {
        // 再检查一遍
        it = view_blocks_.find(hash);
        if (it != view_blocks_.end()) {
            view_block = it->second;
            return Status::kSuccess;
        }
        return Status::kError;
    }

    view_blocks_[hash] = view_block;
    view_blocks_at_height_[view_block->view].push_back(view_block);

    return Status::kSuccess;
}

}
}
