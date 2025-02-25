#pragma once

#include <limits>
#include <queue>

#include "block/account_manager.h"
#include <common/time_utils.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <protos/prefix_db.h>
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace hotstuff {

// Tree of view blocks, showing the parent-child relationship of view blocks
// Notice: the status of view block is not memorized here.
class ViewBlockChain {
public:    
    ViewBlockChain(uint32_t pool_index, std::shared_ptr<db::Db>& db, std::shared_ptr<block::AccountManager> account_mgr);
    ~ViewBlockChain();
    
    ViewBlockChain(const ViewBlockChain&) = delete;
    ViewBlockChain& operator=(const ViewBlockChain&) = delete;

    void UpdateStoredToDbView(View view) {
        if (stored_to_db_view_ < view) {
            stored_to_db_view_ = view;
        }
    }

    // Add Node
    Status Store(
        const std::shared_ptr<ViewBlock>& view_block, 
        bool directly_store, 
        BalanceMapPtr balane_map_ptr,
        std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr);
    // Get Block by hash value, fetch from neighbor nodes if necessary
    std::shared_ptr<ViewBlockInfo> Get(const HashStr& hash);
    std::shared_ptr<ViewBlock> Get(uint64_t view);
    uint64_t GetMaxHeight() {
        if (latest_committed_block_->has_block_info()) {
            return latest_committed_block_->block_info().height();
        }

        return 0;
    }

    // If has block
    bool Has(const HashStr& hash);
    // if in the same branch
    bool Extends(const ViewBlock& block, const ViewBlock& target);
    
    // prune from last prune height to target view block
    Status PruneTo(std::vector<std::shared_ptr<ViewBlock>>& forked_blockes);

    Status GetAll(std::vector<std::shared_ptr<ViewBlock>>&);
    
    Status GetAllVerified(std::vector<std::shared_ptr<ViewBlock>>&);

    Status GetOrderedAll(std::vector<std::shared_ptr<ViewBlock>>&);

    bool GetPrevStorageKeyValue(
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

    evmc::bytes32 GetPrevStorageBytes32KeyValue(
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

    bool GetPrevAddressBalance(const std::string& phash, const std::string& address, int64_t* balance) {
        return false;
    }

    void MergeAllPrevBalanceMap(
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

    bool CheckTxGidValid(const std::string& gid, const std::string& parent_hash) {
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
            
            phash = it->second->view_block->parent_hash();
        }

        if (prefix_db_->JustCheckCommitedGidExists(gid)) {
            ZJC_DEBUG("failed check tx gid exists in db: %s", 
                common::Encode::HexEncode(gid).c_str());
            return false;
        }

        // ZJC_DEBUG("success check tx gid not exists in db: %s, phash: %s", 
        //     common::Encode::HexEncode(gid).c_str(), 
        //     common::Encode::HexEncode(parent_hash).c_str());
        return true;
    }

    Status GetRecursiveChildren(HashStr, std::vector<std::shared_ptr<ViewBlock>>&);

    inline std::shared_ptr<ViewBlock> LatestCommittedBlock() const {
        return latest_committed_block_;
    }

    inline std::shared_ptr<ViewBlock> LatestLockedBlock() const {
        return latest_locked_block_;
    }

    inline void SetLatestCommittedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        if (latest_committed_block_ &&
                (view_block->qc().network_id() !=
                latest_committed_block_->qc().network_id() ||
                latest_committed_block_->qc().view() >= 
                view_block->qc().view())) {
            return;
        }

        // 允许设置旧的 view block
        ZJC_DEBUG("changed latest commited block %u_%u_%lu, new view: %lu",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height(),
            view_block->qc().view());
        latest_committed_block_ = view_block;
        auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end()) {
            it->second->status = ViewBlockStatus::Committed;
        }
    }

    inline void SetLatestLockedBlock(const std::shared_ptr<ViewBlock>& view_block) {
        auto view_block_status = GetViewBlockStatus(view_block);
        if (view_block_status != ViewBlockStatus::Committed) {
            latest_locked_block_ = view_block;
            auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
            if (it != view_blocks_info_.end()) {
                view_blocks_info_[view_block->qc().view_block_hash()]->status = ViewBlockStatus::Locked;
            }
        }        
    }

    inline ViewBlockStatus GetViewBlockStatus(const std::shared_ptr<ViewBlock>& view_block) const {
        auto it = view_blocks_info_.find(view_block->qc().view_block_hash());
        if (it == view_blocks_info_.end()) {
            return ViewBlockStatus::Unknown;
        }
        return it->second->status;        
    } 

    Status StoreToDb(
            const std::shared_ptr<ViewBlock>& v_block,
            uint64_t test_index,
            std::shared_ptr<db::DbWriteBatch>& db_batch) {        
        // 持久化已经生成 qc 的 ViewBlock
        if (v_block == nullptr) {
            return Status::kInvalidArgument;
        }


        if (!IsQcTcValid(v_block->qc())) {            
            ZJC_DEBUG("not has signature, pool: %u, StoreToDb 0, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
                pool_index_, test_index, v_block->block_info().tx_list_size(),
                v_block->qc().network_id(),
                v_block->qc().pool_index(),
                v_block->qc().view(),
                common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str());
            return Status::kSuccess;
        }

        if (prefix_db_->HasViewBlockInfo(v_block->qc().view_block_hash())) {
            ZJC_DEBUG("has in db, pool: %u, StoreToDb 0, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
                pool_index_, test_index, v_block->block_info().tx_list_size(),
                v_block->qc().network_id(),
                v_block->qc().pool_index(),
                v_block->qc().view(),
                common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str());
            return Status::kSuccess;
        }        
        
        prefix_db_->SaveViewBlockInfo(
            v_block->qc().network_id(),
            v_block->qc().pool_index(),
            v_block->block_info().height(),
            *v_block,
            db_batch);
        ZJC_DEBUG("success pool: %u, StoreToDb 3, test_index: %lu, tx size: %u, %u_%u_%lu, hash: %s",
            pool_index_, test_index, v_block->block_info().tx_list_size(),
            v_block->qc().network_id(),
            v_block->qc().pool_index(),
            v_block->qc().view(),
            common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str());
        return Status::kSuccess;
    }
    
    // If a chain is valid
    bool IsValid();
    inline void Clear() {
        view_blocks_info_.clear();
        prune_height_ = View(1);
        // latest_committed_block_ = nullptr;
        // latest_locked_block_ = nullptr;
        start_block_ = nullptr;        
    }

    inline uint32_t Size() const {
        return view_blocks_info_.size();
    }

    void Print() const;
    void PrintBlock(const std::shared_ptr<ViewBlock>& block, const std::string& indent = "") const;
    std::string String() const;

    std::shared_ptr<ViewBlock> ParentBlock(const ViewBlock& view_block) {
        if (view_block.has_parent_hash()) {
            auto it2 = view_blocks_info_.find(view_block.parent_hash());
            if (it2 == view_blocks_info_.end()) {
                return nullptr;
            }

            return it2->second->view_block;
        }

        return nullptr;
    }

    inline std::shared_ptr<ViewBlock> HighViewBlock() const {
        return high_view_block_;
    }

    inline const QC& HighQC() const {
        return high_view_block_->qc();
    }

    void UpdateHighViewBlock(const view_block::protobuf::QcItem& qc_item) {
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
            auto db_bach = std::make_shared<db::DbWriteBatch>();
            StoreToDb(view_block_ptr, 999999, db_bach);
            auto st = db_->Put(*db_bach);
            if (!st.ok()) {
                ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
            }
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

    void ResetViewBlock(const HashStr& hash) {
        auto it = view_blocks_info_.find(hash);
        if (it != view_blocks_info_.end() && 
                it->second->view_block != nullptr && 
                it->second->view_block->qc().sign_x().empty()) {
            it->second->view_block = nullptr;
        }
    }

private:
    void SetViewBlockToMap(const std::shared_ptr<ViewBlockInfo>& view_block_info) {
        assert(!view_block_info->view_block->qc().view_block_hash().empty());
        auto it = view_blocks_info_.find(view_block_info->view_block->qc().view_block_hash());
        if (it != view_blocks_info_.end() && it->second->view_block != nullptr) {
            ZJC_DEBUG("exists, failed add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
                common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
                view_block_info->view_block->qc().network_id(),
                view_block_info->view_block->qc().pool_index(),
                view_block_info->view_block->qc().view(),
                view_block_info->view_block->block_info().height(),
                common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
                view_block_info->view_block->block_info().tx_list_size(),
                String().c_str());
            return;
        }

        if (it != view_blocks_info_.end()) {
            view_block_info->children = it->second->children;
        }
        
        view_blocks_info_[view_block_info->view_block->qc().view_block_hash()] = view_block_info;
        CHECK_MEMORY_SIZE(view_blocks_info_);
        ZJC_DEBUG("success add view block: %s, %u_%u_%lu, height: %lu, parent hash: %s, tx size: %u, strings: %s",
            common::Encode::HexEncode(view_block_info->view_block->qc().view_block_hash()).c_str(),
            view_block_info->view_block->qc().network_id(),
            view_block_info->view_block->qc().pool_index(),
            view_block_info->view_block->qc().view(),
            view_block_info->view_block->block_info().height(),
            common::Encode::HexEncode(view_block_info->view_block->parent_hash()).c_str(),
            view_block_info->view_block->block_info().tx_list_size(),
            String().c_str());
    }

    std::shared_ptr<ViewBlockInfo> GetViewBlockInfo(
            std::shared_ptr<ViewBlock> view_block, 
            BalanceMapPtr acc_balance_map_ptr,
            std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr) {
        auto view_block_info_ptr = std::make_shared<ViewBlockInfo>();
        if (view_block) {
            ZJC_DEBUG("add new view block %s, leader: %d",
                common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), view_block->qc().view());
            for (uint32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
                view_block_info_ptr->added_txs.insert(view_block->block_info().tx_list(i).gid());
            }
        }

        view_block_info_ptr->view_block = view_block;
        view_block_info_ptr->acc_balance_map_ptr = acc_balance_map_ptr;
        view_block_info_ptr->zjc_host_ptr = zjc_host_ptr;
        return view_block_info_ptr;
    }

    void SetStatusToMap(const HashStr& hash, const ViewBlockStatus& status) {
        auto it = view_blocks_info_.find(hash);
        if (it == view_blocks_info_.end()) {
            return;
        }

        view_blocks_info_[hash]->status = status;        
    }

    void AddChildrenToMap(const std::shared_ptr<ViewBlock>& view_block) {
        const HashStr& parent_hash = view_block->parent_hash();
        if (latest_committed_block_ != nullptr && 
                view_block->qc().view() <= latest_committed_block_->qc().view()) {
            return;
        }

        auto it = view_blocks_info_.find(parent_hash);
        if (it == view_blocks_info_.end()) {
            // if (latest_committed_block_ == nullptr || latest_locked_block_->qc().view_block_hash() != parent_hash) {
            //     ZJC_DEBUG("failed find parent hash: %s, latest_locked_block_ hash: %s",
            //         common::Encode::HexEncode(parent_hash).c_str(),
            //         (latest_committed_block_ ? 
            //         common::Encode::HexEncode(latest_locked_block_->qc().view_block_hash()).c_str() : 
            //         ""));
            //     assert(false);
            //     return;
            // }

            // for (uint32_t i = 0; i < latest_committed_block_->block_info().tx_list_size(); ++i) {
            //     auto& tx = latest_committed_block_->block_info().tx_list(i);
            //     for (auto storage_idx = 0; storage_idx < tx.storages_size(); ++storage_idx) {
            //         zjc_host_ptr->SavePrevStorages(
            //             tx.storages(storage_idx).key(), 
            //             tx.storages(storage_idx).value());
            //         ZJC_DEBUG("store success prev storage key: %s",
            //             common::Encode::HexEncode(tx.storages(storage_idx).key()).c_str());
            //     }

            //     if (tx.balance() == 0) {
            //         continue;
            //     }

            //     auto& addr = account_mgr_->GetTxValidAddress(tx);
            //     (*balane_map_ptr)[addr] = tx.balance();
                
            // }

            // TODO: fix storage map            
            auto block_info_ptr = GetViewBlockInfo(nullptr, nullptr, nullptr);
            view_blocks_info_[parent_hash] = block_info_ptr;
            CHECK_MEMORY_SIZE(view_blocks_info_);
            ZJC_DEBUG("add null parent view block: %u_%u_%lu, height: %lu",
                view_block->qc().network_id(), 
                view_block->qc().pool_index(), 
                view_block->qc().view(), 
                view_block->block_info().height());
            // assert(block_info_ptr->view_block->qc().view_block_hash() == parent_hash);
        }

// #ifndef NDEBUG
//         std::string debug_str;
//         auto debug_view_block = view_block;
//         while (true) {
//             auto iter = view_blocks_info_.find(debug_view_block->parent_hash());
//             if (iter == view_blocks_info_.end()) {
//                 break;
//             }

//             auto pview_block = iter->second->view_block;
//             debug_str += common::StringUtil::Format("%u_%u_%lu_%lu-_%u_%u_%lu_%lu-%s_%s-%s_%s --> ", 
//                 debug_view_block->qc().network_id(),
//                 debug_view_block->qc().pool_index(),
//                 debug_view_block->block_info().height(),
//                 debug_view_block->qc().view(),
//                 pview_block->qc().network_id(),
//                 pview_block->qc().pool_index(),
//                 pview_block->block_info().height(),
//                 pview_block->qc().view(),
//                 common::Encode::HexEncode(debug_view_block->qc().view_block_hash()).c_str(),
//                 common::Encode::HexEncode(debug_view_block->parent_hash()).c_str(),
//                 common::Encode::HexEncode(pview_block->qc().view_block_hash()).c_str(),
//                 common::Encode::HexEncode(pview_block->parent_hash()).c_str());
//             if (debug_view_block->block_info().height() != pview_block->block_info().height() + 1) {
//                 ZJC_DEBUG("failed add view block: %s", debug_str.c_str());
//                 assert(false);
//             }

//             if (pview_block == latest_committed_block_) {
//                 break;
//             }

//             debug_view_block = pview_block;
//         }
//         ZJC_DEBUG("success add view block: %s, strings: %s",
//             debug_str.c_str(), String().c_str());
// #endif
        view_blocks_info_[parent_hash]->children.push_back(view_block);
    }

    // prune the branch starting from view_block
    Status GetChildren(const HashStr& hash, std::vector<std::shared_ptr<ViewBlock>>& children);
    
    std::shared_ptr<ViewBlock> high_view_block_ = nullptr;
    View prune_height_ = 0;
    std::shared_ptr<ViewBlock> start_block_;
    std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;
    std::shared_ptr<ViewBlock> latest_committed_block_; // 最新 committed block
    std::shared_ptr<ViewBlock> latest_locked_block_; // locked_block_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    volatile View stored_to_db_view_ = 0llu;
};

// from db
Status GetLatestViewBlockFromDb(
        const std::shared_ptr<db::Db>& db,
        const uint32_t& pool_index,
        std::shared_ptr<ViewBlock>& view_block);
void GetQCWrappedByGenesis(uint32_t pool_index, QC* qc);
        
} // namespace consensus
    
} // namespace shardora


