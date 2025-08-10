#include <algorithm>
#include <iostream>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/log.h"
#include "consensus/hotstuff/block_acceptor.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/hotstuff/types.h"
#include "protos/block.pb.h"

namespace shardora {

namespace hotstuff {

ViewBlockChain::ViewBlockChain() {}

void ViewBlockChain::Init(
        uint32_t pool_index, 
        std::shared_ptr<db::Db>& db, 
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<block::AccountManager> account_mgr, 
        std::shared_ptr<sync::KeyValueSync> kv_sync,
        std::shared_ptr<IBlockAcceptor> block_acceptor,
        std::shared_ptr<pools::TxPoolManager> pools_mgr,
        consensus::BlockCacheCallback new_block_cache_callback) {
    db_ = db;
    pool_index_ = pool_index;
    block_mgr_ = block_mgr;
    account_mgr_ = account_mgr;
    kv_sync_ = kv_sync;
    block_acceptor_ = block_acceptor;
    pools_mgr_ = pools_mgr;
    new_block_cache_callback_ = new_block_cache_callback;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceAndNonceMapPtr balane_map_ptr,
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
        ZJC_EMPTY_DEBUG("view block already stored, hash: %s, view: %lu, propose_debug: %s",
            common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view(),
            ProtobufToJson(cons_debug).c_str());        
#endif
        return Status::kSuccess;
    }

    if (zjc_host_ptr == nullptr) {
        zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    }

