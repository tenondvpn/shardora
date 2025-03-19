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
        BalanceMapPtr balane_map_ptr,
        std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr,
        bool init) {
    if (!network::IsSameToLocalShard(view_block->qc().network_id())) {
        return Status::kSuccess;
    }

    if (!init && view_commited(view_block->qc().network_id(), view_block->qc().view())) {
        return Status::kSuccess;
    }

#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(view_block->debug());
#endif
    if (Has(view_block->qc().view_block_hash())) {
#ifndef NDEBUG
        ZJC_DEBUG("view block already stored, hash: %s, view: %lu, propose_debug: %s",
            common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view(),
            ProtobufToJson(cons_debug).c_str());        
#endif
        return Status::kSuccess;
    }

    if (!network::IsSameToLocalShard(network::kRootCongressNetworkId) && balane_map_ptr == nullptr) {
        balane_map_ptr = std::make_shared<BalanceMap>();
        for (int32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
            auto& tx = view_block->block_info().tx_list(i);
            if (tx.balance() == 0) {
                continue;
            }

            auto& addr = account_mgr_->GetTxValidAddress(tx);
            (*balane_map_ptr)[addr] = tx.balance();
            
        }
    }

#ifndef NDEBUG
    ZJC_DEBUG("merge prev all balance store size: %u, propose_debug: %s, "
        "%u_%u_%lu, %lu, hash: %s, prehash: %s",
        balane_map_ptr ? balane_map_ptr->size() : 0, ProtobufToJson(cons_debug).c_str(),
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), view_block->block_info().height(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str());
#endif
    auto block_info_ptr = GetViewBlockInfo(view_block, balane_map_ptr, zjc_host_ptr);
    if (!start_block_) {
        start_block_ = view_block;
        SetViewBlockToMap(block_info_ptr);
        return Status::kSuccess;
    }

    // 当 view_block 是 start_block_ 的父块，允许添加
    if (start_block_->parent_hash() == view_block->qc().view_block_hash()) {
        SetViewBlockToMap(block_info_ptr);
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
#ifndef NDEBUG
    ZJC_DEBUG("success add block info hash: %s, parent hash: %s, %u_%u_%lu, propose_debug: %s", 
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), ProtobufToJson(cons_debug).c_str());
#endif
    return Status::kSuccess;
}
std::shared_ptr<ViewBlock> ViewBlockChain::GetViewBlockWithHeight(uint32_t network_id, uint64_t height) {
    std::shared_ptr<ViewBlockInfo> view_block_info_ptr;
    while (commited_block_queue_.pop(&view_block_info_ptr)) {
        commited_block_map_[view_block_info_ptr->view_block->block_info().height()] = view_block_info_ptr;
        commited_pri_queue_.push(view_block_info_ptr->view_block->block_info().height());
    }

    std::shared_ptr<ViewBlock> view_block_ptr;
    auto iter = commited_block_map_.find(height);
    if (iter != commited_block_map_.end()) {
        view_block_ptr = iter->second->view_block;
    }

    if (commited_pri_queue_.size() >= kCachedViewBlockCount) {
        auto temp_height = commited_pri_queue_.top();
        auto temp_iter = commited_block_map_.find(temp_height);
        if (temp_iter != commited_block_map_.end()) {
            commited_block_map_.erase(temp_iter);
        }

        commited_pri_queue_.pop();
    }

    if (network_id == 0) {
        return nullptr;
    }

    if (view_block_ptr) {
        return view_block_ptr;
    }

    ZJC_DEBUG("now get block with height from db.");
    view_block_ptr = std::make_shared<ViewBlock>();
    auto& view_block = *view_block_ptr;
    if (prefix_db_->GetBlockWithHeight(network_id, pool_index_, height, &view_block)) {
        return view_block_ptr;
    }

    return nullptr;
}

