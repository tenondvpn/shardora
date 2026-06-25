#include <algorithm>
#include <iostream>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/log.h"
#include "consensus/hotstuff/block_acceptor.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/hotstuff/types.h"
#include "protos/pools.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"

namespace shardora {

namespace hotstuff {

namespace {

int CompareTxNonceResult(
        uint64_t tx_nonce,
        uint64_t account_nonce,
        uint64_t* now_nonce,
        const std::string& addr,
        const std::string& parent_hash) {
    *now_nonce = account_nonce;
    if (account_nonce + 1 != tx_nonce) {
        if (account_nonce >= tx_nonce) {
            SHARDORA_DEBUG("discard failed check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s",
                common::Encode::HexEncode(addr).c_str(),
                tx_nonce,
                account_nonce,
                common::Encode::HexEncode(parent_hash).c_str());
            return 3;
        }

        SHARDORA_INFO("failed check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s",
            common::Encode::HexEncode(addr).c_str(),
            tx_nonce,
            account_nonce,
            common::Encode::HexEncode(parent_hash).c_str());
        return account_nonce + 1 > tx_nonce ? 1 : -1;
    }

#ifndef NDEBUG
    SHARDORA_DEBUG("success check tx nonce not exists in db: %s, %lu, db nonce: %lu, phash: %s",
        common::Encode::HexEncode(addr).c_str(),
        tx_nonce,
        account_nonce,
        common::Encode::HexEncode(parent_hash).c_str());
#endif
    return 0;
}

}  // namespace

ViewBlockChain::ViewBlockChain() {}

void ViewBlockChain::Init(
        ChainType chain_type,
        uint32_t pool_index, 
        std::shared_ptr<db::Db> db, 
        std::shared_ptr<block::BlockManager> block_mgr,
        std::shared_ptr<block::AccountManager> account_mgr, 
        std::shared_ptr<sync::KeyValueSync> kv_sync,
        std::shared_ptr<IBlockAcceptor> block_acceptor,
        std::shared_ptr<pools::TxPoolManager> pools_mgr,
        consensus::BlockCacheCallback new_block_cache_callback) {
    chain_type_ = chain_type;
    db_ = db;
    pool_index_ = pool_index;
    block_mgr_ = block_mgr;
    account_mgr_ = account_mgr;
    kv_sync_ = kv_sync;
    block_acceptor_ = block_acceptor;
    pools_mgr_ = pools_mgr;
    new_block_cache_callback_ = new_block_cache_callback;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    // Recover high_view_block_ from DB if it was persisted before restart
    RecoverHighViewBlock();
}

ViewBlockChain::~ViewBlockChain(){}

Status ViewBlockChain::Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceAndNonceMapPtr balane_map_ptr,
        std::shared_ptr<shardoravm::ShardorahainHost> shardora_host_ptr,
        bool init) {
    // CheckThreadIdValid();
    if (chain_type_ == kLocalChain && !network::IsSameToLocalShard(view_block->qc().network_id())) {
        return Status::kSuccess;
    }

    if (!init && BlockHeightCommited(
            prefix_db_, 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height())) {
        return Status::kSuccess;
    }

#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(view_block->debug());
#endif

    if (Has(view_block->qc().view_block_hash())) {
#ifndef NDEBUG
        SHARDORA_DEBUG("view block already stored, hash: %s, view: %lu, propose_debug: %s",
            common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
            view_block->qc().view(),
            ProtobufToJson(cons_debug).c_str());        
#endif
        return Status::kSuccess;
    }

    if (shardora_host_ptr == nullptr) {
        shardora_host_ptr = std::make_shared<shardoravm::ShardorahainHost>();
    }

    if (chain_type_ == kLocalChain && balane_map_ptr == nullptr) {
        balane_map_ptr = std::make_shared<BalanceAndNonceMap>();
        for (int32_t i = 0; i < view_block->block_info().address_array_size(); ++i) {
            auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>(
                view_block->block_info().address_array(i));
            prefix_db_->AddAddressInfo(new_addr_info->addr(), *new_addr_info, shardora_host_ptr->db_batch_);
            (*balane_map_ptr)[new_addr_info->addr()] = new_addr_info;
            SHARDORA_DEBUG("step: %d, success add addr: %s, value: %s", 
                0,
                common::Encode::HexEncode(new_addr_info->addr()).c_str(), 
                ProtobufToJson(*new_addr_info).c_str());
        }


        for (int32_t i = 0; i < view_block->block_info().key_value_array_size(); ++i) {
            auto key = view_block->block_info().key_value_array(i).addr() + 
                view_block->block_info().key_value_array(i).key();
            prefix_db_->SaveTemporaryKv(
                key, 
                view_block->block_info().key_value_array(i).SerializeAsString(), 
                shardora_host_ptr->db_batch_);
            shardora_host_ptr->SaveKeyValue(
                view_block->block_info().key_value_array(i).addr(),
                view_block->block_info().key_value_array(i).key(), 
                view_block->block_info().key_value_array(i).value());
            SHARDORA_DEBUG("addr: %s, success add key: %s, value: %s", 
                common::Encode::HexEncode(view_block->block_info().key_value_array(i).addr()).c_str(), 
                common::Encode::HexEncode(view_block->block_info().key_value_array(i).key()).c_str(), 
                common::Encode::HexEncode(view_block->block_info().key_value_array(i).value()).c_str());
        }

        for (int32_t i = 0; i < view_block->block_info().joins_size(); i++) {
            auto& join_info = view_block->block_info().joins(i);
            security::Ecdsa ecdsa;
            auto addr = ecdsa.GetAddress(join_info.public_key());
            prefix_db_->SaveNodeVerificationVector(
                addr,
                join_info,
                shardora_host_ptr->db_batch_);
#ifndef NDEBUG
            auto n = common::GlobalInfo::Instance()->each_shard_max_members();
            auto t = common::GetSignerCount(n);
            //assert(join_info.g2_req().verify_vec_size() >= t);
#endif
            prefix_db_->AddBlsVerifyG2(addr, join_info.g2_req(), shardora_host_ptr->db_batch_);
        }
    }

#ifndef NDEBUG
    SHARDORA_DEBUG("merge prev all balance store size: %u, propose_debug: %s, "
        "%u_%u_%lu, %lu, hash: %s, prehash: %s",
        balane_map_ptr ? balane_map_ptr->size() : 0, ProtobufToJson(cons_debug).c_str(),
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), view_block->block_info().height(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str());
#endif
    auto block_info_ptr = GetViewBlockInfo(view_block, balane_map_ptr, shardora_host_ptr);
    if (!start_block_) {
        start_block_ = view_block;
        SetViewBlockToMap(block_info_ptr);
        return Status::kSuccess;
    }

    // When view_block is the parent block of start_block_, it is allowed to be added
    if (start_block_->parent_hash() == view_block->qc().view_block_hash()) {
        SetViewBlockToMap(block_info_ptr);
        // update start_block_
        start_block_ = view_block;
        return Status::kSuccess;
    }
    
    if (!directly_store) {
        // The parent block must exist
        auto it = view_blocks_info_.find(view_block->parent_hash());
        if (it == view_blocks_info_.end() || it->second->view_block == nullptr) {
            auto tmp_latest_committed_block = LatestCommittedBlock();
            if (tmp_latest_committed_block == nullptr ||
                    tmp_latest_committed_block->qc().view_block_hash() != view_block->parent_hash()) {
                SHARDORA_ERROR("lack of parent view block, hash: %s, parent hash: %s, cur view: %lu, pool: %u",
                    common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
                    common::Encode::HexEncode(view_block->parent_hash()).c_str(),
                    view_block->qc().view(), pool_index_);
                //assert(false);
                return Status::kLackOfParentBlock;
            }
        }
    }

    // If there is a qc, the block pointed to by qc must exist
    // if (view_block->has_qc() && !view_block->qc().view_block_hash().empty() && !QCRef(*view_block)) {
    //     SHARDORA_ERROR("view block qc error, hash: %s, view: %lu",
    //         common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view());        
    //     return Status::kError;
    // }
    SetViewBlockToMap(block_info_ptr);
#ifndef NDEBUG
    SHARDORA_DEBUG("success add block info hash: %s, parent hash: %s, %u_%u_%lu, propose_debug: %s", 
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), ProtobufToJson(cons_debug).c_str());
#endif
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> ViewBlockChain::GetViewBlockWithHeight(
        uint32_t network_id, 
        uint64_t height) {
    if (height == 0) {
        return nullptr;
    }

    std::shared_ptr<ViewBlockInfo> view_block_ptr;
    if (latest_commited_height_lru_map_.Get(
            BlockHeightKey(network_id, pool_index_, height), 
            view_block_ptr)) {
        return view_block_ptr->view_block;
    }

    auto latest_view_block = high_view_block_;
    if (latest_view_block && latest_view_block->block_info().height() == height) {
        return latest_view_block;
    }
    
    view_block_ptr = std::make_shared<ViewBlockInfo>();
    view_block_ptr->view_block = std::make_shared<ViewBlock>();
    auto& view_block = *view_block_ptr->view_block;
    if (prefix_db_->GetBlockWithHeight(network_id, pool_index_, height, &view_block)) {
        SHARDORA_DEBUG("success add view block remove add %u_%u_%lu_%lu", 
            view_block.qc().network_id(), 
            view_block.qc().pool_index(), 
            view_block.block_info().height(),
            view_block.qc().view());
        latest_commited_hash_lru_map_.Put(
            view_block_ptr->view_block->qc().view_block_hash(), 
            view_block_ptr);
        latest_commited_height_lru_map_.Put(
            BlockHeightKey(
                view_block_ptr->view_block->qc().network_id(), 
                view_block_ptr->view_block->qc().pool_index(), 
                view_block.block_info().height()), 
            view_block_ptr);
        return view_block_ptr->view_block;
    }

    return nullptr;
}

std::shared_ptr<ViewBlock> ViewBlockChain::GetWithHeight(
        uint32_t network_id,
        uint64_t height) {
    if (height == 0) {
        return nullptr;
    }

    // 1. Check cached_view_with_blocks_ (no queue drain)
    for (auto& [view, blocks] : cached_view_with_blocks_) {
        for (auto& info : blocks) {
            if (info && info->view_block &&
                    info->view_block->block_info().height() == height &&
                    info->view_block->qc().network_id() == network_id) {
                return info->view_block;
            }
        }
    }

    // 2. Check high_view_block_
    auto latest_view_block = high_view_block_;
    if (latest_view_block && latest_view_block->block_info().height() == height) {
        return latest_view_block;
    }

    // 3. Fallback to DB
    auto view_block = std::make_shared<ViewBlock>();
    if (prefix_db_->GetBlockWithHeight(network_id, pool_index_, height, view_block.get())) {
        return view_block;
    }

    return nullptr;
}

std::shared_ptr<ViewBlock> ViewBlockChain::GetViewBlockWithView(
        uint32_t network_id, 
        uint64_t view) {
    // // CheckThreadIdValid();
    if (view == 0) {
        return nullptr;
    }
    
    std::shared_ptr<ViewBlockInfo> view_block_ptr;
    if (latest_commited_view_lru_map_.Get(
            BlockViewKey(network_id, pool_index_, view), 
            view_block_ptr)) {
        return view_block_ptr->view_block;
    }

    auto iter = cached_view_with_blocks_.find(view);
    if (iter != cached_view_with_blocks_.end()) {
        for (auto it = iter->second.begin(); it != iter->second.end(); ) {
            if ((*it)->view_block->qc().sign_x().empty()) {
                ++it;
                continue;
            }

            view_block_ptr = (*it);
            if ((*it)->valid) {
                break;
            }

            auto p_block = GetViewBlockWithHash((*it)->view_block->parent_hash(), false);
            if (!p_block) {
                ++it;
                continue;
            }

            auto gp_block = GetViewBlockWithHash(p_block->view_block->parent_hash(), false);
            if (!gp_block) {
                ++it;
                continue;
            }

            if (gp_block->valid) {
                break;
            }
                
            ++it;
        }

        if (iter->second.empty()) {
            cached_view_with_blocks_.erase(iter);
        }

        if (view_block_ptr) {
            if (view_block_ptr->valid) {
                latest_commited_hash_lru_map_.Put(
                    view_block_ptr->view_block->qc().view_block_hash(), 
                    view_block_ptr);
                latest_commited_view_lru_map_.Put(
                    BlockViewKey(
                        view_block_ptr->view_block->qc().network_id(), 
                        view_block_ptr->view_block->qc().pool_index(), 
                        view_block_ptr->view_block->qc().view()), 
                    view_block_ptr);
                return view_block_ptr->view_block;
            }

            auto latest_commited_block = LatestCommittedBlock();
            if (view_block_ptr->view_block->qc().view() > latest_commited_block->qc().view()) {
                return view_block_ptr->view_block;
            }
        }
    }

    return nullptr;   
}

void ViewBlockChain::DrainCachedBlockQueue() {
    GetViewBlockWithHash("", true);
}

std::shared_ptr<ViewBlockInfo> ViewBlockChain::GetViewBlockWithHash(const HashStr& hash, bool remove) {
    // // CheckThreadIdValid();
    std::shared_ptr<ViewBlockInfo> view_block_info_ptr;
    while (remove && cached_block_queue_.pop(&view_block_info_ptr)) {
        if (!view_block_info_ptr || !view_block_info_ptr->view_block) {
            continue;
        }
        cached_block_map_[view_block_info_ptr->view_block->qc().view_block_hash()] = view_block_info_ptr;
        cached_pri_queue_.push(view_block_info_ptr);
        cached_view_with_blocks_[view_block_info_ptr->view_block->qc().view()].push_back(view_block_info_ptr);
        if (view_block_info_ptr->valid) {
            latest_commited_hash_lru_map_.Put(
                view_block_info_ptr->view_block->qc().view_block_hash(), 
                view_block_info_ptr);
            latest_commited_height_lru_map_.Put(
                BlockHeightKey(view_block_info_ptr->view_block->qc().network_id(), 
                    view_block_info_ptr->view_block->qc().pool_index(),
                    view_block_info_ptr->view_block->block_info().height()), 
                view_block_info_ptr);
            latest_commited_view_lru_map_.Put(
                BlockViewKey(
                    view_block_info_ptr->view_block->qc().network_id(), 
                    view_block_info_ptr->view_block->qc().pool_index(), 
                    view_block_info_ptr->view_block->qc().view()), 
                view_block_info_ptr);
        }
    }

    while (remove && cached_pri_queue_.size() >= kCachedViewBlockCount) {
        auto temp_ptr = cached_pri_queue_.top();
        auto temp_iter = cached_block_map_.find(temp_ptr->view_block->qc().view_block_hash());
        if (temp_iter != cached_block_map_.end()) {
            cached_block_map_.erase(temp_iter);
        }

        auto iter= cached_view_with_blocks_.begin();
        while (iter != cached_view_with_blocks_.end()) {
            if (iter->first <= temp_ptr->view_block->qc().view()) {
                iter = cached_view_with_blocks_.erase(iter);
            } else {
                ++iter;
            }
        }

        cached_pri_queue_.pop();
    }

    if (hash.empty()) {
        return nullptr;
    }

    std::shared_ptr<ViewBlockInfo> view_block_ptr;
    if (latest_commited_hash_lru_map_.Get(hash, view_block_ptr)) {
        return view_block_ptr;
    }

    auto iter = cached_block_map_.find(hash);
    if (iter != cached_block_map_.end()) {
        view_block_ptr = iter->second;
    }

    if (view_block_ptr) {
        return view_block_ptr;
    }

    SHARDORA_DEBUG("now get block with hash from db.");
    view_block_ptr = std::make_shared<ViewBlockInfo>();
    view_block_ptr->view_block = std::make_shared<ViewBlock>();
    auto& view_block = *view_block_ptr->view_block;
    if (prefix_db_->GetBlock(hash, &view_block)) {
        SHARDORA_DEBUG("1 success add view block remove add %u_%u_%lu", 
            view_block.qc().network_id(), 
            view_block.qc().pool_index(), 
            view_block.qc().view());
        view_block_ptr->valid = true;
        latest_commited_hash_lru_map_.Put(
            view_block_ptr->view_block->qc().view_block_hash(), 
            view_block_ptr);
        latest_commited_view_lru_map_.Put(
            BlockViewKey(
                view_block_ptr->view_block->qc().network_id(), 
                view_block_ptr->view_block->qc().pool_index(), 
                view_block_ptr->view_block->qc().view()), 
            view_block_ptr);
        return view_block_ptr;
    }

    return nullptr;    
}

std::shared_ptr<ViewBlockInfo> ViewBlockChain::Get(const HashStr &hash) const {
    // CheckThreadIdValid();
    auto it = view_blocks_info_.find(hash);
    if (it != view_blocks_info_.end()) {
        auto view_block_info_ptr = it->second;
        if (view_block_info_ptr->view_block) {
            auto& view_block = *view_block_info_ptr->view_block;
            // SHARDORA_DEBUG("get block hash: %s, view block hash: %s, %u_%u_%lu, sign x: %s, parent hash: %s",
            //     common::Encode::HexEncode(hash).c_str(), 
            //     common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
            //     view_block.qc().network_id(),
            //     view_block.qc().pool_index(),
            //     view_block.qc().view(),
            //     common::Encode::HexEncode(view_block.qc().sign_x()).c_str(),
            //     common::Encode::HexEncode(view_block.parent_hash()).c_str());
            if (view_block.qc().view_block_hash() != hash) {
                SHARDORA_DEBUG("bug 2 get block hash: %s, view block hash: %s, %u_%u_%lu, sign x: %s, parent hash: %s",
                    common::Encode::HexEncode(hash).c_str(), 
                    common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                    view_block.qc().network_id(),
                    view_block.qc().pool_index(),
                    view_block.qc().view(),
                    common::Encode::HexEncode(view_block.qc().sign_x()).c_str(),
                    common::Encode::HexEncode(view_block.parent_hash()).c_str());
                //assert(false);
            }
            
            return view_block_info_ptr;
        }
    }

    return nullptr;
}

bool ViewBlockChain::ReplaceWithSyncedBlock(std::shared_ptr<ViewBlock>& view_block) {
    // CheckThreadIdValid();
    auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
    if (it != view_blocks_info_.end() && 
            it->second->view_block != nullptr && 
            !it->second->view_block->qc().sign_x().empty()) {
        SHARDORA_DEBUG("block hash exists %u_%u_%lu, height: %lu",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view(), 
            view_block->block_info().height());
        return false;
    }

    // FIX: Store first, then erase old entries only on success.
    // Previously we erased before storing, so if Store() failed (e.g. missing parent),
    // the block was lost from both memory maps with no way to recover.
    auto old_it = view_blocks_info_.end();
    if (it != view_blocks_info_.end()) {
        old_it = it;
    }
    auto old_view_iter = view_with_blocks_.find(view_block->qc().view());

    auto st = Store(view_block, true, nullptr, nullptr, false);
    if (st != Status::kSuccess) {
        SHARDORA_ERROR("ReplaceWithSyncedBlock Store failed, hash: %s, %u_%u_%lu, height: %lu, status: %d",
            common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view(), 
            view_block->block_info().height(),
            (int32_t)st);
        // Do NOT erase old entries — keep whatever we had before.
        return false;
    }

    // Store succeeded. Now safe to clean up old entries if they still point to stale data.
    // Note: Store() via SetViewBlockToMap() already inserted the new entry, so the old
    // iterator may be invalidated. Only erase view_with_blocks_ for the old view if needed.
    if (old_view_iter != view_with_blocks_.end()) {
        // Only erase if the view_with_blocks_ entry still points to the old block
        // (Store may have already replaced it)
        auto current_view_iter = view_with_blocks_.find(view_block->qc().view());
        if (current_view_iter != view_with_blocks_.end() && 
                current_view_iter->second->view_block &&
                current_view_iter->second->view_block->qc().view_block_hash() != view_block->qc().view_block_hash()) {
            view_with_blocks_.erase(current_view_iter);
        }
    }

    SHARDORA_DEBUG("add new block hash: %s, %u_%u_%lu, height: %lu",
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        view_block->qc().network_id(), 
        view_block->qc().pool_index(), 
        view_block->qc().view(), 
        view_block->block_info().height());
    return true;
}

bool ViewBlockChain::Has(const HashStr& hash) {
    // CheckThreadIdValid();
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
        //assert(false);
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
    // CheckThreadIdValid();
    for (auto it = view_blocks_info_.begin(); it != view_blocks_info_.end(); it++) {
        if (it->second->view_block) {
            view_blocks.push_back(it->second->view_block);
        }
    }
    return Status::kSuccess;
}

Status ViewBlockChain::GetAllVerified(std::vector<std::shared_ptr<ViewBlock>>& view_blocks) {
    // CheckThreadIdValid();
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

void ViewBlockChain::CommitSynced(std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block) {
    // not this sharding
    auto shardora_host_ptr = std::make_shared<shardoravm::ShardorahainHost>();
    new_block_cache_callback_(view_block, shardora_host_ptr->db_batch_);
    auto block_info_ptr = GetViewBlockInfo(view_block, nullptr, shardora_host_ptr);
    AddNewBlock(view_block, shardora_host_ptr->db_batch_);
    if (!db_->Put(shardora_host_ptr->db_batch_).ok()) {
        SHARDORA_FATAL("write to db failed!");
    }

    block_mgr_->ConsensusAddBlock(block_info_ptr);
}

void ViewBlockChain::Commit(const std::shared_ptr<ViewBlockInfo>& v_block_info) {
    // CheckThreadIdValid();
    std::list<std::shared_ptr<ViewBlockInfo>> to_commit_blocks;
    std::shared_ptr<ViewBlockInfo> tmp_block_info = v_block_info;
    while (tmp_block_info != nullptr) {
        auto tmp_block = tmp_block_info->view_block;
        SHARDORA_DEBUG("pool: %d, prepare commit view block %u_%u_%lu_%lu, hash: %s, "
            "parent hash: %s, step: %d, statistic_height: %lu, commited: %d, sign empty: %d", 
            pool_index_,
            tmp_block_info->view_block->qc().network_id(), 
            tmp_block_info->view_block->qc().pool_index(), 
            tmp_block_info->view_block->block_info().height(),
            tmp_block_info->view_block->qc().view(),
            common::Encode::HexEncode(tmp_block_info->view_block->qc().view_block_hash()).c_str(),
            common::Encode::HexEncode(tmp_block_info->view_block->parent_hash()).c_str(),
            tmp_block_info->view_block->block_info().tx_list_size() > 0 ? tmp_block_info->view_block->block_info().tx_list(0).step(): -1,
            0,
            BlockHeightCommited(
                prefix_db_,
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(),
                tmp_block->block_info().height()),
            tmp_block->qc().sign_x().empty());
        if (!BlockHeightCommited(
                prefix_db_,
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(),
                tmp_block->block_info().height()) &&
                !tmp_block->qc().sign_x().empty()) {
            SHARDORA_DEBUG("add to commit list view block %u_%u_%lu_%lu, hash: %s",
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(), 
                tmp_block->block_info().height(),
                tmp_block->qc().view(),
                common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str());
            to_commit_blocks.push_front(tmp_block_info);
        } else {
            SHARDORA_DEBUG("view block already commited %u_%u_%lu_%lu, hash: %s",
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(), 
                tmp_block->block_info().height(),
                tmp_block->qc().view(),
                common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str());
        }

        if (tmp_block->qc().sign_x().empty()) {
            if (tmp_block->qc().view() > 0 && !BlockHeightCommited(
                    prefix_db_,
                    tmp_block->qc().network_id(), 
                    tmp_block->qc().pool_index(),
                    tmp_block->block_info().height())) {
                SHARDORA_DEBUG("lack of qc block, add sync view hash: %s, %u_%u_%lu_%lu",
                    common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str(),
                    tmp_block->qc().network_id(), 
                    tmp_block->qc().pool_index(), 
                    tmp_block->block_info().height(),
                    tmp_block->qc().view());
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
            if (latest_committed_block && latest_committed_block->qc().view() < tmp_block->qc().view() - 1) {
                if (tmp_block->qc().view() > 0 && !BlockHeightCommited(
                        prefix_db_,
                        tmp_block->qc().network_id(), 
                        tmp_block->qc().pool_index(), 
                        tmp_block->block_info().height() - 1)) {
                    SHARDORA_DEBUG("lack of qc block, add sync view hash: %s, %u_%u_%lu_%lu",
                        common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str(),
                        tmp_block->qc().network_id(), 
                        tmp_block->qc().pool_index(), 
                        tmp_block->block_info().height(),
                        tmp_block->qc().view());
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
        SHARDORA_DEBUG("now commit view block %u_%u_%lu_%lu, hash: %s, "
            "parent hash: %s, step: %d, statistic_height: %lu, tx size: %u", 
            tmp_block->qc().network_id(), 
            tmp_block->qc().pool_index(), 
            tmp_block->block_info().height(),
            tmp_block->qc().view(),
            common::Encode::HexEncode(tmp_block->qc().view_block_hash()).c_str(),
            common::Encode::HexEncode(tmp_block->parent_hash()).c_str(),
            tmp_block->block_info().tx_list_size() > 0 ? tmp_block->block_info().tx_list(0).step(): -1,
            0,
            tmp_block->block_info().tx_list_size());
        //assert((*iter)->shardora_host_ptr);
        auto& db_batch = (*iter)->shardora_host_ptr->db_batch_;
        new_block_cache_callback_(tmp_block, db_batch);
        if (tmp_block->qc().view() > commited_max_view_) {
            commited_max_view_ = tmp_block->qc().view();
        }

        AddNewBlock(tmp_block, db_batch);
        (*iter)->valid = true;
        // Always update the LRU from address_array in the committed block.
        // address_array contains post-execution balances/nonces written by DoTransactions.
        // acc_balance_map_ptr holds pre-execution copies from addTxsToPool and must NOT
        // be used here — it would overwrite correct post-execution state with stale data.
        for (int32_t ai = 0; ai < tmp_block->block_info().address_array_size(); ++ai) {
            auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>(
                tmp_block->block_info().address_array(ai));
            auto acc_ptr = account_lru_map_.get(new_addr_info->addr());
            if (!acc_ptr ||
                    acc_ptr->latest_height() < new_addr_info->latest_height() ||
                    (acc_ptr->latest_height() == new_addr_info->latest_height() &&
                     acc_ptr->tx_index() < new_addr_info->tx_index())) {
                account_lru_map_.insert(new_addr_info);
#ifndef NDEBUG
                SHARDORA_DEBUG("success update address: %s,balance: %lu, nonce: %lu, new balance: %lu, new nonce: %lu, "
                    "latest height: %lu, tx index: %u, new latest height: %lu, new tx index: %u",
                    common::Encode::HexEncode(new_addr_info->addr()).c_str(),
                    acc_ptr != nullptr ? acc_ptr->balance() : 0,
                    acc_ptr != nullptr ? acc_ptr->nonce() : 0,
                    new_addr_info->balance(),
                    new_addr_info->nonce(),
                    acc_ptr != nullptr ? acc_ptr->latest_height() : 0,
                    acc_ptr != nullptr ? acc_ptr->tx_index() : 0,
                    new_addr_info->latest_height(),
                    new_addr_info->tx_index());
#endif
            }
        }

        for (int32_t i = 0; i < tmp_block->block_info().unique_hashs_size(); ++i) {
            prefix_db_->SaveOverUniqueHash(tmp_block->block_info().unique_hashs(i), db_batch);
        }

        // Clean up view_with_blocks_ for the parent view before erasing from view_blocks_info_
        // FIX: Only erase parent block from view_blocks_info_ if the parent's height is also
        // committed. Previously we unconditionally erased the parent, which could remove blocks
        // still needed by pending sync operations or MergeAllPrevBalanceMap() chain walks.
        auto b_tm = common::TimeUtils::TimestampMs();
        {
            auto parent_info = Get(tmp_block->parent_hash());
            if (parent_info && parent_info->view_block) {
                auto parent_height = parent_info->view_block->block_info().height();
                if (BlockHeightCommited(
                        prefix_db_,
                        parent_info->view_block->qc().network_id(),
                        parent_info->view_block->qc().pool_index(),
                        parent_height)) {
                    view_with_blocks_.erase(parent_info->view_block->qc().view());
                    view_blocks_info_.erase(tmp_block->parent_hash());
                }
            }
        }
        if (BlockHeightCommited(
                prefix_db_,
                tmp_block->qc().network_id(), 
                tmp_block->qc().pool_index(),
                tmp_block->block_info().height() + 1)) {
            view_with_blocks_.erase(tmp_block->qc().view());
            view_blocks_info_.erase(tmp_block->qc().view_block_hash());
        }

        if (block_acceptor_) {
            block_acceptor_->CalculateTps(tmp_block->block_info().tx_list_size());
        }

        commited_view_.insert(tmp_block->qc().view());
        if (commited_view_.size() >= 102400u) {
            commited_view_.erase(commited_view_.begin());
        }

        // Fix: Persist pool_latest_info on every block commit so that on restart,
        // the pacemaker initializes with the correct view instead of a stale one.
        // Previously, SaveLatestPoolInfo was only called during genesis/initial sync,
        // so after restart pool_latest_info.view() was 0 or very old, causing
        // "propose view not match leader view" errors on all pools.
        SHARDORA_DEBUG("persist pool_latest_info for %u_%u_%lu, use time: %lu ms", 
            tmp_block->qc().network_id(),
            tmp_block->qc().pool_index(),
            tmp_block->qc().view(),
            common::TimeUtils::TimestampMs() - b_tm);
        {
            pools::protobuf::PoolLatestInfo pool_info;
            pool_info.set_height(tmp_block->block_info().height());
            pool_info.set_hash(tmp_block->qc().view_block_hash());
            pool_info.set_timestamp(tmp_block->block_info().timestamp());
            pool_info.set_view(tmp_block->qc().view());
            db::DbWriteBatch pool_info_batch;
            prefix_db_->SaveLatestPoolInfo(
                tmp_block->qc().network_id(),
                tmp_block->qc().pool_index(),
                pool_info,
                pool_info_batch);
            if (!db_->Put(pool_info_batch).ok()) {
                SHARDORA_ERROR("failed to persist pool_latest_info for %u_%u_%lu",
                    tmp_block->qc().network_id(),
                    tmp_block->qc().pool_index(),
                    tmp_block->qc().view());
            }
        }

        SHARDORA_DEBUG("success SaveLatestPoolInfo %u_%u_%lu_%lu, use time: %lu ms",
            tmp_block->qc().network_id(), 
            tmp_block->qc().pool_index(), 
            tmp_block->qc().view(), 
            tmp_block->block_info().height(),
            (common::TimeUtils::TimestampMs() - b_tm));
// #ifndef NDEBUG
//         for (auto iter = db_batch.data_map_.begin(); iter != db_batch.data_map_.end(); ++iter) {
//             if (memcmp(iter->first.c_str(), protos::kAddressPrefix.c_str(), protos::kAddressPrefix.size()) == 0) {
//                 address::protobuf::AddressInfo addr_info;
//                 if (!addr_info.ParseFromString(iter->second)) {
//                     //assert(false);
//                 }

//                 SHARDORA_DEBUG("new addr commit %u_%u_%lu, success update addr: %s, balance: %lu, nonce: %lu",
//                     tmp_block->qc().network_id(), 
//                     tmp_block->qc().pool_index(), 
//                     tmp_block->qc().view(),
//                     common::Encode::HexEncode(addr_info.addr()).c_str(),
//                     addr_info.balance(),
//                     addr_info.nonce());
//             }
//         }
// #endif
        const auto db_batch_bytes = db_batch.ApproximateSize();
        const auto db_put_begin_ms = common::TimeUtils::TimestampMs();
        if (!db_->Put(db_batch).ok()) {
            SHARDORA_FATAL("write to db failed!");
        }

        SHARDORA_DEBUG("commit block to db success %u_%u_%lu_%lu, batch_bytes: %lu, "
            "db_put_ms: %lu, use time: %lu ms",
            tmp_block->qc().network_id(),
            tmp_block->qc().pool_index(),
            tmp_block->qc().view(),
            tmp_block->block_info().height(),
            db_batch_bytes,
            common::TimeUtils::TimestampMs() - db_put_begin_ms,
            common::TimeUtils::TimestampMs() - b_tm);
        if (pools_mgr_) {
            const auto txover_begin_ms = common::TimeUtils::TimestampMs();
            pools_mgr_->TxOver(pool_index_, *tmp_block);
            const auto txover_ms = common::TimeUtils::TimestampMs() - txover_begin_ms;
            if (txover_ms >= 100lu ||
                    tmp_block->block_info().tx_list_size() >= 128) {
                SHARDORA_DEBUG("commit TxOver %u_%u_%lu, txs: %d, txover_ms: %lu",
                    tmp_block->qc().network_id(),
                    tmp_block->qc().pool_index(),
                    tmp_block->block_info().height(),
                    tmp_block->block_info().tx_list_size(),
                    txover_ms);
            }
        }

        SHARDORA_DEBUG("add block to block manager %u_%u_%lu_%lu, use time: %lu ms", 
            tmp_block->qc().network_id(), 
            tmp_block->qc().pool_index(), 
            tmp_block->qc().view(), 
            tmp_block->block_info().height(),
            (common::TimeUtils::TimestampMs() - b_tm));
        block_mgr_->ConsensusAddBlock(*iter);
        SHARDORA_DEBUG("success commit view block %u_%u_%lu_%lu, use time: %lu ms", 
            tmp_block->qc().network_id(), 
            tmp_block->qc().pool_index(), 
            tmp_block->qc().view(), 
            tmp_block->block_info().height(),
            common::TimeUtils::TimestampMs() - b_tm);
        stored_to_db_view_ = tmp_block->qc().view();
        latest_commited_block = *iter;
    }
    
    if (latest_commited_block) {
        SetLatestCommittedBlock(latest_commited_block);
    }
    // std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    // auto v_block = v_block_info->view_block;
// #ifndef NDEBUG
//     transport::protobuf::ConsensusDebug cons_debug3;
//     cons_debug3.ParseFromString(v_block->debug());
//     SHARDORA_DEBUG("success commit view block %u_%u_%lu, "
//         "height: %lu, now chain: %s, propose_debug: %s",
//         v_block->qc().network_id(), 
//         v_block->qc().pool_index(), 
//         v_block->qc().view(), 
//         v_block->block_info().height(),
//         String().c_str(),
//         ProtobufToJson(cons_debug3).c_str());
// #endif
}

void ViewBlockChain::HandleTimerMessage() {
    // CheckThreadIdValid();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_check_timeout_blocks_ms_ + 3000u > now_tm_ms) { 
        return;
    }

    prev_check_timeout_blocks_ms_ = now_tm_ms;

    if (view_with_blocks_.size() <= 1) {
        return;
    }

    SHARDORA_DEBUG("network: %d, pool: %d, now check view_with_blocks_ size: %d, "
        "view_blocks_info_ size: %lu", 
        view_with_blocks_.begin()->second->view_block->qc().network_id(),
        pool_index_, view_with_blocks_.size(), view_blocks_info_.size());
    for (auto iter = view_blocks_info_.begin(); iter != view_blocks_info_.end(); ) {
        auto view_block = iter->second->view_block;
        if (view_block) {
            if (BlockHeightCommited(
                    prefix_db_,
                    view_block->qc().network_id(), 
                    view_block->qc().pool_index(),
                    view_block->block_info().height())) {
                if (BlockHeightCommited(
                        prefix_db_,
                        view_block->qc().network_id(), 
                        view_block->qc().pool_index(),
                        view_block->block_info().height() + 1)) {
                    iter = view_blocks_info_.erase(iter);
                    continue;
                }
            }
        }

        ++iter;
    }

    for (auto iter = view_with_blocks_.rbegin(); iter != view_with_blocks_.rend();) {
        bool commited = false;
        auto view_block = iter->second->view_block;
        if (view_block) {
            bool height_commited = BlockHeightCommited(
                prefix_db_,
                view_block->qc().network_id(), 
                view_block->qc().pool_index(),
                view_block->block_info().height());
            SHARDORA_DEBUG("network: %d, pool: %d, height: %lu, height_commited: %d, "
                "now check view_with_blocks_ size: %d", 
                view_with_blocks_.begin()->second->view_block->qc().network_id(),
                pool_index_, 
                view_block->block_info().height(),
                height_commited,
                view_with_blocks_.size());      
            if (height_commited) {
                auto it_to_erase = std::next(iter).base();
                auto next_valid_forward = view_with_blocks_.erase(it_to_erase);
                iter = std::make_reverse_iterator(next_valid_forward);
                continue;
            }

            auto view_block_ptr = CheckCommit(view_block->qc());
            if (view_block_ptr) {
                Commit(view_block_ptr);
                auto it_to_erase = std::next(iter).base();
                auto next_valid_forward = view_with_blocks_.erase(it_to_erase);
                iter = std::make_reverse_iterator(next_valid_forward);
                commited = true;
                break;
            }
        }

        ++iter;
    }

    // Fallback cleanup: remove view_with_blocks_ entries whose view <= commited_max_view_
    // These are definitely committed and should not linger in memory.
    if (view_with_blocks_.size() > 16) {
        auto committed_view = commited_max_view_.load();
        for (auto it = view_with_blocks_.begin(); it != view_with_blocks_.end(); ) {
            if (it->first <= committed_view) {
                it = view_with_blocks_.erase(it);
            } else {
                break;  // map is ordered by view, no need to continue
            }
        }
    }
}

std::shared_ptr<ViewBlockInfo> ViewBlockChain::CheckCommit(const QC& qc) {
    // fast hotstuff 2 phase commit
    // CheckThreadIdValid();
    if (qc.sign_x().empty()) {
        return nullptr;
    }

    //assert(!qc.view_block_hash().empty());
    auto v_block1_info = Get(qc.view_block_hash());
    if (!v_block1_info || v_block1_info->view_block->qc().view() <= 0llu){
        SHARDORA_DEBUG("pool: %d, Failed get v block 1: %s, %u_%u_%lu",
            pool_index_,
            common::Encode::HexEncode(qc.view_block_hash()).c_str(),
            qc.network_id(), qc.pool_index(), qc.view());
        kv_sync_->AddSyncViewHash(qc.network_id(), qc.pool_index(), qc.view_block_hash(), 0);
        // //assert(false);
        return nullptr;
    }

    if (ViewBlockIsCheckedParentHash(prefix_db_, qc.view_block_hash())) {
        return v_block1_info;
    }

    auto v_block1 = v_block1_info->view_block;
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(v_block1->debug());
    SHARDORA_DEBUG("pool: %d, success get v block 1: %s, %u_%u_%lu, propose_debug: %s",
        pool_index_,
        common::Encode::HexEncode(qc.view_block_hash()).c_str(),
        qc.network_id(), qc.pool_index(), qc.view(), ProtobufToJson(cons_debug).c_str());
#endif
    //assert(v_block1->parent_hash() != qc.view_block_hash());
    auto v_block2_info = Get(v_block1->parent_hash());
    if (!v_block2_info) {
        SHARDORA_DEBUG("pool: %d, Failed get v block 2 block hash: %s, %u_%u_%lu, now chain: %s", 
            pool_index_,
            common::Encode::HexEncode(v_block1->parent_hash()).c_str(), 
            qc.network_id(), 
            qc.pool_index(), 
            v_block1->qc().view() - 1,
            String().c_str());
        if (v_block1->qc().view() > 0 && !BlockHeightCommited(
                prefix_db_,
                v_block1->qc().network_id(),
                v_block1->qc().pool_index(),
                v_block1->block_info().height() - 1)) {
            kv_sync_->AddSyncViewHash(qc.network_id(), qc.pool_index(), v_block1->parent_hash(), 0);
        }
        return nullptr;
    }

    auto v_block2 = v_block2_info->view_block;
    if (v_block2->block_info().height() + 1 != v_block1->block_info().height()) {
        SHARDORA_DEBUG("pool: %d, Failed get v block 2 ref: %s, "
            "v_block2->block_info().height() + 1 != v_block1->block_info().height(): %lu, %lu",
            pool_index_,
            common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
            v_block2->block_info().height(),
            v_block1->block_info().height());
        return nullptr;
    }

    return v_block2_info;
}

void ViewBlockChain::AddNewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_item,
        db::DbWriteBatch& db_batch) {
    //assert(!view_block_item->qc().sign_x().empty());
    auto* block_item = &view_block_item->block_info();
    auto btime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
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
    //assert(view_block_item->qc().elect_height() >= 1);
    prefix_db_->SaveBlock(*view_block_item, db_batch);
    prefix_db_->SaveBlockUserTxs(*view_block_item, db_batch);
    prefix_db_->SaveValidViewBlockParentHash(
        view_block_item->parent_hash(), 
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        db_batch);

    if (block_item->has_elect_block()) {
        prefix_db_->SaveLatestElectBlock(block_item->elect_block(), db_batch);
    }

    if (block_item->has_prev_elect_block()) {
        prefix_db_->SaveElectHeightWithBlock(
            block_item->prev_elect_block().shard_network_id(), 
            block_item->prev_elect_block().elect_height(), 
            view_block_item->qc().view_block_hash(), 
            db_batch);
    }
}

bool ViewBlockChain::IsValid() {
    // CheckThreadIdValid();
    if (Size() == 0) {
        return false;
    }

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
            SHARDORA_DEBUG("view block view: %lu, height: %lu, hash: %s, phash: %s, has sign: %d", 
                it->second->view_block->qc().view(),
                it->second->view_block->block_info().height(),
                common::Encode::HexEncode(it->second->view_block->qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(it->second->view_block->parent_hash()).c_str(),
                !it->second->view_block->qc().sign_x().empty());
        }
    }

    if (view_blocks.empty()) {
        return "";
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

    SHARDORA_DEBUG("network: %u, get chain pool: %u, views: %s, all size: %u, block_height_str: %s",
        view_blocks[0]->qc().network_id(),
        pool_index_, ret.c_str(), view_blocks_info_.size(), block_height_str.c_str());
    //assert(height_set.size() < 256);
    return ret;
}

// Get the information of the latest block in db and its QC
Status GetLatestViewBlockFromDb(
        uint32_t sharding_id,
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block) {
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    pools::protobuf::PoolLatestInfo pool_info;
    if (!prefix_db->GetLatestPoolInfo(
            sharding_id,
            pool_index,
            &pool_info)) {
        SHARDORA_DEBUG("failed get genesis block net: %u, pool: %u", sharding_id, pool_index);
        return Status::kError;
    }

    // Get the qc information packaged by the view_block corresponding to the block. If not, it is the genesis block.
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
        SHARDORA_DEBUG("failed get genesis block net: %u, pool: %u, height: %lu",
            sharding_id, pool_index, pool_info.height());
        //assert(false);
        return Status::kError;
    }

    SHARDORA_DEBUG("%u_%u_%lu, pool: %d, latest vb from db2, hash: %s, view: %lu, "
        "leader: %d, parent_hash: %s, sign x: %s, sign y: %s",
        sharding_id, pool_index, pb_view_block.qc().view(),
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
    uint32_t depth = 0;
    const uint32_t kMaxTraversalDepth = 16;
    while (true) {
        if (phash.empty() || depth >= kMaxTraversalDepth) {
            break;
        }

        auto it = view_blocks_info_.find(phash);
        if (it == view_blocks_info_.end()) {
            break;
        }

        if (it->second->view_block->qc().view() <= stored_to_db_view_) {
            break;
        }

        if (it->second->shardora_host_ptr) {
            auto res = it->second->shardora_host_ptr->GetCachedKeyValue(id, key, val);
            if (res == shardoravm::kShardoravmSuccess) {
                return true;
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
        ++depth;
    }

    return false;
}

evmc::bytes32 ViewBlockChain::GetPrevStorageBytes32KeyValue(
        const std::string& parent_hash, 
        const evmc::address& addr,
        const evmc::bytes32& key) {
    // Check cache first — cache values are safe because get_storage()
    // checks accounts_ (current tx writes) and pre_shardora_host_ (same-block writes)
    // before reaching here, so cached values won't shadow uncommitted writes.
    auto* cached = bytes32_storage_cache_.get(addr, key);
    if (cached) {
        return *cached;
    }

    // Traverse view chain
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

        if (it->second->shardora_host_ptr) {
            auto res = it->second->shardora_host_ptr->GetCachedStorage(addr, key);
            if (res) {
                // Cache the result for future lookups
                bytes32_storage_cache_.put(addr, key, res);
                return res;
            }
        }

        if (!it->second->view_block) {
            break;
        }
        
        phash = it->second->view_block->parent_hash();
    }

    evmc::bytes32 tmp_val;
    // Cache the miss result too — an empty bytes32 means "not in view chain, go to DB"
    bytes32_storage_cache_.put(addr, key, tmp_val);
    return tmp_val;
}

void ViewBlockChain::MergeAllPrevBalanceMap(
        const std::string& parent_hash, 
        BalanceAndNonceMap& acc_balance_map) {
    // CheckThreadIdValid();
    std::string phash = parent_hash;
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
                    SHARDORA_DEBUG("merge prev all balance merge prev account balance %s, "
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
        const std::string& parent_hash,
        uint64_t* now_nonce,
        const BalanceAndNonceMap* merged_balance_map) {
    if (merged_balance_map != nullptr) {
        auto iter = merged_balance_map->find(addr);
        if (iter != merged_balance_map->end()) {
            return CompareTxNonceResult(
                nonce, iter->second->nonce(), now_nonce, addr, parent_hash);
        }

        auto addr_info = ChainGetAccountInfo(addr);
        if (addr_info == nullptr) {
            SHARDORA_DEBUG("failed check tx nonce not exists in db: %s, %lu, phash: %s",
                common::Encode::HexEncode(addr).c_str(),
                nonce,
                common::Encode::HexEncode(parent_hash).c_str());
            return -1;
        }

        return CompareTxNonceResult(
            nonce, addr_info->nonce(), now_nonce, addr, parent_hash);
    }

    // CheckThreadIdValid();
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
                return CompareTxNonceResult(
                    nonce, iter->second->nonce(), now_nonce, addr, parent_hash);
            }
        }

        if (!it->second->view_block) {
            return -1;
        }
        
        phash = it->second->view_block->parent_hash();
    }

    auto addr_info = ChainGetAccountInfo(addr);
    if (addr_info == nullptr) {
        SHARDORA_DEBUG("failed check tx nonce not exists in db: %s, %lu, phash: %s", 
            common::Encode::HexEncode(addr).c_str(), 
            nonce,
            common::Encode::HexEncode(parent_hash).c_str());
        return -1;
    }

    return CompareTxNonceResult(
        nonce, addr_info->nonce(), now_nonce, addr, parent_hash);
}

void ViewBlockChain::RecoverHighViewBlock() {
    if (!db_ || !prefix_db_) {
        SHARDORA_DEBUG("db_ or prefix_db_ is null for %u", pool_index_);
        
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id == common::kInvalidUint32) {
        SHARDORA_DEBUG("invalid network id for %u", pool_index_);
        return;
    }

    auto view_block_ptr = std::make_shared<ViewBlock>();
    if (!prefix_db_->GetHighViewBlock(net_id, pool_index_, view_block_ptr.get())) {
        SHARDORA_DEBUG("no persisted high view block for %u_%u", net_id, pool_index_);
        return;
    }

    if (high_view_block_ == nullptr ||
            high_view_block_->qc().view() < view_block_ptr->qc().view()) {
        high_view_block_ = view_block_ptr;
        high_view_block_view_.store(high_view_block_->qc().view());
        SHARDORA_DEBUG("recovered high view block from db: %u_%u_%lu, height: %lu, hash: %s",
            high_view_block_->qc().network_id(),
            pool_index_,
            high_view_block_->qc().view(),
            high_view_block_->block_info().height(),
            common::Encode::HexEncode(high_view_block_->qc().view_block_hash()).c_str());
    } else {
        SHARDORA_DEBUG("high_view_block_ already newer: %lu vs %lu",
            high_view_block_->qc().view(), view_block_ptr->qc().view());
    }
}

void ViewBlockChain::UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item) {
    if (qc_item.view_block_hash().empty()) {
        SHARDORA_DEBUG("qc_item view_block_hash is empty for %u_%u_%lu",
            qc_item.network_id(), qc_item.pool_index(), qc_item.view());
        return;
    }

    auto view_block_ptr_info = Get(qc_item.view_block_hash());
    if (!view_block_ptr_info) {
        view_block_ptr_info = std::make_shared<ViewBlockInfo>();
        view_block_ptr_info->view_block = std::make_shared<ViewBlock>();
        auto& view_block = *view_block_ptr_info->view_block;
        if (!prefix_db_->GetBlock(qc_item.view_block_hash(), &view_block)) {
            SHARDORA_DEBUG("failed get view block %u_%u_%lu, hash: %s",
                qc_item.network_id(), 
                qc_item.pool_index(), 
                qc_item.view(), 
                common::Encode::HexEncode(qc_item.view_block_hash()).c_str());
            kv_sync_->AddSyncViewHash(
                qc_item.network_id(), 
                qc_item.pool_index(), 
                qc_item.view_block_hash(), 
                0);
            // Block not in DB yet (sync pending). Create a placeholder view block
            // from the QC so that BOTH high_view_block_ pointer AND high_view_block_view_
            // advance together. This keeps GetLeader()'s out_view consistent with the
            // network's current view, allowing the backup node to accept proposals
            // and participate in consensus while sync catches up.
            // if (high_view_block_ == nullptr || 
            //         high_view_block_->qc().view() < qc_item.view()) {
            //     auto placeholder = std::make_shared<ViewBlock>();
            //     *placeholder->mutable_qc() = qc_item;
            //     high_view_block_ = placeholder;
            //     high_view_block_view_.store(qc_item.view());
            //     SHARDORA_DEBUG("advanced high_view_block via QC placeholder: %u_%u_%lu, hash: %s",
            //         qc_item.network_id(), qc_item.pool_index(), qc_item.view(),
            //         common::Encode::HexEncode(qc_item.view_block_hash()).c_str());
            // }
            return;
        }

        SetViewBlockToMap(view_block_ptr_info);
    }

    auto view_block_ptr = view_block_ptr_info->view_block;
    if (chain_type_ == kLocalChain && !IsQcTcValid(view_block_ptr->qc())) {
        view_block_ptr->mutable_qc()->set_sign_x(qc_item.sign_x());
        view_block_ptr->mutable_qc()->set_sign_y(qc_item.sign_y());
    }

    if (chain_type_ == kLocalChain) {
        cached_block_queue_.push(view_block_ptr_info);
        SHARDORA_DEBUG("success add view block info cached_block_queue_: %u, %u_%u_%lu_%lu",
            cached_block_queue_.size(), 
            view_block_ptr->qc().network_id(), 
            view_block_ptr->qc().pool_index(), 
            view_block_ptr->qc().view(), 
            view_block_ptr->block_info().height());
    }

    if (high_view_block_ == nullptr || high_view_block_->qc().sign_x().empty() ||
            !high_view_block_->block_info().has_height() ||
            high_view_block_->block_info().height() < view_block_ptr->block_info().height() ||
            high_view_block_->qc().view() < view_block_ptr->qc().view()) {
#ifndef NDEBUG
        if (high_view_block_ != nullptr) {
            SHARDORA_DEBUG("success add update old high view: %lu, high hash: %s, "
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
        SHARDORA_DEBUG("final success add update high hash: %s, "
            "new view: %lu, block: %s, %u_%u_%lu, block tm: %lu, parent hash: %s, tx size: %u ",
            common::Encode::HexEncode(high_view_block_->qc().view_block_hash()).c_str(),
            high_view_block_->qc().view(),
            common::Encode::HexEncode(view_block_ptr->qc().view_block_hash()).c_str(),
            high_view_block_->qc().network_id(),
            high_view_block_->qc().pool_index(),
            high_view_block_->block_info().height(),
            high_view_block_->block_info().timestamp(),
            common::Encode::HexEncode(high_view_block_->parent_hash()).c_str(),
            high_view_block_->block_info().tx_list_size());
        high_view_block_view_.store(high_view_block_->qc().view());
        // Persist high_view_block_ to DB so it can be recovered after restart.
        // Save both the hash pointer AND the block itself, because the block
        // may not have been committed yet (SaveBlock only happens on commit).
        // Without saving the block, RecoverHighViewBlock's GetBlock(hash)
        // fails and the node restarts with a stale view.
        db::DbWriteBatch db_batch;
        prefix_db_->SaveHighViewBlock(
            high_view_block_->qc().network_id(),
            pool_index_,
            high_view_block_->qc().view_block_hash(),
            db_batch);
        auto st = db_->Put(db_batch);
        if (!st.ok()) {
            SHARDORA_ERROR("failed to persist high view block %u_%u_%lu, hash: %s",
                high_view_block_->qc().network_id(),
                pool_index_,
                high_view_block_->qc().view(),
                common::Encode::HexEncode(high_view_block_->qc().view_block_hash()).c_str());
        }
    }
}

protos::AddressInfoPtr ViewBlockChain::ChainGetAccountInfo(const std::string& addr) {
    protos::AddressInfoPtr addr_info = account_lru_map_.get(addr);
    if (addr_info != nullptr) {
        return addr_info;
    }

    auto thread_idx = 0;  // common::GlobalInfo::Instance()->get_thread_index();
    addr_info = account_mgr_->GetAcountInfoFromDb(addr);
    if (!addr_info) {
        BLOCK_DEBUG(
            "get account failed[%s] in thread_idx:%d", 
            common::Encode::HexEncode(addr).c_str(), thread_idx);
    } else {
        // Use atomic get_or_insert to avoid TOCTOU race: another thread
        // may have inserted the same key between our get() above and now.
        addr_info = account_lru_map_.get_or_insert(addr, addr_info);
        SHARDORA_DEBUG("success update address: %s, balance: %lu, nonce: %lu",
            common::Encode::HexEncode(addr_info->addr()).c_str(),
            addr_info->balance(),
            addr_info->nonce());
    }

    return addr_info;
}

void ViewBlockChain::AddPoolStatisticTag(uint64_t height, uint64_t timeblock_addr_nonce) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto tx = msg_ptr->header.mutable_tx_proto();
    auto unique_hash = common::Hash::keccak256("pool_statistic_tag_" + 
        std::to_string(network::GetLocalConsensusNetworkId()) + "_" + 
        std::to_string(pool_index_) + "_" + 
        std::to_string(height));
    tx->set_key(unique_hash);
    char data[8] = {0};
    uint64_t* udata = (uint64_t*)data;
    udata[0] = height;
    tx->set_value(std::string(data, sizeof(data)));
    tx->set_pubkey("");
    tx->set_step(pools::protobuf::kPoolStatisticTag);
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_nonce(timeblock_addr_nonce);
    msg_ptr->address_info = account_mgr_->pools_address_info(
        pools::protobuf::kPoolStatisticTag, pool_index_);
    SHARDORA_DEBUG("check create kPoolStatisticTag nonce: %lu, pool idx: %u, "
        "pool addr: %s, addr get pool: %u, height: %lu, unique_hash: %s",
        tx->nonce(), 
        pool_index_,
        common::Encode::HexEncode(account_mgr_->pool_base_addrs(pools::protobuf::kPoolStatisticTag, pool_index_)).c_str(),
        pool_index_,
        height,
        common::Encode::HexEncode(unique_hash).c_str());
    //assert(msg_ptr->address_info != nullptr);
    tx->set_to(msg_ptr->address_info->addr());
    pools_mgr_->AddPoolMessage(msg_ptr);
    SHARDORA_DEBUG("success create kPoolStatisticTag nonce: %lu, pool idx: %u, "
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
        uint64_t vss_random,
        uint64_t timeblock_addr_nonce) {
    SHARDORA_DEBUG("new timeblock coming: %lu, %lu, lastest_time_block_tm: %lu",
        static_cast<uint64_t>(latest_timeblock_height_), latest_time_block_height, lastest_time_block_tm);
    if (latest_timeblock_height_ < latest_time_block_height) {
        latest_timeblock_height_ = latest_time_block_height;
    }

    if (latest_time_block_height > 1) {
        AddPoolStatisticTag(latest_time_block_height, timeblock_addr_nonce);
    }
}

} // namespace hotstuff

} // namespace shardora