    if (!network::IsSameToLocalShard(network::kRootCongressNetworkId) && balane_map_ptr == nullptr) {
        balane_map_ptr = std::make_shared<BalanceAndNonceMap>();
        for (uint32_t i = 0; i < view_block->block_info().address_array_size(); ++i) {
            auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>(
                view_block->block_info().address_array(i));
            prefix_db_->AddAddressInfo(new_addr_info->addr(), *new_addr_info, zjc_host_ptr->db_batch_);
            (*balane_map_ptr)[new_addr_info->addr()] = new_addr_info;
            ZJC_EMPTY_DEBUG("step: %d, success add addr: %s, value: %s", 
                0,
                common::Encode::HexEncode(new_addr_info->addr()).c_str(), 
                ProtobufToJson(*new_addr_info).c_str());
        }

        for (uint32_t i = 0; i < view_block->block_info().key_value_array_size(); ++i) {
            auto key = view_block->block_info().key_value_array(i).addr() + 
                view_block->block_info().key_value_array(i).key();
            prefix_db_->SaveTemporaryKv(
                key, 
                view_block->block_info().key_value_array(i).value(), 
                zjc_host_ptr->db_batch_);
        }
    }

#ifndef NDEBUG
    ZJC_EMPTY_DEBUG("merge prev all balance store size: %u, propose_debug: %s, "
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
    ZJC_EMPTY_DEBUG("success add block info hash: %s, parent hash: %s, %u_%u_%lu, propose_debug: %s", 
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
        CHECK_MEMORY_SIZE(commited_block_map_);
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

    ZJC_EMPTY_DEBUG("now get block with height from db.");
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
        CHECK_MEMORY_SIZE(cached_block_map_);
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

    ZJC_EMPTY_DEBUG("now get block with hash from db.");
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
        // ZJC_EMPTY_DEBUG("get view block from store propose_debug: %s",
        //     it->second->view_block->debug().c_str());
        if (it->second->view_block) {
            ZJC_EMPTY_DEBUG("get block hash: %s, view block hash: %s, %u_%u_%lu, sign x: %s, parent hash: %s",
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
        ZJC_EMPTY_DEBUG("");
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
    return prefix_db_->ParentHashExists(hash);
}

void ViewBlockChain::CommitSynced(std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block) {
    // not this sharding
    auto zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    new_block_cache_callback_(view_block, zjc_host_ptr->db_batch_);
    auto block_info_ptr = GetViewBlockInfo(view_block, nullptr, zjc_host_ptr);
    AddNewBlock(view_block, zjc_host_ptr->db_batch_);
    if (!db_->Put(zjc_host_ptr->db_batch_).ok()) {
        ZJC_FATAL("write to db failed!");
    }

    block_mgr_->ConsensusAddBlock(block_info_ptr);
}

void ViewBlockChain::Commit(const std::shared_ptr<ViewBlockInfo>& v_block_info) {
    std::list<std::shared_ptr<ViewBlockInfo>> to_commit_blocks;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::shared_ptr<ViewBlockInfo> tmp_block_info = v_block_info;
    while (tmp_block_info != nullptr) {
        auto tmp_block = tmp_block_info->view_block;
        if (!view_commited(
                tmp_block->qc().network_id(), 
                tmp_block->qc().view()) &&
                !tmp_block->qc().sign_x().empty()) {
            to_commit_blocks.push_front(tmp_block_info);
        }

        if (tmp_block->qc().sign_x().empty()) {
            if (tmp_block->qc().view() > 0 && !view_commited(
                    tmp_block->qc().network_id(), tmp_block->qc().view())) {
                kv_sync_->AddSyncViewHash(
                    tmp_block->qc().network_id(), 
                    tmp_block->qc().pool_index(), 
                    tmp_block->qc().view_block_hash(), 
                    0);
            }
        }

        auto parent_block_info = Get(tmp_block->parent_hash());
        if (parent_block_info == nullptr) {
            auto latest_committed_block = LatestCommittedBlock();
            if (latest_committed_block->qc().view() < tmp_block->qc().view() - 1) {
                if (tmp_block->qc().view() > 0 && !view_commited(
                        tmp_block->qc().network_id(), tmp_block->qc().view() - 1)) {
                    kv_sync_->AddSyncViewHash(
                        tmp_block->qc().network_id(), 
                        tmp_block->qc().pool_index(), 
                        tmp_block->parent_hash(), 
                        0);
                }
            }

            break;
        }

        tmp_block_info = parent_block_info;
    }

    std::shared_ptr<ViewBlockInfo> latest_commited_block = nullptr; 
    for (auto iter = to_commit_blocks.begin(); iter != to_commit_blocks.end(); ++iter) {
        auto tmp_block = (*iter)->view_block;
        if (tmp_block->block_info().tx_list_size() > 0 && tmp_block->block_info().tx_list(0).step() == 18) {
            ZJC_INFO("now commit view block %u_%u_%lu, hash: %s, parent hash: %s, step: %d, statistic_height: %lu", 
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(), 
                tmp_block->qc().view(),
                common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(tmp_block->parent_hash()).c_str(),
                tmp_block->block_info().tx_list_size() > 0 ? tmp_block->block_info().tx_list(0).step(): -1,
                0);
        } else {
            ZJC_WARN("now commit view block %u_%u_%lu, hash: %s, parent hash: %s, step: %d, statistic_height: %lu", 
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(), 
                tmp_block->qc().view(),
                common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(tmp_block->parent_hash()).c_str(),
                tmp_block->block_info().tx_list_size() > 0 ? tmp_block->block_info().tx_list(0).step(): -1,
                0);
        }
        
        ADD_DEBUG_PROCESS_TIMESTAMP();
        assert((*iter)->zjc_host_ptr);
        auto& db_batch = (*iter)->zjc_host_ptr->db_batch_;
        new_block_cache_callback_(tmp_block, db_batch);
        ADD_DEBUG_PROCESS_TIMESTAMP();
        // SaveBlockCheckedParentHash(
        //     tmp_block->parent_hash(), 
        //     tmp_block->qc().view());
        ADD_DEBUG_PROCESS_TIMESTAMP();
        if (tmp_block->qc().view() > commited_max_view_) {
            commited_max_view_ = tmp_block->qc().view();
        }

        AddNewBlock(tmp_block, db_batch);
        if ((*iter)->acc_balance_map_ptr) {
            for (auto acc_iter = (*iter)->acc_balance_map_ptr->begin(); 
                    acc_iter != (*iter)->acc_balance_map_ptr->end(); ++acc_iter) {
                account_lru_map_.insert(acc_iter->second);
                ZJC_EMPTY_DEBUG("success update address: %s, balance: %lu, nonce: %lu",
                    common::Encode::HexEncode(acc_iter->second->addr()).c_str(),
                    acc_iter->second->balance(),
                    acc_iter->second->nonce());
            }
        }

        view_blocks_info_.erase(tmp_block->parent_hash());
#ifdef TEST_FORKING_ATTACK
        auto test_iter = view_with_blocks_.begin();
        while (test_iter != view_with_blocks_.end()) {
            if (test_iter->first > tmp_block->qc().view()) {
                break;
            }

            test_iter = view_with_blocks_.erase(test_iter);
        }
#endif
        ADD_DEBUG_PROCESS_TIMESTAMP();
        block_acceptor_->CalculateTps(tmp_block->block_info().tx_list_size());
        commited_view_.insert(tmp_block->qc().view());
        if (commited_view_.size() >= 102400u) {
            commited_view_.erase(commited_view_.begin());
        }

// #ifndef NDEBUG
//         for (auto iter = db_batch.data_map_.begin(); iter != db_batch.data_map_.end(); ++iter) {
//             if (memcmp(iter->first.c_str(), protos::kAddressPrefix.c_str(), protos::kAddressPrefix.size()) == 0) {
//                 address::protobuf::AddressInfo addr_info;
//                 if (!addr_info.ParseFromString(iter->second)) {
//                     assert(false);
//                 }

//                 ZJC_EMPTY_DEBUG("new addr commit %u_%u_%lu, success update addr: %s, balance: %lu, nonce: %lu",
//                     tmp_block->qc().network_id(), 
//                     tmp_block->qc().pool_index(), 
//                     tmp_block->qc().view(),
//                     common::Encode::HexEncode(addr_info.addr()).c_str(),
//                     addr_info.balance(),
//                     addr_info.nonce());
//             }
//         }
// #endif
        if (!db_->Put(db_batch).ok()) {
            ZJC_FATAL("write to db failed!");
        }

        pools_mgr_->TxOver(pool_index_, *tmp_block);
        block_mgr_->ConsensusAddBlock(*iter);
        stored_to_db_view_ = tmp_block->qc().view();
        latest_commited_block = *iter;
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (latest_commited_block) {
        SetLatestCommittedBlock(latest_commited_block);
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_check_timeout_blocks_ms_ + 30000u < now_tm_ms) {
        for (auto iter = view_blocks_info_.begin(); iter != view_blocks_info_.end();) {
            if (view_commited(
                    common::GlobalInfo::Instance()->network_id(), 
                    iter->second->view_block->qc().view() + 1)) {
                iter = view_blocks_info_.erase(iter);
            } else {
                ++iter;
            }
        }

        prev_check_timeout_blocks_ms_ = now_tm_ms;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    // auto v_block = v_block_info->view_block;
// #ifndef NDEBUG
//     transport::protobuf::ConsensusDebug cons_debug3;
//     cons_debug3.ParseFromString(v_block->debug());
//     ZJC_EMPTY_DEBUG("success commit view block %u_%u_%lu, "
//         "height: %lu, now chain: %s, propose_debug: %s",
//         v_block->qc().network_id(), 
//         v_block->qc().pool_index(), 
//         v_block->qc().view(), 
//         v_block->block_info().height(),
//         String().c_str(),
//         ProtobufToJson(cons_debug3).c_str());
// #endif
}

void ViewBlockChain::AddNewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_item,
        db::DbWriteBatch& db_batch) {
    assert(!view_block_item->qc().sign_x().empty());
    auto* block_item = &view_block_item->block_info();
    // TODO: check all block saved success
    auto btime = common::TimeUtils::TimestampMs();
    ZJC_EMPTY_DEBUG("new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
        "tx size: %u, hash: %s, prehash: %s, elect height: %lu, tm height: %lu, step: %d, status: %d",
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        block_item->height(),
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block_item->parent_hash()).c_str(),
        view_block_item->qc().elect_height(),
        block_item->timeblock_height(),
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).step() : -1),
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).status() : -1));
    assert(view_block_item->qc().elect_height() >= 1);
    // block 两条信息持久化
    if (!prefix_db_->SaveBlock(*view_block_item, db_batch)) {
        ZJC_EMPTY_DEBUG("block saved: %lu", block_item->height());
        return;
    }