std::shared_ptr<ViewBlock> ViewBlockChain::GetViewBlockWithHash(const HashStr& hash) {
    std::shared_ptr<ViewBlockInfo> view_block_info_ptr;
    while (cached_block_queue_.pop(&view_block_info_ptr)) {
        cached_block_map_[view_block_info_ptr->view_block->qc().view_block_hash()] = view_block_info_ptr;
        cached_pri_queue_.push(view_block_info_ptr);
    }

    std::shared_ptr<ViewBlock> view_block_ptr;
    auto iter = cached_block_map_.find(hash);
    if (iter != cached_block_map_.end()) {
        view_block_ptr = iter->second->view_block;
    }

    if (cached_pri_queue_.size() >= kCachedViewBlockCount) {
        auto temp_ptr = cached_pri_queue_.top();
        auto temp_iter = cached_block_map_.find(temp_ptr->view_block->qc().view_block_hash());
        if (temp_iter != cached_block_map_.end()) {
            cached_block_map_.erase(temp_iter);
        }

        cached_pri_queue_.pop();
    }

    if (hash.empty()) {
        return nullptr;
    }

    if (view_block_ptr) {
        return view_block_ptr;
    }

    ZJC_DEBUG("now get block with hash from db.");
    view_block_ptr = std::make_shared<ViewBlock>();
    auto& view_block = *view_block_ptr;
    if (prefix_db_->GetBlock(hash, &view_block)) {
        return view_block_ptr;
    }

    return nullptr;    
}

std::shared_ptr<ViewBlockInfo> ViewBlockChain::Get(const HashStr &hash) {
    auto it = view_blocks_info_.find(hash);
    if (it != view_blocks_info_.end()) {
        // ZJC_DEBUG("get view block from store propose_debug: %s",
        //     it->second->view_block->debug().c_str());
        if (it->second->view_block) {
            ZJC_DEBUG("get block hash: %s, view block hash: %s, %u_%u_%lu, sign x: %s, parent hash: %s",
                common::Encode::HexEncode(hash).c_str(), 
                common::Encode::HexEncode(it->second->view_block->qc().view_block_hash()).c_str(),
                it->second->view_block->qc().network_id(),
                it->second->view_block->qc().pool_index(),
                it->second->view_block->qc().view(),
                common::Encode::HexEncode(it->second->view_block->qc().sign_x()).c_str(),
                common::Encode::HexEncode(it->second->view_block->parent_hash()).c_str());
            assert(it->second->view_block->qc().view_block_hash() == hash);
            return it->second;
        }
    }

    return nullptr;    
}

