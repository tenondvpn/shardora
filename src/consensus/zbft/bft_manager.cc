#include "consensus/zbft/bft_manager.h"

#include <cassert>

#include <common/log.h>
#include <libbls/tools/utils.h>
#include <protos/pools.pb.h>

#include "consensus/zbft/root_zbft.h"
#include "consensus/zbft/zbft.h"
#include "consensus/zbft/zbft_utils.h"
#include "consensus/zbft/waiting_txs_pools.h"
#include "bls/bls_utils.h"
#include "bls/bls_manager.h"
#include "bls/bls_sign.h"
#include "common/hash.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "common/encode.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "elect/elect_manager.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "pools/tx_pool_manager.h"
#include "transport/processor.h"

namespace zjchain {

namespace consensus {

BftManager::BftManager() {}

BftManager::~BftManager() {
    if (bft_queue_ != nullptr) {
        delete []bft_queue_;
    }
}

int BftManager::Init(
        block::BlockAggValidCallback block_agg_valid_func,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
        std::shared_ptr<consensus::ContractGasPrepayment>& gas_prepayment,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count,
        BlockCacheCallback new_block_cache_callback) {
    block_agg_valid_func_ = block_agg_valid_func;
    contract_mgr_ = contract_mgr;
    gas_prepayment_ = gas_prepayment;
    vss_mgr_ = vss_mgr;
    account_mgr_ = account_mgr;
    block_mgr_ = block_mgr;
    elect_mgr_ = elect_mgr;
    pools_mgr_ = pool_mgr;
    tm_block_mgr_ = tm_block_mgr;
    bls_mgr_ = bls_mgr;
    kv_sync_ = kv_sync;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    new_block_cache_callback_ = new_block_cache_callback;
    RegisterCreateTxCallbacks();
    security_ptr_ = security_ptr;
    txs_pools_ = std::make_shared<WaitingTxsPools>(pools_mgr_, block_mgr, tm_block_mgr);
    thread_count_ = thread_count;
    bft_queue_ = new std::queue<ZbftPtr>[thread_count];
    elect_items_[0] = std::make_shared<ElectItem>();
    elect_items_[1] = std::make_shared<ElectItem>();

#ifdef ZJC_UNITTEST
    now_msg_ = new transport::MessagePtr[thread_count_];
#endif
    for (uint8_t i = 0; i < thread_count_; ++i) {
        bft_gids_[i] = common::Hash::keccak256(common::Random::RandomString(1024));
        bft_gids_index_[i] = 0;
    }

    network::Route::Instance()->RegisterMessage(
        common::kConsensusMessage,
        std::bind(&BftManager::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kConsensusTimerMessage,
        std::bind(&BftManager::ConsensusTimerMessage, this, std::placeholders::_1));
    return kConsensusSuccess;
}

void BftManager::RegisterCreateTxCallbacks() {
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalFrom,
        std::bind(&BftManager::CreateFromTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalTo,
        std::bind(&BftManager::CreateToTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kConsensusLocalTos,
        std::bind(&BftManager::CreateToTxLocal, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreateByRootTo,
        std::bind(&BftManager::CreateContractByRootToTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kRootCreateAddress,
        std::bind(&BftManager::CreateRootToTxItem, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreate,
        std::bind(&BftManager::CreateContractUserCreateCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreateByRootFrom,
        std::bind(&BftManager::CreateContractByRootFromTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractGasPrepayment,
        std::bind(&BftManager::CreateContractUserCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractExcute,
        std::bind(&BftManager::CreateContractCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kJoinElect,
        std::bind(&BftManager::CreateJoinElectTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kCreateLibrary,
        std::bind(&BftManager::CreateLibraryTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kRootCross,
        std::bind(&BftManager::CreateRootCrossTx, this, std::placeholders::_1));
    block_mgr_->SetCreateToTxFunction(
        std::bind(&BftManager::CreateToTx, this, std::placeholders::_1));
    block_mgr_->SetCreateStatisticTxFunction(
        std::bind(&BftManager::CreateStatisticTx, this, std::placeholders::_1));
    block_mgr_->SetCreateElectTxFunction(
        std::bind(&BftManager::CreateElectTx, this, std::placeholders::_1));
    block_mgr_->SetCreateCrossTxFunction(
        std::bind(&BftManager::CreateCrossTx, this, std::placeholders::_1));
    tm_block_mgr_->SetCreateTmTxFunction(
        std::bind(&BftManager::CreateTimeblockTx, this, std::placeholders::_1));
}

void BftManager::NotifyRotationLeader(
        uint64_t elect_height,
        int32_t pool_mod_index,
        uint32_t old_leader_idx,
        uint32_t new_leader_idx) {
    auto new_idx = elect_item_idx_;
    auto old_elect_item = elect_items_[elect_item_idx_];
    if (old_elect_item->elect_height != elect_height) {
        old_elect_item = elect_items_[(elect_item_idx_ + 1) % 2];
        if (old_elect_item->elect_height != elect_height) {
            return;
        }

        new_idx = (elect_item_idx_ + 1) % 2;
    }

    auto elect_item_ptr = std::make_shared<ElectItem>(*old_elect_item);
    auto& elect_item = *elect_item_ptr;
    if (elect_item.local_node_member_index == old_leader_idx) {
        for (uint8_t j = 0; j < thread_count_; ++j) {
            elect_item.thread_set[j] = nullptr;
        }
    }

    if (elect_item.local_node_member_index == new_leader_idx) {
        SetThreadItem(elect_item.leader_count, pool_mod_index, elect_item.thread_set);
    }

    elect_items_[new_idx] = elect_item_ptr;
}

void BftManager::OnNewElectBlock(
        uint64_t block_tm_ms,
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& sec_key) {
    if (sharding_id > max_consensus_sharding_id_) {
        max_consensus_sharding_id_ = sharding_id;
    }

    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    auto tmp_item_ptr = elect_items_[elect_item_idx_];
    if (tmp_item_ptr->elect_height >= elect_height) {
        return;
    }

    auto elect_item_ptr = std::make_shared<ElectItem>();
    auto& elect_item = *elect_item_ptr;
    elect_item.members = members;
    int32_t local_node_pool_mod_num = -1;
    elect_item.leader_count = 0;
    for (uint32_t i = 0; i < members->size(); ++i) {
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            elect_item.local_member = (*members)[i];
            elect_item.local_node_member_index = i;
            local_node_pool_mod_num = (*members)[i]->pool_index_mod_num;
            if ((*members)[i]->bls_publick_key != libff::alt_bn128_G2::zero()) {
                elect_item.bls_valid = true;
            }
        }

        if ((*members)[i]->pool_index_mod_num >= 0) {
            ++elect_item.leader_count;
            elect_item.mod_with_leader_index[(*members)[i]->pool_index_mod_num] = i;
        }
    }

    if (elect_item.local_node_member_index >= members->size()) {
        auto new_idx = (elect_item_idx_ + 1) % 2;
        elect_items_[new_idx].reset();
        elect_items_[new_idx] = elect_item_ptr;
        elect_item_idx_ = new_idx;
        return;
    }

    elect_item.elect_height = elect_height;
    elect_item.member_size = members->size();
    elect_item.common_pk = common_pk;
    elect_item.sec_key = sec_key;
    auto new_idx = (elect_item_idx_ + 1) % 2;
    elect_items_[new_idx].reset();
    elect_items_[new_idx] = elect_item_ptr;
    ZJC_DEBUG("new elect block local leader index: %d, leader_count: %d, thread_count_: %d, elect height: %lu, member size: %d",
        local_node_pool_mod_num, elect_item.leader_count, thread_count_, elect_item.elect_height, members->size());
    auto& thread_set = elect_item.thread_set;
    SetThreadItem(elect_item.leader_count, local_node_pool_mod_num, thread_set);
    thread_set[0]->member_ips[elect_item.local_node_member_index] = common::IpToUint32(
        common::GlobalInfo::Instance()->config_local_ip().c_str());
    thread_set[0]->valid_ip_count = 1;
    minimal_node_count_to_consensus_ = members->size() * 2 / 3;
    if (minimal_node_count_to_consensus_ + 1 < members->size()) {
        ++minimal_node_count_to_consensus_;
    }

    elect_item_idx_ = new_idx;
}

void BftManager::SetThreadItem(
        uint32_t leader_count,
        int32_t local_node_pool_mod_num,
        std::shared_ptr<PoolTxIndexItem>* thread_set) {
    std::set<uint32_t> leader_pool_set;
    if (local_node_pool_mod_num >= 0) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            if (i % leader_count == (uint32_t)local_node_pool_mod_num) {
                leader_pool_set.insert(i);
            }
        }
    }

    for (uint8_t j = 0; j < thread_count_; ++j) {
        std::string thread_debug_str;
        auto thread_item = std::make_shared<PoolTxIndexItem>();
        auto& pools_set = common::GlobalInfo::Instance()->thread_with_pools()[j];
        for (auto iter = pools_set.begin(); iter != pools_set.end(); ++iter) {
            if (leader_pool_set.find(*iter) != leader_pool_set.end()) {
                thread_item->pools.push_back(*iter);
                thread_debug_str += std::to_string(*iter) + " ";
            }
        }

        thread_item->prev_index = 0;
        thread_set[j] = thread_item;  // ptr change, multi-thread safe
        ZJC_DEBUG("local_node_pool_mod_num: %d, leader_count: %d, thread: %d handle pools: %s",
            local_node_pool_mod_num, leader_count, j, thread_debug_str.c_str());
    }
}

void BftManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
#ifndef ZJC_UNITTEST
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    Start(msg_ptr->thread_idx, nullptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    PopAllPoolTxs(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CheckTimeout(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CheckMessageTimeout(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    BroadcastInvalidGids(msg_ptr->thread_idx);
    CheckInvalidGids(msg_ptr->thread_idx);
#endif
}

void BftManager::CheckInvalidGids(uint8_t thread_idx) {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_idx) {
            std::vector<std::shared_ptr<pools::InvalidGidItem>> items;
            pools_mgr_->BftCheckInvalidGids(pool_idx, items);
            if (items.empty()) {
                continue;
            }

            for (auto iter = items.begin(); iter != items.end(); ++iter) {
                auto& invalid_gid_item = *iter;
                //                 if (invalid_gid_item->prepare_hashs.size() == invalid_gid_item->precommit_hashs.size()) {
                //                     continue;
                //                 }
                // 
                ZJC_DEBUG("success add invalid hash: %u, %lu, %s",
                    invalid_gid_item->max_pool_index,
                    invalid_gid_item->max_pool_height,
                    common::Encode::HexEncode(invalid_gid_item->max_precommit_hash).c_str());
                if (invalid_gid_item->prepare_hashs.size() < (2 * invalid_gid_item->precommit_hashs.size() / 3)) {
                    pools_mgr_->AddChangeLeaderInvalidHash(
                        invalid_gid_item->max_pool_index,
                        invalid_gid_item->max_pool_height,
                        invalid_gid_item->max_precommit_hash);
                }

                if (pools_with_zbfts_[pool_idx] != nullptr) {
                    if (pools_with_zbfts_[pool_idx]->gid() == invalid_gid_item->gid) {
                        pools_with_zbfts_[pool_idx]->Destroy();
                        pools_with_zbfts_[pool_idx] = nullptr;
                        ZJC_DEBUG("remove gid: %s", common::Encode::HexEncode(invalid_gid_item->gid).c_str());
                    }
                }
            }
        }
    }
}

void BftManager::PopAllPoolTxs(uint8_t thread_index) {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_index) {
            pools_mgr_->PopTxs(pool_idx);
            pools_mgr_->CheckTimeoutTx(pool_idx);
        }
    }
}

void BftManager::RotationLeader(
        int32_t leader_mod_num,
        uint64_t elect_height,
        uint32_t new_leader_idx) {
    auto old_elect_item_ptr = elect_items_[elect_item_idx_];
    if (old_elect_item_ptr->elect_height != elect_height) {
        return;
    }

    auto elect_item_ptr = std::make_shared<ElectItem>(*old_elect_item_ptr);
    auto old_leader_idx = elect_item_ptr->mod_with_leader_index[leader_mod_num];
    if (old_leader_idx == (int32_t)new_leader_idx) {
        return;
    }

    (*elect_item_ptr->members)[old_leader_idx]->pool_index_mod_num = -1;
    (*elect_item_ptr->members)[new_leader_idx]->pool_index_mod_num = leader_mod_num;
    if ((int32_t)elect_item_ptr->local_node_member_index == old_leader_idx) {
        for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
            elect_item_ptr->thread_set[i] = nullptr;
        }
    }

    if (elect_item_ptr->local_node_member_index == new_leader_idx) {
        auto& thread_set = elect_item_ptr->thread_set;
        SetThreadItem(elect_item_ptr->leader_count, leader_mod_num, thread_set);
    } else {
        for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
            elect_item_ptr->thread_set[i] = nullptr;
        }
    }

    elect_item_ptr->mod_with_leader_index[leader_mod_num] = new_leader_idx;
    assert(new_leader_idx < elect_item_ptr->members->size());
    elect_items_[elect_item_idx_].reset();
    elect_items_[elect_item_idx_] = elect_item_ptr;
    ZJC_INFO("rotation leader success: %d, %lu, old_leader_idx: %u, "
        "new leader idx: %u, local index: %d, "
        "now_ms: %lu, leader valid: %lu, change valid: %lu, invalid: %lu",
        leader_mod_num, elect_height, old_leader_idx, new_leader_idx,
        elect_item_ptr->local_node_member_index,
        common::TimeUtils::TimestampMs(),
        elect_item_ptr->time_valid,
        elect_item_ptr->change_leader_time_valid,
        elect_item_ptr->invalid_time);
}

ZbftPtr BftManager::Start(
        uint8_t thread_index,
        ZbftPtr commited_bft_ptr) {
#ifndef ZJC_UNITTEST
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) <
            common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
//         ZJC_DEBUG("thread idx error 0: %d", thread_index);
        return nullptr;
    }
#endif
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    if (elect_item_ptr == nullptr) {
        ZJC_DEBUG("thread idx error 1: %d", thread_index);
        return nullptr;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (elect_item_ptr->time_valid > now_tm_ms) {
        auto item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
        if (item_ptr == nullptr) {
            ZJC_DEBUG("thread idx error 2: %d", thread_index);
            return nullptr;
        }

        if (item_ptr->time_valid > now_tm_ms) {
            ZJC_DEBUG("thread idx error 3: %d", thread_index);
            return nullptr;
        }

        ZJC_DEBUG("elect time valid use old new elect height: %lu, "
            "old elect height: %lu, time valid: %l, now: %lu",
            elect_item_ptr->elect_height, item_ptr->elect_height,
            elect_item_ptr->time_valid, now_tm_ms);
        elect_item_ptr = item_ptr;
    }
    
    if (commited_bft_ptr != nullptr &&
            commited_bft_ptr->elect_item_ptr().get() != elect_item_ptr.get()) {
        ZJC_DEBUG("leader changed.");
        return nullptr;
    }

    auto& elect_item = *elect_item_ptr;
    auto& thread_set = elect_item.thread_set;
    auto thread_item = thread_set[thread_index];
    if (thread_item == nullptr || thread_item->pools.empty()) {
//         ZJC_DEBUG("thread idx error 4: %d", thread_index);
        return nullptr;
    }

    // 获取交易池中的待处理交易
    
    std::shared_ptr<WaitingTxsItem> txs_ptr = get_txs_ptr(thread_item, commited_bft_ptr);
    if (txs_ptr == nullptr) {
//         ZJC_DEBUG("thread idx error 5: %d", thread_index);
        return nullptr;
    }
    
    if (txs_ptr->tx_type == pools::protobuf::kNormalFrom) {
        if (block_mgr_->ShouldStopConsensus()) {
            ZJC_DEBUG("should stop consensus.");
            ZJC_DEBUG("thread idx error 6: %d", thread_index);
            return nullptr;
        }
    }

    txs_ptr->thread_index = thread_index;
    auto zbft_ptr = StartBft(elect_item_ptr, txs_ptr, commited_bft_ptr);
	
    if (zbft_ptr == nullptr) {
        for (auto iter = txs_ptr->txs.begin(); iter != txs_ptr->txs.end(); ++iter) {
            iter->second->in_consensus = false;
        }

        ZJC_DEBUG("leader start bft failed, thread: %d, pool: %d, "
            "thread_item->pools.size(): %d, "
            "elect_item_ptr->elect_height: %lu,elect_item_ptr->time_valid: %lu now_tm_ms: %lu",
            thread_index, txs_ptr->pool_index,
            thread_item->pools.size(),
            elect_item_ptr->elect_height,
            elect_item_ptr->time_valid,
            now_tm_ms);
        return nullptr;
    }

    ZJC_DEBUG("leader start bft success, thread: %d, pool: %d,"
        "thread_item->pools.size(): %d, "
        "elect_item_ptr->elect_height: %lu,elect_item_ptr->time_valid: %lu now_tm_ms: %lu",
        thread_index, zbft_ptr->pool_index(),
        thread_item->pools.size(),
        elect_item_ptr->elect_height,
        elect_item_ptr->time_valid,
        now_tm_ms);
    return zbft_ptr;
}

std::shared_ptr<WaitingTxsItem> BftManager::get_txs_ptr(
        std::shared_ptr<PoolTxIndexItem>& thread_item,
        ZbftPtr& commited_bft_ptr) {
    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (commited_bft_ptr == nullptr) {
        if (thread_item->pools[thread_item->pools.size() - 1] == common::kRootChainPoolIndex) {
            if (pools_prev_bft_timeout_[common::kRootChainPoolIndex] < now_tm_ms) {
                if (pools_with_zbfts_[common::kRootChainPoolIndex] != nullptr) {
                    auto bft_ptr = pools_with_zbfts_[common::kRootChainPoolIndex];
                    if (!bft_ptr->this_node_is_leader()) {
                        if (bft_ptr->timeout(now_tm_ms * 1000lu)) {
                            LeaderRemoveTimeoutPrepareBft(bft_ptr);
                            txs_ptr = txs_pools_->LeaderGetValidTxs(common::kRootChainPoolIndex);
                        }
                    } else {
                        txs_ptr = txs_pools_->LeaderGetValidTxs(common::kRootChainPoolIndex);
                    }
                } else {
                    txs_ptr = txs_pools_->LeaderGetValidTxs(common::kRootChainPoolIndex);
                }
            }
        }
        
        auto begin_index = thread_item->prev_index;
        if (txs_ptr == nullptr) {
            // now leader create zbft ptr and start consensus
            for (; thread_item->prev_index < thread_item->pools.size(); ++thread_item->prev_index) {
                auto pool_idx = thread_item->pools[thread_item->prev_index];
                if (pools_prev_bft_timeout_[pool_idx] >= now_tm_ms) {
                    continue;
                }

                if (pools_with_zbfts_[pool_idx] != nullptr) {
                    auto bft_ptr = pools_with_zbfts_[pool_idx];
                    if (bft_ptr->this_node_is_leader()) {
                        continue;
                    }

                    if (!bft_ptr->timeout(now_tm_ms * 1000lu)) {
                        continue;
                    }

                    LeaderRemoveTimeoutPrepareBft(bft_ptr);
                }
                txs_ptr = txs_pools_->LeaderGetValidTxs(pool_idx);
                if (txs_ptr != nullptr) {
                    // now leader create zbft ptr and start consensus
                    break;
                }
            }
        }

        if (txs_ptr == nullptr) {
            for (thread_item->prev_index = 0;
                    thread_item->prev_index < begin_index; ++thread_item->prev_index) {
                auto pool_idx = thread_item->pools[thread_item->prev_index];
                if (pools_prev_bft_timeout_[pool_idx] >= now_tm_ms) {
                    continue;
                }

                if (pools_with_zbfts_[pool_idx] != nullptr) {
                    auto bft_ptr = pools_with_zbfts_[pool_idx];
                    if (bft_ptr->this_node_is_leader()) {
                        continue;
                    }

                    if (!bft_ptr->timeout(now_tm_ms * 1000lu)) {
                        continue;
                    }

                    LeaderRemoveTimeoutPrepareBft(bft_ptr);
                }

                txs_ptr = txs_pools_->LeaderGetValidTxs(pool_idx);
                if (txs_ptr != nullptr) {
                    // now leader create zbft ptr and start consensus
                    break;
                }
            }
        }

        if (thread_item->pools.size() > 0) {
            thread_item->prev_index = ++thread_item->prev_index % thread_item->pools.size();
        }
    } else {
        txs_ptr = txs_pools_->LeaderGetValidTxs(commited_bft_ptr->pool_index());
    }

    return txs_ptr;
}

void BftManager::LeaderRemoveTimeoutPrepareBft(ZbftPtr& bft_ptr) {
    ZJC_DEBUG("remove bft gid: %s, pool_index: %d",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_ptr->pool_index());
    pools_with_zbfts_[bft_ptr->pool_index()] = nullptr;
    bft_ptr->Destroy();

}

bool BftManager::CheckChangedLeaderBftsValid(
        uint32_t pool_index,
        uint64_t height,
        const std::string& gid) {
    if (changed_leader_pools_height_[pool_index] == nullptr) {
        return true;
    }

    if (changed_leader_pools_height_[pool_index]->gid() == gid) {
        return true;
    }

    if (changed_leader_pools_height_[pool_index]->prepare_block() == nullptr) {
        return true;
    }

    if (changed_leader_pools_height_[pool_index]->prepare_block()->height() < height) {
        return true;
    }

    if (changed_leader_pools_height_[pool_index]->consensus_status() == kConsensusPrepare) {
        return true;
    }

    return false;
}

int BftManager::ChangePrecommitBftLeader(
        ZbftPtr& bft_ptr,
        uint32_t leader_idx,
        const ElectItem& elect_item) {
    ZJC_DEBUG("now change precommit leader: %s, leader idx: %d, old elect height: %lu, elect height: %lu",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(), leader_idx, bft_ptr->elect_height(), elect_item.elect_height);
    // pre-commit timeout and changed new leader
    if (bft_ptr->elect_height() > elect_item.elect_height) {
        return kConsensusSuccess;
    }

    bft_ptr->ChangeLeader(leader_idx, elect_item.elect_height);
//     if (bft_ptr->prepare_block() != nullptr) {
//         pools_mgr_->AddChangeLeaderInvalidHash(
//             bft_ptr->pool_index(),
//             bft_ptr->prepare_block()->height(),
//             bft_ptr->prepare_block()->hash());
//     }

    return kConsensusSuccess;
}

int BftManager::InitZbftPtr(int32_t leader_idx, const ElectItem& elect_item, ZbftPtr& bft_ptr) {
    if (bft_ptr->Init(
            leader_idx,
            elect_item.leader_count,
            elect_item.elect_height,
            elect_item.members,
            elect_item.common_pk,
            elect_item.sec_key) != kConsensusSuccess) {
        ZJC_ERROR("bft init failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

ZbftPtr BftManager::StartBft(
        const std::shared_ptr<ElectItem>& elect_item_ptr,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr commited_bft_ptr) {    
    ZbftPtr bft_ptr = nullptr;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        bft_ptr = std::make_shared<RootZbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            txs_pools_,
            tm_block_mgr_);
    } else {
        bft_ptr = std::make_shared<Zbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            txs_pools_,
            tm_block_mgr_);
    }

    auto& elect_item = *elect_item_ptr;
    bft_ptr->set_elect_item_ptr(elect_item_ptr);
	
    if (InitZbftPtr(
            elect_item.local_node_member_index,
            elect_item,
            bft_ptr) != kConsensusSuccess) {
        ZJC_ERROR("InitZbftPtr failed!");
        return nullptr;
    }

	

    auto& gid = bft_gids_[txs_ptr->thread_index];
	
    uint64_t* tmp_gid = (uint64_t*)gid.data();
    tmp_gid[0] = bft_gids_index_[txs_ptr->thread_index]++;
    bft_ptr->set_gid(gid);
    bft_ptr->set_network_id(common::GlobalInfo::Instance()->network_id());
    bft_ptr->set_member_count(elect_item.member_size);
    // LeaderPrepare 中会调用到 DoTransaction，本地执行块内交易
    int leader_pre = LeaderPrepare(elect_item, bft_ptr, commited_bft_ptr);
	
    if (leader_pre != kConsensusSuccess) {
        ZJC_ERROR("leader prepare failed!");
        return nullptr;
    }

    ZJC_DEBUG("this node is leader and start bft: %s,"
        "pool index: %d, thread index: %d, prepare hash: %s, pre hash: %s, "
        "tx size: %d, elect height: %lu, gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_ptr->pool_index(),
        bft_ptr->thread_index(),
        common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
        bft_ptr->prepare_block() == nullptr ? "" : common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
        txs_ptr->txs.size(),
        elect_item.elect_height,
        common::Encode::HexEncode(gid).c_str());
    return bft_ptr;
}

void BftManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    ZJC_DEBUG("message coming msg hash: %lu, thread idx: %u, prepare: %s, "
        "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
        msg_ptr->header.hash64(), msg_ptr->thread_idx,
        common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
        header.zbft().pool_index(),
        msg_ptr->header.zbft().sync_block());
    
    if (header.has_zbft() && header.zbft().leader_idx() < 0 && !msg_ptr->header.zbft().sync_block()) {
        dht::DhtKeyManager dht_key(
            msg_ptr->header.src_sharding_id(),
            security_ptr_->GetAddress());
        if (msg_ptr->header.des_dht_key() != dht_key.StrKey()) {
            network::Route::Instance()->Send(msg_ptr);
            ZJC_DEBUG("backup message resend to leader by latest node net: %u, id: %s, des dht: %s, local: %s",
                msg_ptr->header.src_sharding_id(), common::Encode::HexEncode(security_ptr_->GetAddress()).c_str(),
                common::Encode::HexEncode(msg_ptr->header.des_dht_key()).c_str(),
                common::Encode::HexEncode(dht_key.StrKey()).c_str());
            return;
        }
    }

    assert(header.type() == common::kConsensusMessage);
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    if (msg_ptr->header.zbft().sync_block() && msg_ptr->header.zbft().has_block()) {
        ElectItem& elect_item = *elect_item_ptr;
        return HandleSyncConsensusBlock(msg_ptr);
    }

    // leader's message
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    int res = kConsensusSuccess;

    auto zbft = header.zbft();
    auto new_height = zbft.tx_bft().height();
    
    if (isFromLeader(zbft)) {
        std::shared_ptr<BftMessageInfo> bft_msgs = gid_with_msg_map_[zbft.pool_index()];
        
        if (isCommit(header.zbft())) {
            auto commit_bft_ptr = GetBft(zbft.pool_index(), zbft.commit_gid());
            if (commit_bft_ptr == nullptr) {
                ZJC_DEBUG("get commit gid failed: %s, pool: %u",
                    common::Encode::HexEncode(zbft.commit_gid()).c_str(),
                    zbft.pool_index());
                SyncConsensusBlock(
                    msg_ptr->thread_idx,
                    zbft.pool_index(),
                    zbft.commit_gid());
            } else {
                // 只有当前状态是 PreCommit 的 bft 才允许 Commit
                if (commit_bft_ptr->consensus_status() == kConsensusPreCommit) {
                    if (BackupCommit(commit_bft_ptr, msg_ptr) != kConsensusSuccess) {
                        ZJC_ERROR("backup commit bft failed: %s",
                            common::Encode::HexEncode(zbft.commit_gid()).c_str());
                        assert(false);
                    }
                    
                    // 收到 commit 消息后，无论 commit 后续成功与否，都清空该交易池的 bft_msgs 对象
                    if (isCurrentBft(zbft)) {
                        gid_with_msg_map_[zbft.pool_index()] = nullptr;
                    }

                    auto& zjc_block = commit_bft_ptr->prepare_block();
                    if (zjc_block != nullptr) {
                        ZJC_DEBUG("now remove gid with height: %s, %u, %lu",
                            common::Encode::HexEncode(commit_bft_ptr->gid()).c_str(),
                            zjc_block->pool_index(), zjc_block->height());
                        RemoveBftWithBlockHeight(zjc_block->pool_index(), zjc_block->height());
                        RemoveWaitingBlock(zjc_block->pool_index(), zjc_block->height());
                    }

                    RemoveBft(commit_bft_ptr->pool_index(), commit_bft_ptr->gid());
                } else {
                    // 如果收到非当前 bft 的 commit 消息，不应该 commit，应该直接 return，等待后面同步
                    // TODO 或者先缓存起来，等补全前面的 commit 消息后再执行这个 commit 消息，避免同步延迟
                    if (!isCurrentBft(zbft)) {
                        return;
                    }

                    bft_msgs->msgs[2] = msg_ptr;
                }
            }
        }

        if (isPrepare(zbft)) {
             // TODO if not new prepare, return directly
            if (!isCurrentBft(zbft)) {
                if (isNewerBft(zbft)) {
                    // 如果 backup 在收到 commit 消息之前，或者是在 commit 消息但在成功出块之前收到了下一消息的 prepare
                    // 则自旋等待一定时间
                    WaitForLastCommitIfNeeded(zbft.pool_index(), COMMIT_MSG_TIMEOUT_MS);
                    
                    bft_msgs = std::make_shared<BftMessageInfo>(header.zbft().prepare_gid());
                    gid_with_msg_map_[header.zbft().pool_index()] = bft_msgs;
                } else {
                    ZJC_DEBUG("gid oldest for old: %s, %lu, %lu",
                        common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
                        zbft.tx_bft().height(),
                        getCurrentBftHeight(zbft.pool_index()));
                    return;
                }
            }

            bft_msgs->msgs[0] = msg_ptr;
            ZJC_INFO("====1.1 backup receive prepare msg: %s", common::Encode::HexEncode(zbft.prepare_gid()).c_str());
            
            // TODO 此处应该回复消息给 leader，避免 leader 等待 bft 的 10s 超时，造成待共识队列阻塞
            if (new_height < latest_commit_height(zbft.pool_index()) + 1) {
                return;
            }
            if (new_height > latest_commit_height(zbft.pool_index()) + 1) {
                ZJC_INFO("====1.1.1 %s", common::Encode::HexEncode(zbft.prepare_gid()).c_str());
                kv_sync_->AddSyncHeight(
                        msg_ptr->thread_idx,
                        common::GlobalInfo::Instance()->network_id(),
                        zbft.pool_index(),
                        new_height,
                        sync::kSyncHighest);
                return;
            }

            ZJC_INFO("====1.1.3 %s leader: %d, pool: %d",
                common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
                zbft.leader_idx(),
                zbft.pool_index());
        }

        if (isPrecommit(zbft)) {
            if (!isCurrentBft(zbft)) {
                if (!isNewerBft(zbft)) {
                    return;
                }
                bft_msgs = std::make_shared<BftMessageInfo>(header.zbft().precommit_gid());
                gid_with_msg_map_[header.zbft().pool_index()] = bft_msgs;
            }

            bft_msgs->msgs[1] = msg_ptr;
            ZJC_INFO("====1.2 backup receive precommit msg: %s", common::Encode::HexEncode(header.zbft().precommit_gid()).c_str());

            if (bft_msgs->msgs[0] != nullptr &&
                bft_msgs->msgs[0]->header.zbft().tx_bft().height() != latest_commit_height(zbft.pool_index()) + 1) {
                
                return;
            }
            ZJC_INFO("====1.2.1 %s", common::Encode::HexEncode(header.zbft().precommit_gid()).c_str());
        }

        if (bft_msgs == nullptr) {
            ZJC_DEBUG("bft_msgs == nullptr message coming msg hash: %lu, thread idx: %u, prepare: %s, "
                "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
                msg_ptr->header.hash64(), msg_ptr->thread_idx,
                common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
                common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
                common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
                header.zbft().pool_index(),
                msg_ptr->header.zbft().sync_block());
            return;
        }

        for (int32_t i = 0; i < 3; ++i) {
            auto& tmp_msg_ptr = bft_msgs->msgs[i];
            if (tmp_msg_ptr == nullptr) {
                break;
            }
            
            if (tmp_msg_ptr->handled) {
                ZJC_DEBUG("tmp_msg_ptr->handled message coming msg hash: %lu, thread idx: %u, prepare: %s, "
                    "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
                    msg_ptr->header.hash64(), msg_ptr->thread_idx,
                    common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
                    common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
                    common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
                    header.zbft().pool_index(),
                    msg_ptr->header.zbft().sync_block());
                continue;
            }

            ZJC_DEBUG("backup handle message coming msg hash: %lu, thread idx: %u, prepare: %s, "
                "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
                msg_ptr->header.hash64(), msg_ptr->thread_idx,
                common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
                common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
                common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
                header.zbft().pool_index(),
                msg_ptr->header.zbft().sync_block());
            BackupHandleZbftMessage(tmp_msg_ptr->thread_idx, tmp_msg_ptr);
            tmp_msg_ptr->handled = true;
        }
    } else {
        LeaderHandleZbftMessage(msg_ptr);
    }
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
}

void BftManager::HandleSyncedBlock(uint8_t thread_idx, std::shared_ptr<block::protobuf::Block>& block_ptr) {
    if (!block_ptr->is_commited_block() &&
            (block_ptr->height() > pools_mgr_->latest_height(block_ptr->pool_index()) ||
            pools_mgr_->latest_height(block_ptr->pool_index()) == common::kInvalidUint64)) {
        AddWaitingBlock(block_ptr);
        RemoveWaitingBlock(block_ptr->pool_index(), block_ptr->height() - 1);
        return;
    }

    if (block_ptr->network_id() != common::GlobalInfo::Instance()->network_id() &&
        block_ptr->network_id() + network::kConsensusWaitingShardOffset !=
        common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    auto db_batch = std::make_shared<db::DbWriteBatch>();
    auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(block_ptr, db_batch);
    new_block_cache_callback_(
        thread_idx,
        queue_item_ptr->block_ptr,
        *queue_item_ptr->db_batch);
    block_mgr_->ConsensusAddBlock(thread_idx, queue_item_ptr);
    pools_mgr_->TxOver(block_ptr->pool_index(), block_ptr->tx_list());
    // remove bft
    ZJC_DEBUG("sync block message net: %u, pool: %u, height: %lu, block hash: %s",
        block_ptr->network_id(),
        block_ptr->pool_index(),
        block_ptr->height(),
        common::Encode::HexEncode(GetBlockHash(*block_ptr)).c_str());
    RemoveBftWithBlockHeight(block_ptr->pool_index(), block_ptr->height());
    RemoveWaitingBlock(block_ptr->pool_index(), block_ptr->height());
}

ZbftPtr BftManager::GetBftWithHash(uint32_t pool_index, const std::string& hash) {
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    if (bft_ptr == nullptr) {
        return nullptr;
    }

    if (bft_ptr->prepare_block() != nullptr && bft_ptr->prepare_block()->hash() == hash) {
        return bft_ptr;
    }

    return nullptr;
}

void BftManager::HandleSyncConsensusBlock(const transport::MessagePtr& msg_ptr) {
    auto& req_bft_msg = msg_ptr->header.zbft();
    auto bft_ptr = pools_with_zbfts_[req_bft_msg.pool_index()];
    if (bft_ptr == nullptr || bft_ptr->gid() != req_bft_msg.precommit_gid()) {
        if (!req_bft_msg.has_block()) {
            return;
        }
    }

    ZJC_DEBUG("sync consensus block coming pool: %u, height: %lu, "
        "hash: %s, is commited block: %d, hash64: %lu, bft_ptr == nullptr: %d,"
        " status: %d, gid: %s, latest: %lu",
        req_bft_msg.block().pool_index(),
        req_bft_msg.block().height(),
        common::Encode::HexEncode(req_bft_msg.block().hash()).c_str(),
        req_bft_msg.block().is_commited_block(),
        msg_ptr->header.hash64(),
        (bft_ptr == nullptr),
        bft_ptr == nullptr ? -1 : bft_ptr->consensus_status(),
        common::Encode::HexEncode(req_bft_msg.precommit_gid()).c_str(),
        pools_mgr_->latest_height(req_bft_msg.block().pool_index()));
    if (req_bft_msg.has_block()) {
        // verify and add new block
        auto elect_item_ptr = elect_items_[elect_item_idx_];
        auto& elect_item = *elect_item_ptr;
        if (bft_ptr == nullptr) {
            HandleCommitedSyncBlock(msg_ptr->thread_idx, req_bft_msg);
        } else {
            if (bft_ptr->prepare_block() == nullptr) {
                auto block_hash = GetBlockHash(req_bft_msg.block());
                if (bft_ptr->consensus_status() == kConsensusLeaderWaitingBlock) {
                    if (block_hash == bft_ptr->leader_waiting_prepare_hash()) {
                        bft_ptr->set_prepare_block(std::make_shared<block::protobuf::Block>(req_bft_msg.block()));
                        bft_ptr->LeaderResetPrepareBitmap(block_hash);
                        ReConsensusPrepareBft(elect_item, bft_ptr);
                    }

                    return;
                }

                assert(false);
            } else {
                HandleCommitedSyncBlock(msg_ptr->thread_idx, req_bft_msg);
            }
        }
    } else {
        if (bft_ptr == nullptr) {
            return;
        }

        if (bft_ptr->prepare_block() == nullptr) {
            return;
        }

        if (bft_ptr->consensus_status() != kConsensusPrepare) {
            return;
        }

        transport::protobuf::Header msg;
        if (!AddSyncKeyValue(&msg, *bft_ptr->prepare_block())) {
            ZJC_WARN("get key value failed, sync block failed!");
            return;
        }

        auto& elect_item = *bft_ptr->elect_item_ptr();
        msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
        dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kConsensusMessage);
        auto& bft_msg = *msg.mutable_zbft();
        bft_msg.set_sync_block(true);
        bft_msg.set_precommit_gid(req_bft_msg.precommit_gid());
        bft_msg.set_pool_index(bft_ptr->pool_index());
        ZJC_DEBUG("gid: %s, set pool index: %u", common::Encode::HexEncode(bft_ptr->gid()).c_str(), bft_ptr->pool_index());
        bft_msg.set_member_index(elect_item.local_node_member_index);
        bft_msg.set_elect_height(elect_item.elect_height);
        assert(elect_item.elect_height > 0);
        *bft_msg.mutable_block() = *bft_ptr->prepare_block();
        assert(bft_msg.block().height() > 0);
        transport::TcpTransport::Instance()->SetMessageHash(msg, msg_ptr->thread_idx);
        transport::TcpTransport::Instance()->Send(
            msg_ptr->thread_idx,
            msg_ptr->conn,
            msg);
        ZJC_DEBUG("send res to block hash: %s, gid: %s, hash64: %lu",
            common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
            common::Encode::HexEncode(req_bft_msg.precommit_gid()).c_str(),
            msg.hash64());
    }
}

void BftManager::HandleCommitedSyncBlock(uint8_t thread_idx, const zbft::protobuf::ZbftMessage& req_bft_msg) {
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    auto& elect_item = *elect_item_ptr;
    ZJC_DEBUG("commited block with bft coming: %u, %lu, %s, gid: %s",
        req_bft_msg.block().pool_index(), req_bft_msg.block().height(),
        common::Encode::HexEncode(req_bft_msg.block().hash()).c_str(),
        common::Encode::HexEncode(req_bft_msg.precommit_gid()).c_str());
    if (!req_bft_msg.block().is_commited_block()) {
        assert(false);
        return;
    }

    if (!req_bft_msg.block().has_bls_agg_sign_x() || !req_bft_msg.block().has_bls_agg_sign_y()) {
        ZJC_DEBUG("not has agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
            req_bft_msg.block().network_id(),
            req_bft_msg.block().pool_index(),
            req_bft_msg.block().height(),
            common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
        return;
    }

    auto tmp_hash = GetBlockHash(req_bft_msg.block());
    if (tmp_hash != req_bft_msg.block().hash()) {
        ZJC_DEBUG("block hash error: %s, %s",
            common::Encode::HexEncode(tmp_hash).c_str(),
            common::Encode::HexEncode(req_bft_msg.block().hash()).c_str());
        return;
    }

    auto block_ptr = std::make_shared<block::protobuf::Block>(req_bft_msg.block());
    if (pools_mgr_->is_next_block_checked(
            block_ptr->pool_index(),
            block_ptr->height(),
            block_ptr->hash())) {
        HandleSyncedBlock(thread_idx, block_ptr);
        return;
    }

    if (block_ptr->height() < pools_mgr_->latest_height(block_ptr->pool_index())) {
        waiting_agg_verify_blocks_[block_ptr->pool_index()][block_ptr->height()] = block_ptr;
        ZJC_DEBUG("block_ptr height < pools latest_height pool_index: %u %lu, %lu",
            block_ptr->pool_index(),
            block_ptr->height(),
            pools_mgr_->latest_height(block_ptr->pool_index()));
        return;
    }

    if (elect_item.elect_height < block_ptr->electblock_height()) {
        waiting_agg_verify_blocks_[block_ptr->pool_index()][block_ptr->height()] = block_ptr;
        ZJC_DEBUG("elect_item.elect_height block_ptr->electblock_height() pool_index: %u %lu, %lu",
            block_ptr->pool_index(),
            elect_item.elect_height,
            block_ptr->electblock_height());
        return;
    }

    // check bls sign
    if (!block_agg_valid_func_(thread_idx, req_bft_msg.block())) {
        ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
            req_bft_msg.block().network_id(),
            req_bft_msg.block().pool_index(),
            req_bft_msg.block().height(),
            common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
        //assert(false);
        return;
    }

    HandleSyncedBlock(thread_idx, block_ptr);
}

void BftManager::AddWaitingBlock(std::shared_ptr<block::protobuf::Block>& block_ptr) {
    auto& block_map = waiting_blocks_[block_ptr->pool_index()];
    auto iter = block_map.find(block_ptr->height());
    if (iter != block_map.end()) {
        return;
    }

    if (prefix_db_->BlockExists(block_ptr->hash())) {
        return;
    }

    block_map[block_ptr->height()] = block_ptr;
    ZJC_DEBUG("add new block pool: %u, height: %lu, hash: %s, size: %u",
        block_ptr->pool_index(),
        block_ptr->height(),
        common::Encode::HexEncode(block_ptr->hash()).c_str(),
        block_map.size());
}

void BftManager::RemoveWaitingBlock(uint32_t pool_index, uint64_t height) {
    auto& block_map = waiting_blocks_[pool_index];
    auto iter = block_map.begin();
    while (iter != block_map.end()) {
        if (iter->first > height) {
            break;
        }

        auto block_ptr = iter->second;
        ZJC_DEBUG("remove new block pool: %u, height: %lu, hash: %s",
            block_ptr->pool_index(),
            block_ptr->height(),
            common::Encode::HexEncode(block_ptr->hash()).c_str());
        iter = block_map.erase(iter);
        // check bls sign
        auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[pool_index];
        if (block_agg_valid_func_(thread_idx, *block_ptr)) {
            auto db_batch = std::make_shared<db::DbWriteBatch>();
            auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(block_ptr, db_batch);
            new_block_cache_callback_(
                thread_idx,
                queue_item_ptr->block_ptr,
                *queue_item_ptr->db_batch);
            block_mgr_->ConsensusAddBlock(thread_idx, queue_item_ptr);
            pools_mgr_->TxOver(block_ptr->pool_index(), block_ptr->tx_list());
            ZJC_DEBUG("sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                block_ptr->network_id(),
                block_ptr->pool_index(),
                block_ptr->height(),
                common::Encode::HexEncode(GetBlockHash(*block_ptr)).c_str());
            // remove bft
            RemoveBftWithBlockHeight(block_ptr->pool_index(), block_ptr->height());
        }
    }
}

void BftManager::SaveKeyValue(const transport::protobuf::Header& msg) {
    for (int32_t i = 0; i < msg.sync().items_size(); ++i) {
//         ZJC_DEBUG("save storage %s, %s",
//             common::Encode::HexEncode(msg.sync().items(i).key()).c_str(),
//             common::Encode::HexEncode(msg.sync().items(i).value()).c_str());
        prefix_db_->SaveTemporaryKv(
            msg.sync().items(i).key(),
            msg.sync().items(i).value());
    }
}

bool BftManager::AddSyncKeyValue(
        transport::protobuf::Header* msg,
        const block::protobuf::Block& block) {
    auto* sync_info = msg->mutable_sync();
    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        auto& tx = block.tx_list(i);
        for (int32_t j = 0; j < block.tx_list(i).storages_size(); ++j) {
            auto& storage = block.tx_list(i).storages(j);
//             ZJC_DEBUG("add storage %s, %s, %d", storage.key().c_str(), common::Encode::HexEncode(storage.val_hash()).c_str(), storage.val_size());
            if (storage.val_hash().size() == 32) {
                std::string val;
                if (!prefix_db_->GetTemporaryKv(storage.val_hash(), &val)) {
                    continue;
                }

                auto* sync_item = sync_info->add_items();
                sync_item->set_key(storage.val_hash());
                sync_item->set_value(val);
            }
        }
    }

    return true;
}

void BftManager::SyncConsensusBlock(
        uint8_t thread_idx,
        uint32_t pool_index,
        const std::string& bft_gid) {
    dht::BaseDhtPtr dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    dht::DhtPtr readobly_dht = dht->readonly_hash_sort_dht();
    if (readobly_dht->empty()) {
        return;
    }

    transport::protobuf::Header msg;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto& bft_msg = *msg.mutable_zbft();
    bft_msg.set_sync_block(true);
    bft_msg.set_precommit_gid(bft_gid);
    bft_msg.set_pool_index(pool_index);
    ZJC_DEBUG("gid: %s, set pool index: %u", common::Encode::HexEncode(bft_gid).c_str(), pool_index);
    auto tmp_pos = common::Random::RandomUint32() % readobly_dht->size();
    transport::TcpTransport::Instance()->Send(
        thread_idx,
        (*readobly_dht)[tmp_pos]->public_ip,
        (*readobly_dht)[tmp_pos]->public_port,
        msg);
    ZJC_DEBUG("send sync block %s:%d bft gid: %s",
        (*readobly_dht)[tmp_pos]->public_ip.c_str(),
        (*readobly_dht)[tmp_pos]->public_port,
        common::Encode::HexEncode(bft_gid).c_str());
}

bool BftManager::LeaderSignMessage(transport::MessagePtr& msg_ptr) {
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string sign;
    if (security_ptr_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return false;
    }

//     ZJC_DEBUG("sign message success pk: %s, hash: %s, sign: %s",
//         common::Encode::HexEncode(security_ptr_->GetPublicKey()).c_str(),
//         common::Encode::HexEncode(msg_hash).c_str(),
//         common::Encode::HexEncode(sign).c_str());
    msg_ptr->header.set_sign(sign);
    return true;
}

bool BftManager::SetBackupEcdhData(transport::MessagePtr& msg_ptr, common::BftMemberPtr& mem_ptr) {
    std::string& ecdh_key = mem_ptr->peer_ecdh_key;
    if (ecdh_key.empty()) {
        if (security_ptr_->GetEcdhKey(
                mem_ptr->pubkey,
                &ecdh_key) != security::kSecuritySuccess) {
            ZJC_ERROR("get ecdh key failed peer pk: %s",
                common::Encode::HexEncode(mem_ptr->pubkey).c_str());
            return false;
        }
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string enc_out;
    if (security_ptr_->Encrypt(msg_hash, ecdh_key, &enc_out) != security::kSecuritySuccess) {
        ZJC_ERROR("encrypt key failed peer pk: %s",
            common::Encode::HexEncode(mem_ptr->pubkey).c_str());
        return false;
    }

//     ZJC_DEBUG("set enc data: %s, msg_hash: %s, local pk: %s, peer pk: %s, ecdh_key: %s",
//         common::Encode::HexEncode(enc_out).c_str(),
//         common::Encode::HexEncode(msg_hash).c_str(),
//         common::Encode::HexEncode(security_ptr_->GetPublicKey()).c_str(),
//         common::Encode::HexEncode(mem_ptr->pubkey).c_str(),
//         common::Encode::HexEncode(ecdh_key).c_str());
    msg_ptr->header.mutable_zbft()->set_backup_enc_data(enc_out);
    return true;
}

bool BftManager::VerifyLeaderIdValid(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr) {
    if (!msg_ptr->header.has_sign()) {
        assert(false);
        return false;
    }

    auto mod_num = msg_ptr->header.zbft().pool_index() % elect_item.leader_count;
    if (elect_item.mod_with_leader_index[mod_num] != (int32_t)msg_ptr->header.zbft().member_index()) {
        //assert(false);
        return false;
    }

    auto& mem_ptr = (*elect_item.members)[msg_ptr->header.zbft().member_index()];
    if (mem_ptr->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_DEBUG("verify sign failed, backup invalid bls pk: %s",
            common::Encode::HexEncode(mem_ptr->id).c_str());
        return false;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_ptr_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_DEBUG("verify leader sign failed: %s", common::Encode::HexEncode(mem_ptr->id).c_str());
        //assert(false);
        return false;
    }

    return true;
}

void BftManager::BackupHandleZbftMessage(
        uint8_t thread_index,
        const transport::MessagePtr& msg_ptr) {
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    auto& header = msg_ptr->header;
    auto& zbft = msg_ptr->header.zbft();
    if (isPrecommit(zbft)) {
        ZJC_INFO("====1.2.2 %s", common::Encode::HexEncode(zbft.precommit_gid()).c_str());
        auto precommit_bft_ptr = pools_with_zbfts_[zbft.pool_index()];
        if (precommit_bft_ptr == nullptr) {
            ZJC_DEBUG("get precommit gid failed: %s", common::Encode::HexEncode(zbft.precommit_gid()).c_str());
            return;
        }
        ZJC_INFO("====1.2.2.1 %s", common::Encode::HexEncode(zbft.precommit_gid()).c_str());
        elect_item_ptr = precommit_bft_ptr->elect_item_ptr();
    } else {
        if (elect_item_ptr->elect_height != zbft.elect_height()) {
            auto tmp_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
            if (tmp_ptr == nullptr) {
                ZJC_ERROR("elect height error: %lu, %lu",
                    elect_item_ptr->elect_height, zbft.elect_height());
                return;
            }

            if (tmp_ptr->elect_height != zbft.elect_height()) {
                ZJC_DEBUG("elect height error: %lu, %lu, %lu",
                    elect_item_ptr->elect_height,
                    zbft.elect_height(),
                    tmp_ptr->elect_height);
                return;
            }

            elect_item_ptr = tmp_ptr;
        }
    }
    
    if (elect_item_ptr->local_member->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_DEBUG("elect_item_ptr->local_member->bls_publick_key message coming msg hash: %lu, thread idx: %u, prepare: %s, "
            "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
            msg_ptr->header.hash64(), msg_ptr->thread_idx,
            common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
            common::Encode::HexEncode(zbft.precommit_gid()).c_str(),
            common::Encode::HexEncode(zbft.commit_gid()).c_str(),
            zbft.pool_index(),
            zbft.sync_block());
        return;
    }

    auto& elect_item = *elect_item_ptr;
    if (!VerifyLeaderIdValid(elect_item, msg_ptr)) {
        ZJC_ERROR("leader invalid!");
        return;
    }

    if (zbft.ips_size() > 0) {
        auto& thread_set = elect_item.thread_set;
        auto thread_item = thread_set[msg_ptr->thread_idx];
        ZJC_DEBUG("0 get leader ips size: %u, thread: %d",
            zbft.ips_size(), msg_ptr->thread_idx);
        if (thread_item != nullptr) {
            ZJC_DEBUG("get leader ips size: %u", zbft.ips_size());
            for (int32_t i = 0; i < zbft.ips_size(); ++i) {
                auto iter = thread_item->all_members_ips[i].find(zbft.ips(i));
                if (iter == thread_item->all_members_ips[i].end()) {
                    thread_item->all_members_ips[i][zbft.ips(i)] = 1;
                    if (elect_item.leader_count <= 8) {
                        (*elect_item.members)[i]->public_ip = zbft.ips(i);
                        ZJC_DEBUG("member set ip %d, %u", i, (*elect_item.members)[i]->public_ip);
                    }
                } else {
                    ++iter->second;
                    if (iter->second >= (elect_item.leader_count * 3 / 2 + 1)) {
                        (*elect_item.members)[i]->public_ip = iter->first;
                        ZJC_DEBUG("member set ip %d, %u", i, (*elect_item.members)[i]->public_ip);
                    }
                }
            }
        }
    }

    if (isPrepare(zbft)) {
        ZJC_INFO("====1.1.4 %s", common::Encode::HexEncode(zbft.prepare_gid()).c_str());
        std::vector<uint8_t> invalid_txs;
        int res = BackupPrepare(elect_item, msg_ptr, &invalid_txs);
        if (res == kConsensusOppose) {
            BackupSendPrepareMessage(elect_item, msg_ptr, false, invalid_txs);
        } else if (res == kConsensusAgree) {
            BackupSendPrepareMessage(elect_item, msg_ptr, true, invalid_txs);
            pools_with_zbfts_[zbft.pool_index()]->set_elect_item_ptr(elect_item_ptr);
            pools_with_zbfts_[zbft.pool_index()]->AfterNetwork();
        } else {
            ZJC_DEBUG("backup prepare failed message coming msg hash: %lu, thread idx: %u, prepare: %s, "
                "precommit: %s, commit: %s, pool index: %u, sync_block: %d",
                msg_ptr->header.hash64(), msg_ptr->thread_idx,
                common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
                common::Encode::HexEncode(zbft.precommit_gid()).c_str(),
                common::Encode::HexEncode(zbft.commit_gid()).c_str(),
                zbft.pool_index(),
                zbft.sync_block());
            // timer to re-handle the message
            auto now_us = common::TimeUtils::TimestampUs();
            if (msg_ptr->timeout > now_us) {
                ZJC_DEBUG("0 push prepare message : %s, hash64: %lu",
                    common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
                    msg_ptr->header.hash64());
                backup_prapare_msg_queue_[msg_ptr->thread_idx].push_back(msg_ptr);
                if (backup_prapare_msg_queue_[msg_ptr->thread_idx].size() > 16) {
                    backup_prapare_msg_queue_[msg_ptr->thread_idx].pop_front();
                }
            }
        }
    }

    if (isPrecommit(zbft)) {
        ZJC_INFO("====1.2.3 %s", common::Encode::HexEncode(zbft.precommit_gid()).c_str());
        ZJC_DEBUG("handle precommit gid: %s, pool: %u",
            common::Encode::HexEncode(zbft.precommit_gid()).c_str(),
            zbft.pool_index());
        auto precommit_bft_ptr = pools_with_zbfts_[zbft.pool_index()];
        if (precommit_bft_ptr == nullptr || precommit_bft_ptr->gid() != zbft.precommit_gid()) {
            ZJC_DEBUG("get precommit gid failed: %s, pool: %u",
                common::Encode::HexEncode(zbft.precommit_gid()).c_str(), zbft.pool_index());
            return;
        }

        int res = BackupPrecommit(precommit_bft_ptr, msg_ptr);
        ZJC_INFO("====1.2.4 %s, res: %d", common::Encode::HexEncode(zbft.precommit_gid()).c_str(), res);
        if (res == kConsensusOppose) {
            BackupSendPrecommitMessage(elect_item, msg_ptr, false);
        } else if (res == kConsensusAgree) {
            BackupSendPrecommitMessage(elect_item, msg_ptr, true);
            pools_with_zbfts_[zbft.pool_index()]->AfterNetwork();
        } else {
        }
    }
}

bool BftManager::VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string& ecdh_key = mem_ptr->peer_ecdh_key;
    if (ecdh_key.empty()) {
        if (security_ptr_->GetEcdhKey(
                mem_ptr->pubkey,
                &ecdh_key) != security::kSecuritySuccess) {
            ZJC_ERROR("get ecdh key failed peer pk: %s",
                common::Encode::HexEncode(mem_ptr->pubkey).c_str());
            return false;
        }
    }

    std::string enc_out;
    if (security_ptr_->Encrypt(msg_hash, ecdh_key, &enc_out) != security::kSecuritySuccess) {
        ZJC_ERROR("encrypt key failed peer pk: %s",
            common::Encode::HexEncode(mem_ptr->pubkey).c_str());
        return false;
    }

//     ZJC_DEBUG("verif enc data src: %s, now: %s, msg_hash: %s, local_pk: %s, peer pk: %s, ecdh_key: %s",
//         common::Encode::HexEncode(bft_msg.backup_enc_data()).c_str(),
//         common::Encode::HexEncode(enc_out).c_str(),
//         common::Encode::HexEncode(msg_hash).c_str(),
//         common::Encode::HexEncode(security_ptr_->GetPublicKey()).c_str(),
//         common::Encode::HexEncode(mem_ptr->pubkey).c_str(),
//         common::Encode::HexEncode(ecdh_key).c_str());
    return enc_out == bft_msg.backup_enc_data();
}

ZbftPtr BftManager::CreateBftPtr(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        std::vector<uint8_t>* invalid_txs) {
    auto& bft_msg = msg_ptr->header.zbft();
    std::vector<uint64_t> bloom_data;
    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    if (bft_msg.tx_bft().tx_hash_list_size() > 0) {
        // get txs direct
        if (bft_msg.tx_bft().tx_type() == pools::protobuf::kNormalTo) {
            txs_ptr = txs_pools_->GetToTxs(bft_msg.pool_index(), false);
            if (txs_ptr == nullptr) {
                ZJC_ERROR("invalid consensus kNormalTo, txs not equal to leader. pool_index: %d, gid: %s",
                    bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            }
        } else if (bft_msg.tx_bft().tx_type() == pools::protobuf::kStatistic) {
            txs_ptr = txs_pools_->GetStatisticTx(bft_msg.pool_index(), false);
            if (txs_ptr == nullptr) {
                ZJC_ERROR("invalid consensus kStatistic, txs not equal to leader. pool_index: %d, gid: %s",
                    bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            }
        } else if (bft_msg.tx_bft().tx_type() == pools::protobuf::kCross) {
            txs_ptr = txs_pools_->GetCrossTx(bft_msg.pool_index(), false);
            if (txs_ptr == nullptr) {
                ZJC_ERROR("invalid consensus kCross, txs not equal to leader. pool_index: %d, gid: %s",
                    bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            }
        } else if (bft_msg.tx_bft().tx_type() == pools::protobuf::kConsensusRootElectShard) {
            if (bft_msg.tx_bft().tx_hash_list_size() == 1) {
                txs_ptr = txs_pools_->GetElectTx(bft_msg.pool_index(), bft_msg.tx_bft().tx_hash_list(0));
                if (txs_ptr == nullptr) {
                    ZJC_ERROR("invalid consensus kConsensusRootElectShard, txs not equal to leader. pool_index: %d, gid: %s",
                        bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
                }
            }
        } else if (bft_msg.tx_bft().tx_type() == pools::protobuf::kConsensusRootTimeBlock) {
            txs_ptr = txs_pools_->GetTimeblockTx(bft_msg.pool_index(), false);
        } else {
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
            txs_ptr = txs_pools_->FollowerGetTxs(
                bft_msg.pool_index(),
                bft_msg.tx_bft().tx_hash_list(),
                msg_ptr->thread_idx,
                nullptr);
            if (txs_ptr == nullptr) {
                PopAllPoolTxs(msg_ptr->thread_idx);
                txs_ptr = txs_pools_->FollowerGetTxs(
                    bft_msg.pool_index(),
                    bft_msg.tx_bft().tx_hash_list(),
                    msg_ptr->thread_idx,
                    invalid_txs);
                if (txs_ptr == nullptr) {
                    ZJC_ERROR("invalid consensus kNormal, txs not equal to leader. pool_index: %d, gid: %s, tx size: %u",
                        bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(), bft_msg.tx_bft().tx_hash_list_size());
                }
            }
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        }
    } else {
        ZJC_ERROR("invalid consensus, tx empty.");
        return nullptr;
    }

    if (txs_ptr != nullptr && (int32_t)txs_ptr->txs.size() != bft_msg.tx_bft().tx_hash_list_size()) {
        ZJC_ERROR("invalid consensus, txs not equal to leader.");
        txs_ptr = nullptr;
    }
    
    if (txs_ptr == nullptr) {
        return nullptr;
    }

    if (txs_ptr->tx_type == pools::protobuf::kNormalFrom) {
        if (block_mgr_->ShouldStopConsensus()) {
            ZJC_DEBUG("should stop consensus.");
            return nullptr;
        }
    }

    txs_ptr->thread_index = msg_ptr->thread_idx;
    txs_ptr->pool_index = bft_msg.pool_index();
    ZbftPtr bft_ptr = nullptr;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        bft_ptr = std::make_shared<RootZbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            txs_pools_,
            tm_block_mgr_);
    } else {
        bft_ptr = std::make_shared<Zbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            txs_pools_,
            tm_block_mgr_);
    }
    
    if (InitZbftPtr(bft_msg.leader_idx(), elect_item, bft_ptr) != kConsensusSuccess) {
//         assert(false);
        return nullptr;
    }

    bft_ptr->set_gid(bft_msg.prepare_gid());
    bft_ptr->set_network_id(bft_msg.net_id());
    bft_ptr->set_member_count(elect_item.member_size);
    ZJC_DEBUG("success create bft: %s, tx size: %u",
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(), txs_ptr->txs.size());
    return bft_ptr;
}

int BftManager::AddBft(ZbftPtr& bft_ptr) {
    auto& tmp_bft = pools_with_zbfts_[bft_ptr->pool_index()];
    if (tmp_bft != nullptr && tmp_bft->gid() == bft_ptr->gid()) {
        assert(false);
        return kConsensusError;
    }

    if (tmp_bft != nullptr &&
            tmp_bft->height() != common::kInvalidUint64 &&
            bft_ptr->pool_index() == tmp_bft->pool_index() &&
            bft_ptr->height() <= tmp_bft->height()) {
        if (bft_ptr->height() == tmp_bft->height()) {
            if (tmp_bft->consensus_status() != kConsensusPrepare) {
                ZJC_DEBUG("elect height error: %u, %lu %lu, %s, %s, status error: %d",
                    bft_ptr->pool_index(), bft_ptr->height(), tmp_bft->height(),
                    common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                    common::Encode::HexEncode(tmp_bft->gid()).c_str(),
                    tmp_bft->consensus_status());
                return kConsensusError;
            }

            ZJC_DEBUG("remove bft gid: %s, pool_index: %d",
                common::Encode::HexEncode(tmp_bft->gid()).c_str(),
                bft_ptr->pool_index());
            tmp_bft->Destroy();
            pools_with_zbfts_[bft_ptr->pool_index()] = nullptr;
        } else {
            ZJC_DEBUG("elect height error: %u, %lu %lu, %s, %s",
                bft_ptr->pool_index(), bft_ptr->height(), tmp_bft->height(),
                common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                common::Encode::HexEncode(tmp_bft->gid()).c_str());
            return kConsensusError;
        }
    }

    pools_with_zbfts_[bft_ptr->pool_index()] = bft_ptr;
    ZJC_DEBUG("success add bft pool idx: %d, add gid: %s",
        bft_ptr->pool_index(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

ZbftPtr BftManager::GetBft(uint32_t pool_index, const std::string& gid) {
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    if (bft_ptr == nullptr) {
        return nullptr;
    }

    if (bft_ptr->gid() == gid) {
        return bft_ptr;
    }
    
    return nullptr;
}

void BftManager::RemoveBftWithBlockHeight(uint32_t pool_index, uint64_t height) {
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    if (bft_ptr == nullptr) {
        return;
    }

    if (bft_ptr->prepare_block() == nullptr || bft_ptr->prepare_block()->height() <= height) {
        ZJC_DEBUG("remove bft gid: %s, pool_index: %d",
            common::Encode::HexEncode(bft_ptr->gid()).c_str(), pool_index);
        bft_ptr->Destroy();
        pools_with_zbfts_[pool_index] = nullptr;
    }
}

void BftManager::RemoveBft(uint32_t pool_index, const std::string& gid) {
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    if (bft_ptr == nullptr) {
        return;
    }

    auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[pool_index];
    if (bft_ptr->gid() != gid) {
        return;
    }

    if (bft_ptr->consensus_status() == kConsensusPreCommit) {
        if (bft_ptr->this_node_is_leader()) {
            ReConsensusBft(bft_ptr);
        } else if (bft_ptr->IsChangedLeader()) {
            ReConsensusChangedLeaderBft(bft_ptr);
        }
    } else {
        if (bft_ptr->consensus_status() == kConsensusPrepare) {
            if (!bft_ptr->this_node_is_leader()) {
                return;
            }
        }

        bft_ptr->Destroy();
        pools_with_zbfts_[pool_index] = nullptr;
        ZJC_DEBUG("remove bft gid: %s, pool_index: %d", common::Encode::HexEncode(gid).c_str(), pool_index);
    }
}

void BftManager::ReConsensusChangedLeaderBft(ZbftPtr& bft_ptr) {
//     auto msg_ptr = std::make_shared<transport::TransportMessage>();
//     msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
//     auto elect_item_ptr = elect_items_[elect_item_idx_];
//     if (elect_item_ptr->elect_height != bft_ptr->changed_leader_elect_height()) {
//         elect_item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
//         if (elect_item_ptr == nullptr ||
//                 elect_item_ptr->elect_height != bft_ptr->changed_leader_elect_height()) {
//             ZJC_DEBUG("elect height error: %lu, %lu, %lu",
//                 bft_ptr->changed_leader_elect_height(),
//                 elect_items_[elect_item_idx_]->elect_height,
//                 elect_items_[(elect_item_idx_ + 1) % 2]->elect_height);
//             return;
//         }
//     }
// 
//     auto next_prepare_bft = Start(
//         msg_ptr->thread_idx,
//         msg_ptr->response);
//     if (next_prepare_bft == nullptr) {
//         return;
//     }
// 
//     SetDefaultResponse(msg_ptr);
//     std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
//     msg_ptr->tmp_ptr = &zbft_vec;
//     auto elect_item = *elect_item_ptr;
//     std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
//     ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
//         common::Encode::HexEncode(next_prepare_bft->gid()).c_str());
//     zbft_vec[0] = next_prepare_bft;
//     common::BftMemberPtr mem_ptr = nullptr;
//     CreateResponseMessage(
//         elect_item,
//         false,
//         zbft_vec,
//         msg_ptr,
//         mem_ptr);
//     next_prepare_bft->AfterNetwork();
//     if (zbft_vec[1] != nullptr) {
//         zbft_vec[1]->AfterNetwork();
//     }
}

void BftManager::CheckMessageTimeout(uint8_t thread_index) {
    auto& msg_set = backup_prapare_msg_queue_[thread_index];
    auto now_tm_us = common::TimeUtils::TimestampUs();
    auto iter = msg_set.begin();
    while (iter != msg_set.end()) {
        auto msg_ptr = *iter;
        assert(msg_ptr->thread_idx == thread_index);
        if (msg_ptr->timeout < now_tm_us) {
            msg_set.erase(iter);
            break;
        }

        if (msg_ptr->prev_timestamp <= now_tm_us) {
            msg_ptr->prev_timestamp = now_tm_us + transport::kMessagePeriodUs;
            msg_set.erase(iter);
            HandleMessage(msg_ptr);
            break;
        }

        ++iter;
    }
}

void BftManager::CheckTimeout(uint8_t thread_idx) {
    auto now_timestamp_us = common::TimeUtils::TimestampUs();
    if (prev_checktime_out_milli_ > now_timestamp_us / 1000lu) {
        return;
    }

    auto now_ms = now_timestamp_us / 1000lu;
    prev_checktime_out_milli_ = now_timestamp_us / 1000 + kCheckTimeoutPeriodMilli;
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    for (uint32_t pool_index = 0; pool_index < common::kInvalidPoolIndex; ++pool_index) {
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_index] != thread_idx) {
            continue;
        }

        auto& waiting_agg_block_map = waiting_agg_verify_blocks_[pool_index];
        auto witer = waiting_agg_block_map.begin();
        while (witer != waiting_agg_block_map.end()) {
            auto& block_ptr = witer->second;
            if (block_ptr->electblock_height() > elect_item_ptr->elect_height) {
                break;
            }

            if (pools_mgr_->is_next_block_checked(block_ptr->pool_index(), block_ptr->height(), block_ptr->hash())) {
                HandleSyncedBlock(thread_idx, block_ptr);
                witer = waiting_agg_block_map.erase(witer);
            } else {
                if (block_ptr->height() < pools_mgr_->latest_height(block_ptr->pool_index())) {
                    ++witer;
                    continue;
                }

                if (witer != waiting_agg_block_map.begin()) {
                    ++witer;
                    continue;
                }

                if (!block_agg_valid_func_(thread_idx, *block_ptr)) {
                    ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                        block_ptr->network_id(),
                        block_ptr->pool_index(),
                        block_ptr->height(),
                        common::Encode::HexEncode(GetBlockHash(*block_ptr)).c_str());
                    break;
                }

                HandleSyncedBlock(thread_idx, block_ptr);
                witer = waiting_agg_block_map.erase(witer);
            }
        }

        if (pools_with_zbfts_[pool_index] == nullptr) {
            continue;
        }

        auto bft_ptr = pools_with_zbfts_[pool_index];
//         if (bft_ptr->pool_mod_num() >= 0 && bft_ptr->pool_mod_num() < elect_item_ptr->leader_count) {
//             auto valid_leader_idx = elect_item_ptr->mod_with_leader_index[bft_ptr->pool_mod_num()];
//             if (valid_leader_idx >= (int32_t)elect_item_ptr->members->size()) {
//                 ZJC_DEBUG("invalid leader index %u, mod num: %d, gid: %s",
//                     valid_leader_idx, bft_ptr->pool_mod_num(),
//                     common::Encode::HexEncode(bft_ptr->gid()).c_str());
//                 assert(false);
//             } else {
//                 if ((int32_t)bft_ptr->leader_index() != valid_leader_idx &&
//                         elect_item_ptr->change_leader_time_valid < now_ms &&
//                         bft_ptr->timeout(now_timestamp_us) &&
//                         bft_ptr->consensus_status() == kConsensusPreCommit) {
//                     if ((int32_t)bft_ptr->changed_leader_new_index() != valid_leader_idx) {
//                         ChangePrecommitBftLeader(bft_ptr, valid_leader_idx, *elect_item_ptr);
//                     }
//                 }
//             }
//         } else {
//             ZJC_DEBUG("pool mod invalid: %u, leader size: %u", bft_ptr->pool_mod_num(), elect_item_ptr->leader_count);
//             if (bft_ptr->pool_mod_num() < 0) {
//                 assert(false);
//             }
//         }

        if (bft_ptr->timeout(now_timestamp_us)) {
            RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
        }
    }
}

void BftManager::ReConsensusPrepareBft(const ElectItem& elect_item, ZbftPtr& bft_ptr) {
//     if (bft_ptr->consensus_status() != kConsensusLeaderWaitingBlock) {
//         return;
//     }
// 
//     if (elect_item.local_node_member_index >= elect_item.members->size()) {
//         return;
//     }
// 
//     auto msg_ptr = std::make_shared<transport::TransportMessage>();
//     msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
//     SetDefaultResponse(msg_ptr);
//     std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
//     msg_ptr->tmp_ptr = &zbft_vec;
//     std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
//     ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
//         common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
//     msg_ptr->header.mutable_zbft()->set_agree_precommit(true);
//     msg_ptr->header.mutable_zbft()->set_agree_commit(true);
//     msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
//     LeaderCallPrecommit(elect_item, bft_ptr, msg_ptr);
//     ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
//     zbft_vec[0] = nullptr;
//     zbft_vec[1] = bft_ptr;
//     common::BftMemberPtr mem_ptr = nullptr;
//     CreateResponseMessage(
//         elect_item,
//         false,
//         zbft_vec,
//         msg_ptr,
//         mem_ptr);
//     bft_ptr->AfterNetwork();
}

void BftManager::ReConsensusBft(ZbftPtr& bft_ptr) {
//     assert(bft_ptr->consensus_status() == kConsensusPreCommit);
//     auto tmp_msg_ptr = bft_ptr->reconsensus_msg_ptr();
//     if (tmp_msg_ptr != nullptr) {
//         transport::TcpTransport::Instance()->SetMessageHash(
//             tmp_msg_ptr->header, tmp_msg_ptr->thread_idx);
//         if (!LeaderSignMessage(tmp_msg_ptr)) {
//             return;
//         }
// 
//         bft_ptr->reset_timeout();
//         network::Route::Instance()->Send(tmp_msg_ptr);
//         return;
//     }
// 
//     auto msg_ptr = std::make_shared<transport::TransportMessage>();
//     msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
//     auto elect_item_ptr = elect_items_[elect_item_idx_];
//     if (elect_item_ptr->elect_height != bft_ptr->elect_height()) {
//         elect_item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
//         if (elect_item_ptr->elect_height != bft_ptr->elect_height()) {
//             ZJC_DEBUG("elect height error: %lu, %lu, %lu",
//                 bft_ptr->elect_height(),
//                 elect_items_[elect_item_idx_]->elect_height,
//                 elect_items_[(elect_item_idx_ + 1) % 2]->elect_height);
//             return;
//         }
//     }
// 
//     SetDefaultResponse(msg_ptr);
//     std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
//     msg_ptr->tmp_ptr = &zbft_vec;
//     auto elect_item = *elect_item_ptr;
//     std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
//     ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
//         common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
//     bft_ptr->set_precoimmit_hash();
//     ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
//         common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
//     libff::alt_bn128_G1 sign;
//     if (bls_mgr_->Sign(
//             bft_ptr->min_aggree_member_count(),
//             bft_ptr->member_count(),
//             bft_ptr->local_sec_key(),
//             bft_ptr->g1_precommit_hash(),
//             &sign) != bls::kBlsSuccess) {
//         ZJC_ERROR("leader signature error.");
//         return;
//     }
// 
//     bft_ptr->RechallengePrecommitClear();
//     bft_ptr->reset_timeout();
//     if (bft_ptr->LeaderCommitOk(
//             elect_item.local_node_member_index,
//             sign,
//             security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
//         ZJC_ERROR("leader commit failed!");
//         return;
//     }
// 
//     ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
//     zbft_vec[0] = nullptr;
//     zbft_vec[1] = bft_ptr;
//     common::BftMemberPtr mem_ptr = nullptr;
//     CreateResponseMessage(
//         elect_item,
//         false,
//         zbft_vec,
//         msg_ptr,
//         mem_ptr);
//     bft_ptr->set_reconsensus_msg_ptr(msg_ptr->response);
//     bft_ptr->AfterNetwork();
}

int BftManager::LeaderPrepare(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        ZbftPtr& commited_bft_ptr) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    msg_ptr->thread_idx = bft_ptr->thread_index();
    ZJC_DEBUG("now leader call prepare: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    int res = bft_ptr->Prepare(true);
    if (res != kConsensusSuccess) {
        assert(false);
        return kConsensusError;
    }

    ZJC_DEBUG("now leader add bft: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    res = AddBft(bft_ptr);
    if (res != kConsensusSuccess) {
        ZJC_ERROR("AddBft failed[%u].", res);
        return res;
    }

    header.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    header.set_des_dht_key(dht_key.StrKey());
    header.set_type(common::kConsensusMessage);
    header.set_hop_count(0);

    auto broad_param = header.mutable_broadcast();
    auto& bft_msg = *header.mutable_zbft();
    zbft::protobuf::TxBft& tx_bft = *bft_msg.mutable_tx_bft();
    tx_bft.set_height(bft_ptr->prepare_block()->height());
    auto& tx_map = bft_ptr->txs_ptr()->txs;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        tx_bft.add_tx_hash_list(iter->first);
    }

    bft_msg.set_leader_idx(elect_item.local_node_member_index);
    bft_msg.set_prepare_gid(bft_ptr->gid());
    ZJC_INFO("====0.1 leader send prepare msg: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_elect_height(bft_ptr->elect_height());
    bft_msg.mutable_tx_bft()->set_tx_type(bft_ptr->txs_ptr()->tx_type);
    bft_msg.set_member_index(elect_item.local_node_member_index);
    if (commited_bft_ptr != nullptr) {
        bft_msg.set_commit_gid(commited_bft_ptr->gid());
        bft_msg.set_agree_commit(true);
        auto& bls_commit_sign = commited_bft_ptr->bls_commit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->Y));
    }

    assert(elect_item.elect_height > 0);
    bft_ptr->reset_timeout();
    bft_ptr->set_consensus_status(kConsensusPrepare);
    transport::TcpTransport::Instance()->SetMessageHash(header, msg_ptr->thread_idx);
    ZJC_DEBUG("now leader sign prepare: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    if (!LeaderSignMessage(msg_ptr)) {
        assert(false);
        return kConsensusError;
    }

    ZJC_DEBUG("now leader send prepare: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
#ifdef ZJC_UNITTEST
    now_msg_[msg_ptr->thread_idx] = msg_ptr;
#else
    ZJC_DEBUG("this node is leader and start bft: %s,"
        "pool index: %d, thread index: %d, prepare hash: %s, pre hash: %s, "
        "elect height: %lu, hash64: %lu",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_ptr->pool_index(),
        bft_ptr->thread_index(),
        common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
        bft_ptr->prepare_block() == nullptr ? "" : common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
        elect_item.elect_height,
        msg_ptr->header.hash64());
    network::Route::Instance()->Send(msg_ptr);
#endif

    bft_ptr->AfterNetwork();
    pools_prev_bft_timeout_[bft_ptr->pool_index()] = common::TimeUtils::TimestampMs() + 10000lu;
    return kConsensusSuccess;
}

bool BftManager::CheckAggSignValid(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return false;
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    if (!bft_ptr->set_bls_precommit_agg_sign(
            sign,
            bft_ptr->precommit_bls_agg_verify_hash())) {
        ZJC_WARN("verify agg sign error!");
        return false;
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    return true;
}

void BftManager::BackupSendPrepareMessage(
        const ElectItem& elect_item,
        const transport::MessagePtr& leader_msg_ptr,
        bool agree,
        const std::vector<uint8_t>& invalid_txs) {
    auto pool_index = leader_msg_ptr->header.zbft().pool_index();
    auto& gid = leader_msg_ptr->header.zbft().prepare_gid();
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    msg_ptr->thread_idx = leader_msg_ptr->thread_idx;
    auto& bft_msg = *header.mutable_zbft();
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kConsensusMessage);
    header.set_hop_count(0);
    bft_msg.set_leader_idx(-1);
    bft_msg.set_prepare_gid(gid);
    bft_msg.set_member_index(elect_item.local_node_member_index);
    bft_msg.set_pool_index(pool_index);
    if (agree) {
        auto& bft_ptr = pools_with_zbfts_[pool_index];
        assert(bft_ptr != nullptr);
        bft_msg.set_prepare_hash(bft_ptr->prepare_hash());
        bft_msg.mutable_tx_bft()->set_tx_type(bft_ptr->txs_ptr()->tx_type);
        bft_msg.set_agree_precommit(true);
        std::string bls_sign_x;
        std::string bls_sign_y;
        if (bls_mgr_->Sign(
                bft_ptr->min_aggree_member_count(),
                bft_ptr->member_count(),
                bft_ptr->local_sec_key(),
                bft_ptr->g1_prepare_hash(),
                &bls_sign_x,
                &bls_sign_y) != bls::kBlsSuccess) {
            return;
        }

        bft_msg.set_bls_sign_x(bls_sign_x);
        bft_msg.set_bls_sign_y(bls_sign_y);
    } else {
        bft_msg.set_agree_precommit(false);
        if (!invalid_txs.empty()) {
            for (uint32_t i = 0; i < invalid_txs.size(); ++i) {
                bft_msg.add_invaid_txs(invalid_txs[i]);
            }
        }
    }

    auto leader_member = (*elect_item.members)[leader_msg_ptr->header.zbft().leader_idx()];
    dht::DhtKeyManager dht_key(
        common::GlobalInfo::Instance()->network_id(),
        leader_member->id);
    header.set_des_dht_key(dht_key.StrKey());
    if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
        auto dht_ptr = network::DhtManager::Instance()->GetDht(
            leader_msg_ptr->header.src_sharding_id());
        if (dht_ptr != nullptr) {
            auto nodes = dht_ptr->readonly_hash_sort_dht();
            for (auto iter = nodes->begin(); iter != nodes->end(); ++iter) {
                if ((*iter)->id == leader_member->id) {
                    leader_member->public_ip = common::IpToUint32((*iter)->public_ip.c_str());
                    leader_member->public_port = (*iter)->public_port;
                    break;
                }
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(header, leader_msg_ptr->header.zbft().leader_idx());
    if (!SetBackupEcdhData(msg_ptr, leader_member)) {
        assert(false);
        return;
    }

    if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
        network::Route::Instance()->Send(msg_ptr);
        ZJC_DEBUG("backup direct send bft message prepare gid: %s, hash64: %lu, src hash64: %lu, res: %d, try_times: %d",
            common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
            header.hash64(),
            leader_msg_ptr->header.hash64(),
            0,
            0);
    } else {
        auto to_ip = common::Uint32ToIp(leader_member->public_ip);
        transport::TcpTransport::Instance()->Send(
            msg_ptr->thread_idx,
            to_ip,
            leader_member->public_port,
            header);
        ZJC_DEBUG("backup direct send bft message prepare gid: %s, "
            "hash64: %lu, src hash64: %lu, res: %d, try_times: %d, %s:%u",
            common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
            header.hash64(),
            leader_msg_ptr->header.hash64(),
            0,
            0,
            to_ip.c_str(), leader_member->public_port);
    }
}

void BftManager::BackupSendPrecommitMessage(
        const ElectItem& elect_item,
        const transport::MessagePtr& leader_msg_ptr,
        bool agree) {
    auto pool_index = leader_msg_ptr->header.zbft().pool_index();
    auto& gid = leader_msg_ptr->header.zbft().precommit_gid();
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    msg_ptr->thread_idx = leader_msg_ptr->thread_idx;
    auto& bft_msg = *header.mutable_zbft();
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kConsensusMessage);
    header.set_hop_count(0);
    bft_msg.set_pool_index(pool_index);
    bft_msg.clear_prepare_gid();
    bft_msg.set_leader_idx(-1);
    bft_msg.set_member_index(elect_item.local_node_member_index);
    bft_msg.set_precommit_gid(gid);
    ZJC_INFO("====1.2.5 %s, agree: %d, member idx: %d", common::Encode::HexEncode(gid).c_str(), agree, bft_msg.member_index());
    if (agree) {
        auto& bft_ptr = pools_with_zbfts_[pool_index];
        assert(bft_ptr != nullptr);
        bft_msg.set_prepare_hash(bft_ptr->prepare_hash());
        bft_msg.mutable_tx_bft()->set_tx_type(bft_ptr->txs_ptr()->tx_type);
        bft_msg.set_agree_commit(true);
        std::string bls_sign_x;
        std::string bls_sign_y;
        if (bls_mgr_->Sign(
                bft_ptr->min_aggree_member_count(),
                bft_ptr->member_count(),
                bft_ptr->local_sec_key(),
                bft_ptr->g1_precommit_hash(),
                &bls_sign_x,
                &bls_sign_y) != bls::kBlsSuccess) {
            assert(false);
            return;
        }

        bft_msg.set_bls_sign_x(bls_sign_x);
        bft_msg.set_bls_sign_y(bls_sign_y);
        ZJC_DEBUG("backup success sign bls commit hash: %s, g1 hash: %s, gid: %s",
            common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(bft_ptr->g1_precommit_hash().X).c_str(),
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
    } else {
        bft_msg.set_agree_commit(false);
    }

    auto leader_member = (*elect_item.members)[leader_msg_ptr->header.zbft().leader_idx()];
    dht::DhtKeyManager dht_key(
        common::GlobalInfo::Instance()->network_id(),
        leader_member->id);
    header.set_des_dht_key(dht_key.StrKey());
    if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
        auto dht_ptr = network::DhtManager::Instance()->GetDht(
            leader_msg_ptr->header.src_sharding_id());
        if (dht_ptr != nullptr) {
            auto nodes = dht_ptr->readonly_hash_sort_dht();
            for (auto iter = nodes->begin(); iter != nodes->end(); ++iter) {
                if ((*iter)->id == leader_member->id) {
                    leader_member->public_ip = common::IpToUint32((*iter)->public_ip.c_str());
                    leader_member->public_port = (*iter)->public_port;
                    break;
                }
            }
        }
    }
    ZJC_INFO("====1.2.6 %s, agree: %d, member idx: %d", common::Encode::HexEncode(gid).c_str(), agree, bft_msg.member_index());
    transport::TcpTransport::Instance()->SetMessageHash(header, leader_msg_ptr->header.zbft().leader_idx());
    if (!SetBackupEcdhData(msg_ptr, leader_member)) {
        assert(false);
        return;
    }

    ZJC_INFO("====1.2.7 %s, agree: %d, member idx: %d", common::Encode::HexEncode(gid).c_str(), agree, bft_msg.member_index());
    if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
        network::Route::Instance()->Send(msg_ptr);
        ZJC_DEBUG("backup direct send bft message prepare gid: %s, hash64: %lu, src hash64: %lu, res: %d, try_times: %d",
            common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
            header.hash64(),
            leader_msg_ptr->header.hash64(),
            0,
            0);
    } else {
        auto to_ip = common::Uint32ToIp(leader_member->public_ip);
        transport::TcpTransport::Instance()->Send(
            msg_ptr->thread_idx,
            to_ip,
            leader_member->public_port,
            header);
        ZJC_DEBUG("backup direct send bft message prepare gid: %s, hash64: %lu, src hash64: %lu, res: %d, try_times: %d, %s:%u",
            common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
            header.hash64(),
            leader_msg_ptr->header.hash64(),
            0,
            0,
            to_ip.c_str(), leader_member->public_port);
    }
}

int BftManager::BackupPrepare(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        std::vector<uint8_t>* invalid_txs) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (bft_msg.has_agree_precommit() && !bft_msg.agree_precommit()) {
        ZJC_DEBUG("precommit failed, remove all prepare gid; %s, precommit gid: %s, commit gid: %s",
            common::Encode::HexEncode(bft_msg.oppose_prepare_gid()).c_str(),
            common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
            common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
        auto& prepare_bft = pools_with_zbfts_[bft_msg.pool_index()];
        if (prepare_bft != nullptr) {
            prepare_bft->Destroy();
            pools_with_zbfts_[bft_msg.pool_index()] = nullptr;
        }
    }

    ZJC_DEBUG("has prepare: %d, prepare gid: %s, precommit gid: %s, commit gid: %s, set pool index: %u",
        bft_msg.has_prepare_gid(),
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
        common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
        common::Encode::HexEncode(bft_msg.commit_gid()).c_str(),
        bft_msg.pool_index());
    auto now_ms = common::TimeUtils::TimestampMs();
    auto now_elect_item = elect_items_[elect_item_idx_];
    if (now_elect_item->change_leader_time_valid <= now_ms &&
            now_elect_item->elect_height != elect_item.elect_height) {
        ZJC_ERROR("BackupPrepare failed %s invalid elect height: %lu, %lu",
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
            now_elect_item->elect_height,
            elect_item.elect_height);
        return kConsensusOppose;
    }

    auto bft_ptr = CreateBftPtr(elect_item, msg_ptr, invalid_txs);
    if (bft_ptr == nullptr ||
            bft_ptr->txs_ptr() == nullptr ||
            bft_ptr->txs_ptr()->txs.empty()) {
        // oppose
        ZJC_ERROR("create bft ptr failed backup create consensus bft gid: %s",
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        return kConsensusOppose;
    }

    int prepare_res = bft_ptr->Prepare(false);
    if (prepare_res != kConsensusSuccess || bft_ptr->prepare_block() == nullptr) {
        ZJC_ERROR("prepare failed gid: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        return kConsensusOppose;
    }

    ZJC_DEBUG("success create bft ptr backup create consensus bft gid: %s",
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
#ifdef ZJC_UNITTEST
    if (test_for_prepare_evil_) {
        ZJC_ERROR("1 bft backup prepare failed! not agree bft gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusOppose;
    }
#endif
    ZJC_DEBUG("backup create consensus bft prepare hash: %s, gid: %s, tx size: %d",
        common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
        bft_ptr->txs_ptr()->txs.size());
    if (bft_ptr->local_prepare_hash().empty()) {
        ZJC_DEBUG("failed backup create consensus bft prepare hash: %s, prehash: %s, "
            "leader prehash: %s, pre height: %lu, leader pre height: %lu, gid: %s, tx size: %d",
            common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
            common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
            common::Encode::HexEncode(bft_msg.prepare_hash()).c_str(),
            bft_ptr->prepare_block()->height(),
            bft_msg.prepare_height(),
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
            bft_ptr->txs_ptr()->txs.size());
        //             if (!bft_msg.prepare_hash().empty() && bft_ptr->prepare_block()->prehash() != bft_msg.prepare_hash()) {
        //                 assert(false);
        //             }
        return kConsensusOppose;
    }

    if (AddBft(bft_ptr) != kConsensusSuccess) {
        return kConsensusOppose;
    }

    bft_ptr->set_prepare_msg_ptr(msg_ptr);
    bft_ptr->set_consensus_status(kConsensusPrepare);
    return kConsensusAgree;
}

void BftManager::LeaderSendPrecommitMessage(const transport::MessagePtr& leader_msg_ptr, bool agree) {
    auto pool_index = leader_msg_ptr->header.zbft().pool_index();
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    auto& gid = leader_msg_ptr->header.zbft().prepare_gid();
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    msg_ptr->thread_idx = leader_msg_ptr->thread_idx;
    auto& bft_msg = *header.mutable_zbft();
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kConsensusMessage);
    header.set_hop_count(0);
    auto& elect_item = *bft_ptr->elect_item_ptr();
    assert(elect_item.elect_height > 0);
    if (msg_ptr->thread_idx == 0) {
        auto& thread_set = elect_item.thread_set;
        auto thread_item = thread_set[msg_ptr->thread_idx];
        if (thread_item != nullptr && !thread_item->synced_ip) {
            if (thread_item->valid_ip_count * 10 / 9 >= (int32_t)elect_item.members->size()) {
                for (uint32_t i = 0; i < elect_item.members->size(); ++i) {
                    bft_msg.add_ips(thread_item->member_ips[i]);
                    thread_item->all_members_ips[i][thread_item->member_ips[i]] = 1;
                    if (elect_item.leader_count <= 8) {
                        (*elect_item.members)[i]->public_ip = thread_item->member_ips[i];
                        ZJC_DEBUG("leader member set ip %d, %u", i, (*elect_item.members)[i]->public_ip);
                    }
                }

                thread_item->synced_ip = true;
            }
        }
    }

    bft_msg.set_leader_idx(elect_item.local_node_member_index);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

    bft_msg.clear_prepare_gid();
    bft_msg.set_leader_idx(elect_item.local_node_member_index);
    bft_msg.set_precommit_gid(bft_ptr->gid());
    bft_msg.set_pool_index(pool_index);
    bft_msg.set_member_index(elect_item.local_node_member_index);
    bft_msg.set_agree_precommit(agree);
    bft_msg.mutable_tx_bft()->set_tx_type(bft_ptr->txs_ptr()->tx_type);
    bft_msg.set_elect_height(bft_ptr->elect_height());
    if (agree) {
        auto& bls_precommit_sign = bft_ptr->bls_precommit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->Y));
    }

    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    assert(header.has_broadcast());
    transport::TcpTransport::Instance()->SetMessageHash(header, msg_ptr->thread_idx);
    if (!LeaderSignMessage(msg_ptr)) {
        return;
    }

    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("leader broadcast bft message prepare gid: %s, precommit: %s, commit: %s, hash64: %lu",
        common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
        header.hash64());
    bft_ptr->AfterNetwork();
}

void BftManager::LeaderSendCommitMessage(const transport::MessagePtr& leader_msg_ptr, bool agree) {
    auto pool_index = leader_msg_ptr->header.zbft().pool_index();
    auto& bft_ptr = pools_with_zbfts_[pool_index];
    auto& gid = leader_msg_ptr->header.zbft().prepare_gid();
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    msg_ptr->thread_idx = leader_msg_ptr->thread_idx;
    auto& bft_msg = *header.mutable_zbft();
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kConsensusMessage);
    header.set_hop_count(0);
    auto& elect_item = *bft_ptr->elect_item_ptr();
    assert(elect_item.elect_height > 0);
    if (msg_ptr->thread_idx == 0) {
        auto& thread_set = elect_item.thread_set;
        auto thread_item = thread_set[msg_ptr->thread_idx];
        if (thread_item != nullptr && !thread_item->synced_ip) {
            if (thread_item->valid_ip_count * 10 / 9 >= (int32_t)elect_item.members->size()) {
                for (uint32_t i = 0; i < elect_item.members->size(); ++i) {
                    bft_msg.add_ips(thread_item->member_ips[i]);
                    thread_item->all_members_ips[i][thread_item->member_ips[i]] = 1;
                    if (elect_item.leader_count <= 8) {
                        (*elect_item.members)[i]->public_ip = thread_item->member_ips[i];
                        ZJC_DEBUG("leader member set ip %d, %u", i, (*elect_item.members)[i]->public_ip);
                    }
                }

                thread_item->synced_ip = true;
            }
        }
    }

    bft_msg.set_leader_idx(elect_item.local_node_member_index);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

    bft_msg.clear_prepare_gid();
    bft_msg.clear_precommit_gid();
    bft_msg.set_leader_idx(elect_item.local_node_member_index);
    bft_msg.set_commit_gid(bft_ptr->gid());
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_agree_commit(agree);
    bft_msg.set_member_index(elect_item.local_node_member_index);
    bft_msg.mutable_tx_bft()->set_tx_type(bft_ptr->txs_ptr()->tx_type);
    bft_msg.set_elect_height(bft_ptr->elect_height());
    if (agree) {
        auto& bls_commit_sign = bft_ptr->bls_commit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->Y));
    }

    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    assert(header.has_broadcast());
    transport::TcpTransport::Instance()->SetMessageHash(header, msg_ptr->thread_idx);
    if (!LeaderSignMessage(msg_ptr)) {
        return;
    }

    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("leader broadcast bft message prepare gid: %s, precommit: %s, commit: %s, hash64: %lu",
        common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
        header.hash64());
}

void BftManager::LeaderHandleZbftMessage(const transport::MessagePtr& msg_ptr) {
    auto& zbft = msg_ptr->header.zbft();
    if (isPrepare(zbft)) {
        int res = LeaderHandlePrepare(msg_ptr);
        ZJC_INFO("====1.1 leader receive prepare msg: %s, res: %d, leader: %d, member: %d", common::Encode::HexEncode(zbft.prepare_gid()).c_str(), res, zbft.leader_idx(), zbft.member_index());
        if (res == kConsensusAgree) {
            LeaderSendPrecommitMessage(msg_ptr, true);
        } else if (res == kConsensusOppose) {
            RemoveBft(zbft.pool_index(), zbft.prepare_gid());
        } else {
            // waiting
        }
    }

    if (isPrecommit(zbft)) {
        ZJC_DEBUG("has precommit now leader handle gid: %s",
            common::Encode::HexEncode(zbft.precommit_gid()).c_str());
        auto bft_ptr = LeaderGetZbft(msg_ptr, zbft.precommit_gid());
        ZJC_INFO("====1.2 leader receive precommit msg: %s, has res: %d, leader: %d, member: %d", common::Encode::HexEncode(zbft.precommit_gid()).c_str(), bft_ptr != nullptr, zbft.leader_idx(), zbft.member_index());
        if (bft_ptr == nullptr) {
//             ZJC_ERROR("precommit get bft failed: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            return;
        }

        auto& member_ptr = (*bft_ptr->members_ptr())[zbft.member_index()];
        if (zbft.agree_commit()) {
            if (LeaderCommit(bft_ptr, msg_ptr) == kConsensusAgree) {
                auto next_ptr = Start(msg_ptr->thread_idx, bft_ptr);
                if (next_ptr == nullptr) {
                    LeaderSendCommitMessage(msg_ptr, true);
                }

                auto& zjc_block = bft_ptr->prepare_block();
                if (zjc_block != nullptr) {
                    ZJC_DEBUG("now remove gid with height: %s, %u, %lu",
                        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                        zjc_block->pool_index(), zjc_block->height());
                    RemoveBftWithBlockHeight(zjc_block->pool_index(), zjc_block->height());
                    RemoveWaitingBlock(zjc_block->pool_index(), zjc_block->height());
                }

                RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
            }
        } else {
            if (bft_ptr->AddPrecommitOpposeNode(member_ptr->id) == kConsensusOppose) {
                ZJC_ERROR("gid: %s, set pool index: %u",
                    common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                    bft_ptr->pool_index());
                assert(false);
            }
        }
    }
}

int BftManager::LeaderHandlePrepare(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    ZJC_DEBUG("has prepare  now leader handle gid: %s",
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
    auto bft_ptr = LeaderGetZbft(msg_ptr, bft_msg.prepare_gid());
    if (bft_ptr == nullptr) {
        ZJC_DEBUG("prepare get bft failed: %s",
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        return kConsensusError;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    ZJC_DEBUG("has prepare  now leader handle gid: %s, agree precommit: %d, prepare hash: %s, local hash: %s",
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
        bft_msg.agree_precommit(),
        common::Encode::HexEncode(bft_msg.prepare_hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->prepare_hash()).c_str());
    if (bft_msg.agree_precommit()) {
        libff::alt_bn128_G1 sign;
        try {
            sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
            sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
            sign.Z = libff::alt_bn128_Fq::one();
        } catch (std::exception& e) {
            ZJC_ERROR("get invalid bls sign.");
            return kConsensusError;
        }

        auto& tx_bft = bft_msg.tx_bft();
        int res = bft_ptr->LeaderPrecommitOk(
            bft_msg.prepare_hash(),
            bft_msg.member_index(),
            sign,
            member_ptr->id);
        ZJC_DEBUG("LeaderHandleZbftMessage res: %d, mem: %d, precommit gid: %s",
            res,
            bft_msg.member_index(),
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        if (res == kConsensusLeaderWaitingBlock) {
            ZJC_DEBUG("invalid block and sync from other hash: %s, gid: %s",
                common::Encode::HexEncode(bft_ptr->leader_waiting_prepare_hash()).c_str(),
                common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            SyncConsensusBlock(
                msg_ptr->thread_idx,
                bft_ptr->pool_index(),
                bft_msg.prepare_gid());
        } else if (res == kConsensusAgree) {
            msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            if (bft_ptr->prepare_block() == nullptr) {
                ZJC_DEBUG("invalid block and sync from other gid: %s",
                    common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
                assert(false);
            }

            msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            if (LeaderCallPrecommit(bft_ptr, msg_ptr) != kConsensusSuccess) {
                return kConsensusError;
            }

            msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            return kConsensusAgree;
        } else if (res == kConsensusOppose) {
            return kConsensusOppose;
        }
    } else {
        for (int32_t i = 0; i < bft_msg.invaid_txs_size(); ++i) {
            bft_ptr->AddInvalidTx(bft_msg.invaid_txs(i));
        }

        if (bft_ptr->AddPrepareOpposeNode(member_ptr->id) == kConsensusOppose) {
            ZJC_DEBUG("gid: %s, set pool index: %u",
                common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                bft_ptr->pool_index());
            bft_ptr->set_consensus_status(kConsensusFailed);
            ZJC_ERROR("precommit call oppose now step: %d, gid: %s, prepare hash: %s,"
                " precommit gid: %s, agree commit: %d",
                bft_ptr->txs_ptr()->tx_type,
                common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
                common::Encode::HexEncode(bft_ptr->prepare_hash()).c_str(),
                common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
                bft_msg.agree_commit());
            auto& invalid_set = bft_ptr->invalid_txs();
            if (!invalid_set.empty()) {
                auto& txs_map = bft_ptr->txs_ptr()->txs;
                uint8_t index = 0;
                auto iter = txs_map.begin();
                while (iter != txs_map.end()) {
                    auto invalid_iter = invalid_set.find(index);
                    if (invalid_iter != invalid_set.end()) {
                        pools_mgr_->RemoveTx(bft_ptr->pool_index(), iter->first);
                        ZJC_DEBUG("remove invalid tx: %d, %s",
                            index,
                            common::Encode::HexEncode(iter->first).c_str());
                        iter = txs_map.erase(iter);
                    } else {
                        ++iter;
                    }
                   
                    ++index;
                }

                ZJC_DEBUG("remove bft gid: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
                pools_with_zbfts_[bft_ptr->thread_index()] = nullptr;
                bft_ptr->Destroy();
                Start(bft_ptr->thread_index(), nullptr);
            }

            return kConsensusOppose;
        }
    }

    return kConsensusSuccess;
}

ZbftPtr BftManager::LeaderGetZbft(
        const transport::MessagePtr& msg_ptr,
        const std::string& bft_gid) {
    auto& bft_msg = msg_ptr->header.zbft();
    auto& bft_ptr = pools_with_zbfts_[bft_msg.pool_index()];
    if (bft_ptr == nullptr || bft_ptr->gid() != bft_gid) {
        return nullptr;
    }

    if (bft_ptr->gid() != bft_gid) {
        ZJC_DEBUG("leader get bft gid failed[%s], pool: %u, hash64: %lu",
            common::Encode::HexEncode(bft_gid).c_str(), bft_msg.pool_index(), msg_ptr->header.hash64());
        assert(false);
        return nullptr;
    }

   
    if (!bft_ptr->this_node_is_leader()) {
        ZJC_DEBUG("not valid leader get bft gid failed[%s]",
            common::Encode::HexEncode(bft_gid).c_str());
        return nullptr;
    }

    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("backup message member index invalid. %d", bft_msg.member_index());
        return nullptr;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        ZJC_ERROR("verify backup valid error: %d!", bft_msg.member_index());
        return nullptr;
    }

    if (msg_ptr->thread_idx == 0) {
        auto& thread_set = bft_ptr->elect_item_ptr()->thread_set;
        auto thread_item = thread_set[msg_ptr->thread_idx];
        if (thread_item != nullptr && !thread_item->synced_ip) {
            if (thread_item->member_ips[bft_msg.member_index()] == 0) {
                thread_item->member_ips[bft_msg.member_index()] =
                    common::IpToUint32(msg_ptr->conn->PeerIp().c_str());
                ++thread_item->valid_ip_count;
                ZJC_DEBUG("leader member set ip %d, %s",
                    bft_msg.member_index(), msg_ptr->conn->PeerIp().c_str());
            }
        }
    }

    return bft_ptr;
}

int BftManager::LeaderCallPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    bft_ptr->set_precoimmit_hash();
    ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
        common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    libff::alt_bn128_G1 sign;
    if (bls_mgr_->Sign(
            bft_ptr->min_aggree_member_count(),
            bft_ptr->member_count(),
            bft_ptr->local_sec_key(),
            bft_ptr->g1_precommit_hash(),
            &sign) != bls::kBlsSuccess) {
        ZJC_ERROR("leader signature error.");
        return kConsensusError;
    }

    ZJC_DEBUG("leader success sign bls commit hash: %s, g1 hash: %s, gid: %s",
        common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(bft_ptr->g1_precommit_hash().X).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());

    if (bft_ptr->LeaderCommitOk(
            bft_ptr->elect_item_ptr()->local_node_member_index,
            sign,
            security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
        ZJC_ERROR("leader commit failed!");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusPreCommit);
    bft_ptr->reset_timeout();
    ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

int BftManager::BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("BackupPrecommit gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.agree_precommit()) {
        ZJC_DEBUG("BackupPrecommit gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

#ifdef ZJC_UNITTEST
    if (test_for_precommit_evil_) {
        ZJC_ERROR("1 bft backup precommit failed! not agree bft gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }
#endif

    bft_ptr->set_precoimmit_hash();
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusOppose;
    }

    if (!bft_ptr->verify_bls_precommit_agg_sign(
            sign,
            bft_ptr->precommit_bls_agg_verify_hash())) {
        ZJC_ERROR("backup verify leader agg sign failed.");
        return kConsensusOppose;
    }

    bft_ptr->set_consensus_status(kConsensusPreCommit);
    ZJC_DEBUG("BackupPrecommit success: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusAgree;
}

int BftManager::LeaderCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("LeaderCommit coming gid: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_ptr->this_node_is_leader()) {
        ZJC_ERROR("check leader error.%s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("bft_ptr->members_ptr()->size() <= bft_msg.member_index() gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    if (bft_msg.member_index() == elect::kInvalidMemberIndex) {
        ZJC_ERROR("bft_msg.member_index() == elect::kInvalidMemberIndex.%s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        ZJC_ERROR("verify backup valid error!%s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        assert(false);
        return kConsensusError;
    }

    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.%s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("bft_msg.member_index() == elect::kInvalidMemberIndex.%s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    int res = bft_ptr->LeaderCommitOk(
        bft_msg.member_index(),
        sign,
        member_ptr->id);
    if (res == kConsensusAgree) {
        if (bft_ptr->prepare_block() != nullptr) {
            HandleLocalCommitBlock(msg_ptr, bft_ptr);
            pools_prev_bft_timeout_[bft_ptr->pool_index()] = 0;
        } else {
            assert(false);
            return kConsensusError;
        }

        return kConsensusAgree;
    }

    ZJC_DEBUG("LeaderCommit coming gid: %s, res: %d",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(), res);
    return kConsensusSuccess;
}

void BftManager::HandleLocalCommitBlock(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr) {
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    auto& zjc_block = bft_ptr->prepare_block();
    // TODO: for test
    {
        auto& block = *bft_ptr->prepare_block();
        if (block.bls_agg_sign_x().empty()) {
            ZJC_DEBUG("not has bls agg sign: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
            return;
        }

        assert(block.hash() == GetBlockHash(block));
        auto g1_hash = libBLS::Bls::Hashing(block.hash());
        libff::alt_bn128_G2 common_pk = libff::alt_bn128_G2::zero();
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            block.electblock_height(),
            block.network_id(),
            &common_pk,
            nullptr);
        if (members == nullptr || common_pk == libff::alt_bn128_G2::zero()) {
            ZJC_ERROR("failed get elect members or common pk: %u, %lu, %d",
                block.network_id(),
                block.electblock_height(),
                (common_pk == libff::alt_bn128_G2::zero()));
            assert(false);
        }

        libff::alt_bn128_G1 sign;
        sign.X = libff::alt_bn128_Fq(common::Encode::HexEncode(block.bls_agg_sign_x()).c_str());
        sign.Y = libff::alt_bn128_Fq(common::Encode::HexEncode(block.bls_agg_sign_y()).c_str());
        sign.Z = libff::alt_bn128_Fq::one();
        ZJC_DEBUG("verification agg sign hash: %s, signx: %s, common pk x: %s",
            common::Encode::HexEncode(block.hash()).c_str(),
            common::Encode::HexEncode(block.bls_agg_sign_x()).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0).c_str());
        try {
#if MOCK_SIGN
            bool check_res = true;
#else            
            bool check_res = libBLS::Bls::Verification(g1_hash, sign, common_pk);
#endif                        
            if (!check_res) {
                assert(check_res);
                return;
            }

        } catch (std::exception& e) {
            ZJC_ERROR("verification agg sign failed");
            assert(false);
            return;
        }
    }


    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(zjc_block, bft_ptr->db_batch());
    new_block_cache_callback_(
        msg_ptr->thread_idx,
        queue_item_ptr->block_ptr,
        *queue_item_ptr->db_batch);
    
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    block_mgr_->ConsensusAddBlock(msg_ptr->thread_idx, queue_item_ptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    if (bft_ptr->this_node_is_leader()) {
        LeaderBroadcastBlock(msg_ptr->thread_idx, zjc_block);
    }
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    pools_mgr_->TxOver(
        zjc_block->pool_index(),
        zjc_block->tx_list());
    bft_ptr->set_consensus_status(kConsensusCommited);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (prev_tps_tm_us_ == 0) {
        prev_tps_tm_us_ = now_tm_us;
    }

    {
        common::AutoSpinLock auto_lock(prev_count_mutex_);
        prev_count_ += zjc_block->tx_list_size();
        if (now_tm_us > prev_tps_tm_us_ + 3000000lu) {
            ZJC_INFO("tps: %.2f", (double(prev_count_) / (double(now_tm_us - prev_tps_tm_us_) / 1000000.0)));
            prev_tps_tm_us_ = now_tm_us;
            prev_count_ = 0;
        }
    }

    ZJC_INFO("[NEW BLOCK] hash: %s, gid: %s, is leader: %d, leader idx: %d, thread idx: %d, key: %u_%u_%u_%u",
        common::Encode::HexEncode(zjc_block->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_ptr->this_node_is_leader(),
        bft_ptr->leader_index(),
        msg_ptr->thread_idx,
        zjc_block->network_id(),
        zjc_block->pool_index(),
        zjc_block->height(),
        zjc_block->electblock_height());
}

void BftManager::LeaderBroadcastBlock(
        uint8_t thread_index,
        const std::shared_ptr<block::protobuf::Block>& block) {
//     BroadcastWaitingBlock(thread_index, block);
    if (block->pool_index() == common::kRootChainPoolIndex) {
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            BroadcastBlock(thread_index, network::kNodeNetworkId, block);
        } else {
            BroadcastBlock(thread_index, network::kRootCongressNetworkId, block);
        }

        return;
    }

    if (block->tx_list_size() != 1) {
        return;
    }

    switch (block->tx_list(0).step()) {
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kNormalTo:
        ZJC_DEBUG("broadcast to block step: %u, height: %lu",
            block->tx_list(0).step(), block->height());
        BroadcastLocalTosBlock(thread_index, block);
        break;
    case pools::protobuf::kConsensusRootElectShard:
        BroadcastBlock(thread_index, network::kNodeNetworkId, block);
        break;
    default:
        break;
    }
}

void BftManager::BroadcastInvalidGids(uint8_t thread_idx) {
    auto now_timestamp_us = common::TimeUtils::TimestampUs();
    if (prev_broadcast_invalid_gid_tm_[thread_idx] > now_timestamp_us) {
        return;
    }

    prev_broadcast_invalid_gid_tm_[thread_idx] = now_timestamp_us + 10000000lu;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kPoolsMessage);
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    for (uint32_t pool_index = 0; pool_index < common::kInvalidPoolIndex; ++pool_index) {
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_index] != thread_idx) {
            continue;
        }

        if (pools_with_zbfts_[pool_index] == nullptr) {
            continue;
        }

        auto& bft_ptr = pools_with_zbfts_[pool_index];
        if (bft_ptr->timeout(now_timestamp_us) &&
                (bft_ptr->consensus_status() == kConsensusPreCommit ||
                 bft_ptr->consensus_status() == kConsensusPrepare)) {
            auto iter = broadcasted_gids_[thread_idx].find(bft_ptr->gid());
            if (iter != broadcasted_gids_[thread_idx].end()) {
                continue;
            }

            broadcasted_gids_[thread_idx].insert(bft_ptr->gid());
            auto invalid_bfts = msg.add_invalid_bfts();
            invalid_bfts->set_pool_index(pool_index);
            invalid_bfts->set_gid(bft_ptr->gid());
            invalid_bfts->set_hash(bft_ptr->prepare_block()->hash());
            invalid_bfts->set_height(bft_ptr->prepare_block()->height());
            ZJC_DEBUG("success broadcast invalid gids to pool: %u, gid: %s, hash: %s",
                pool_index,
                common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str());
        }
    }

    if (msg.invalid_bfts_size() <= 0) {
        return;
    }

    auto elect_item_ptr = elect_items_[elect_item_idx_];
    auto& elect_item = *elect_item_ptr;
    msg.mutable_zbft()->set_member_index(elect_item.local_node_member_index);
    msg.mutable_zbft()->set_elect_height(elect_item.elect_height);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto* brdcast = msg.mutable_broadcast();
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    if (security_ptr_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    msg.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
}

void BftManager::BroadcastBlock(
        uint8_t thread_idx,
        uint32_t des_shard,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(des_shard);
    msg.set_type(common::kBlockMessage);
    dht::DhtKeyManager dht_key(des_shard);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& tx = block_item->tx_list(0);
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        std::string val;
        if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
            auto kv = msg.mutable_sync()->add_items();
            kv->set_key(tx.storages(i).val_hash());
            kv->set_value(val);
        }
    }

    *msg.mutable_block() = *block_item;
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto* brdcast = msg.mutable_broadcast();
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success broadcast to %u, pool: %u, height: %lu, hash64: %lu",
        des_shard, block_item->pool_index(), block_item->height(), msg.hash64());
}

void BftManager::BroadcastWaitingBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    for (int32_t tx_idx = 0; tx_idx < block_item->tx_list_size(); ++tx_idx) {
        auto& tx = block_item->tx_list(tx_idx);
        for (int32_t i = 0; i < tx.storages_size(); ++i) {
            if (tx.storages(i).val_hash().size() == 32) {
                std::string val;
                if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                    auto kv = msg.mutable_sync()->add_items();
                    kv->set_key(tx.storages(i).val_hash());
                    kv->set_value(val);
                }
            }
        }
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kConsensusMessage);
    dht::DhtKeyManager dht_key(
        common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& zbft_msg = *msg.mutable_zbft();
    *zbft_msg.mutable_block() = *block_item;
    assert(block_item->has_bls_agg_sign_y() && block_item->has_bls_agg_sign_x());
    zbft_msg.set_pool_index(block_item->pool_index());
    zbft_msg.set_sync_block(true);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto* brdcast = msg.mutable_broadcast();
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success broadcast waiting block height: %lu, sharding id: %u",
        block_item->height(),
        common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset);
}

void BftManager::BroadcastLocalTosBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    auto& tx = block_item->tx_list(0);
    pools::protobuf::ToTxMessage to_tx;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        std::string val;
        if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
            ZJC_DEBUG("get to tx storage key: %s, value: %s",
                common::Encode::HexEncode(tx.storages(i).val_hash()).c_str(),
                common::Encode::HexEncode(val).c_str());
            if (tx.storages(i).key() != protos::kNormalToShards) {
                assert(false);
                continue;
            }

            if (!to_tx.ParseFromString(val)) {
                assert(false);
                continue;
            }

            if (to_tx.to_heights().sharding_id() == common::GlobalInfo::Instance()->network_id()) {
                continue;
            }

            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx;
            auto& msg = msg_ptr->header;
            auto kv = msg.mutable_sync()->add_items();
            kv->set_key(tx.storages(i).val_hash());
            kv->set_value(val);
            msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
            msg.set_type(common::kBlockMessage);
            dht::DhtKeyManager dht_key(to_tx.to_heights().sharding_id());
            msg.set_des_dht_key(dht_key.StrKey());
            transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
            *msg.mutable_block() = *block_item;
            auto* brdcast = msg.mutable_broadcast();
            network::Route::Instance()->Send(msg_ptr);
            ZJC_DEBUG("success broadcast cross tos height: %lu, sharding id: %u",
                block_item->height(), to_tx.to_heights().sharding_id());
        } else {
            assert(false);
        }
    }
}

int BftManager::BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("BackupCommit gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    if (bft_ptr->prepare_block() == nullptr) {
        ZJC_DEBUG("prepare block null, BackupCommit gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.agree_commit()) {
        ZJC_ERROR("BackupCommit gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        assert(false);
        return kConsensusSuccess;
    }

    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusError;
    }

    if (!bft_ptr->set_bls_commit_agg_sign(sign)) {
        ZJC_ERROR("set commit agg sign failed!");
        return kConsensusError;
    }

    HandleLocalCommitBlock(msg_ptr, bft_ptr);
    return kConsensusSuccess;
}

bool BftManager::IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info) {
    if (tx_info.step() != pools::protobuf::kContractCreate &&
        tx_info.step() != pools::protobuf::kContractCreateByRootTo) {
        return false;
    }

    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.has_contract_code()) {
            if (zjcvm::IsContractBytesCode(tx_info.contract_code())) {
                return true;
            }
        }
    }

    return false;
}

}  // namespace consensus

}  // namespace zjchain
