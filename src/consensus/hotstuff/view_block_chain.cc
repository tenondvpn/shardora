#include <algorithm>
#include <iostream>

#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

ViewBlockChain::ViewBlockChain(
        uint32_t pool_idx, 
        std::shared_ptr<db::Db>& db, 
        std::shared_ptr<block::AccountManager> account_mgr) : 
        db_(db), pool_index_(pool_idx), account_mgr_(account_mgr) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceMapPtr balane_map_ptr) {
    if (!network::IsSameToLocalShard(view_block->qc().network_id())) {
        return Status::kSuccess;
    }
    
    if (Has(view_block->qc().view_block_hash())) {
        ZJC_ERROR("view block already stored, hash: %s, view: %lu, propose_debug: %s",
            common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view(),
            view_block->debug().c_str());        
        return Status::kSuccess;
    }

    if (!network::IsSameToLocalShard(network::kRootCongressNetworkId) && balane_map_ptr == nullptr) {
        balane_map_ptr = std::make_shared<BalanceMap>();
        for (uint32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
            auto& tx = view_block->block_info().tx_list(i);
            if (tx.balance() == 0) {
                continue;
            }

            auto& addr = account_mgr_->GetTxValidAddress(tx);
            (*balane_map_ptr)[addr] = tx.balance();
        }
    }

#ifndef NDEBUG
    if (balane_map_ptr && balane_map_ptr->size() > 0) {
        for (auto iter = balane_map_ptr->begin(); iter != balane_map_ptr->end(); ++iter) {
            ZJC_DEBUG("store view block %u_%u_%lu, height: %lu, from: %s, balance: %lu, propose_debug: %s", 
                view_block->qc().network_id(), 
                view_block->qc().pool_index(), 
                view_block->qc().view(), 
                view_block->block_info().height(), 
                common::Encode::HexEncode(iter->first).c_str(), 
                iter->second,
                view_block->debug().c_str());
        }
    }
#endif

    ZJC_DEBUG("merge prev all balance store size: %u, propose_debug: %s, "
        "%u_%u_%lu, %lu, hash: %s, prehash: %s",
        balane_map_ptr ? balane_map_ptr->size() : 0, view_block->debug().c_str(),
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), view_block->block_info().height(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str());
    auto block_info_ptr = GetViewBlockInfo(view_block, balane_map_ptr);
    auto& view_block_at_height_vec = view_blocks_at_height_[view_block->qc().view()];
    if (!view_block_at_height_vec.empty()) {
        for (auto iter = view_block_at_height_vec.begin(); iter != view_block_at_height_vec.end();) {
            if ((*iter)->qc().has_sign_x()) {
                ZJC_DEBUG("invalid view has much more view block: %lu, "
                    "count: %u, %u_%u_%lu, %lu hash: %s, , %u_%u_%lu, %lu new block hash: %s", 
                    view_block->qc().view(),
                    view_block_at_height_vec.size(),
                    (*iter)->qc().network_id(),
                    (*iter)->qc().pool_index(),
                    (*iter)->qc().view(),
                    (*iter)->block_info().height(),
                    common::Encode::HexEncode((*iter)->qc().view_block_hash()).c_str(),
                    view_block->qc().network_id(),
                    view_block->qc().pool_index(),
                    view_block->qc().view(),
                    view_block->block_info().height(),
                    common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str());
                assert(false);
                ++iter;
            } else {
                iter = view_block_at_height_vec.erase(iter);
            }
        }
    }

    if (!start_block_) {
        start_block_ = view_block;
        //view_blocks_[view_block->hash] = view_block;
        SetViewBlockToMap(block_info_ptr);
        view_block_at_height_vec.push_back(view_block);
#ifndef NDEBUG
    if (view_block_at_height_vec.size() > 1) {
        ZJC_DEBUG("invalid view has much more view block: %lu, count: %u", 
            view_block->qc().view(),
            view_block_at_height_vec.size());
        assert(view_block_at_height_vec.size() == 1);
    }
#endif
        prune_height_ = view_block->qc().view();
        return Status::kSuccess;
    }

    // 当 view_block 是 start_block_ 的父块，允许添加
    if (start_block_->parent_hash() == view_block->qc().view_block_hash()) {
        SetViewBlockToMap(block_info_ptr);
        view_block_at_height_vec.push_back(view_block);
#ifndef NDEBUG
    if (view_block_at_height_vec.size() > 1) {
        ZJC_DEBUG("invalid view has much more view block: %lu, count: %u", 
            view_block->qc().view(),
            view_block_at_height_vec.size());
        assert(view_block_at_height_vec.size() == 1);
    }
#endif
        AddChildrenToMap(start_block_);
        // 更新 start_block_
        start_block_ = view_block;
        return Status::kSuccess;
    }
    
    if (!directly_store) {
        // 父块必须存在
        auto it = view_blocks_info_.find(view_block->parent_hash());
        if (it == view_blocks_info_.end() || it->second->view_block == nullptr) {
            if (latest_committed_block_ == nullptr ||
                    latest_committed_block_->qc().view_block_hash() != view_block->parent_hash()) {
                ZJC_ERROR("lack of parent view block, hash: %s, parent hash: %s, cur view: %lu, pool: %u",
                    common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
                    common::Encode::HexEncode(view_block->parent_hash()).c_str(),
                    view_block->qc().view(), pool_index_);
                assert(false);
                return Status::kLackOfParentBlock;
            }
        }
    }

    // 如果有 qc，则 qc 指向的块必须存在
    // if (view_block->has_qc() && !view_block->qc().view_block_hash().empty() && !QCRef(*view_block)) {
    //     ZJC_ERROR("view block qc error, hash: %s, view: %lu",
    //         common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view());        
    //     return Status::kError;
    // }
    SetViewBlockToMap(block_info_ptr);
    view_block_at_height_vec.push_back(view_block);

#ifndef NDEBUG
    if (view_block_at_height_vec.size() > 1) {
        ZJC_DEBUG("invalid view has much more view block: %lu, count: %u", 
            view_block->qc().view(),
            view_block_at_height_vec.size());
        assert(view_block_at_height_vec.size() == 1);
    }
#endif

    AddChildrenToMap(view_block);
    ZJC_DEBUG("success add block info hash: %s, parent hash: %s, %u_%u_%lu, propose_debug: %s", 
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), view_block->debug().c_str());
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> ViewBlockChain::Get(const HashStr &hash) {
    auto it = view_blocks_info_.find(hash);
    if (it != view_blocks_info_.end()) {
        // ZJC_DEBUG("get view block from store propose_debug: %s",
        //     it->second->view_block->debug().c_str());
        return it->second->view_block;
    }

    if (latest_committed_block_ && latest_committed_block_->qc().view_block_hash() == hash) {
        ZJC_DEBUG("now use latest commited block: %s, %u_%u_%lu, height: %lu",
            common::Encode::HexEncode(hash).c_str(),
            latest_committed_block_->qc().network_id(),
            latest_committed_block_->qc().pool_index(),
            latest_committed_block_->qc().view(),
            latest_committed_block_->block_info().height());
        return latest_committed_block_;
    }

    return nullptr;    
}