    prefix_db_->SaveValidViewBlockParentHash(
        view_block_item->parent_hash(), 
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        db_batch);
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
            ZJC_EMPTY_DEBUG("view block view: %lu, height: %lu, hash: %s, phash: %s, has sign: %d", 
                it->second->view_block->qc().view(),
                it->second->view_block->block_info().height(),
                common::Encode::HexEncode(it->second->view_block->qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(it->second->view_block->parent_hash()).c_str(),
                !it->second->view_block->qc().sign_x().empty());
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

    ZJC_INFO("get chain pool: %u, views: %s, all size: %u, block_height_str: %s",
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
        ZJC_EMPTY_DEBUG("failed get genesis block net: %u, pool: %u", sharding_id, pool_index);
        return Status::kError;
    }

    // 获取 block 对应的 view_block 所打包的 qc 信息，如果没有，说明是创世块
    View view = pool_info.view();
    uint32_t leader_idx = 0;
    HashStr parent_hash = "";
    auto& pb_view_block = *view_block;
    auto r = prefix_db->GetBlockWithHeight(
        sharding_id, 
        pool_index, 
        pool_info.height(), 
        &pb_view_block);
    if (!r) {
        ZJC_EMPTY_DEBUG("failed get genesis block net: %u, pool: %u, height: %lu",
            sharding_id, pool_index, pool_info.height());
        assert(false);
        return Status::kError;
    }

    ZJC_EMPTY_DEBUG("pool: %d, latest vb from db2, hash: %s, view: %lu, "
        "leader: %d, parent_hash: %s, sign x: %s, sign y: %s",
        pool_index,
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        pb_view_block.qc().view(), pb_view_block.qc().leader_idx(),
        common::Encode::HexEncode(pb_view_block.parent_hash()).c_str(),
        common::Encode::HexEncode(view_block->qc().sign_x()).c_str(),
        common::Encode::HexEncode(view_block->qc().sign_y()).c_str());    
    return Status::kSuccess;
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

        // ZJC_EMPTY_DEBUG("now merge prev storage map: %s", common::Encode::HexEncode(phash).c_str());
        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            break;
        }

