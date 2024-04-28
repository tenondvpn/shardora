#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/types.h>
#include <iostream>
#include <algorithm>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

ViewBlockChain::ViewBlockChain() {
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(const std::shared_ptr<ViewBlock>& view_block) {
    if (!view_block->Valid()) {
        ZJC_ERROR("view block is not valid, hash: %s",
            common::Encode::HexEncode(view_block->hash).c_str());
        return Status::kError;
    }

    if (Has(view_block->hash)) {
        return Status::kError;
    }
    
    if (!start_block_) {
        start_block_ = view_block;
        view_blocks_[view_block->hash] = view_block;
        view_blocks_at_height_[view_block->view].push_back(view_block);
        
        prune_height_ = view_block->view;        
        return Status::kSuccess;
    }
    // 父块必须存在
    auto it = view_blocks_.find(view_block->parent_hash);
    if (it == view_blocks_.end()) {
        return Status::kError;
    }

    // 如果有 qc，则 qc 指向的块必须存在
    if (view_block->qc && !view_block->qc->view_block_hash.empty() && !QCRef(view_block)) {
        return Status::kError;
    }

    // 分叉数上限验证，避免恶意的无用分叉消耗内存
    if (view_blocks_at_height_[view_block->view].size() > MaxBlockNumForView) {
        return Status::kError;
    }
    
    
    view_blocks_[view_block->hash] = view_block;
    view_blocks_at_height_[view_block->view].push_back(view_block);
    view_block_children_[view_block->parent_hash].push_back(view_block);
    view_block_qc_map_[view_block->qc->view_block_hash] = view_block->qc;

    return Status::kSuccess;
}


Status ViewBlockChain::Get(const HashStr &hash, std::shared_ptr<ViewBlock> &view_block) {
    auto it = view_blocks_.find(hash);
    if (it != view_blocks_.end()) {
        view_block = it->second;
        return Status::kSuccess;
    }

    return Status::kNotFound;
}

bool ViewBlockChain::Has(const HashStr& hash) {
    auto it = view_blocks_.find(hash);
    return it != view_blocks_.end();
}

bool ViewBlockChain::Extends(const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target) {
    if (!target || !block) {
        return true;
    }
    auto current = block;
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->view > target->view) {
        s = Get(current->parent_hash, current);
    }

    return s == Status::kSuccess && current->hash == target->hash;
}

Status ViewBlockChain::GetAll(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    for (auto it = view_blocks_.begin(); it != view_blocks_.end(); it++) {
        view_blocks.push_back(it->second);
    }
    return Status::kSuccess;
}

Status ViewBlockChain::GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    GetAll(view_blocks);
    std::sort(view_blocks.begin(), view_blocks.end(), [](const std::shared_ptr<ViewBlock>& a, const std::shared_ptr<ViewBlock>& b) {
        return a->view < b->view;
    });
    return Status::kSuccess;
}

// 剪掉从上次 prune_height 到 height 之间，latest_committed 之前的所有分叉，并返回这些分叉上的 blocks
Status ViewBlockChain::PruneTo(const HashStr& target_hash, std::vector<std::shared_ptr<ViewBlock>>& forked_blockes, bool include_history) {
    std::shared_ptr<ViewBlock> current = nullptr;
    Get(target_hash, current);
    if (!current) {
        return Status::kError;
    }

    auto target_block = current;
    
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

    if (include_history) {
        PruneHistoryTo(target_block);
    }

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

Status ViewBlockChain::PruneHistoryTo(const std::shared_ptr<ViewBlock>& target_block) {
    if (!target_block) {
        return Status::kError;
    }

    auto current = target_block;
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current) {
        s = Get(current->parent_hash, current);
        DeleteViewBlock(current);
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
        view_block_qc_map_.erase(hash);
    } catch (...) {
        view_block_children_[view_block->parent_hash] = original_child_blocks;
        view_blocks_at_height_[view_block->view] = original_blocks_at_height;
        throw;
    }
    
    return Status::kSuccess;
}

bool ViewBlockChain::IsValid() {
    if (Size() == 0) {
        return false;
    }

    // 有且只有一个节点不存在父节点
    uint32_t num = 0;
    for (auto it = view_blocks_.begin(); it != view_blocks_.end(); it++) {
        auto& vb = it->second;
        std::shared_ptr<ViewBlock> parent = nullptr;
        Get(vb->parent_hash, parent);
        if (parent == nullptr) {
            num++;
        }
    }

    return num == 1;
}

void ViewBlockChain::PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent) const {
    std::cout << indent << block->view << ":"
              << common::Encode::HexEncode(block->hash).c_str() << "[status]:"
              << static_cast<int>(GetViewBlockStatus(block)) << "\n";
    auto childrenIt = view_block_children_.find(block->hash);
    if (childrenIt != view_block_children_.end()) {
        std::string childIndent = indent + "  ";
        for (const auto& child : childrenIt->second) {
            std::cout << indent << "|\n";
            std::cout << indent << "+--";
            PrintBlock(child, childIndent);
        }
    }
}

void ViewBlockChain::Print() const {
    auto it = view_blocks_at_height_.find(GetMinHeight());
    if (it != view_blocks_at_height_.end()) {
        PrintBlock(it->second[0]);
    }
}

std::shared_ptr<ViewBlock> GetGenesisViewBlock(const std::shared_ptr<db::Db>& db, uint32_t pool_index) {
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    uint32_t sharding_id = common::GlobalInfo::Instance()->network_id();

    block::protobuf::Block block;
    bool r = prefix_db->GetBlockWithHeight(sharding_id, pool_index, 0, &block);
    if (!r) {
        ZJC_ERROR("no genesis block found");
        return nullptr;
    }

    auto block_ptr = std::make_shared<block::protobuf::Block>(block);
    return std::make_shared<ViewBlock>("", GetQCWrappedByGenesis(), block_ptr, GenesisView, 0);
}

std::shared_ptr<QC> GetQCWrappedByGenesis() {
    return std::make_shared<QC>(nullptr, BeforeGenesisView, "");
}

std::shared_ptr<QC> GetGenesisQC(const HashStr& genesis_view_block_hash) {
    return std::make_shared<QC>(
            std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::zero()),
            GenesisView,
            genesis_view_block_hash);
}

} // namespace hotstuff

} // namespace shardora

