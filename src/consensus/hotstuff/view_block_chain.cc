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

ViewBlockChain::ViewBlockChain(std::shared_ptr<db::Db>& db) : db_(db) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
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
        //view_blocks_[view_block->hash] = view_block;
        SetViewBlockToMap(view_block->hash, view_block);
        
        view_blocks_at_height_[view_block->view].push_back(view_block);
        prune_height_ = view_block->view;
        
        return Status::kSuccess;
    }
    // 父块必须存在
    auto it = view_blocks_info_.find(view_block->parent_hash);
    if (it == view_blocks_info_.end() || it->second->view_block == nullptr) {
        return Status::kError;
    }

    // 如果有 qc，则 qc 指向的块必须存在
    if (view_block->qc && !view_block->qc->view_block_hash.empty() && !QCRef(view_block)) {
        return Status::kError;
    }
    

    SetViewBlockToMap(view_block->hash, view_block);
    view_blocks_at_height_[view_block->view].push_back(view_block);

    ZJC_DEBUG("view_block_height_size: %lu", view_blocks_at_height_.size());
    
    AddChildrenToMap(view_block->parent_hash, view_block);
    SetQcOf(view_block->qc->view_block_hash, view_block->qc);

    return Status::kSuccess;
}


Status ViewBlockChain::Get(const HashStr &hash, std::shared_ptr<ViewBlock> &view_block) {
   auto it = view_blocks_info_.find(hash);
    if (it != view_blocks_info_.end()) {
        view_block = it->second->view_block;
        return Status::kSuccess;
    }

    return Status::kNotFound;    
}

bool ViewBlockChain::Has(const HashStr& hash) {
    auto it = view_blocks_info_.find(hash);
    return (it != view_blocks_info_.end() && it->second->view_block != nullptr);    
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
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
        }
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

    start_block_ = target_block;

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
        if (current) {
            DeleteViewBlock(current);
        }
    }

    return Status::kSuccess;
}

Status ViewBlockChain::GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children) {
    auto it = view_blocks_info_.find(hash);
    if (it == view_blocks_info_.end()) {
        return Status::kSuccess;
    }
    children = it->second->children;
    return Status::kSuccess;    
}

Status ViewBlockChain::DeleteViewBlock(const std::shared_ptr<ViewBlock>& view_block) {
    ZJC_DEBUG("del view block: %s view: %lu", common::Encode::HexEncode(view_block->hash).c_str(), view_block->view);
    auto original_child_blocks = std::vector<std::shared_ptr<ViewBlock>>();
    auto childIt = view_blocks_info_.find(view_block->parent_hash);
    if (childIt != view_blocks_info_.end()) {
        original_child_blocks = view_blocks_info_[view_block->parent_hash]->children;
    }
    auto original_blocks_at_height = view_blocks_at_height_[view_block->view];
    auto hash = view_block->hash;
    auto view = view_block->view;

    try {
        auto it = view_blocks_info_.find(view_block->parent_hash);
        if (it != view_blocks_info_.end() && !it->second->children.empty()) {
            auto& child_blocks = it->second->children;
            child_blocks.erase(std::remove_if(child_blocks.begin(), child_blocks.end(),
                    [&hash](const std::shared_ptr<ViewBlock>& item) { return item->hash == hash; }),
                child_blocks.end());            
        }

        auto& blocks = view_blocks_at_height_[view];
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
            [&hash](const std::shared_ptr<ViewBlock>& item) { return item->hash == hash; }),
            blocks.end());

        view_blocks_info_.erase(hash);
    } catch (std::exception& e) {
        ZJC_ERROR("del view block error %s", e.what());
        if (!original_child_blocks.empty()) {
            view_blocks_info_[view_block->parent_hash]->children = original_child_blocks;
        }
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
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        auto& vb = it->second->view_block;
        if (!vb) {
            continue;
        }
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
              << static_cast<int>(GetViewBlockStatus(block)) << "[txs]:" 
              << block->block->tx_list_size() << "\n";
    auto childrenIt = view_blocks_info_.find(block->hash);
    if (childrenIt != view_blocks_info_.end()) {
        std::string childIndent = indent + "  ";
        for (const auto& child : childrenIt->second->children) {
            std::cout << indent << "|\n";
            std::cout << indent << "+--";
            PrintBlock(child, childIndent);
        }
    }
}

void ViewBlockChain::Print() const {
    PrintBlock(start_block_);
}

// 获取 db 中最新块的信息和它的 QC
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block,
        std::shared_ptr<QC>& self_qc) {
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    uint32_t sharding_id = common::GlobalInfo::Instance()->network_id();
    pools::protobuf::PoolLatestInfo pool_info;
    if (!prefix_db->GetLatestPoolInfo(
            sharding_id,
            pool_index,
            &pool_info)) {
        ZJC_DEBUG("failed get genesis block net: %u, pool: %u", sharding_id, pool_index);
        return Status::kError;
    }

    block::protobuf::Block block;
    bool r = prefix_db->GetBlockWithHeight(sharding_id, pool_index, pool_info.height(), &block);
    if (!r) {
        ZJC_ERROR("no genesis block found");
        return Status::kError;
    }

    // 获取 block 对应的 view_block 所打包的 qc 信息，如果没有，说明是创世块
    auto qc = GetQCWrappedByGenesis();
    View view = GenesisView;
    uint32_t leader_idx = 0;
    HashStr parent_hash = "";
    view_block::protobuf::ViewBlockItem pb_view_block;
    r = prefix_db->GetViewBlockInfo(sharding_id, pool_index, pool_info.height(), &pb_view_block);
    if (r) {
        bool r2 = qc->Unserialize(pb_view_block.qc_str());
        if (!r2) {
            auto qc = GetQCWrappedByGenesis();
        }
        view = pb_view_block.view();
        leader_idx = pb_view_block.leader_idx();
        parent_hash = pb_view_block.parent_hash();
    }

    auto block_ptr = std::make_shared<block::protobuf::Block>(block);
    view_block = std::make_shared<ViewBlock>(parent_hash, qc, block_ptr, view, leader_idx);  
    
    r = self_qc->Unserialize(pb_view_block.self_qc_str());
    if (!r || self_qc->view < GenesisView) {
        self_qc = GetGenesisQC(view_block->hash);
    }

    ZJC_DEBUG("pool: %d, latest vb from db, vb view: %lu, self_qc view: %lu",
        pool_index, view_block->view, self_qc->view);
    return Status::kSuccess;
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