        ZJC_EMPTY_DEBUG("get cached key value UpdateStoredToDbView %u_%u_%lu, "
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

        // ZJC_EMPTY_DEBUG("now merge prev storage map: %s", common::Encode::HexEncode(phash).c_str());
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

void ViewBlockChain::MergeAllPrevBalanceMap(
        const std::string& parent_hash, 
        BalanceAndNonceMap& acc_balance_map) {
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
                    acc_balance_map[iter->first] = std::make_shared<address::protobuf::AddressInfo>(*iter->second);
                    ZJC_EMPTY_DEBUG("merge prev all balance merge prev account balance %s, "
                        "balance: %lu, nonce: %lu, %u_%u_%lu, block height: %lu",
                        common::Encode::HexEncode(iter->first).c_str(), 
                        iter->second->balance(), 
                        iter->second->nonce(), 
                        it->second->view_block->qc().network_id(), 
                        it->second->view_block->qc().pool_index(),
                        it->second->view_block->qc().view(),
                        it->second->view_block->block_info().height());
                }
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
    }
}

int ViewBlockChain::CheckTxNonceValid(
        const std::string& addr, 
        uint64_t nonce, 
        const std::string& parent_hash) {
    std::string phash = parent_hash;
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
            auto& tmp_map = *it->second->acc_balance_map_ptr;
            auto iter = tmp_map.find(addr);
            if (iter != tmp_map.end()) {
                if (iter->second->nonce() + 1 != nonce) {
                    ZJC_EMPTY_DEBUG("success check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s", 
                        common::Encode::HexEncode(addr).c_str(), 
                        nonce,
                        iter->second->nonce(),
                        common::Encode::HexEncode(parent_hash).c_str());
                    return iter->second->nonce() + 1 > nonce ? 1 : -1;
                }

                return 0;
            }
        }

        if (!it->second->view_block) {
            return false;
        }
        
