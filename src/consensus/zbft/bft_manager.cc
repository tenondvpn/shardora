#include "consensus/zbft/bft_manager.h"

#include <cassert>

#include "consensus/zbft/bft_proto.h"
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
        pools::protobuf::kRootCreateAddress,
        std::bind(&BftManager::CreateRootToTxItem, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreate,
        std::bind(&BftManager::CreateContractUserCreateCallTx, this, std::placeholders::_1));
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
    transport::MessagePtr prepare_msg_ptr = nullptr;
    ZbftPtr prev_bft = nullptr;
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    Start(msg_ptr->thread_idx, prev_bft, prepare_msg_ptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    PopAllPoolTxs(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CheckTimeout(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CheckMessageTimeout(msg_ptr->thread_idx);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
#endif
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
    if (old_leader_idx == new_leader_idx) {
        return;
    }

    (*elect_item_ptr->members)[old_leader_idx]->pool_index_mod_num = -1;
    (*elect_item_ptr->members)[new_leader_idx]->pool_index_mod_num = leader_mod_num;
    if (elect_item_ptr->local_node_member_index == old_leader_idx) {
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
    assert(false);
}

ZbftPtr BftManager::Start(
        uint8_t thread_index,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr) {
#ifndef ZJC_UNITTEST
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) <
            common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
        return nullptr;
    }
#endif

    auto elect_item_ptr = elect_items_[elect_item_idx_];
    if (elect_item_ptr == nullptr) {
        return nullptr;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    bool waiting_change_elect = false;
    if (elect_item_ptr->time_valid > now_tm_ms) {
        waiting_change_elect = true;
        auto item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
        if (item_ptr == nullptr) {
            return nullptr;
        }

        elect_item_ptr = item_ptr;
        if (elect_item_ptr->time_valid > now_tm_ms) {
            return nullptr;
        }
    }

    if (prev_bft != nullptr && prev_bft->elect_height() != elect_item_ptr->elect_height) {
        return nullptr;
    }

    auto& elect_item = *elect_item_ptr;
    auto& thread_set = elect_item.thread_set;
    auto thread_item = thread_set[thread_index];
    if (thread_item == nullptr || thread_item->pools.empty()) {
        return nullptr;
    }

    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    if (prev_bft == nullptr) {
        if (thread_item->pools[thread_item->pools.size() - 1] == common::kRootChainPoolIndex) {
            if (pools_with_zbfts_[common::kRootChainPoolIndex].empty()) {
                txs_ptr = txs_pools_->LeaderGetValidTxs(common::kRootChainPoolIndex);
            }

            if (txs_ptr == nullptr) {
                if (prev_bft == nullptr) {
                    if (now_bft_count_ >= kMaxBftCount) {
                        return nullptr;
                    }
                }
            }
        }

        auto begin_index = thread_item->prev_index;
        if (txs_ptr == nullptr) {
            // now leader create zbft ptr and start consensus
            for (; thread_item->prev_index < thread_item->pools.size(); ++thread_item->prev_index) {
                auto pool_idx = thread_item->pools[thread_item->prev_index];
                if (!pools_with_zbfts_[pool_idx].empty()) {
                    continue;
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
                if (!pools_with_zbfts_[pool_idx].empty()) {
                    continue;
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
        txs_ptr = txs_pools_->LeaderGetValidTxs(prev_bft->pool_index());
    }

    if (txs_ptr == nullptr) {
        return nullptr;
    }

    txs_ptr->thread_index = thread_index;
    if (txs_ptr->tx_type == pools::protobuf::kNormalFrom && waiting_change_elect) {
        txs_ptr->tx_type = pools::protobuf::kChangeLeaderTxs;
    }

    auto zbft_ptr = StartBft(elect_item, txs_ptr, prev_bft, prepare_msg_ptr);
    if (zbft_ptr == nullptr) {
        for (auto iter = txs_ptr->txs.begin(); iter != txs_ptr->txs.end(); ++iter) {
            iter->second->in_consensus = false;
        }

        return nullptr;
    }

    ZJC_DEBUG("leader start bft success, thread: %d, pool: %d, bft size: %u, "
        "waiting_change_elect: %d,thread_item->pools.size(): %d, "
        "elect_item_ptr->elect_height: %lu,elect_item_ptr->time_valid: %lu now_tm_ms: %lu",
        thread_index, zbft_ptr->pool_index(),
        pools_with_zbfts_[zbft_ptr->pool_index()].size(),
        waiting_change_elect, thread_item->pools.size(),
        elect_item_ptr->elect_height,
        elect_item_ptr->time_valid,
        now_tm_ms);
    return zbft_ptr;
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
    if (bft_ptr->prepare_block() != nullptr) {
        pools_mgr_->AddChangeLeaderInvalidHash(
            bft_ptr->pool_index(),
            bft_ptr->prepare_block()->height(),
            bft_ptr->prepare_block()->hash());
    }

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
        const ElectItem& elect_item,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr) {
    ZbftPtr bft_ptr = nullptr;
    auto msg_ptr = prepare_msg_ptr;
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
    bft_ptr->set_prev_bft_ptr(prev_bft);
    int leader_pre = LeaderPrepare(elect_item, bft_ptr, prepare_msg_ptr);
    if (leader_pre != kConsensusSuccess) {
        ZJC_ERROR("leader prepare failed!");
        return nullptr;
    }

    if (prepare_msg_ptr != nullptr) {
        ZJC_DEBUG("use pipeline: %d, this node is leader and start bft: %s,"
            "pool index: %d, thread index: %d, prepare hash: %s, pre hash: %s, tx size: %d,"
            "msg tx size: %u, elect height: %lu, prebft: %s",
            (prepare_msg_ptr != nullptr),
            common::Encode::HexEncode(bft_ptr->gid()).c_str(),
            bft_ptr->pool_index(),
            bft_ptr->thread_index(),
            common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
            bft_ptr->prepare_block() == nullptr ? "" : common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
            txs_ptr->txs.size(),
            prepare_msg_ptr->header.zbft().tx_bft().tx_hash_list_size(),
            elect_item.elect_height,
            prev_bft == nullptr ? "" : common::Encode::HexEncode(prev_bft->gid()).c_str());
        assert(prepare_msg_ptr->header.zbft().tx_bft().tx_hash_list_size() > 0);
    } else {
        ZJC_DEBUG("use pipeline: %d, this node is leader and start bft: %s,"
            "pool index: %d, thread index: %d, prepare hash: %s, pre hash: %s, "
            "tx size: %d, elect height: %lu, prebft: %s",
            (prepare_msg_ptr != nullptr),
            common::Encode::HexEncode(bft_ptr->gid()).c_str(),
            bft_ptr->pool_index(),
            bft_ptr->thread_index(),
            common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
            bft_ptr->prepare_block() == nullptr ? "" : common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
            txs_ptr->txs.size(),
            elect_item.elect_height,
            prev_bft == nullptr ? "" : common::Encode::HexEncode(prev_bft->gid()).c_str());
    }

    return bft_ptr;
}

void BftManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("message coming msg hash: %lu", msg_ptr->header.hash64());
    auto& header = msg_ptr->header;
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
        return HandleSyncConsensusBlock(elect_item, msg_ptr);
    }

    auto now_ms = common::TimeUtils::TimestampMs();
    assert(common::GlobalInfo::Instance()->pools_with_thread()[header.zbft().pool_index()] == msg_ptr->thread_idx);
    do
    {
        if (elect_item_ptr->elect_height != header.zbft().elect_height()) {
            auto old_elect_item = elect_items_[(elect_item_idx_ + 1) % 2];
            if (old_elect_item == nullptr || old_elect_item->elect_height != header.zbft().elect_height()) {
                ZJC_DEBUG("elect height error: %lu, %lu, %lu, "
                    "prepare gid: %s, precommit gid: %s, commit gid: %s thread idx: %d, "
                    "has sync: %d, txhash: %lu,",
                    header.zbft().elect_height(),
                    elect_item_ptr->elect_height,
                    old_elect_item == nullptr ? 0 : old_elect_item->elect_height,
                    common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
                    common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
                    common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
                    msg_ptr->thread_idx,
                    header.zbft().has_sync_block(),
                    header.hash64());
                elect_item_ptr = nullptr;
                break;
            }

            elect_item_ptr = old_elect_item;
        }

        ZJC_DEBUG("consensus message coming pool index: %u, prepare gid: %s, precommit gid: %s, "
            "commit gid: %s thread idx: %d, has sync: %d, txhash: %lu, "
            "member index: %d, other member index: %d, pool index: %d, "
            "elect height: %lu, local elect height: %lu",
            header.zbft().pool_index(),
            common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
            common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
            common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
            msg_ptr->thread_idx,
            header.zbft().has_sync_block(),
            header.hash64(),
            elect_item_ptr->local_node_member_index,
            header.zbft().member_index(),
            header.zbft().pool_index(),
            header.zbft().elect_height(),
            elect_item_ptr->elect_height);
        if (elect_item_ptr->local_node_member_index == header.zbft().member_index()) {
            //assert(false);
            elect_item_ptr = nullptr;
            break;
        }

        if (header.zbft().has_sync_block() && header.zbft().sync_block()) {
            return HandleSyncConsensusBlock(*elect_item_ptr, msg_ptr);
        }
    
        if (!elect_item_ptr->bls_valid) {
            elect_item_ptr = nullptr;
            break;
        }
    } while (0);

    if (elect_item_ptr == nullptr) {
        if (header.zbft().leader_idx() >= 0 && !header.zbft().prepare_gid().empty()) {
            // timer to re-handle the message
            if (msg_ptr->timeout > now_ms * 1000lu) {
                backup_prapare_msg_queue_[msg_ptr->thread_idx].push_back(msg_ptr);
                if (backup_prapare_msg_queue_[msg_ptr->thread_idx].size() > 16) {
                    backup_prapare_msg_queue_[msg_ptr->thread_idx].pop_front();
                }
            }
        }

        return;
    }

    auto& elect_item = *elect_item_ptr;
    assert(header.zbft().elect_height() > 0);
    assert(header.zbft().has_member_index());
    SetDefaultResponse(msg_ptr);
    std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
    msg_ptr->tmp_ptr = &zbft_vec;
    auto& members = elect_item.members;
    if (header.zbft().member_index() >= members->size()) {
        return;
    }

    auto mem_ptr = (*members)[header.zbft().member_index()];
    if (mem_ptr->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_DEBUG("invalid bls signature.");
        return;
    }

    // leader's message
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    int res = kConsensusSuccess;
    if (header.zbft().leader_idx() >= 0) {
        if (header.zbft().has_commit_gid() && !header.zbft().commit_gid().empty()) {
            auto commit_bft = GetBft(header.zbft().pool_index(), header.zbft().commit_gid());
            if (commit_bft == nullptr) {
                SyncConsensusBlock(
                    elect_item,
                    msg_ptr->thread_idx,
                    header.zbft().pool_index(),
                    header.zbft().commit_gid());
            }
        }

        BackupHandleZbftMessage(msg_ptr->thread_idx, elect_item, msg_ptr);
        if (!header.zbft().prepare_gid().empty()) {
            if (!msg_ptr->response->header.has_zbft() || !msg_ptr->response->header.zbft().has_agree_precommit()) {
                // timer to re-handle the message
                if (msg_ptr->timeout > now_ms * 1000lu) {
                    backup_prapare_msg_queue_[msg_ptr->thread_idx].push_back(msg_ptr);
                    if (backup_prapare_msg_queue_[msg_ptr->thread_idx].size() > 16) {
                        backup_prapare_msg_queue_[msg_ptr->thread_idx].pop_front();
                    }
                    return;
                }
            }
        }

    } else {
        LeaderHandleZbftMessage(elect_item, msg_ptr);
    }

    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CreateResponseMessage(
        elect_item,
        header.zbft().leader_idx() >= 0,
        zbft_vec,
        msg_ptr,
        mem_ptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
//     ClearBft(msg_ptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    if (zbft_vec[0] != nullptr) {
        zbft_vec[0]->AfterNetwork();
    } 
    
    if (zbft_vec[1] != nullptr) {
        zbft_vec[1]->AfterNetwork();
    }

    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
}

void BftManager::HandleSyncedBlock(uint8_t thread_idx, std::shared_ptr<block::protobuf::Block>& block_ptr) {
    if (!block_ptr->is_cross_block() &&
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
    auto& bft_queue = pools_with_zbfts_[pool_index];
    for (auto iter = bft_queue.begin(); iter != bft_queue.end(); ++iter) {
        if ((*iter)->prepare_block() == nullptr) {
            continue;
        }

        if ((*iter)->prepare_block()->hash() == hash) {
            return *iter;
        }
    }

    return nullptr;
}

void BftManager::HandleSyncConsensusBlock(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr) {
    auto& req_bft_msg = msg_ptr->header.zbft();
    auto bft_ptr = GetBft(req_bft_msg.pool_index(), req_bft_msg.precommit_gid());
    if (bft_ptr == nullptr) {
        if (!req_bft_msg.has_block()) {
            return;
        }

        bft_ptr = GetBftWithHash(req_bft_msg.pool_index(), req_bft_msg.block().hash());
    }

    ZJC_DEBUG("sync consensus block coming pool: %u, height: %lu, "
        "hash: %s, is cross block: %d, hash64: %lu, bft_ptr == nullptr: %d,"
        " status: %d, gid: %s, latest: %lu",
        req_bft_msg.block().pool_index(),
        req_bft_msg.block().height(),
        common::Encode::HexEncode(req_bft_msg.block().hash()).c_str(),
        req_bft_msg.block().is_cross_block(),
        msg_ptr->header.hash64(),
        (bft_ptr == nullptr),
        bft_ptr == nullptr ? -1 : bft_ptr->consensus_status(),
        common::Encode::HexEncode(req_bft_msg.precommit_gid()).c_str(),
        pools_mgr_->latest_height(req_bft_msg.block().pool_index()));
    if (req_bft_msg.has_block()) {
        // verify and add new block
        if (bft_ptr == nullptr) {
            if (!req_bft_msg.block().has_bls_agg_sign_x() || !req_bft_msg.block().has_bls_agg_sign_y()) {
                ZJC_DEBUG("not has agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                    req_bft_msg.block().network_id(),
                    req_bft_msg.block().pool_index(),
                    req_bft_msg.block().height(),
                    common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
                return;
            }

            auto block_ptr = std::make_shared<block::protobuf::Block>(req_bft_msg.block());
            if (elect_item.elect_height < block_ptr->electblock_height()) {
                waiting_agg_verify_blocks_[block_ptr->pool_index()][block_ptr->height()] = block_ptr;
                return;
            }

            // check bls sign
            if (!block_agg_valid_func_(msg_ptr->thread_idx, req_bft_msg.block())) {
                ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                    req_bft_msg.block().network_id(),
                    req_bft_msg.block().pool_index(),
                    req_bft_msg.block().height(),
                    common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
                //assert(false);
                return;
            }

            HandleSyncedBlock(msg_ptr->thread_idx, block_ptr);
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
                if (bft_ptr->consensus_status() == kConsensusPreCommit) {
                    // check bls sign
                    if (!block_agg_valid_func_(msg_ptr->thread_idx, req_bft_msg.block())) {
                        ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                            req_bft_msg.block().network_id(),
                            req_bft_msg.block().pool_index(),
                            req_bft_msg.block().height(),
                            common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
                        //assert(false);
                        return;
                    }

                    if (!CheckChangedLeaderBftsValid(
                            req_bft_msg.block().pool_index(),
                            req_bft_msg.block().height(),
                            bft_ptr->gid())) {
                        return;
                    }

                    auto block_ptr = std::make_shared<block::protobuf::Block>(req_bft_msg.block());
                    bft_ptr->set_prepare_block(block_ptr);
                    HandleLocalCommitBlock(msg_ptr, bft_ptr);
                } else {
                    auto block_ptr = std::make_shared<block::protobuf::Block>(req_bft_msg.block());
                    if (elect_item.elect_height < block_ptr->electblock_height()) {
                        waiting_agg_verify_blocks_[block_ptr->pool_index()][block_ptr->height()] = block_ptr;
                        return;
                    }

                    // check bls sign
                    if (!block_agg_valid_func_(msg_ptr->thread_idx, req_bft_msg.block())) {
                        ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                            req_bft_msg.block().network_id(),
                            req_bft_msg.block().pool_index(),
                            req_bft_msg.block().height(),
                            common::Encode::HexEncode(GetBlockHash(req_bft_msg.block())).c_str());
                        //assert(false);
                        return;
                    }

                    HandleSyncedBlock(msg_ptr->thread_idx, block_ptr);
                }
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
    ZJC_DEBUG("add new block pool: %u, height: %lu, hash: %s",
        block_ptr->pool_index(),
        block_ptr->height(),
        common::Encode::HexEncode(block_ptr->hash()).c_str());
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
        const ElectItem& elect_item,
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
    bft_msg.set_member_index(elect_item.local_node_member_index);
    bft_msg.set_elect_height(elect_item.elect_height);
    assert(elect_item.elect_height > 0);
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

void BftManager::ClearBft(const transport::MessagePtr& msg_ptr) {
    if (!msg_ptr->response->header.has_zbft()) {
        return;
    }

    bool is_leader = msg_ptr->header.zbft().leader_idx() >= 0;
    if (!is_leader) {
        return;
    }

    auto& zbft = *msg_ptr->response->header.mutable_zbft();
    auto& from_zbft = msg_ptr->header.zbft();
    if (zbft.has_agree_commit() && !zbft.agree_commit()) {
        ZJC_WARN("not agree commit: %u, prepare gid; %s, precommit gid: %s. commit gid: %s",
            zbft.pool_index(),
            common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
            common::Encode::HexEncode(zbft.precommit_gid()).c_str(),
            common::Encode::HexEncode(zbft.commit_gid()).c_str());
        auto prepare_bft = GetBft(from_zbft.pool_index(), from_zbft.prepare_gid());
        if (prepare_bft != nullptr) {
            RemoveBft(prepare_bft->pool_index(), prepare_bft->gid());
        }
//         if (!zbft.precommit_gid().empty()) {
//             assert(false);
//         }
    }
    
    if (zbft.has_agree_precommit() && !zbft.agree_precommit()) {
        zbft.release_tx_bft();
        ZJC_WARN("not agree precommit: %u, prepare gid; %s, precommit gid: %s. commit gid: %s",
            zbft.pool_index(),
            common::Encode::HexEncode(zbft.prepare_gid()).c_str(),
            common::Encode::HexEncode(zbft.precommit_gid()).c_str(),
            common::Encode::HexEncode(zbft.commit_gid()).c_str());
        auto prepare_bft = GetBft(from_zbft.pool_index(), from_zbft.prepare_gid());
        if (prepare_bft != nullptr) {
            RemoveBft(prepare_bft->pool_index(), prepare_bft->gid());
        }
//         auto precommit_bft = prepare_bft->pipeline_prev_zbft_ptr();
//         if (precommit_bft != nullptr) {
//             assert(false);
//             RemoveBft(precommit_bft->pool_index(), precommit_bft->gid());
//         }
    }
}

void BftManager::SetDefaultResponse(const transport::MessagePtr& msg_ptr) {
    msg_ptr->response = std::make_shared<transport::TransportMessage>();
    msg_ptr->response->thread_idx = msg_ptr->thread_idx;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    msg_ptr->response->header.set_src_sharding_id(net_id);
    msg_ptr->response->header.set_type(common::kConsensusMessage);
    transport::TcpTransport::Instance()->SetMessageHash(
        msg_ptr->response->header,
        msg_ptr->thread_idx);
}

void BftManager::CreateResponseMessage(
        const ElectItem& elect_item,
        bool response_to_leader,
        const std::vector<ZbftPtr>& zbft_vec,
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr) {
    auto elect_height = elect_item.elect_height;
    if (response_to_leader) {
        // pre-commit reuse prepare's bls sign
        if (zbft_vec[0] != nullptr) {
            elect_height = zbft_vec[0]->elect_height();
            auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
            std::string precommit_gid = "";
            if (zbft_vec[1] != nullptr) {
                precommit_gid = zbft_vec[1]->gid();
            }

            bool res = BftProto::BackupCreatePrepare(
                bls_mgr_,
                zbft_vec[0],
                precommit_gid,
                new_bft_msg);
            if (!res) {
                ZJC_ERROR("message set data failed!");
                return;
            }
        } else if (zbft_vec[1] != nullptr) {
            elect_height = zbft_vec[1]->elect_height();
            auto res = BftProto::BackupCreatePreCommit(
                bls_mgr_,
                zbft_vec[1],
                msg_ptr->response->header);
            if (!res) {
                ZJC_ERROR("BackupCreatePreCommit not has data.");
                return;
            }
        }
    } else {
        if (zbft_vec[0] != nullptr) {
            elect_height = zbft_vec[0]->elect_height();
            auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
            std::string precommit_gid;
            std::string commit_gid;
            if (zbft_vec[0]->pipeline_prev_zbft_ptr() != nullptr) {
                auto& precommit_bft = zbft_vec[0]->pipeline_prev_zbft_ptr();
                precommit_gid = precommit_bft->gid();
                if (precommit_bft->pipeline_prev_zbft_ptr() != nullptr) {
                    commit_gid = precommit_bft->pipeline_prev_zbft_ptr()->gid();
//                     precommit_bft->set_prev_bft_ptr(nullptr);
                }
            }

            auto msg_res = BftProto::LeaderCreatePrepare(
                elect_item.local_node_member_index,
                zbft_vec[0],
                precommit_gid,
                commit_gid,
                msg_ptr->response->header,
                new_bft_msg);
            if (!msg_res) {
                return;
            }
        } else if (zbft_vec[1] != nullptr) {
            elect_height = zbft_vec[1]->elect_height();
            std::string commit_gid;
            if (zbft_vec[1]->pipeline_prev_zbft_ptr() != nullptr) {
                commit_gid = zbft_vec[1]->pipeline_prev_zbft_ptr()->gid();
            }

            auto res = BftProto::LeaderCreatePreCommit(
                elect_item.local_node_member_index,
                zbft_vec[1],
                true,
                commit_gid,
                msg_ptr->response->header);
            if (!res) {
                return;
            }
        }
    }
        
    if (msg_ptr->response->header.has_zbft()) {
        assert(msg_ptr->response->header.zbft().has_pool_index());
        msg_ptr->response->header.mutable_zbft()->set_member_index(
            elect_item.local_node_member_index);
        msg_ptr->response->header.mutable_zbft()->set_elect_height(elect_height);
        assert(elect_item.elect_height > 0);
        if (!response_to_leader) {
            if (msg_ptr->thread_idx == 0) {
                auto& thread_set = elect_item.thread_set;
                auto thread_item = thread_set[msg_ptr->thread_idx];
                if (thread_item != nullptr && !thread_item->synced_ip) {
                    if (thread_item->valid_ip_count * 10 / 9 >= (int32_t)elect_item.members->size()) {
                        auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
                        for (uint32_t i = 0; i < elect_item.members->size(); ++i) {
                            new_bft_msg->add_ips(thread_item->member_ips[i]);
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

            msg_ptr->response->header.mutable_zbft()->set_leader_idx(elect_item.local_node_member_index);
        }

#ifdef ZJC_UNITTEST
//         ZJC_DEBUG("seet now message ok.");
        now_msg_[msg_ptr->thread_idx] = msg_ptr->response;
#else
        if (!response_to_leader) {
            if (!msg_ptr->response->header.has_broadcast()) {
                auto broadcast = msg_ptr->response->header.mutable_broadcast();
            }

            dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
            msg_ptr->response->header.set_des_dht_key(dht_key.StrKey());
            assert(msg_ptr->response->header.has_broadcast());
            if (!LeaderSignMessage(msg_ptr->response)) {
                return;
            }

            network::Route::Instance()->Send(msg_ptr->response);
            ZJC_DEBUG("leader broadcast bft message prepare gid: %s, hash64: %lu",
                common::Encode::HexEncode(msg_ptr->response->header.zbft().prepare_gid()).c_str(),
                msg_ptr->response->header.hash64());
        } else {
            auto leader_member = (*elect_item.members)[msg_ptr->header.zbft().leader_idx()];
            dht::DhtKeyManager dht_key(
                common::GlobalInfo::Instance()->network_id(),
                leader_member->id);
            msg_ptr->response->header.set_des_dht_key(dht_key.StrKey());
            msg_ptr->response->header.mutable_zbft()->set_leader_idx(-1);
            if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
                auto dht_ptr = network::DhtManager::Instance()->GetDht(msg_ptr->header.src_sharding_id());
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
            
            if (!SetBackupEcdhData(msg_ptr->response, mem_ptr)) {
                return;
            }

            if (leader_member->public_ip == 0 || leader_member->public_port == 0) {
                network::Route::Instance()->Send(msg_ptr->response);
                ZJC_DEBUG("backup direct send bft message prepare gid: %s, hash64: %lu, src hash64: %lu, res: %d, try_times: %d",
                    common::Encode::HexEncode(msg_ptr->response->header.zbft().prepare_gid()).c_str(),
                    msg_ptr->response->header.hash64(),
                    msg_ptr->header.hash64(),
                    0,
                    0);
            } else {
                auto to_ip = common::Uint32ToIp(leader_member->public_ip);
                transport::TcpTransport::Instance()->Send(
                    msg_ptr->thread_idx,
                    to_ip,
                    leader_member->public_port,
                    msg_ptr->response->header);
                ZJC_DEBUG("backup direct send bft message prepare gid: %s, hash64: %lu, src hash64: %lu, res: %d, try_times: %d, %s:%u",
                    common::Encode::HexEncode(msg_ptr->response->header.zbft().prepare_gid()).c_str(),
                    msg_ptr->response->header.hash64(),
                    msg_ptr->header.hash64(),
                    0,
                    0,
                    to_ip.c_str(), leader_member->public_port);
            }
//             int32_t try_times = 0;
//             while (try_times++ < 3) {
//                 int res = transport::TcpTransport::Instance()->Send(
//                     msg_ptr->thread_idx,
//                     msg_ptr->conn,
//                     msg_ptr->response->header);
                
//                 if (res == transport::kTransportSuccess) {
//                     break;
//                 }
//             }
        }
#endif
    } else {
        if (response_to_leader) {
#ifdef ZJC_UNITTEST
            now_msg_[msg_ptr->thread_idx] = nullptr;
#endif
        }
    }
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
    if (elect_item.mod_with_leader_index[mod_num] != msg_ptr->header.zbft().member_index()) {
        //assert(false);
        return false;
    }

    auto& mem_ptr = (*elect_item.members)[msg_ptr->header.zbft().member_index()];
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
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr) {
    if (!VerifyLeaderIdValid(elect_item, msg_ptr)) {
        ZJC_ERROR("leader invalid!");
        return;
    }

    if (msg_ptr->header.zbft().ips_size() > 0) {
        auto& thread_set = elect_item.thread_set;
        auto thread_item = thread_set[msg_ptr->thread_idx];
        ZJC_DEBUG("0 get leader ips size: %u, thread: %d",
            msg_ptr->header.zbft().ips_size(), msg_ptr->thread_idx);
        if (thread_item != nullptr) {
            ZJC_DEBUG("get leader ips size: %u", msg_ptr->header.zbft().ips_size());
            for (int32_t i = 0; i < msg_ptr->header.zbft().ips_size(); ++i) {
                auto iter = thread_item->all_members_ips[i].find(
                    msg_ptr->header.zbft().ips(i));
                if (iter == thread_item->all_members_ips[i].end()) {
                    thread_item->all_members_ips[i][msg_ptr->header.zbft().ips(i)] = 1;
                    if (elect_item.leader_count <= 8) {
                        (*elect_item.members)[i]->public_ip = msg_ptr->header.zbft().ips(i);
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

    BackupPrepare(elect_item, msg_ptr);
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
        const transport::MessagePtr& msg_ptr) {
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
                msg_ptr->thread_idx);
            if (txs_ptr == nullptr) {
                ZJC_ERROR("invalid consensus kNormal, txs not equal to leader. pool_index: %d, gid: %s, tx size: %u",
                    bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(), bft_msg.tx_bft().tx_hash_list_size());
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

    auto precommit_ptr = GetBft(bft_msg.pool_index(), bft_msg.precommit_gid());
    if (txs_ptr != nullptr && precommit_ptr != nullptr) {
        for (auto iter = txs_ptr->txs.begin(); iter != txs_ptr->txs.end(); ++iter) {
            if (precommit_ptr->txs_ptr()->txs.find(iter->first) !=
                    precommit_ptr->txs_ptr()->txs.end()) {
                txs_ptr = nullptr;
                ZJC_DEBUG("tx invalid: %s, gid: %s",
                    common::Encode::HexEncode(iter->first).c_str(),
                    common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
                break;
            }
        }
    }
    
    if (txs_ptr == nullptr) {
        return nullptr;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (txs_ptr->tx_type == pools::protobuf::kNormalFrom &&
            elect_items_[elect_item_idx_]->time_valid > now_tm_ms) {
        txs_ptr->tx_type = pools::protobuf::kChangeLeaderTxs;
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
    
    if (precommit_ptr != nullptr && precommit_ptr->prepare_block() != nullptr) {
        bft_ptr->set_prev_bft_ptr(precommit_ptr);
        ZJC_DEBUG("backup set precommit gid: %s, pre hash: %s, gid: %s",
            common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
            common::Encode::HexEncode(precommit_ptr->prepare_block()->hash()).c_str(),
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        if (bft_msg.has_prepare_height()) {
            assert(bft_msg.prepare_height() == precommit_ptr->prepare_block()->height());
        }

        if (bft_msg.has_prepare_hash()) {
            assert(bft_msg.prepare_hash() == precommit_ptr->prepare_block()->hash());
        }
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
    auto& bft_queue = pools_with_zbfts_[bft_ptr->pool_index()];
    auto iter = bft_queue.begin();
    while (iter != bft_queue.end()) {
        ZbftPtr tmp_bft = *iter;
        if (tmp_bft->gid() == bft_ptr->gid()) {
            assert(false);
            return kConsensusError;
        }

        if (tmp_bft->height() != common::kInvalidUint64 &&
                bft_ptr->pool_index() == tmp_bft->pool_index() &&
                bft_ptr->height() <= tmp_bft->height()) {
            if (bft_ptr->height() == tmp_bft->height()) {
                if (tmp_bft->consensus_status() == kConsensusPrepare) {
                    ZJC_DEBUG("remove bft gid: %s, pool_index: %d",
                        common::Encode::HexEncode(tmp_bft->gid()).c_str(),
                        bft_ptr->pool_index());
                    tmp_bft->Destroy();
                    iter = bft_queue.erase(iter);
                    continue;
                }

                ZJC_DEBUG("elect height error: %u, %lu %lu, %s, %s, status error: %d",
                    bft_ptr->pool_index(), bft_ptr->height(), tmp_bft->height(),
                    common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                    common::Encode::HexEncode(tmp_bft->gid()).c_str(),
                    tmp_bft->consensus_status());
                return kConsensusError;
            } else {
                ZJC_DEBUG("elect height error: %u, %lu %lu, %s, %s",
                    bft_ptr->pool_index(), bft_ptr->height(), tmp_bft->height(),
                    common::Encode::HexEncode(bft_ptr->gid()).c_str(),
                    common::Encode::HexEncode(tmp_bft->gid()).c_str());
//                 assert(false);
                return kConsensusError;
            }
        }

        ++iter;
    }

    bft_queue.push_back(bft_ptr);
    ++now_bft_count_;
    ZJC_DEBUG("success add bft pool idx: %d, add gid: %s",
        bft_ptr->pool_index(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

ZbftPtr BftManager::GetBft(uint32_t pool_index, const std::string& gid) {
    auto& bft_queue = pools_with_zbfts_[pool_index];
    for (auto iter = bft_queue.begin(); iter != bft_queue.end(); ++iter) {
        if ((*iter)->gid() == gid) {
            return *iter;
        }
    }
    
    return nullptr;
}

void BftManager::RemoveBftWithBlockHeight(uint32_t pool_index, uint64_t height) {
    auto& bft_queue = pools_with_zbfts_[pool_index];
    auto iter = bft_queue.begin();
    while(iter != bft_queue.end()) {
        if ((*iter)->prepare_block() == nullptr || (*iter)->prepare_block()->height() <= height) {
            ZJC_DEBUG("remove bft gid: %s, pool_index: %d",
                common::Encode::HexEncode((*iter)->gid()).c_str(), pool_index);
            (*iter)->Destroy();
            iter == bft_queue.erase(iter);
            --now_bft_count_;
        } else {
            ++iter;
        }
    }
}

void BftManager::RemoveBft(uint32_t pool_index, const std::string& gid) {
    auto& bft_queue = pools_with_zbfts_[pool_index];
    ZJC_DEBUG("try to remove bft gid: %s, pool_index: %d, now size: %u",
        common::Encode::HexEncode(gid).c_str(), pool_index, bft_queue.size());
    for (auto iter = bft_queue.begin(); iter != bft_queue.end(); ++iter) {
        auto& bft_ptr = *iter;
        if (bft_ptr->gid() != gid) {
            continue;
        }

        auto next_iter = iter;
        ++next_iter;
        if (bft_ptr->consensus_status() == kConsensusPreCommit) {
            if (bft_ptr->this_node_is_leader()) {
                ReConsensusBft(bft_ptr);
            } else if (bft_ptr->IsChangedLeader()) {
                ReConsensusChangedLeaderBft(bft_ptr);
            }
        } else {
            bft_ptr->Destroy();
            --now_bft_count_;
            bft_queue.erase(iter);
            ZJC_DEBUG("remove bft gid: %s, pool_index: %d", common::Encode::HexEncode(gid).c_str(), pool_index);
        }

        break;
    }
}

void BftManager::ReConsensusChangedLeaderBft(ZbftPtr& bft_ptr) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    if (elect_item_ptr->elect_height != bft_ptr->changed_leader_elect_height()) {
        elect_item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
        if (elect_item_ptr == nullptr ||
                elect_item_ptr->elect_height != bft_ptr->changed_leader_elect_height()) {
            ZJC_DEBUG("elect height error: %lu, %lu, %lu",
                bft_ptr->changed_leader_elect_height(),
                elect_items_[elect_item_idx_]->elect_height,
                elect_items_[(elect_item_idx_ + 1) % 2]->elect_height);
            return;
        }
    }

    auto prev_bft = bft_ptr->pipeline_prev_zbft_ptr();
    auto next_prepare_bft = Start(
        msg_ptr->thread_idx,
        prev_bft,
        msg_ptr->response);
    if (next_prepare_bft == nullptr) {
        return;
    }

    SetDefaultResponse(msg_ptr);
    std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
    msg_ptr->tmp_ptr = &zbft_vec;
    auto elect_item = *elect_item_ptr;
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
        common::Encode::HexEncode(next_prepare_bft->gid()).c_str());
    zbft_vec[0] = next_prepare_bft;
    zbft_vec[1] = next_prepare_bft->pipeline_prev_zbft_ptr();
    common::BftMemberPtr mem_ptr = nullptr;
    CreateResponseMessage(
        elect_item,
        false,
        zbft_vec,
        msg_ptr,
        mem_ptr);
    next_prepare_bft->AfterNetwork();
    if (zbft_vec[1] != nullptr) {
        zbft_vec[1]->AfterNetwork();
    }
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
            if (witer->second->electblock_height() > elect_item_ptr->elect_height) {
                break;
            }

            if (!block_agg_valid_func_(thread_idx, *witer->second)) {
                ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                    witer->second->network_id(),
                    witer->second->pool_index(),
                    witer->second->height(),
                    common::Encode::HexEncode(GetBlockHash(*witer->second)).c_str());
            } else {
                HandleSyncedBlock(thread_idx, witer->second);
            }

            witer = waiting_agg_block_map.erase(witer);
        }

        auto& bft_queue = pools_with_zbfts_[pool_index];
        if (bft_queue.empty()) {
            continue;
        }

        auto bft_ptr = *bft_queue.rbegin();
        if (bft_ptr->pool_mod_num() >= 0 && bft_ptr->pool_mod_num() < elect_item_ptr->leader_count) {
            auto valid_leader_idx = elect_item_ptr->mod_with_leader_index[bft_ptr->pool_mod_num()];
            if (valid_leader_idx >= elect_item_ptr->members->size()) {
                ZJC_DEBUG("invalid leader index %u, mod num: %d, gid: %s",
                    valid_leader_idx, bft_ptr->pool_mod_num(),
                    common::Encode::HexEncode(bft_ptr->gid()).c_str());
                assert(false);
            } else {
                if (bft_ptr->leader_index() != valid_leader_idx &&
                        elect_item_ptr->change_leader_time_valid < now_ms &&
                        bft_ptr->timeout(now_timestamp_us) &&
                        bft_ptr->consensus_status() == kConsensusPreCommit) {
                    ChangePrecommitBftLeader(bft_ptr, valid_leader_idx, *elect_item_ptr);
                }
            }
        } else {
            ZJC_DEBUG("pool mod invalid: %u, leader size: %u", bft_ptr->pool_mod_num(), elect_item_ptr->leader_count);
            if (bft_ptr->pool_mod_num() < 0) {
                assert(false);
            }
        }

        if (bft_ptr->timeout(now_timestamp_us)) {
            RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
        }
    }
}

void BftManager::ReConsensusPrepareBft(const ElectItem& elect_item, ZbftPtr& bft_ptr) {
    if (bft_ptr->consensus_status() != kConsensusLeaderWaitingBlock) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
    SetDefaultResponse(msg_ptr);
    std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
    msg_ptr->tmp_ptr = &zbft_vec;
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
        common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    msg_ptr->header.mutable_zbft()->set_agree_precommit(true);
    msg_ptr->header.mutable_zbft()->set_agree_commit(true);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    LeaderCallPrecommit(elect_item, bft_ptr, msg_ptr);
    msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
    ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    zbft_vec[0] = nullptr;
    zbft_vec[1] = bft_ptr;
    common::BftMemberPtr mem_ptr = nullptr;
    CreateResponseMessage(
        elect_item,
        false,
        zbft_vec,
        msg_ptr,
        mem_ptr);
    bft_ptr->AfterNetwork();
}

void BftManager::ReConsensusBft(ZbftPtr& bft_ptr) {
    assert(bft_ptr->consensus_status() == kConsensusPreCommit);
    auto tmp_msg_ptr = bft_ptr->reconsensus_msg_ptr();
    if (tmp_msg_ptr != nullptr) {
        transport::TcpTransport::Instance()->SetMessageHash(
            tmp_msg_ptr->header, tmp_msg_ptr->thread_idx);
        if (!LeaderSignMessage(tmp_msg_ptr)) {
            return;
        }

        network::Route::Instance()->Send(tmp_msg_ptr);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[bft_ptr->pool_index()];
    auto elect_item_ptr = elect_items_[elect_item_idx_];
    if (elect_item_ptr->elect_height != bft_ptr->elect_height()) {
        elect_item_ptr = elect_items_[(elect_item_idx_ + 1) % 2];
        if (elect_item_ptr->elect_height != bft_ptr->elect_height()) {
            ZJC_DEBUG("elect height error: %lu, %lu, %lu",
                bft_ptr->elect_height(),
                elect_items_[elect_item_idx_]->elect_height,
                elect_items_[(elect_item_idx_ + 1) % 2]->elect_height);
            return;
        }
    }

    SetDefaultResponse(msg_ptr);
    std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
    msg_ptr->tmp_ptr = &zbft_vec;
    auto elect_item = *elect_item_ptr;
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
        common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
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
        return;
    }

    bft_ptr->RechallengePrecommitClear();
    bft_ptr->reset_timeout();
    if (bft_ptr->LeaderCommitOk(
            elect_item.local_node_member_index,
            sign,
            security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
        ZJC_ERROR("leader commit failed!");
        return;
    }

    ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    zbft_vec[0] = nullptr;
    zbft_vec[1] = bft_ptr;
    common::BftMemberPtr mem_ptr = nullptr;
    CreateResponseMessage(
        elect_item,
        false,
        zbft_vec,
        msg_ptr,
        mem_ptr);
    bft_ptr->set_reconsensus_msg_ptr(msg_ptr->response);
    bft_ptr->AfterNetwork();
}

int BftManager::LeaderPrepare(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& prepare_msg_ptr) {
    zbft::protobuf::ZbftMessage bft_msg;
    auto msg_ptr = prepare_msg_ptr;
    if (msg_ptr == nullptr) {
        msg_ptr = std::make_shared<transport::TransportMessage>();
    }

        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();

    auto& header = (msg_ptr->response != nullptr && msg_ptr->response->header.has_zbft()) ? 
        msg_ptr->response->header : msg_ptr->header;
    if (!header.has_hash64()) {
        transport::TcpTransport::Instance()->SetMessageHash(header, bft_ptr->thread_index());
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);


    msg_ptr->thread_idx = bft_ptr->thread_index();
    auto* new_bft_msg = header.mutable_zbft();
    int res = bft_ptr->Prepare(true, new_bft_msg);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    res = AddBft(bft_ptr);
    if (res != kConsensusSuccess) {
        ZJC_ERROR("AddBft failed[%u].", res);
        return res;
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    if (prepare_msg_ptr == nullptr) {
        header.set_src_sharding_id(bft_ptr->network_id());
        dht::DhtKeyManager dht_key(bft_ptr->network_id());
        header.set_des_dht_key(dht_key.StrKey());
        header.set_type(common::kConsensusMessage);
        header.set_hop_count(0);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        auto msg_res = BftProto::LeaderCreatePrepare(
            elect_item.local_node_member_index,
            bft_ptr,
            "",
            "",
            header,
            new_bft_msg);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        if (!msg_res) {
            assert(false);
            return kConsensusError;
        }
    }

    new_bft_msg->set_member_index(elect_item.local_node_member_index);
    new_bft_msg->set_elect_height(elect_item.elect_height);
    assert(elect_item.elect_height > 0);
    bft_ptr->reset_timeout();
    bft_ptr->set_consensus_status(kConsensusPrepare);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    if (prepare_msg_ptr == nullptr) {
        transport::TcpTransport::Instance()->SetMessageHash(msg_ptr->header, msg_ptr->thread_idx);
        if (!LeaderSignMessage(msg_ptr)) {
            return kConsensusError;
        }
#ifdef ZJC_UNITTEST
        now_msg_[msg_ptr->thread_idx] = msg_ptr;
#else
        ZJC_DEBUG("cross block use pipeline: %d, this node is leader and start bft: %s,"
            "pool index: %d, thread index: %d, prepare hash: %s, pre hash: %s, "
            "elect height: %lu, hash64: %lu",
            (prepare_msg_ptr != nullptr),
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
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

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

int BftManager::CheckPrecommit(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    bool backup_agree_commit = false;
    do {
        if (!bft_msg.has_precommit_gid() || bft_msg.precommit_gid().empty()) {
            backup_agree_commit = true;
            break;
        }

        auto bft_ptr = GetBft(bft_msg.pool_index(), bft_msg.precommit_gid());
        if (bft_ptr == nullptr) {
            ZJC_DEBUG("failed get precommit bft_msg.pool_index(): %u, gid: %s",
                bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            break;
        }

        if (bft_ptr->leader_index() != bft_msg.member_index()) {
            ZJC_DEBUG("leader changed failed CheckPrecommit: %s",
                common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            break;
        }

        ZJC_DEBUG("Backup CheckPrecommit: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
#ifdef ZJC_UNITTEST
        if (test_for_precommit_evil_) {
            ZJC_ERROR("1 bft backup precommit failed! not agree bft gid: %s",
                common::Encode::HexEncode(bft_ptr->gid()).c_str());
            break;
        }
#endif
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        if (!CheckAggSignValid(msg_ptr, bft_ptr)) {
            //assert(false);
            ZJC_DEBUG("check agg sign failed backup agree commit: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            break;
        }
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        bft_ptr->set_consensus_status(kConsensusPreCommit);
        backup_agree_commit = true;
        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
        bft_vec[1] = bft_ptr;
    } while (0);

    ZJC_DEBUG("Backup CheckPrecommit: %s, aggree commit: %d",
        common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
        backup_agree_commit);
    CheckCommit(msg_ptr, false);
    if (!backup_agree_commit) {
        ZJC_DEBUG("failed backup agree commit: %s",
            common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
        return kConsensusError;
    }

    msg_ptr->response->header.mutable_zbft()->set_agree_commit(backup_agree_commit);
    return kConsensusSuccess;
}

int BftManager::CheckCommit(const transport::MessagePtr& msg_ptr, bool check_agg) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.has_commit_gid() || bft_msg.commit_gid().empty()) {
        return kConsensusSuccess;
    }

    ZJC_DEBUG("backup CheckCommit: %s", common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
    auto bft_ptr = GetBft(bft_msg.pool_index(), bft_msg.commit_gid());
    if (bft_ptr == nullptr) {
//         assert(false);
        return kConsensusError;
    }

    do {
        if (check_agg) {
            if (!CheckAggSignValid(msg_ptr, bft_ptr)) {
                assert(false);
                break;
            }
        }

        if (bft_ptr->prepare_block() != nullptr) {
            ZJC_DEBUG("success backup CheckCommit: %s", common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
            HandleLocalCommitBlock(msg_ptr, bft_ptr);
        } else {
            // sync block from neighbor nodes
            ZJC_ERROR("backup commit block failed should sync gid: %s",
                common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
            RemoveBft(bft_msg.pool_index(), bft_msg.commit_gid());
            return kConsensusError;
        }
    } while (0);
    
    // start new bft
    return kConsensusSuccess;
}

void BftManager::BackupPrepare(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (bft_msg.has_agree_commit() && !bft_msg.agree_commit()) {
        assert(false);
        return;
    }

    if (bft_msg.has_agree_precommit() && !bft_msg.agree_precommit()) {
        if (bft_msg.has_agree_commit()) {
            CheckCommit(msg_ptr, true);
        }

        ZJC_DEBUG("precommit failed, remove all prepare gid; %s, precommit gid: %s, commit gid: %s",
            common::Encode::HexEncode(bft_msg.oppose_prepare_gid()).c_str(),
            common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
            common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
        auto prepare_bft = GetBft(bft_msg.pool_index(), bft_msg.oppose_prepare_gid());
        if (prepare_bft != nullptr) {
            prepare_bft->set_consensus_status(kConsensusFailed);
            RemoveBft(prepare_bft->pool_index(), prepare_bft->gid());
        }
    }

    ZJC_DEBUG("has prepare: %d, prepare gid: %s, precommit gid: %s, commit gid: %s, set pool index: %u",
        bft_msg.has_prepare_gid(),
        common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
        common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
        common::Encode::HexEncode(bft_msg.commit_gid()).c_str(),
        bft_msg.pool_index());
    msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_msg.pool_index());
    if (bft_msg.has_prepare_gid() && !bft_msg.prepare_gid().empty()) {
        msg_ptr->response->header.mutable_zbft()->clear_agree_precommit();
        if (CheckPrecommit(elect_item, msg_ptr) != kConsensusSuccess) {
            ZJC_DEBUG("check precommit failed precommit gid: %s",
                common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            msg_ptr->response->header.mutable_zbft()->set_prepare_gid(bft_msg.prepare_gid());
            msg_ptr->response->header.mutable_zbft()->set_precommit_gid(bft_msg.precommit_gid());
            msg_ptr->response->header.mutable_zbft()->set_agree_commit(false);
            return;
        }

        auto now_ms = common::TimeUtils::TimestampMs();
        auto now_elect_item = elect_items_[elect_item_idx_];
        if (now_elect_item->change_leader_time_valid <= now_ms &&
                now_elect_item->elect_height != elect_item.elect_height) {
            ZJC_ERROR("BackupPrepare failed %s invalid elect height: %lu, %lu",
                common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
                now_elect_item->elect_height,
                elect_item.elect_height);
            msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
            return;
        }

        auto bft_ptr = CreateBftPtr(elect_item, msg_ptr);
        if (bft_ptr == nullptr ||
                bft_ptr->txs_ptr() == nullptr ||
                bft_ptr->txs_ptr()->txs.empty()) {
            // oppose
            ZJC_ERROR("create bft ptr failed backup create consensus bft gid: %s",
                common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            return;
        }

        auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
        int prepare_res = bft_ptr->Prepare(false, new_bft_msg);
        if (prepare_res != kConsensusSuccess || bft_ptr->prepare_block() == nullptr) {
            ZJC_ERROR("prepare failed gid: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            return;
        }

        auto& pools_bft_vec = pools_with_zbfts_[bft_msg.pool_index()];
        auto iter = pools_bft_vec.begin();
        while (iter != pools_bft_vec.end()) {
            auto tmp_bft = *iter;
            if (tmp_bft->prepare_block() == nullptr) {
                ++iter;
                continue;
            }

            if (tmp_bft->pool_index() != bft_ptr->pool_index()) {
                assert(false);
                return;
            }

            if (tmp_bft->prepare_block()->height() == bft_ptr->prepare_block()->height()) {
                if (tmp_bft->consensus_status() == kConsensusPrepare) {
                    if (tmp_bft->pipeline_prev_zbft_ptr() == nullptr ||
                            tmp_bft->pipeline_prev_zbft_ptr()->gid() == bft_msg.precommit_gid()) {
                        tmp_bft->Destroy();
                        iter = pools_bft_vec.erase(iter);
                        continue;
                    }
                    
                    ZJC_DEBUG("bft consensus status error: %u, %s, height: %lu",
                        tmp_bft->consensus_status(),
                        common::Encode::HexEncode(tmp_bft->gid()).c_str(),
                        tmp_bft->prepare_block()->height());
//                     assert(false);
                    return;
                }

                if (tmp_bft->changed_leader_new_index() != bft_msg.leader_idx()) {
//                     assert(false);
                    return;
                }

                if (tmp_bft->changed_leader_elect_height() != bft_msg.elect_height()) {
//                     assert(false);
                    return;
                }

                bool invalid_hash_found = false;
                for (int32_t i = 0; i < tmp_bft->prepare_block()->change_leader_invalid_hashs_size(); ++i) {
                    if (tmp_bft->prepare_block()->change_leader_invalid_hashs(i) ==
                            tmp_bft->prepare_block()->hash()) {
                        invalid_hash_found = true;
                        break;
                    }
                }

                if (!invalid_hash_found) {
                    return;
                }

                ZJC_DEBUG("backup success change leader and start new bft: %s, "
                    "change leader idx: %u, elect height: %lu",
                    common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
                    tmp_bft->changed_leader_new_index(),
                    tmp_bft->changed_leader_elect_height());
                changed_leader_pools_height_[bft_ptr->pool_index()] = bft_ptr;
            }

            ++iter;
        }

        ZJC_DEBUG("success create bft ptr backup create consensus bft gid: %s",
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
#ifdef ZJC_UNITTEST
        if (test_for_prepare_evil_) {
            ZJC_ERROR("1 bft backup prepare failed! not agree bft gid: %s",
                common::Encode::HexEncode(bft_ptr->gid()).c_str());
            return;
        }
#endif
        ZJC_DEBUG("backup create consensus bft prepare hash: %s, gid: %s, tx size: %d",
            common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
            bft_ptr->txs_ptr()->txs.size());
        if (bft_ptr->local_prepare_hash().empty()) {
            ZJC_DEBUG("failed backup create consensus bft prepare hash: %s, prehash: %s, leader prehash: %s, pre height: %lu, leader pre height: %lu, gid: %s, tx size: %d",
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
            return;
        }

        if (AddBft(bft_ptr) != kConsensusSuccess) {
            msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
        }
        else {
            msg_ptr->response->header.mutable_zbft()->set_agree_precommit(true);
            bft_ptr->set_prepare_msg_ptr(msg_ptr);
        }

        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        bft_ptr->set_consensus_status(kConsensusPrepare);
        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
        bft_vec[0] = bft_ptr;
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        ZJC_DEBUG("BackupPrepare success: %s", common::Encode::HexEncode(bft_vec[0]->gid()).c_str());
        return;
    }

    if (bft_msg.has_precommit_gid() && !bft_msg.precommit_gid().empty()) {
        ZJC_DEBUG("handle precommit gid: %s",
            common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
        msg_ptr->response->header.mutable_zbft()->set_agree_commit(false);
        auto precommit_bft_ptr = GetBft(bft_msg.pool_index(), bft_msg.precommit_gid());
        if (precommit_bft_ptr == nullptr) {
            ZJC_DEBUG("get precommit gid failed: %s",
                common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            return;
        }

        if (BackupPrecommit(precommit_bft_ptr, msg_ptr) != kConsensusSuccess) {
            return;
        }

        msg_ptr->response->header.mutable_zbft()->set_agree_commit(true);
        CheckCommit(msg_ptr, false);
        return;
    }

    if (bft_msg.has_commit_gid() && !bft_msg.commit_gid().empty()) {
        auto commit_bft_ptr = GetBft(bft_msg.pool_index(), bft_msg.commit_gid());
        if (commit_bft_ptr == nullptr) {
            ZJC_ERROR("get commit bft failed: %s",
                common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
            return;
        }

        if (BackupCommit(commit_bft_ptr, msg_ptr) != kConsensusSuccess) {
            ZJC_ERROR("backup commit bft failed: %s",
                common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
            return;
        }
    }
}

int BftManager::LeaderHandleZbftMessage(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (bft_msg.has_prepare_gid() && !bft_msg.prepare_gid().empty()) {
        ZJC_DEBUG("has prepare  now leader handle gid: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        auto bft_ptr = LeaderGetZbft(msg_ptr, elect_item, bft_msg.prepare_gid());
        if (bft_ptr == nullptr) {
            ZJC_DEBUG("prepare get bft failed: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            return kConsensusError;
        }

        auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
        ZJC_DEBUG("has prepare  now leader handle gid: %s, agree precommit: %d, prepare hash: %s, local hash: %s",
            common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
            bft_msg.agree_precommit(),
            common::Encode::HexEncode(bft_msg.prepare_hash()).c_str(),
            common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
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
                tx_bft,
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
                    elect_item,
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

                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(true);
                msg_ptr->response->header.mutable_zbft()->set_agree_commit(true);
                msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
                LeaderCallPrecommit(elect_item, bft_ptr, msg_ptr);
                if (!msg_ptr->response->header.mutable_zbft()->has_pool_index()) {
                    msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                    ZJC_DEBUG("gid: %s, set pool index: %u", common::Encode::HexEncode(bft_ptr->gid()).c_str(), bft_ptr->pool_index());
                }
                msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            } else if (res == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
                msg_ptr->response->header.mutable_zbft()->set_oppose_prepare_gid(bft_msg.prepare_gid());
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                auto prev_ptr = bft_ptr->pipeline_prev_zbft_ptr();
                if (prev_ptr != nullptr) {
                    ZbftPtr next_prepare_bft = nullptr;
                    if (!prev_ptr->is_cross_block()) {
                        next_prepare_bft = Start(msg_ptr->thread_idx, prev_ptr, msg_ptr->response);
                    }

                    if (next_prepare_bft != nullptr) {
                        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
                        bft_vec[0] = next_prepare_bft;
                        ZJC_DEBUG("oppose use next prepare.");
                    } else {
                        ZJC_DEBUG("ReConsensusBft not use next prepare: %s", common::Encode::HexEncode(prev_ptr->gid()).c_str());
                        ReConsensusBft(prev_ptr);
                    }
                }
            }
        } else {
            if (bft_ptr->AddPrepareOpposeNode(member_ptr->id) == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
                msg_ptr->response->header.mutable_zbft()->set_oppose_prepare_gid(bft_msg.prepare_gid());
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                ZJC_DEBUG("gid: %s, set pool index: %u", common::Encode::HexEncode(bft_ptr->gid()).c_str(), bft_ptr->pool_index());
                auto prev_ptr = bft_ptr->pipeline_prev_zbft_ptr();
                bft_ptr->set_consensus_status(kConsensusFailed);
                RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
                ZJC_ERROR("precommit call oppose now step: %d, gid: %s, prepare hash: %s,"
                    " precommit gid: %s, has prev bft: %d, agree commit: %d",
                    bft_ptr->txs_ptr()->tx_type,
                    common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
                    common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
                    common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
                    (prev_ptr != nullptr),
                    bft_msg.agree_commit());
                if (prev_ptr != nullptr) {
                    // precommit prev consensus
                    ZbftPtr next_prepare_bft = nullptr;
                    if (!prev_ptr->is_cross_block()) {
                        next_prepare_bft = Start(msg_ptr->thread_idx, prev_ptr, msg_ptr->response);
                    }

                    if (next_prepare_bft != nullptr) {
                        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
                        bft_vec[0] = next_prepare_bft;
                        ZJC_DEBUG("oppose use next prepare.");
                    } else {
                        ZJC_DEBUG("ReConsensusBft not use next prepare: %s", common::Encode::HexEncode(prev_ptr->gid()).c_str());
                        ReConsensusBft(prev_ptr);
                    }
                } else {
                    assert(bft_msg.precommit_gid().empty());
                }
//                 assert(false);
                // just all consensus rollback
            }
        }

        return kConsensusSuccess;
    }

    if (bft_msg.has_precommit_gid() && !bft_msg.precommit_gid().empty()) {
        ZJC_DEBUG("has precommit now leader handle gid: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
        auto bft_ptr = LeaderGetZbft(msg_ptr, elect_item, bft_msg.precommit_gid());
        if (bft_ptr == nullptr) {
//             ZJC_ERROR("precommit get bft failed: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            return kConsensusError;
        }

        auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
        if (bft_msg.agree_commit()) {
            LeaderCommit(elect_item, bft_ptr, msg_ptr);
        } else {
            if (bft_ptr->AddPrecommitOpposeNode(member_ptr->id) == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_commit(false);
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                ZJC_DEBUG("gid: %s, set pool index: %u", common::Encode::HexEncode(bft_ptr->gid()).c_str(), bft_ptr->pool_index());
                auto prev_ptr = bft_ptr->pipeline_prev_zbft_ptr();
                bft_ptr->set_consensus_status(kConsensusFailed);
                RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
                if (prev_ptr != nullptr) {
                    ZbftPtr next_prepare_bft = Start(msg_ptr->thread_idx, prev_ptr, msg_ptr->response);
                    if (next_prepare_bft != nullptr) {
                        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
                        bft_vec[0] = next_prepare_bft;
                        ZJC_DEBUG("oppose use next prepare.");
                    } else {
                        ZJC_DEBUG("ReConsensusBft not use next prepare: %s", common::Encode::HexEncode(prev_ptr->gid()).c_str());
                        ReConsensusBft(prev_ptr);
                    }
                    ZJC_ERROR("commit call oppose now.");
                }
            }
        }
    }

    return kConsensusSuccess;
}

ZbftPtr BftManager::LeaderGetZbft(
        const transport::MessagePtr& msg_ptr,
        const ElectItem& elect_item,
        const std::string& bft_gid) {
    auto& bft_msg = msg_ptr->header.zbft();
    auto bft_ptr = GetBft(bft_msg.pool_index(), bft_gid);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    if (bft_ptr == nullptr) {
//         ZJC_DEBUG("leader get bft gid failed[%s], hash64: %lu",
//             common::Encode::HexEncode(bft_gid).c_str(), msg_ptr->header.hash64());
        return nullptr;
    }

    if (!bft_ptr->this_node_is_leader()) {
//         ZJC_DEBUG("not valid leader get bft gid failed[%s]",
//             common::Encode::HexEncode(bft_gid).c_str());
        return nullptr;
    }

//     ZJC_DEBUG("LeaderHandleZbftMessage precommit gid: %s, prepare gid: %s,"
//         "agree precommit: %d, agree commit: %d",
//         common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//         common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
//         bft_msg.agree_precommit(),
//         bft_msg.agree_commit());
    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("backup message member index invalid. %d", bft_msg.member_index());
        return nullptr;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        ZJC_ERROR("verify backup valid error: %d!", bft_msg.member_index());
//         assert(false);
        return nullptr;
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    if (msg_ptr->thread_idx == 0) {
        auto& thread_set = elect_item.thread_set;
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

int BftManager::LeaderCallPrecommit(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
    auto prev_ptr = bft_ptr->pipeline_prev_zbft_ptr();
    if (prev_ptr != nullptr) {
        if (prev_ptr->prepare_block() != nullptr) {
            HandleLocalCommitBlock(msg_ptr, prev_ptr);
        } else {
            ZJC_ERROR("leader must sync block: %s, gid: %s",
                common::Encode::HexEncode(prev_ptr->local_prepare_hash()).c_str(),
                common::Encode::HexEncode(bft_ptr->gid()).c_str());
        }
    } else {
        ZJC_DEBUG("bft not has prev bft: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    }

    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    ZbftPtr next_prepare_bft = nullptr;
    if (!bft_ptr->is_cross_block()) {
        next_prepare_bft = Start(msg_ptr->thread_idx, bft_ptr, msg_ptr->response);
    }

    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
//     if (msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] > 20000lu) {
//         ZJC_INFO("%d use time: %lu", msg_ptr->times_idx, (msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2]));
//     }
    if (next_prepare_bft != nullptr) {
        bft_vec[0] = next_prepare_bft;
        ZJC_DEBUG("use next prepare.");
    } else {
        ZJC_DEBUG("direct precommit not use next prepare: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
        ZJC_DEBUG("use g1_precommit_hash prepare hash: %s, gid: %s",
            common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
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

        if (bft_ptr->LeaderCommitOk(
                elect_item.local_node_member_index,
                sign,
                security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
            ZJC_ERROR("leader commit failed!");
            return kConsensusError;
        }
    }

    bft_ptr->set_consensus_status(kConsensusPreCommit);
    bft_ptr->reset_timeout();
    bft_vec[1] = bft_ptr;
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
        return kConsensusError;
    }

    if (!bft_ptr->verify_bls_precommit_agg_sign(
            sign,
            bft_ptr->precommit_bls_agg_verify_hash())) {
        ZJC_ERROR("backup verify leader agg sign failed.");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusPreCommit);
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    bft_vec[1] = bft_ptr;
    ZJC_DEBUG("BackupPrecommit success: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

int BftManager::LeaderCommit(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
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
        LeaderCallCommit(elect_item, msg_ptr, bft_ptr);
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
            bool check_res = libBLS::Bls::Verification(g1_hash, sign, common_pk);
            if (!check_res) {
                assert(check_res);
                return;
            }
        } catch (std::exception& e) {
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
    RemoveBft(bft_ptr->pool_index(), bft_ptr->gid());
    msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    RemoveBftWithBlockHeight(zjc_block->pool_index(), zjc_block->height());
    RemoveWaitingBlock(zjc_block->pool_index(), zjc_block->height());
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

    ZJC_DEBUG("new block: %s, gid: %s. is leader: %d, thread idx: %d",
        common::Encode::HexEncode(zjc_block->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_ptr->this_node_is_leader(),
        msg_ptr->thread_idx);
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

int BftManager::LeaderCallCommit(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr) {
    ZJC_DEBUG("leader commit called: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
    // check pre-commit multi sign and leader commit
    auto res = BftProto::LeaderCreateCommit(
        elect_item.local_node_member_index,
        bft_ptr,
        true,
        msg_ptr->response->header);
    if (!res) {
        ZJC_ERROR("leader create commit message failed!");
        return kConsensusError;
    }

    if (bft_ptr->prepare_block() != nullptr) {
        HandleLocalCommitBlock(msg_ptr, bft_ptr);
    } else {
        // sync block from neighbor nodes
        // if (bft_ptr->pool_index() == common::kImmutablePoolSize) {
        //     sync::KeyValueSync::Instance()->AddSyncHeight(
        //         network::kRootCongressNetworkId,
        //         bft_ptr->pool_index(),
        //         bft_ptr->prepare_latest_height(),
        //         sync::kSyncHighest);
        // } else {
        //     sync::KeyValueSync::Instance()->AddSyncHeight(
        //         bft_ptr->network_id(),
        //         bft_ptr->pool_index(),
        //         bft_ptr->prepare_latest_height(),
        //         sync::kSyncHighest);
        // }
        ZJC_DEBUG("leader should sync block now gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        assert(false);
        return kConsensusSuccess;
    }
    
    return kConsensusSuccess;
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
        return kConsensusError;
    }

    HandleLocalCommitBlock(msg_ptr, bft_ptr);
    return kConsensusSuccess;
}

bool BftManager::IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info) {
    if (tx_info.step() != pools::protobuf::kContractCreate) {
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