bool ViewBlockChain::Has(const HashStr& hash) {
    auto it = view_blocks_info_.find(hash);
    if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
        return true;
    }

    // if (prefix_db_->BlockExists(hash)) {
    //     return true;
    // }

    return false;
}

bool ViewBlockChain::Extends(const ViewBlock& block, const ViewBlock& target) {
    if (!target.qc().has_view_block_hash()) {
        assert(false);
        return true;
    }

    auto* tmp_block = &block;
    Status s = Status::kSuccess;
    std::shared_ptr<ViewBlock> parent_block = nullptr;
    while (tmp_block->qc().view() > target.qc().view()) {
        parent_block = Get(tmp_block->parent_hash());
        if (parent_block == nullptr) {
            break;
        }

        tmp_block = &(*parent_block);
    }

    return s == Status::kSuccess && tmp_block->qc().view_block_hash() == target.qc().view_block_hash();
}

Status ViewBlockChain::GetAll(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
        }
    }
    return Status::kSuccess;
}

Status ViewBlockChain::GetAllVerified(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
        }
    }
    return Status::kSuccess;    
}

Status ViewBlockChain::GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    GetAll(view_blocks);
    std::sort(view_blocks.begin(), view_blocks.end(), [](
            const std::shared_ptr<ViewBlock>& a, 
            const std::shared_ptr<ViewBlock>& b) {
        return a->qc().view() < b->qc().view();
    });
    return Status::kSuccess;
}