bool ViewBlockChain::ReplaceWithSyncedBlock(std::shared_ptr<ViewBlock>& view_block) {
    auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
    if (it != view_blocks_info_.end() && 
            it->second->view_block != nullptr && 
            !it->second->view_block->qc().sign_x().empty()) {
        ZJC_DEBUG("");
        return false;
    }

    if (it == view_blocks_info_.end()) {
        return true;
    }
    
    view_blocks_info_.erase(it);
    return true;
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
        auto parent_block_info = Get(tmp_block->parent_hash());
        if (parent_block_info == nullptr) {
            break;
        }

        tmp_block = &(*parent_block_info->view_block);
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


bool ViewBlockChain::ViewBlockIsCheckedParentHash(const std::string& hash) {
    auto iter = valid_parent_block_hash_.find(hash);
    if (iter != valid_parent_block_hash_.end()) {
        return true;
    }

    return prefix_db_->ParentHashExists(hash);
}

void ViewBlockChain::SaveBlockCheckedParentHash(const std::string& hash, uint64_t view) {
    valid_parent_block_hash_[hash] = view;
    View tmp_view;
    while (stored_view_queue_.pop(&tmp_view)) {
        commited_view_.insert(tmp_view);
        if (commited_view_.size() > kCachedViewBlockCount * 10) {
            commited_view_.erase(commited_view_.begin());
        }
    }
}

// 剪掉从上次 prune_height 到 height 之间，latest_committed 之前的所有分叉，并返回这些分叉上的 blocks
Status ViewBlockChain::PruneTo(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes) {
    View tmp_view = 0;
    ZJC_DEBUG("pool: %u, now PruneTo: %lu, view_blocks_info_ size: %u", 
        pool_index_, stored_to_db_view_, view_blocks_info_.size());
    for (auto iter = view_blocks_info_.begin(); iter != view_blocks_info_.end();) {
        if (iter->second->view_block &&
                iter->second->view_block->qc().view() < stored_to_db_view_) {
            if (view_commited(
                    iter->second->view_block->qc().network_id(),
                    iter->second->view_block->qc().view()) || 
                    iter->second->view_block->qc().sign_x().empty()) {
                iter = view_blocks_info_.erase(iter);
                CHECK_MEMORY_SIZE(view_blocks_info_);
            } else {
                ++iter;
            }
        } else {
            ++iter;
        }
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

        auto parent_info = Get(vb->parent_hash());
        if (parent_info == nullptr) {
            num++;
        }
    }    

    return num == 1;
}

std::string ViewBlockChain::String() const {
#ifdef NDEBUG
    return "";
#endif
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
            ZJC_DEBUG("view block view: %lu, height: %lu, hash: %s, phash: %s", 
                it->second->view_block->qc().view(),
                it->second->view_block->block_info().height(),
                common::Encode::HexEncode(it->second->view_block->qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(it->second->view_block->parent_hash()).c_str());
        }
    }

    std::sort(
            view_blocks.begin(), 
            view_blocks.end(), 
            [](const std::shared_ptr<ViewBlock>& a, const std::shared_ptr<ViewBlock>& b) {
        return a->qc().view() < b->qc().view();
    });

    std::string ret;
    std::string block_height_str;
    std::set<uint64_t> height_set;
    std::set<uint64_t> view_set;
    for (const auto& vb : view_blocks) {
        ret += "," + std::to_string(vb->qc().view());
        block_height_str += "," + std::to_string(vb->block_info().height());
        height_set.insert(vb->block_info().height());
        view_set.insert(vb->qc().view());
    }

    ZJC_DEBUG("get chain pool: %u, views: %s, all size: %u, block_height_str: %s",
        pool_index_, ret.c_str(), view_blocks_info_.size(), block_height_str.c_str());
    assert(height_set.size() < 256);
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

bool ViewBlockChain::GetPrevStorageKeyValue(
        const std::string& parent_hash, 
        const std::string& id, 
        const std::string& key, 
        std::string* val) {
    std::string phash = parent_hash;
    // TODO: check valid
    while (true) {
        if (phash.empty()) {
            break;
        }

        // ZJC_DEBUG("now merge prev storage map: %s", common::Encode::HexEncode(phash).c_str());
        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            break;
        }

        ZJC_DEBUG("get cached key value UpdateStoredToDbView %u_%u_%lu, "
            "stored_to_db_view_: %lu, %s%s", 
            3, pool_index_, it->second->view_block->qc().view(), 
            stored_to_db_view_, common::Encode::HexEncode(id).c_str(), 
            common::Encode::HexEncode(key).c_str());
        if (it->second->view_block->qc().view() <= stored_to_db_view_) {
            break;
        }

        if (it->second->zjc_host_ptr) {
            auto res = it->second->zjc_host_ptr->GetCachedKeyValue(id, key, val);
            if (res == zjcvm::kZjcvmSuccess) {
                return true;
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
    }
    return false;
}

evmc::bytes32 ViewBlockChain::GetPrevStorageBytes32KeyValue(
        const std::string& parent_hash, 
        const evmc::address& addr,
        const evmc::bytes32& key) {
    std::string phash = parent_hash;
    // TODO: check valid
    while (true) {
        if (phash.empty()) {
            break;
        }

        // ZJC_DEBUG("now merge prev storage map: %s", common::Encode::HexEncode(phash).c_str());
        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            break;
        }
    
        if (it->second->view_block->qc().view() <= stored_to_db_view_) {
            break;
        }

        if (it->second->zjc_host_ptr) {
            auto res = it->second->zjc_host_ptr->get_cached_storage(addr, key);
            if (res) {
                return res;
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
    }

    evmc::bytes32 tmp_val;
    return tmp_val;
}

bool ViewBlockChain::GetPrevAddressBalance(const std::string& phash, const std::string& address, int64_t* balance) {
    return false;
}

void ViewBlockChain::MergeAllPrevBalanceMap(
        const std::string& parent_hash, 
        BalanceMap& acc_balance_map) {
    std::string phash = parent_hash;
    // TODO: check valid
    uint32_t count = 0;
    while (true) {
        if (phash.empty()) {
            break;
        }

        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            break;
        }

        if (it->second->view_block->qc().view() <= stored_to_db_view_) {
            break;
        }

        if (it->second->acc_balance_map_ptr) {
            auto& prev_acc_balance_map = *it->second->acc_balance_map_ptr;
            for (auto iter = prev_acc_balance_map.begin(); iter != prev_acc_balance_map.end(); ++iter) {
                auto fiter = acc_balance_map.find(iter->first);
                if (fiter == acc_balance_map.end()) {
                    acc_balance_map[iter->first] = iter->second;
                    // ZJC_DEBUG("merge prev all balance merge prev account balance %s: %lu, %u_%u_%lu, block height: %lu",
                    //     common::Encode::HexEncode(iter->first).c_str(), iter->second, 
                    //     it->second->view_block->qc().network_id(), 
                    //     it->second->view_block->qc().pool_index(),
                    //     it->second->view_block->qc().view(),
                    //     it->second->view_block->block_info().height());
                }
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
    }
}

bool ViewBlockChain::CheckTxGidValid(const std::string& gid, const std::string& parent_hash) {
    std::string phash = parent_hash;
    while (true) {
        if (phash.empty()) {
            ZJC_DEBUG("gid phash empty: %s, phash: %s", common::Encode::HexEncode(gid).c_str(), common::Encode::HexEncode(phash).c_str());
            break;
        }

        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            ZJC_DEBUG("gid phash not exist: %s, phash: %s", common::Encode::HexEncode(gid).c_str(), common::Encode::HexEncode(phash).c_str());
            break;
        }

        if (it->second->view_block->qc().view() <= stored_to_db_view_) {
            ZJC_DEBUG("gid phash view invalid: %lu, %lu, %s, phash: %s", it->second->view_block->qc().view(),
                stored_to_db_view_, common::Encode::HexEncode(gid).c_str(), common::Encode::HexEncode(phash).c_str());
            break;
        }

        auto iter = it->second->added_txs.find(gid);
        if (iter != it->second->added_txs.end()) {
            ZJC_DEBUG("failed check tx gid: %s, phash: %s",
                common::Encode::HexEncode(gid).c_str(),
                common::Encode::HexEncode(phash).c_str());
            return false;
        }

        if (!it->second->view_block) {
            return false;
        }
        
        ZJC_DEBUG("gid phash empty: %s, phash: %s, pphash: %s", 
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(phash).c_str(),
            common::Encode::HexEncode(it->second->view_block->parent_hash()).c_str());
        phash = it->second->view_block->parent_hash();
    }

    if (prefix_db_->JustCheckCommitedGidExists(gid)) {
        ZJC_DEBUG("failed check tx gid exists in db: %s", 
            common::Encode::HexEncode(gid).c_str());
        return false;
    }

    ZJC_DEBUG("success check tx gid not exists in db: %s, phash: %s", 
        common::Encode::HexEncode(gid).c_str(), 
        common::Encode::HexEncode(parent_hash).c_str());
    return true;
}

void ViewBlockChain::UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item) {
    auto view_block_ptr_info = Get(qc_item.view_block_hash());
    if (!view_block_ptr_info) {
        ZJC_DEBUG("failed get view block %u_%u_%lu, hash: %s",
            qc_item.network_id(), 
            qc_item.pool_index(), 
            qc_item.view(), 
            common::Encode::HexEncode(qc_item.view_block_hash()).c_str());
        return;
    }

    auto view_block_ptr = view_block_ptr_info->view_block;
    if (!IsQcTcValid(view_block_ptr->qc())) {
        view_block_ptr->mutable_qc()->set_sign_x(qc_item.sign_x());
        view_block_ptr->mutable_qc()->set_sign_y(qc_item.sign_y());
        cached_block_queue_.push(view_block_ptr_info);
    }

    if (high_view_block_ == nullptr ||
            high_view_block_->qc().view() < view_block_ptr->qc().view()) {
#ifndef NDEBUG
        if (high_view_block_ != nullptr) {
            ZJC_DEBUG("success add update old high view: %lu, high hash: %s, "
                "new view: %lu, block: %s, %u_%u_%lu, parent hash: %s, tx size: %u ",
                high_view_block_->qc().view(),
                common::Encode::HexEncode(high_view_block_->qc().view_block_hash()).c_str(),
                view_block_ptr->qc().view(),
                common::Encode::HexEncode(view_block_ptr->qc().view_block_hash()).c_str(),
                view_block_ptr->qc().network_id(),
                view_block_ptr->qc().pool_index(),
                view_block_ptr->block_info().height(),
                common::Encode::HexEncode(view_block_ptr->parent_hash()).c_str(),
                view_block_ptr->block_info().tx_list_size());
        }
#endif
        
        high_view_block_ = view_block_ptr;
        ZJC_DEBUG("final success add update high hash: %s, "
            "new view: %lu, block: %s, %u_%u_%lu, parent hash: %s, tx size: %u ",
            common::Encode::HexEncode(high_view_block_->qc().view_block_hash()).c_str(),
            high_view_block_->qc().view(),
            common::Encode::HexEncode(view_block_ptr->qc().view_block_hash()).c_str(),
            high_view_block_->qc().network_id(),
            high_view_block_->qc().pool_index(),
            high_view_block_->block_info().height(),
            common::Encode::HexEncode(high_view_block_->parent_hash()).c_str(),
            high_view_block_->block_info().tx_list_size());
    }
}

} // namespace hotstuff

} // namespace shardora

