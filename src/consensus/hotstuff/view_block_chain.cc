#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/types.h>

namespace shardora {
namespace consensus {

ViewBlockChain::ViewBlockChain(const std::shared_ptr<ViewBlock>& genesis_view_block) : prune_height_(View(1)) {
    Store(genesis_view_block);
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(const std::shared_ptr<ViewBlock>& view_block) {
    // 父块必须存在
    auto it = view_blocks_.find(view_block->parent_hash);
    if (it == view_blocks_.end()) {
        return Status::kError;
    }

    // view 必须连续
    if (it->second->view + 1 != view_block->view) {
        return Status::kError;
    }
    
    view_blocks_[view_block->hash] = view_block;
    view_blocks_at_height_[view_block->view].push_back(view_block);
    view_block_children_[view_block->parent_hash].push_back(view_block);
    return Status::kSuccess;
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
Status ViewBlockChain::PruneTo(const HashStr& target_hash, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes) {
    auto target_it = view_blocks_.find(target_hash);
    if (target_it == view_blocks_.end()) {
        return Status::kError;
    }

    auto current = target_it->second;
    auto target_height = current->view;

    if (prune_height_ >= target_height) {
        return Status::kSuccess;
    }
    
    std::unordered_set<HashStr> hashes_of_branch;
    
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->view > prune_height_) {
        s = Get(current->parent_hash, current);
        if (s == Status::kSuccess && !current) {
            hashes_of_branch.insert(current->hash);
            continue;
        }
        return Status::kError;
    }

    auto start_blocks = view_blocks_at_height_[prune_height_];
    if (start_blocks.empty()) {
        return Status::kError;
    }
    auto start_block = start_blocks[0];
    
    PruneFrom(start_block, hashes_of_branch, forked_blockes);
    prune_height_ = target_height;
    return Status::kSuccess;
}

Status ViewBlockChain::PruneFrom(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& hashes_of_branch, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks) {
    auto it = view_block_children_.find(view_block->hash);
    if (it == view_block_children_.end()) {
        return Status::kSuccess;
    }

    auto& child_blocks = it->second;
    for (auto child_iter = child_blocks.begin(); child_iter < child_blocks.end();) {
        if (hashes_of_branch.find((*child_iter)->hash) == hashes_of_branch.end()) {
            auto hash = (*child_iter)->hash;
            auto view = (*child_iter)->view;            
            // 删除 it2 节点

            view_blocks_at_height_[view].erase(child_iter);
            view_blocks_.erase(hash);
            child_iter = child_blocks.erase(child_iter);

            forked_blocks.push_back(*child_iter);
            PruneFrom((*child_iter), hashes_of_branch, forked_blocks);
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
    return orphan_block;
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

std::shared_ptr<ViewBlock> GetGenesisViewBlock() {
    return nullptr;
}

}
}
