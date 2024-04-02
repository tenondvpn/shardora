#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/types.h>

namespace shardora {
namespace consensus {

ViewBlockChain::ViewBlockChain() : latest_committed_height_(View(0)), prune_height_(View(0)) {
    // TODO genesis block
    Store(GetGenesisViewBlock());
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(const std::shared_ptr<ViewBlock>& view_block) {
    // 父块必须存在
    auto it = view_blocks_.find(view_block->parent_hash);
    if (it == view_blocks_.end()) {
        return Status::kError;
    }
    view_blocks_[view_block->hash] = view_block;
    view_blocks_at_height_[view_block->view].push_back(view_block);
    view_block_children_[view_block->parent_hash].push_back(view_block);
}

Status ViewBlockChain::Get(const HashStr &hash, std::shared_ptr<ViewBlock> &view_block) {
    auto it = view_blocks_.find(hash);
    if (it != view_blocks_.end()) {
        view_block = it->second;
        return Status::kSuccess;
    }

    return Status::kError;
}

bool ViewBlockChain::Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target) {
    auto current = block;
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->view > target->view) {
        s = Get(current->parent_hash, current);
    }

    return s == Status::kSuccess && current->hash == target->hash;
}

// 剪掉从上次 prune_height 到 height 之间，latest_committed 之前的所有分叉，并返回这些分叉上的 blocks
Status ViewBlockChain::PruneToLatestCommitted(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes) {
    if (prune_height_ == LatestCommittedHeight()) {
        return Status::kSuccess;
    }
    // 计算 prune_height 到 latestcommittedheight 中间所有的 committed_hashes
    auto blocks = view_blocks_at_height_[LatestCommittedHeight()];
    if (blocks.empty()) {
        return Status::kError;
    }
    // 每个 committed_height 只会有一个 block，就是 committed_block
    std::unordered_set<HashStr> committed_hashes;
    auto latest_committed_block = blocks[0];
    auto current = latest_committed_block;
    // auto current = std::make_shared<ViewBlock>();
    // Get(latest_committed_block->parent_hash, current);
    
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->view > prune_height_) {
        s = Get(current->parent_hash, current);
        if (s == Status::kSuccess && !current) {
            committed_hashes.insert(current->hash);
            continue;
        }
        return Status::kError;
    }

    auto start_blocks = view_blocks_at_height_[prune_height_];
    if (start_blocks.empty()) {
        return Status::kError;
    }
    auto start_block = start_blocks[0];
    
    PruneFrom(start_block, committed_hashes, forked_blockes);
    prune_height_ = LatestCommittedHeight();
    return Status::kSuccess;
}

Status ViewBlockChain::PruneFrom(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& committed_hashes, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks) {
    auto it = view_block_children_.find(view_block->hash);
    if (it == view_block_children_.end()) {
        return Status::kSuccess;
    }

    auto& child_blocks = it->second;
    for (auto child_iter = child_blocks.begin(); child_iter < child_blocks.end();) {
        if (committed_hashes.find((*child_iter)->hash) == committed_hashes.end()) {
            auto hash = (*child_iter)->hash;
            auto view = (*child_iter)->view;            
            // 删除 it2 节点

            view_blocks_at_height_[view].erase(child_iter);
            view_blocks_.erase(hash);
            child_iter = child_blocks.erase(child_iter);

            forked_blocks.push_back(*child_iter);
            PruneFrom((*child_iter), committed_hashes, forked_blocks);
            continue;
        }
        ++child_iter;
    }

    return Status::kSuccess;
}

void ViewBlockChain::AddOrphanBlock(const std::shared_ptr<ViewBlock>& view_block) {
    orphan_blocks_.push(view_block);
    orphan_added_us_[view_block->hash] = common::TimeUtils::TimestampUs();
}

std::shared_ptr<ViewBlock> ViewBlockChain::PopOrphanBlock() {
    std::shared_ptr<ViewBlock> orphan_block = OrphanBlocks().top();
    OrphanBlocks().pop();
    orphan_added_us_.erase(orphan_block->hash);
}

bool ViewBlockChain::IsOrphanBlockTimeout(const std::shared_ptr<ViewBlock> view_block) const {
    if (!view_block) {
        return false;
    }
    auto it = orphan_added_us_.find(view_block->hash);
    if (it == orphan_added_us_.end()) {
        return false;
    }

    uint64_t added_us = it->second;
    return added_us + ORPHAN_BLOCK_TIMEOUT_US <= common::TimeUtils::TimestampUs();
}

}
}