        phash = it->second->view_block->parent_hash();
    }

    auto addr_info = ChainGetAccountInfo(addr);
    if (addr_info == nullptr) {
        ZJC_EMPTY_DEBUG("failed check tx nonce not exists in db: %s, %lu, phash: %s", 
            common::Encode::HexEncode(addr).c_str(), 
            nonce,
            common::Encode::HexEncode(parent_hash).c_str());
        return -1;
    }

    if (addr_info->nonce() + 1 != nonce) {
        ZJC_EMPTY_DEBUG("failed check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s", 
            common::Encode::HexEncode(addr).c_str(), 
            nonce,
            addr_info->nonce(),
            common::Encode::HexEncode(parent_hash).c_str());
        return addr_info->nonce() + 1 > nonce ? 1 : -1;
    }

    ZJC_EMPTY_DEBUG("success check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s", 
        common::Encode::HexEncode(addr).c_str(), 
        nonce,
        addr_info->nonce(),
        common::Encode::HexEncode(parent_hash).c_str());
    return 0;
}

void ViewBlockChain::UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item) {
    auto view_block_ptr_info = Get(qc_item.view_block_hash());
    if (!view_block_ptr_info) {
        ZJC_WARN("failed get view block %u_%u_%lu, hash: %s",
            qc_item.network_id(), 
            qc_item.pool_index(), 
            qc_item.view(), 
            common::Encode::HexEncode(qc_item.view_block_hash()).c_str());
        kv_sync_->AddSyncViewHash(
            qc_item.network_id(), 
            qc_item.pool_index(), 
            qc_item.view_block_hash(), 
            0);
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
            ZJC_EMPTY_DEBUG("success add update old high view: %lu, high hash: %s, "
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
        ZJC_EMPTY_DEBUG("final success add update high hash: %s, "
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

protos::AddressInfoPtr ViewBlockChain::ChainGetAccountInfo(const std::string& addr) {
    protos::AddressInfoPtr addr_info = account_lru_map_.get(addr);
    if (addr_info != nullptr && addr_info->type() != address::protobuf::kWaitingRootConfirm) {
        return addr_info;
    }

    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    addr_info = account_mgr_->GetAcountInfoFromDb(addr);
    if (!addr_info || addr_info->type() == address::protobuf::kWaitingRootConfirm) {
        BLOCK_DEBUG(
            "get account failed[%s] in thread_idx:%d", 
            common::Encode::HexEncode(addr).c_str(), thread_idx);
    } else {
        account_lru_map_.insert(addr_info);
        ZJC_EMPTY_DEBUG("success update address: %s, balance: %lu, nonce: %lu",
            common::Encode::HexEncode(addr_info->addr()).c_str(),
            addr_info->balance(),
            addr_info->nonce());
    }

    return addr_info;
}

protos::AddressInfoPtr ViewBlockChain::ChainGetPoolAccountInfo(uint32_t pool_index) {
    auto& addr = account_mgr_->pool_base_addrs(pool_index);
    return ChainGetAccountInfo(addr);
}

void ViewBlockChain::AddPoolStatisticTag(uint64_t height) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->address_info = ChainGetPoolAccountInfo(pool_index_);
    assert(msg_ptr->address_info != nullptr);
    auto tx = msg_ptr->header.mutable_tx_proto();
    auto unique_hash = common::Hash::keccak256("pool_statistic_tag_" + 
        std::to_string(pool_index_) + "_" + 
        std::to_string(height));
    tx->set_key(unique_hash);
    char data[8] = {0};
    uint64_t* udata = (uint64_t*)data;
    udata[0] = height;
    tx->set_value(std::string(data, sizeof(data)));
    tx->set_pubkey("");
    tx->set_to(msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kPoolStatisticTag);
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_nonce(height);
    pools_mgr_->HandleMessage(msg_ptr);
    ZJC_INFO("success create kPoolStatisticTag nonce: %lu, pool idx: %u, "
        "pool addr: %s, addr get pool: %u, height: %lu, unique_hash: %s",
        tx->nonce(), 
        pool_index_,
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
        common::GetAddressPoolIndex(msg_ptr->address_info->addr()),
        height,
        common::Encode::HexEncode(unique_hash).c_str());
}

void ViewBlockChain::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    ZJC_INFO("new timeblock coming: %lu, %lu, lastest_time_block_tm: %lu",
        latest_timeblock_height_, latest_time_block_height, lastest_time_block_tm);
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    if (latest_time_block_height > 1) {
        AddPoolStatisticTag(latest_time_block_height);
    }

    latest_timeblock_height_ = latest_time_block_height;
}

} // namespace hotstuff

} // namespace shardora