// 获取某个 hash 所有的子块（不同 level）
Status ViewBlockChain::GetRecursiveChildren(HashStr hash, std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    std::vector<std::shared_ptr<ViewBlock>> level_children;
    Status s = GetChildren(hash, level_children);
    if (s != Status::kSuccess) {
        return s;
    }

    if (level_children.empty()) {
        return Status::kSuccess;
    }
    
    for (const auto& vb : level_children) {
        view_blocks.push_back(vb);
        Status s = GetRecursiveChildren(vb->qc().view_block_hash(), view_blocks);
        if (s != Status::kSuccess) {
            return s;
        }
    }
    return Status::kSuccess;
}

// 剪掉从上次 prune_height 到 height 之间，latest_committed 之前的所有分叉，并返回这些分叉上的 blocks
Status ViewBlockChain::PruneTo(
        const HashStr& target_hash, 
        std::vector<std::shared_ptr<ViewBlock>>& forked_blockes, 
        bool include_history) {
    std::shared_ptr<ViewBlock> current = Get(target_hash);
    if (!current) {
        return Status::kError;
    }

    auto target_block = current;
    
    auto target_height = current->qc().view();
    if (prune_height_ >= target_height) {
        return Status::kSuccess;
    }
    
    std::unordered_set<HashStr> hashes_of_branch;
    hashes_of_branch.insert(current->qc().view_block_hash());
    
    Status s = Status::kSuccess;
    while (s == Status::kSuccess && current->qc().view() > prune_height_) {
        current = Get(current->parent_hash());
        if (current) {
            hashes_of_branch.insert(current->qc().view_block_hash());
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

Status ViewBlockChain::PruneFromBlockToTargetHash(
        const std::shared_ptr<ViewBlock>& view_block, 
        const std::unordered_set<HashStr>& hashes_of_branch, 
        std::vector<std::shared_ptr<ViewBlock>>& forked_blocks, 
        const HashStr& target_hash) {
    if (view_block->qc().view_block_hash() == target_hash) {
        return Status::kSuccess;
    }
    
    std::vector<std::shared_ptr<ViewBlock>> child_blocks;
    GetChildren(view_block->qc().view_block_hash(), child_blocks);

    if (child_blocks.empty()) {
        return Status::kSuccess;
    }

    for (auto child_iter = child_blocks.begin(); child_iter < child_blocks.end(); child_iter++) {
        // delete the view block that is not on the same branch
        if (hashes_of_branch.find((*child_iter)->qc().view_block_hash()) == hashes_of_branch.end()) {
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
        current = Get(current->parent_hash());
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
    ZJC_DEBUG("del view block: %s view: %lu, hash: %s",
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
        view_block->qc().view(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str());
    auto original_child_blocks = std::vector<std::shared_ptr<ViewBlock>>();
    auto childIt = view_blocks_info_.find(view_block->parent_hash());
    if (childIt != view_blocks_info_.end()) {
        original_child_blocks = view_blocks_info_[view_block->parent_hash()]->children;
    }

    auto original_blocks_at_height = view_blocks_at_height_[view_block->qc().view()];
    auto& hash = view_block->qc().view_block_hash();
    auto view = view_block->qc().view();
    try {
        auto it = view_blocks_info_.find(view_block->parent_hash());
        if (it != view_blocks_info_.end() && !it->second->children.empty()) {
            auto& child_blocks = it->second->children;
            child_blocks.erase(std::remove_if(child_blocks.begin(), child_blocks.end(),
                    [&hash](const std::shared_ptr<ViewBlock>& item) { return item->qc().view_block_hash() == hash; }),
                child_blocks.end());            
        }

        auto& blocks = view_blocks_at_height_[view];
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
            [&hash](const std::shared_ptr<ViewBlock>& item) { return item->qc().view_block_hash() == hash; }),
            blocks.end());
        if (blocks.size() == 0) {
            view_blocks_at_height_.erase(view);
        }

        view_blocks_info_.erase(hash);
        ZJC_DEBUG("delete view block %u_%u_%lu, height: %lu, %s", 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view(), 
            view_block->block_info().height(),
            common::Encode::HexEncode(hash).c_str());
    } catch (std::exception& e) {
        ZJC_ERROR("del view block error %s", e.what());
        if (!original_child_blocks.empty()) {
            view_blocks_info_[view_block->parent_hash()]->children = original_child_blocks;
        }

        view_blocks_at_height_[view_block->qc().view()] = original_blocks_at_height;
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
        parent = Get(vb->parent_hash());
        if (parent == nullptr) {
            num++;
        }
    }    

    return num == 1;
}

void ViewBlockChain::PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent) const {
    std::cout << indent << block->qc().view() << ":"
              << common::Encode::HexEncode(block->qc().view_block_hash()).c_str() << "[status]:"
              << static_cast<int>(GetViewBlockStatus(block)) << "[txs]:" 
              << block->block_info().tx_list_size() << "\n";
    auto childrenIt = view_blocks_info_.find(block->qc().view_block_hash());
    if (childrenIt != view_blocks_info_.end()) {
        std::string childIndent = indent + "  ";
        for (const auto& child : childrenIt->second->children) {
            std::cout << indent << "|\n";
            std::cout << indent << "+--";
            PrintBlock(child, childIndent);
        }
    }
}

void ViewBlockChain::Print() const { PrintBlock(start_block_); }

std::string ViewBlockChain::String() const {
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;

    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
        }
    }

    std::sort(view_blocks.begin(), view_blocks.end(), [](const std::shared_ptr<ViewBlock>& a, const std::shared_ptr<ViewBlock>& b) {
        return a->qc().view() < b->qc().view();
    });

    std::string ret;
    for (const auto& vb : view_blocks) {
        ret += "," + std::to_string(vb->qc().view());
    }

    return ret;
}

// 获取 db 中最新块的信息和它的 QC
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block) {
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

    // 获取 block 对应的 view_block 所打包的 qc 信息，如果没有，说明是创世块
    View view = GenesisView;
    uint32_t leader_idx = 0;
    HashStr parent_hash = "";
    auto& pb_view_block = *view_block;
    auto r = prefix_db->GetBlockWithHeight(
        sharding_id, 
        pool_index, 
        pool_info.height(), 
        &pb_view_block);
    if (!r) {
        ZJC_DEBUG("failed get genesis block net: %u, pool: %u, height: %lu",
            sharding_id, pool_index, pool_info.height());
        assert(false);
        return Status::kError;
    }

    ZJC_DEBUG("pool: %d, latest vb from db2, hash: %s, view: %lu, "
        "leader: %d, parent_hash: %s, sign x: %s, sign y: %s",
        pool_index,
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        pb_view_block.qc().view(), pb_view_block.qc().leader_idx(),
        common::Encode::HexEncode(pb_view_block.parent_hash()).c_str(),
        common::Encode::HexEncode(view_block->qc().sign_x()).c_str(),
        common::Encode::HexEncode(view_block->qc().sign_y()).c_str());    
    
    return Status::kSuccess;
}

void GetQCWrappedByGenesis(uint32_t pool_index, QC* qc) {
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id > network::kConsensusShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    QC& qc_item = *qc;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_index);
    qc_item.set_view(BeforeGenesisView);
    qc_item.set_view_block_hash("");
    qc_item.set_elect_height(1);
    qc_item.set_leader_idx(0);
}

} // namespace hotstuff

} // namespace shardora

