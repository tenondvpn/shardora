#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/types.h>
#include <iostream>

namespace shardora {
namespace consensus {

ViewBlockChain::ViewBlockChain(const std::shared_ptr<ViewBlock>& genesis_block) : prune_height_(View(1)) {
    assert(genesis_block->view == 1);
    view_blocks_[genesis_block->hash] = genesis_block;
    view_blocks_at_height_[genesis_block->view].push_back(genesis_block);
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
    std::shared_ptr<ViewBlock> current = nullptr;
    Get(target_hash, current);
    if (!current) {
        return Status::kError;
    }
    
    auto target_height = current->view;
    if (prune_height_ >= target_height) {
        return Status::kSuccess;
    }
    
    std::unordered_set<HashStr> hashes_of_branch;
    hashes_of_branch.insert(current->hash);
    
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->view > prune_height_) {
        s = Get(current->parent_hash, current);
        if (s == Status::kSuccess && current) {
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

    PruneFromBlockToTargetHash(start_block, hashes_of_branch, forked_blockes, target_hash);
    prune_height_ = target_height;
    return Status::kSuccess;
}

Status ViewBlockChain::PruneFromBlockToTargetHash(const std::shared_ptr<ViewBlock>& view_block, const std::unordered_set<HashStr>& hashes_of_branch, std::vector<std::shared_ptr<ViewBlock>>& forked_blocks, const HashStr& target_hash) {
    if (view_block->hash == target_hash) {
        return Status::kSuccess;
    }
    
    std::vector<std::shared_ptr<ViewBlock>> child_blocks;
    GetChildren(view_block->hash, child_blocks);

    if (child_blocks.empty()) {
        return Status::kSuccess;
    }

    for (auto child_iter = child_blocks.begin(); child_iter < child_blocks.end(); child_iter++) {
        // delete the view block that is not on the same branch
        if (hashes_of_branch.find((*child_iter)->hash) == hashes_of_branch.end()) {
            DeleteViewBlock(*child_iter);

            forked_blocks.push_back(*child_iter);
        }
        PruneFromBlockToTargetHash((*child_iter), hashes_of_branch, forked_blocks, target_hash);
    }

    return Status::kSuccess;
}

Status ViewBlockChain::GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children) {
    auto it = view_block_children_.find(hash);
    if (it == view_block_children_.end()) {
        return Status::kSuccess;
    }
    children = it->second;
    return Status::kSuccess;
}

Status ViewBlockChain::DeleteViewBlock(const std::shared_ptr<ViewBlock>& view_block) {
    auto original_child_blocks = view_block_children_[view_block->parent_hash];
    auto original_blocks_at_height = view_blocks_at_height_[view_block->view];
    auto hash = view_block->hash;
    auto view = view_block->view;

    try {
        auto& child_blocks = view_block_children_[view_block->parent_hash];
        child_blocks.erase(std::remove_if(child_blocks.begin(), child_blocks.end(),
            [&hash](const std::shared_ptr<ViewBlock>& item) { return item->hash == hash; }),
            child_blocks.end());

        auto& blocks = view_blocks_at_height_[view];
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
            [&hash](const std::shared_ptr<ViewBlock>& item) { return item->hash == hash; }),
            blocks.end());

        view_blocks_.erase(hash);
    } catch (...) {
        view_block_children_[view_block->parent_hash] = original_child_blocks;
        view_blocks_at_height_[view_block->view] = original_blocks_at_height;
        throw;
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
