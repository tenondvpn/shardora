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
    if (bft_hash_map_ != nullptr) {
        delete []bft_hash_map_;
    }

    if (bft_queue_ != nullptr) {
        delete []bft_queue_;
    }
}

int BftManager::Init(
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count,
        BlockCacheCallback new_block_cache_callback) {
    vss_mgr_ = vss_mgr;
    account_mgr_ = account_mgr;
    block_mgr_ = block_mgr;
    elect_mgr_ = elect_mgr;
    pools_mgr_ = pool_mgr;
    tm_block_mgr_ = tm_block_mgr;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    new_block_cache_callback_ = new_block_cache_callback;
    RegisterCreateTxCallbacks();
    security_ptr_ = security_ptr;
    txs_pools_ = std::make_shared<WaitingTxsPools>(pools_mgr_, block_mgr, tm_block_mgr);
    thread_count_ = thread_count;
    bft_hash_map_ = new std::unordered_map<std::string, ZbftPtr>[thread_count];
    bft_queue_ = new std::queue<ZbftPtr>[thread_count];
#ifdef ZJC_UNITTEST
    now_msg_ = new transport::MessagePtr[thread_count_];
#endif
    for (uint8_t i = 0; i < thread_count_; ++i) {
        elect_items_[0].thread_set[i] = nullptr;
        elect_items_[1].thread_set[i] = nullptr;
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
        pools::protobuf::kRootLocalTos,
        std::bind(&BftManager::CreateRootToTxItem, this, std::placeholders::_1));
    block_mgr_->SetCreateToTxFunction(
        std::bind(&BftManager::CreateToTx, this, std::placeholders::_1));
    tm_block_mgr_->SetCreateTmTxFunction(
        std::bind(&BftManager::CreateTimeblockTx, this, std::placeholders::_1));
}

void BftManager::OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members) {
    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    auto& elect_item = elect_items_[(elect_item_idx_ + 1) % 2];
    elect_item.members = members;
    elect_item.leader_member = elect_mgr_->local_mem_ptr(sharding_id);;
    for (uint32_t i = 0; i < members->size(); ++i) {
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            elect_item.local_member = (*members)[i];
            elect_item.local_node_member_index = i;
            break;
        }
    }

    elect_item.local_node_pool_mod_num = elect_mgr_->local_node_pool_mod_num();
    elect_item.leader_count = elect_mgr_->GetNetworkLeaderCount(sharding_id);
    elect_item.elect_height = elect_mgr_->latest_height(sharding_id);
    elect_item.member_size = members->size();
    auto members_ptr = elect_mgr_->GetNetworkMembersWithHeight(
        elect_item.elect_height,
        sharding_id,
        &elect_item.common_pk,
        &elect_item.sec_key);
    if (members_ptr == nullptr) {
        ZJC_ERROR("bft init failed elect_height: %lu, network_id: %u",
            elect_item.elect_height, sharding_id);
        return;
    }

    for (uint8_t i = 0; i < common::kMaxThreadCount; ++i) {
        elect_item.thread_set[i] = nullptr;
    }

    ZJC_INFO("new elect block local leader index: %d, leader_count: %d, thread_count_: %d",
        elect_item.local_node_pool_mod_num, elect_item.leader_count, thread_count_);
    if (elect_item.local_node_pool_mod_num < 0 ||
            elect_item.local_node_pool_mod_num >= elect_item.leader_count) {
        elect_item_idx_ = (elect_item_idx_ + 1) % 2;
        return;
    }


    auto& thread_set = elect_item.thread_set;
    std::set<uint32_t> leader_pool_set;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        if (i % elect_item.leader_count == elect_item.local_node_pool_mod_num) {
            leader_pool_set.insert(i);
        }
    }

    for (uint8_t j = 0; j < thread_count_; ++j) {
        auto thread_item = std::make_shared<PoolTxIndexItem>();
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            if (i % thread_count_ == j && leader_pool_set.find(i) != leader_pool_set.end()) {
                thread_item->pools.push_back(i);
            }
        }

        thread_item->prev_index = 0;
        thread_set[j] = thread_item;  // ptr change, multi-thread safe
    }

    minimal_node_count_to_consensus_ = members->size() * 2 / 3;
    if (minimal_node_count_to_consensus_ + 1 < members->size()) {
        ++minimal_node_count_to_consensus_;
    }

    elect_item_idx_ = (elect_item_idx_ + 1) % 2;
}

void BftManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
#ifndef ZJC_UNITTEST
    transport::MessagePtr prepare_msg_ptr = nullptr;
    ZbftPtr prev_bft = nullptr;
    Start(msg_ptr->thread_idx, prev_bft, prepare_msg_ptr);
//     if (bft_ptr == nullptr) {
//         auto btime = common::TimeUtils::TimestampUs();
        PopAllPoolTxs(msg_ptr->thread_idx);
//         auto etime = common::TimeUtils::TimestampUs();
//         ZJC_DEBUG("pop all txs use time: %lu us", (etime - btime));
//     }
    
    CheckTimeout(msg_ptr->thread_idx);
    auto now_tm = common::TimeUtils::TimestampUs();
    if (prev_test_bft_size_[msg_ptr->thread_idx] + 3000000lu < now_tm) {
        prev_test_bft_size_[msg_ptr->thread_idx] = now_tm;
//         ZJC_INFO("thread idx: %d, bft size; %u",
//             msg_ptr->thread_idx,
//             bft_hash_map_[msg_ptr->thread_idx].size());
    }
#endif
}

void BftManager::PopAllPoolTxs(uint8_t thread_index) {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
        if (pool_idx % thread_count_ == thread_index) {
            pools_mgr_->PopTxs(pool_idx);
            pools_mgr_->CheckTimeoutTx(pool_idx);
        }
    }
}

ZbftPtr BftManager::Start(
        uint8_t thread_index,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr) {
#ifndef ZJC_UNITTEST
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) < minimal_node_count_to_consensus_) {
        return nullptr;
    }
#endif

    auto msg_ptr = prepare_msg_ptr;
    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    }

    auto& thread_set = elect_items_[elect_item_idx_].thread_set;
    auto thread_item = thread_set[thread_index];
    if (thread_item == nullptr) {
        return nullptr;
    }

    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    auto begin_index = thread_item->prev_index;
    auto can_new_bft = bft_hash_map_[thread_index].empty();
    for (; thread_item->prev_index < thread_item->pools.size(); ++thread_item->prev_index) {
        auto pool_idx = thread_item->pools[thread_item->prev_index];
        if (!can_new_bft && prepare_msg_ptr == nullptr) {
            continue;
        }

        txs_ptr = txs_pools_->LeaderGetValidTxs(false, pool_idx);
        if (txs_ptr != nullptr) {
            // now leader create zbft ptr and start consensus
            break;
        }
    }

    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    if (txs_ptr == nullptr) {
        for (thread_item->prev_index = 0;
                thread_item->prev_index < begin_index; ++thread_item->prev_index) {
            auto pool_idx = thread_item->pools[thread_item->prev_index];
            if (!can_new_bft && prepare_msg_ptr == nullptr) {
                continue;
            }

            txs_ptr = txs_pools_->LeaderGetValidTxs(false, pool_idx);
            if (txs_ptr != nullptr) {
                // now leader create zbft ptr and start consensus
                break;
            }
        }
    }
    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    if (txs_ptr == nullptr) {
        return nullptr;
    }

//     ZJC_INFO("thread_index: %d, pool index: %d, tx size: %u, pool zbft size: %d",
//         thread_index, txs_ptr->pool_index, txs_ptr->txs.size(), txs_pools_->ZbftSize(txs_ptr->pool_index));
    txs_ptr->thread_index = thread_index;
    return StartBft(txs_ptr, prev_bft, prepare_msg_ptr);
}

int BftManager::InitZbftPtr(bool leader, ZbftPtr& bft_ptr) {
    auto& elect_item = elect_items_[elect_item_idx_];
    common::BftMemberPtr leader_mem_ptr = nullptr;
    if (leader) {
        leader_mem_ptr = elect_item.leader_member;
    }

    if (bft_ptr->Init(
            elect_item.elect_height,
            leader_mem_ptr,
            elect_item.members,
            elect_item.common_pk,
            elect_item.sec_key) != kConsensusSuccess) {
        ZJC_ERROR("bft init failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

ZbftPtr BftManager::StartBft(
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr) {
    ZbftPtr bft_ptr = nullptr;
    auto msg_ptr = prepare_msg_ptr;
    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

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

    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    if (InitZbftPtr(true, bft_ptr) != kConsensusSuccess) {
        ZJC_ERROR("InitZbftPtr failed!");
        return nullptr;
    }

    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    auto& gid = bft_gids_[txs_ptr->thread_index];
    uint64_t* tmp_gid = (uint64_t*)gid.data();
    tmp_gid[0] = bft_gids_index_[txs_ptr->thread_index]++;
    bft_ptr->set_gid(gid);
    bft_ptr->set_network_id(common::GlobalInfo::Instance()->network_id());
    // bft_ptr->set_randm_num(vss::VssManager::Instance()->EpochRandom());
    auto& elect_item = elect_items_[elect_item_idx_];
    bft_ptr->set_member_count(elect_item.member_size);
    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    bft_ptr->set_prev_bft_ptr(prev_bft);
    int leader_pre = LeaderPrepare(bft_ptr, prepare_msg_ptr);
    if (msg_ptr != nullptr) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    }

    if (leader_pre != kConsensusSuccess) {
        ZJC_ERROR("leader prepare failed!");
        return nullptr;
    }

//     ZJC_INFO("use pipeline: %d, this node is leader and start bft: %s,"
//         "pool index: %d, thread index: %d, prepare hash: %s, tx size: %d",
//         (prepare_msg_ptr != nullptr),
//         common::Encode::HexEncode(bft_ptr->gid()).c_str(),
//         bft_ptr->pool_index(),
//         bft_ptr->thread_index(),
//         common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
//         txs_ptr->txs.size());
    return bft_ptr;
}

void BftManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kConsensusMessage);
    auto& elect_item = elect_items_[elect_item_idx_];
    ZJC_INFO("consensus message coming prepare gid: %s, precommit gid: %s, "
        "commit gid: %s thread idx: %d, has sync: %d, txhash: %lu, "
        "member index: %d, other member index: %d, pool index: %d",
        common::Encode::HexEncode(header.zbft().prepare_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().precommit_gid()).c_str(),
        common::Encode::HexEncode(header.zbft().commit_gid()).c_str(),
        msg_ptr->thread_idx,
        header.zbft().has_sync_block(),
        header.hash64(),
        elect_item.local_node_member_index,
        header.zbft().member_index(),
        header.zbft().pool_index());
    if (elect_item.local_node_member_index == header.zbft().member_index()) {
        assert(false);
    }

    if (header.zbft().has_sync_block() && header.zbft().sync_block()) {
        return HandleSyncConsensusBlock(msg_ptr);
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    assert(header.zbft().has_member_index());
    SetDefaultResponse(msg_ptr);
    std::vector<ZbftPtr> zbft_vec = { nullptr, nullptr, nullptr };
    msg_ptr->tmp_ptr = &zbft_vec;
    auto& members = elect_items_[elect_item_idx_].members;
    if (header.zbft().member_index() >= members->size()) {
        return;
    }

    auto mem_ptr = (*members)[header.zbft().member_index()];
    // leader's message
    auto btime = msg_ptr->times_idx;
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    int res = kConsensusSuccess;
    if (!header.zbft().leader()) {
        BackupHandleZbftMessage(msg_ptr->thread_idx, msg_ptr);
    } else {
        LeaderHandleZbftMessage(msg_ptr);
    }

    auto etime = msg_ptr->times_idx;
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
//     if ((msg_ptr->times[etime] - msg_ptr->times[btime]) > 20000) {
//         for (uint32_t i = btime + 1; i <= etime; ++i) {
//             std::cout << i << " : " << (msg_ptr->times[i] - msg_ptr->times[i - 1]) << std::endl;
//         }
//         assert(false);
//     }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    CreateResponseMessage(!header.zbft().leader(), zbft_vec, msg_ptr, mem_ptr);
    ClearBft(msg_ptr);
    //     ZJC_DEBUG("create response over.");
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    if (zbft_vec[0] != nullptr) {
//         ZJC_DEBUG("delay create bls verify hash leader: %d", header.zbft().leader());
        zbft_vec[0]->AfterNetwork();
    } else if (zbft_vec[1] != nullptr) {
        zbft_vec[1]->AfterNetwork();
    }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
}

void BftManager::HandleSyncConsensusBlock(const transport::MessagePtr& msg_ptr) {
    auto& req_bft_msg = msg_ptr->header.zbft();
    auto bft_ptr = GetBft(msg_ptr->thread_idx, req_bft_msg.precommit_gid(), false);
    if (bft_ptr == nullptr) {
        bft_ptr = GetBft(msg_ptr->thread_idx, req_bft_msg.precommit_gid(), true);
    }

    if (req_bft_msg.has_block()) {
        // verify and add new block
        if (bft_ptr == nullptr) {
            if (!req_bft_msg.block().has_bls_agg_sign_x() || !req_bft_msg.block().has_bls_agg_sign_y()) {
                return;
            }

            auto block_ptr = std::make_shared<block::protobuf::Block>(req_bft_msg.block());
            auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(block_ptr);
            new_block_cache_callback_(
                msg_ptr->thread_idx,
                queue_item_ptr->block_ptr,
                queue_item_ptr->db_batch);
            block_mgr_->ConsensusAddBlock(msg_ptr->thread_idx, queue_item_ptr);
            pools_mgr_->TxOver(block_ptr->pool_index(), block_ptr->tx_list());
            // remove bft
            ZJC_DEBUG("sync block message height: %lu, block hash: %s",
                block_ptr->height(),
                common::Encode::HexEncode(GetBlockHash(*block_ptr)).c_str());
        } else {
            if (bft_ptr->prepare_block() == nullptr) {
                auto block_hash = GetBlockHash(req_bft_msg.block());
//                 ZJC_DEBUG("receive block hash: %s, local: %s",
//                     common::Encode::HexEncode(block_hash).c_str(),
//                     common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
                if (block_hash == bft_ptr->local_prepare_hash()) {
                    bft_ptr->set_prepare_block(std::make_shared<block::protobuf::Block>(req_bft_msg.block()));
                    SaveKeyValue(msg_ptr->header);
                    if (bft_ptr->consensus_status() == kConsensusCommited) {
                        HandleLocalCommitBlock(msg_ptr->thread_idx, bft_ptr);
//                         ZJC_DEBUG("commited  receive block hash: %s",
//                             common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str());
                    }
//                     ZJC_DEBUG("receive block hash: %s",
//                         common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str());
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
        auto& elect_item = elect_items_[elect_item_idx_];
        bft_msg.set_member_index(elect_item.local_node_member_index);
        *bft_msg.mutable_block() = *bft_ptr->prepare_block();
        assert(bft_msg.block().height() > 0);
        transport::TcpTransport::Instance()->Send(
            msg_ptr->thread_idx,
            msg_ptr->conn,
            msg);
//         ZJC_DEBUG("send res to block hash: %s, gid: %s",
//             common::Encode::HexEncode(bft_ptr->prepare_block()->hash()).c_str(),
//             common::Encode::HexEncode(req_bft_msg.precommit_gid()).c_str());
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
            if (storage.val_size() == 0) {  // 0 just save value hash else direct value
                std::string val;
                if (!prefix_db_->GetTemporaryKv(storage.val_hash(), &val)) {
                    return false;
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
    std::vector<uint32_t> pos_vec;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < readobly_dht->size(); ++i) {
        pos_vec.push_back(i);
    }

    if (pos_vec.size() > kSyncFromOtherCount) {
        std::random_shuffle(pos_vec.begin(), pos_vec.end());
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
    auto& elect_item = elect_items_[elect_item_idx_];
    bft_msg.set_member_index(elect_item.local_node_member_index);
    for (uint32_t i = 0; i < pos_vec.size() && i < kSyncFromOtherCount; ++i) {
        transport::TcpTransport::Instance()->Send(
            thread_idx,
            (*readobly_dht)[pos_vec[i]]->public_ip,
            (*readobly_dht)[pos_vec[i]]->public_port,
            msg);
        ZJC_INFO("send sync block %s:%d bft gid: %s",
            (*readobly_dht)[pos_vec[i]]->public_ip.c_str(),
            (*readobly_dht)[pos_vec[i]]->public_port,
            common::Encode::HexEncode(bft_gid).c_str());
    }
}

void BftManager::ClearBft(const transport::MessagePtr& msg_ptr) {
    if (!msg_ptr->response->header.has_zbft()) {
        return;
    }

    bool is_leader = msg_ptr->header.zbft().leader();
    if (!is_leader) {
        return;
    }

    auto& zbft = *msg_ptr->response->header.mutable_zbft();
    auto& from_zbft = msg_ptr->header.zbft();
    if (zbft.has_agree_commit() && !zbft.agree_commit()) {
//         ZJC_DEBUG("not agree commit.");
        zbft.set_prepare_gid(from_zbft.prepare_gid());
        zbft.release_tx_bft();
        auto prepare_bft = GetBft(msg_ptr->thread_idx, from_zbft.prepare_gid(), is_leader);
        if (prepare_bft == nullptr) {
//             ZJC_DEBUG("not agree commit prepare gid failed: %s", common::Encode::HexEncode(from_zbft.prepare_gid()).c_str());
            return;
        }

        RemoveBft(msg_ptr->thread_idx, prepare_bft->gid(), is_leader);
        auto precommit_bft = prepare_bft->pipeline_prev_zbft_ptr();
        if (precommit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, precommit_bft->gid(), is_leader);
        auto commit_bft = precommit_bft->pipeline_prev_zbft_ptr();
        if (commit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, commit_bft->gid(), is_leader);
    }
    
    if (zbft.has_agree_precommit() && !zbft.agree_precommit()) {
        zbft.release_tx_bft();
//         ZJC_DEBUG("not agree precommit.");
        auto prepare_bft = GetBft(msg_ptr->thread_idx, from_zbft.prepare_gid(), is_leader);
        if (prepare_bft == nullptr) {
//             ZJC_DEBUG("not agree precommit prepare gid failed: %s", common::Encode::HexEncode(from_zbft.prepare_gid()).c_str());
            return;
        }

        RemoveBft(msg_ptr->thread_idx, prepare_bft->gid(), is_leader);
        auto precommit_bft = prepare_bft->pipeline_prev_zbft_ptr();
        if (precommit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, precommit_bft->gid(), is_leader);
    }
}

void BftManager::SetDefaultResponse(const transport::MessagePtr& msg_ptr) {
    msg_ptr->response = std::make_shared<transport::TransportMessage>();
    msg_ptr->response->thread_idx = msg_ptr->thread_idx;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    msg_ptr->response->header.set_src_sharding_id(net_id);
    dht::DhtKeyManager dht_key(net_id);
    msg_ptr->response->header.set_des_dht_key(dht_key.StrKey());
    msg_ptr->response->header.set_type(common::kConsensusMessage);
    transport::TcpTransport::Instance()->SetMessageHash(
        msg_ptr->response->header,
        msg_ptr->thread_idx);
}

void BftManager::CreateResponseMessage(
        bool response_to_leader,
        const std::vector<ZbftPtr>& zbft_vec,
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr) {
    if (response_to_leader) {
        // pre-commit reuse prepare's bls sign
        if (zbft_vec[0] != nullptr) {
            auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
            std::string precommit_gid = "";
            if (zbft_vec[1] != nullptr) {
                precommit_gid = zbft_vec[1]->gid();
            }

            bool res = BftProto::BackupCreatePrepare(
                bls_mgr_,
                zbft_vec[0],
                true,
                precommit_gid,
                new_bft_msg);
            if (!res) {
                ZJC_ERROR("message set data failed!");
                return;
            }
        } else if (zbft_vec[1] != nullptr) {
            auto res = BftProto::BackupCreatePreCommit(
                bls_mgr_,
                zbft_vec[1],
                true,
                msg_ptr->response->header);
            if (!res) {
                ZJC_ERROR("BackupCreatePreCommit not has data.");
                return;
            }
        }
    } else {
        if (zbft_vec[0] != nullptr) {
            auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
            std::string precommit_gid;
            std::string commit_gid;
            if (zbft_vec[0]->pipeline_prev_zbft_ptr() != nullptr) {
                auto& precommit_bft = zbft_vec[0]->pipeline_prev_zbft_ptr();
                precommit_gid = precommit_bft->gid();
                if (precommit_bft->pipeline_prev_zbft_ptr() != nullptr) {
                    commit_gid = precommit_bft->pipeline_prev_zbft_ptr()->gid();
                    precommit_bft->set_prev_bft_ptr(nullptr);
                }
            }

            auto msg_res = BftProto::LeaderCreatePrepare(
                zbft_vec[0],
                precommit_gid,
                commit_gid,
                msg_ptr->response->header,
                new_bft_msg);
            if (!msg_res) {
                return;
            }
        } else if (zbft_vec[1] != nullptr) {
            std::string commit_gid;
            if (zbft_vec[1]->pipeline_prev_zbft_ptr() != nullptr) {
                commit_gid = zbft_vec[1]->pipeline_prev_zbft_ptr()->gid();
            }

            auto res = BftProto::LeaderCreatePreCommit(
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
        auto& elect_item = elect_items_[elect_item_idx_];
        msg_ptr->response->header.mutable_zbft()->set_member_index(
            elect_item.local_node_member_index);
        if (response_to_leader) {
            assert(msg_ptr->response->header.mutable_zbft()->member_index() != 0);
            msg_ptr->response->header.mutable_zbft()->set_leader(true);
            if (!SetBackupEcdhData(msg_ptr->response, mem_ptr)) {
                return;
            }
        } else {
            msg_ptr->response->header.mutable_zbft()->set_leader(false);
            if (!LeaderSignMessage(msg_ptr->response)) {
                return;
            }
        }

#ifdef ZJC_UNITTEST
//         ZJC_DEBUG("seet now message ok.");
        now_msg_[msg_ptr->thread_idx] = msg_ptr->response;
#else
        if (!response_to_leader) {
            if (!msg_ptr->response->header.has_broadcast()) {
                auto broadcast = msg_ptr->response->header.mutable_broadcast();
            }

            assert(msg_ptr->response->header.has_broadcast());
            network::Route::Instance()->Send(msg_ptr->response);
        } else {
            transport::TcpTransport::Instance()->Send(
                msg_ptr->thread_idx,
                msg_ptr->conn,
                msg_ptr->response->header);
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

bool BftManager::VerifyLeaderIdValid(const transport::MessagePtr& msg_ptr) {
    if (!msg_ptr->header.has_sign()) {
        assert(false);
        return false;
    }

    auto& elect_item = elect_items_[elect_item_idx_];
    auto& mem_ptr = (*elect_item.members)[msg_ptr->header.zbft().member_index()];
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_ptr_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        assert(false);
        return false;
    }

    return true;
}

void BftManager::BackupHandleZbftMessage(
        uint8_t thread_index,
        const transport::MessagePtr& msg_ptr) {
    if (!VerifyLeaderIdValid(msg_ptr)) {
        ZJC_ERROR("leader invalid!");
        return;
    }

    BackupPrepare(msg_ptr);
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

ZbftPtr BftManager::CreateBftPtr(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    std::vector<uint64_t> bloom_data;
    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    auto& bloom_filter = bft_msg.tx_bft().bloom_filter();
    if (!bloom_filter.empty()) {
        for (int32_t i = 0; i < bloom_filter.size(); ++i) {
            bloom_data.push_back(bloom_filter[i]);
        }

        common::BloomFilter bf(bloom_data, kHashCount);
        txs_ptr = txs_pools_->FollowerGetTxs(bft_msg.pool_index(), bf, msg_ptr->thread_idx);
        assert(false);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

//         ZJC_DEBUG("get tx count: %u, pool: %d", bloom_data.size(), bft_msg.pool_index());
    } else if (bft_msg.tx_bft().tx_hash_list_size() > 0) {
        // get txs direct
        if (bft_msg.tx_bft().tx_type() == pools::protobuf::kNormalTo) {
            txs_ptr = txs_pools_->GetToTxs(bft_msg.pool_index(), false);
            if (txs_ptr == nullptr) {
                ZJC_ERROR("invalid consensus kNormalTo, txs not equal to leader. pool_index: %d, gid: %s",
                    bft_msg.pool_index(), common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
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
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        }
    } else {
        ZJC_ERROR("invalid consensus, tx empty.");
        return nullptr;
    }

    if (txs_ptr != nullptr && txs_ptr->txs.size() != bft_msg.tx_bft().tx_hash_list_size()) {
        ZJC_ERROR("invalid consensus, txs not equal to leader.");
        txs_ptr = nullptr;
    }

    auto precommit_ptr = GetBft(msg_ptr->thread_idx, bft_msg.precommit_gid(), false);
    if (txs_ptr != nullptr && precommit_ptr != nullptr) {
        for (auto iter = txs_ptr->txs.begin(); iter != txs_ptr->txs.end(); ++iter) {
            if (precommit_ptr->txs_ptr()->txs.find(iter->first) !=
                precommit_ptr->txs_ptr()->txs.end()) {
                txs_ptr = nullptr;
                break;
            }
        }
    }
    
    if (txs_ptr == nullptr) {
        txs_ptr = std::make_shared<WaitingTxsItem>();
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
        if (bft_msg.has_prepare_height()) {
            assert(bft_msg.prepare_height() == precommit_ptr->prepare_block()->height());
        }

        if (bft_msg.has_prepare_hash()) {
            assert(bft_msg.prepare_hash() == precommit_ptr->prepare_block()->hash());
        }
    } else {
        if (bft_msg.has_prepare_height()) {
            bft_ptr->set_leader_pre_height(bft_msg.prepare_height());
        }

        if (bft_msg.has_prepare_hash()) {
            bft_ptr->set_leader_pre_hash(bft_msg.prepare_hash());
        }
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    if (InitZbftPtr(false, bft_ptr) != kConsensusSuccess) {
        return nullptr;
    }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    bft_ptr->set_gid(bft_msg.prepare_gid());
    bft_ptr->set_network_id(bft_msg.net_id());
    bft_ptr->set_consensus_status(kConsensusPrepare);
    auto& elect_item = elect_items_[elect_item_idx_];
    bft_ptr->set_member_count(elect_item.member_size);
    return bft_ptr;
}

int BftManager::AddBft(ZbftPtr& bft_ptr) {
    auto gid = bft_ptr->gid();
    if (bft_ptr->this_node_is_leader()) {
        gid += "L";
    }

    auto iter = bft_hash_map_[bft_ptr->thread_index()].find(gid);
    if (iter != bft_hash_map_[bft_ptr->thread_index()].end()) {
        return kConsensusAdded;
    }

    bft_hash_map_[bft_ptr->thread_index()][gid] = bft_ptr;
    bft_queue_[bft_ptr->thread_index()].push(bft_ptr);
    return kConsensusSuccess;
}

ZbftPtr BftManager::GetBft(uint8_t thread_index, const std::string& in_gid, bool leader) {
    auto gid = in_gid;
    if (leader) {
        gid += "L";
    }

    auto iter = bft_hash_map_[thread_index].find(gid);
    if (iter == bft_hash_map_[thread_index].end()) {
        return nullptr;
    }

    iter->second->ClearTime();
    return iter->second;
}

void BftManager::RemoveBft(uint8_t thread_idx, const std::string& in_gid, bool leader) {
    auto gid = in_gid;
    if (leader) {
        gid += "L";
    }

    ZbftPtr bft_ptr{ nullptr };
    {
        auto iter = bft_hash_map_[thread_idx].find(gid);
        if (iter != bft_hash_map_[thread_idx].end()) {
            bft_ptr = iter->second;
            bft_ptr->Destroy();
            bft_hash_map_[thread_idx].erase(iter);
//             ZJC_DEBUG("remove bft gid: %s", common::Encode::HexEncode(gid).c_str());
        }
    }
}

int BftManager::LeaderPrepare(ZbftPtr& bft_ptr, const transport::MessagePtr& prepare_msg_ptr) {
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
            bft_ptr,
            "",
            "",
            header,
            new_bft_msg);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        if (!msg_res) {
            return kConsensusError;
        }
    }

    auto& elect_item = elect_items_[elect_item_idx_];
    new_bft_msg->set_member_index(elect_item.local_node_member_index);
    bft_ptr->init_prepare_timeout();
    bft_ptr->set_consensus_status(kConsensusPreCommit);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    if (prepare_msg_ptr == nullptr) {
        if (!LeaderSignMessage(msg_ptr)) {
            return kConsensusError;
        }
#ifdef ZJC_UNITTEST
        now_msg_[msg_ptr->thread_idx] = msg_ptr;
#else
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
        ZJC_ERROR("verify agg sign error!");
        return false;
    }

    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
    return true;
}

int BftManager::CheckPrecommit(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.has_precommit_gid() || bft_msg.precommit_gid().empty()) {
        return kConsensusSuccess;
    }

    auto bft_ptr = GetBft(msg_ptr->thread_idx, bft_msg.precommit_gid(), false);
    if (bft_ptr == nullptr) {
        return kConsensusError;
    }

    bool backup_agree_commit = false;
    do {
        if (bft_msg.prepare_hash() != bft_ptr->local_prepare_hash()) {
            // sync from other nodes
            bft_ptr->set_prepare_hash(bft_msg.prepare_hash());
            bft_ptr->CreatePrecommitVerifyHash();
            ZJC_INFO("use leader prepare hash: %s",
                common::Encode::HexEncode(bft_msg.prepare_hash()).c_str());
            bft_ptr->set_prepare_block(nullptr);
            SyncConsensusBlock(
                msg_ptr->thread_idx,
                bft_ptr->pool_index(),
                bft_msg.precommit_gid());
        }

//         ZJC_DEBUG("Backup CheckPrecommit: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
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
            assert(false);
            break;
        }
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        std::vector<uint64_t> bitmap_data;
        for (int32_t i = 0; i < bft_msg.bitmap_size(); ++i) {
            bitmap_data.push_back(bft_msg.bitmap(i));
        }

        bft_ptr->set_precoimmit_hash(bft_ptr->local_prepare_hash());
        bft_ptr->set_prepare_bitmap(bitmap_data);
        backup_agree_commit = true;
    } while (0);
    msg_ptr->response->header.mutable_zbft()->set_agree_commit(backup_agree_commit);
    assert(backup_agree_commit);
    if (!backup_agree_commit) {
        return kConsensusError;
    }
    
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    // now check commit
    CheckCommit(msg_ptr, false);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    return kConsensusSuccess;
}

int BftManager::CheckCommit(const transport::MessagePtr& msg_ptr, bool check_agg) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.has_commit_gid() || bft_msg.commit_gid().empty()) {
        return kConsensusSuccess;
    }

//     ZJC_DEBUG("backup CheckCommit: %s", common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
    auto bft_ptr = GetBft(msg_ptr->thread_idx, bft_msg.commit_gid(), false);
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

        bft_ptr->set_consensus_status(kConsensusCommited);
        if (bft_ptr->prepare_block() != nullptr) {
//             ZJC_DEBUG("backup CheckCommit success");
            HandleLocalCommitBlock(msg_ptr->thread_idx, bft_ptr);
        } else {
            // sync block from neighbor nodes
            ZJC_ERROR("backup commit block failed should sync: %s",
                common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
            return kConsensusError;
        }
    } while (0);
    
    // start new bft
    return kConsensusSuccess;
}

void BftManager::BackupPrepare(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (bft_msg.has_agree_commit() && !bft_msg.agree_commit()) {
        // just clear all zbft
//         ZJC_DEBUG("commit failed, remove all prepare gid; %s, precommit gid: %s, commit gid: %s",
//             common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//             common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
//             common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
        auto prepare_bft = GetBft(msg_ptr->thread_idx, bft_msg.prepare_gid(), false);
        if (prepare_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, prepare_bft->gid(), false);
        auto precommit_bft = prepare_bft->pipeline_prev_zbft_ptr();
        if (precommit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, precommit_bft->gid(), false);
        auto commit_bft = precommit_bft->pipeline_prev_zbft_ptr();
        if (commit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, commit_bft->gid(), false);
        return;
    }

    if (bft_msg.has_agree_precommit() && !bft_msg.agree_precommit()) {
        if (bft_msg.has_agree_commit()) {
            CheckCommit(msg_ptr, true);
        }

//         ZJC_DEBUG("precommit failed, remove all prepare gid; %s, precommit gid: %s, commit gid: %s",
//             common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//             common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
//             common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
        auto prepare_bft = GetBft(msg_ptr->thread_idx, bft_msg.prepare_gid(), false);
        if (prepare_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, prepare_bft->gid(), false);
        auto precommit_bft = prepare_bft->pipeline_prev_zbft_ptr();
        if (precommit_bft == nullptr) {
            return;
        }

        RemoveBft(msg_ptr->thread_idx, precommit_bft->gid(), false);
        return;
    }

//     ZJC_DEBUG("prepare gid: %s, precommit gid: %s, commit gid: %s",
//         common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//         common::Encode::HexEncode(bft_msg.precommit_gid()).c_str(),
//         common::Encode::HexEncode(bft_msg.commit_gid()).c_str());
    msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_msg.pool_index());
    if (bft_msg.has_prepare_gid() && !bft_msg.prepare_gid().empty()) {
        msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        if (CheckPrecommit(msg_ptr) != kConsensusSuccess) {
            return;
        }
        
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        auto bft_ptr = CreateBftPtr(msg_ptr);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        if (bft_ptr == nullptr || !bft_ptr->BackupCheckLeaderValid(&bft_msg)) {
            // oppose
//             ZJC_DEBUG("create bft ptr failed!");
            return;
        }
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        auto* new_bft_msg = msg_ptr->response->header.mutable_zbft();
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        int prepare_res = bft_ptr->Prepare(false, new_bft_msg);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
#ifdef ZJC_UNITTEST
        if (test_for_prepare_evil_) {
            ZJC_ERROR("1 bft backup prepare failed! not agree bft gid: %s",
                common::Encode::HexEncode(bft_ptr->gid()).c_str());
            return;
        }
#endif
//         ZJC_DEBUG("backup create consensus bft prepare hash: %s, gid: %s, tx size: %d",
//             common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
//             common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//             bft_ptr->txs_ptr()->txs.size());
        if (!bft_ptr->local_prepare_hash().empty()) {
//             ZJC_DEBUG("backup create consensus bft prepare hash: %s, prehash: %s, leader prehash: %s, pre height: %lu, leader pre height: %lu, gid: %s, tx size: %d",
//                 common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
//                 common::Encode::HexEncode(bft_ptr->prepare_block()->prehash()).c_str(),
//                 common::Encode::HexEncode(bft_msg.prepare_hash()).c_str(),
//                 bft_ptr->prepare_block()->height(),
//                 bft_msg.prepare_height(),
//                 common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//                 bft_ptr->txs_ptr()->txs.size());
            if (!bft_msg.prepare_hash().empty() && bft_ptr->prepare_block()->prehash() != bft_msg.prepare_hash()) {
                assert(false);
            }
            msg_ptr->response->header.mutable_zbft()->set_agree_precommit(true);
        }

        AddBft(bft_ptr);
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        bft_ptr->set_consensus_status(kConsensusPreCommit);
        std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
        bft_vec[0] = bft_ptr;
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        //         ZJC_DEBUG("BackupPrepare success: %s", common::Encode::HexEncode(bft_vec[0]->gid()).c_str());
        return;
    }

    if (bft_msg.has_precommit_gid() && !bft_msg.precommit_gid().empty()) {
        msg_ptr->response->header.mutable_zbft()->set_agree_commit(false);
        auto precommit_bft_ptr = GetBft(msg_ptr->thread_idx, bft_msg.precommit_gid(), false);
        if (precommit_bft_ptr == nullptr) {
            return;
        }

        if (BackupPrecommit(precommit_bft_ptr, msg_ptr) != kConsensusSuccess) {
            // sync block from others
            precommit_bft_ptr->set_prepare_hash(bft_msg.prepare_hash());
            precommit_bft_ptr->CreatePrecommitVerifyHash();
            ZJC_INFO("1 use leader prepare hash: %s",
                common::Encode::HexEncode(bft_msg.prepare_hash()).c_str());
            precommit_bft_ptr->set_prepare_block(nullptr);
            SyncConsensusBlock(
                msg_ptr->thread_idx,
                precommit_bft_ptr->pool_index(),
                precommit_bft_ptr->gid());
            return;
        }

        msg_ptr->response->header.mutable_zbft()->set_agree_commit(true);
    }

    if (bft_msg.has_commit_gid() && !bft_msg.commit_gid().empty()) {
        auto commit_bft_ptr = GetBft(msg_ptr->thread_idx, bft_msg.commit_gid(), false);
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

int BftManager::LeaderHandleZbftMessage(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.zbft();
    if (bft_msg.has_prepare_gid() && !bft_msg.prepare_gid().empty()) {
//         ZJC_DEBUG("has prepare  now leader handle gid: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        auto bft_ptr = LeaderGetZbft(msg_ptr, bft_msg.prepare_gid());
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        if (bft_ptr == nullptr) {
//             ZJC_ERROR("prepare get bft failed: %s", common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
            return kConsensusError;
        }

        auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
        if (bft_msg.agree_precommit() && bft_msg.prepare_hash() == bft_ptr->local_prepare_hash()) {
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
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

            int res = bft_ptr->LeaderPrecommitOk(
                tx_bft,
                bft_msg.member_index(),
                sign,
                member_ptr->id);
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //msg_ptr->times[msg_ptr->times_idx - 2] = msg_ptr->times[msg_ptr->times_idx - 1];
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

//             ZJC_DEBUG("LeaderHandleZbftMessage res: %d, mem: %d", res, bft_msg.member_index());
            if (res == kConsensusAgree) {
                //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
                //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

                if (bft_ptr->prepare_block() == nullptr) {
//                     ZJC_DEBUG("invalid block and sync from other hash: %s, gid: %s",
//                         common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str(),
//                         common::Encode::HexEncode(bft_msg.prepare_gid()).c_str());
                    assert(false);
                }
                //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
                //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(true);
                msg_ptr->response->header.mutable_zbft()->set_agree_commit(true);
                LeaderCallPrecommit(bft_ptr, msg_ptr);
                if (!msg_ptr->response->header.mutable_zbft()->has_pool_index()) {
                    msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                }
                //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
                //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

            } else if (res == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
                msg_ptr->response->header.mutable_zbft()->set_prepare_gid(bft_msg.prepare_gid());
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                ZJC_DEBUG("precommit call oppose now.");
            }
        } else {
            if (bft_ptr->AddPrepareOpposeNode(member_ptr->id) == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_precommit(false);
                msg_ptr->response->header.mutable_zbft()->set_prepare_gid(bft_msg.prepare_gid());
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                ZJC_INFO("precommit call oppose now gid: %s, prepare hash: %s",
                    common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
                    common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
                // just all consensus rollback
            }
        }
    }

    if (bft_msg.has_precommit_gid() && !bft_msg.precommit_gid().empty()) {
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

//         ZJC_DEBUG("has precommit now leader handle gid: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
        auto bft_ptr = LeaderGetZbft(msg_ptr, bft_msg.precommit_gid());
        if (bft_ptr == nullptr) {
//             ZJC_ERROR("precommit get bft failed: %s", common::Encode::HexEncode(bft_msg.precommit_gid()).c_str());
            return kConsensusError;
        }
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
        if (bft_msg.agree_commit()) {
            LeaderCommit(bft_ptr, msg_ptr);
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);
        } else {
            if (bft_ptr->AddPrecommitOpposeNode(member_ptr->id) == kConsensusOppose) {
                msg_ptr->response->header.mutable_zbft()->set_agree_commit(false);
                msg_ptr->response->header.mutable_zbft()->set_pool_index(bft_ptr->pool_index());
                //                 ZJC_DEBUG("commit call oppose now.");
            }
        }
    }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    return kConsensusSuccess;
}

ZbftPtr BftManager::LeaderGetZbft(
        const transport::MessagePtr& msg_ptr,
        const std::string& bft_gid) {
    auto& bft_msg = msg_ptr->header.zbft();
    auto bft_ptr = GetBft(msg_ptr->thread_idx, bft_gid, true);
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
        assert(false);
        return nullptr;
    }
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    return bft_ptr;
}

int BftManager::LeaderCallPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    auto next_prepare_bft = Start(msg_ptr->thread_idx, bft_ptr, msg_ptr->response);
    //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
    //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    if (next_prepare_bft != nullptr) {
        bft_vec[0] = next_prepare_bft;
//         ZJC_DEBUG("use next prepare.");
    } else {
//         ZJC_DEBUG("use g1_precommit_hash prepare.");
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

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
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        auto& elect_item = elect_items_[elect_item_idx_];
        if (bft_ptr->LeaderCommitOk(
                elect_item.local_node_member_index,
                sign,
                security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
            ZJC_ERROR("leader commit failed!");
            return kConsensusError;
        }
        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    }

    bft_ptr->init_precommit_timeout();
    bft_ptr->set_consensus_status(kConsensusCommit);
    bft_vec[1] = bft_ptr;
    auto prev_ptr = bft_ptr->pipeline_prev_zbft_ptr();
    if (prev_ptr != nullptr) {
        prev_ptr->set_consensus_status(kConsensusCommited);
        if (prev_ptr->prepare_block() != nullptr) {
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

            HandleLocalCommitBlock(msg_ptr->thread_idx, prev_ptr);
            //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
            //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        } else {
            ZJC_ERROR("leader must sync block: %s",
                common::Encode::HexEncode(prev_ptr->local_prepare_hash()).c_str());
            return kConsensusSuccess;
        }

        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

        //msg_ptr->times[msg_ptr->times_idx++] = common::TimeUtils::TimestampUs();
        //assert(msg_ptr->times[msg_ptr->times_idx - 1] - msg_ptr->times[msg_ptr->times_idx - 2] < 10000);

    }

//     ZJC_DEBUG("LeaderCallPrecommit success gid: %s",
//         common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

int BftManager::BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
//     ZJC_DEBUG("BackupPrecommit");
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.agree_precommit()) {
        ZJC_INFO("BackupPrecommit LeaderCallCommitOppose gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusSuccess;
    }

#ifdef ZJC_UNITTEST
    if (test_for_precommit_evil_) {
        ZJC_ERROR("1 bft backup precommit failed! not agree bft gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }
#endif

    std::string msg_hash_src;
    msg_hash_src.reserve(32 + 128);
    msg_hash_src.append(bft_ptr->local_prepare_hash());
    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < bft_msg.bitmap_size(); ++i) {
        auto data = bft_msg.bitmap(i);
        bitmap_data.push_back(data);
        msg_hash_src.append((char*)&data, sizeof(data));
    }

    bft_ptr->set_precoimmit_hash(common::Hash::keccak256(msg_hash_src));
    bft_ptr->set_prepare_bitmap(bitmap_data);
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusError;
    }

    if (!bft_ptr->set_bls_precommit_agg_sign(
            sign,
            bft_ptr->precommit_bls_agg_verify_hash())) {
        ZJC_ERROR("backup verify leader agg sign failed.");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommit);
    std::vector<ZbftPtr>& bft_vec = *static_cast<std::vector<ZbftPtr>*>(msg_ptr->tmp_ptr);
    bft_vec[1] = bft_ptr;
//     ZJC_DEBUG("BackupPrecommit success.");
    return kConsensusSuccess;
}

int BftManager::LeaderCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
//     ZJC_DEBUG("LeaderCommit");
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_ptr->this_node_is_leader()) {
        ZJC_ERROR("check leader error.");
        return kConsensusError;
    }

    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("bft_ptr->members_ptr()->size() <= bft_msg.member_index()",
            bft_ptr->members_ptr()->size(), bft_msg.member_index());
        return kConsensusError;
    }

    if (bft_msg.member_index() == elect::kInvalidMemberIndex) {
        ZJC_ERROR("bft_msg.member_index() == elect::kInvalidMemberIndex.");
        return kConsensusError;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        ZJC_ERROR("verify backup valid error!");
        assert(false);
        return kConsensusError;
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

    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("bft_msg.member_index() == elect::kInvalidMemberIndex.",
            bft_ptr->members_ptr()->size(), bft_msg.member_index());
        return kConsensusError;
    }

    int res = bft_ptr->LeaderCommitOk(
        bft_msg.member_index(),
        sign,
        member_ptr->id);
    if (res == kConsensusAgree) {
        LeaderCallCommit(msg_ptr, bft_ptr);
    }

    return kConsensusSuccess;
}

int BftManager::LeaderCallCommitOppose(
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr) {
    auto res = BftProto::LeaderCreateCommit(
        bft_ptr, false, msg_ptr->response->header);
    if (!res) {
        ZJC_ERROR("leader create commit message failed!");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommited);
    ZJC_ERROR("LeaderCallCommitOppose gid: %s",
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

void BftManager::HandleLocalCommitBlock(int32_t thread_idx, ZbftPtr& bft_ptr) {
    auto& zjc_block = bft_ptr->prepare_block();
    zjc_block->set_pool_index(bft_ptr->pool_index());
    const auto& prepare_bitmap_data = bft_ptr->prepare_bitmap().data();
    std::vector<uint64_t> bitmap_data;
    for (uint32_t i = 0; i < prepare_bitmap_data.size(); ++i) {
        zjc_block->add_precommit_bitmap(prepare_bitmap_data[i]);
        bitmap_data.push_back(prepare_bitmap_data[i]);
    }

    auto& bls_commit_sign = bft_ptr->bls_precommit_agg_sign();
    zjc_block->set_bls_agg_sign_x(
        libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->X));
    zjc_block->set_bls_agg_sign_y(
        libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->Y));
    auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(zjc_block);
    new_block_cache_callback_(
        thread_idx,
        queue_item_ptr->block_ptr,
        queue_item_ptr->db_batch);
    pools_mgr_->TxOver(
        queue_item_ptr->block_ptr->pool_index(),
        queue_item_ptr->block_ptr->tx_list());
    block_mgr_->ConsensusAddBlock(thread_idx, queue_item_ptr);
    if (bft_ptr->this_node_is_leader()) {
        LeaderBroadcastBlock(thread_idx, zjc_block);
    }

    RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), bft_ptr->this_node_is_leader());
    assert(bft_ptr->prepare_block()->precommit_bitmap_size() == zjc_block->precommit_bitmap_size());
    // for test
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

    ZJC_DEBUG("new block: %s, gid: %s",
        common::Encode::HexEncode(zjc_block->hash()).c_str(),
        common::Encode::HexEncode(bft_ptr->gid()).c_str());
}

void BftManager::LeaderBroadcastBlock(
        uint8_t thread_index,
        const std::shared_ptr<block::protobuf::Block>& block) {
    if (block->tx_list_size() != 1) {
        return;
    }

    switch (block->tx_list(0).step()) {
    case pools::protobuf::kConsensusRootTimeBlock:
        tm_block_mgr_->BroadcastTimeblock(thread_index, block);
        break;
    case pools::protobuf::kNormalTo:
        BroadcastLocalTosBlock(thread_index, block);
        break;
    default:
        break;
    }
}

void BftManager::BroadcastLocalTosBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (block_item->tx_list_size() != 1) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& tx = block_item->tx_list(0);
    pools::protobuf::ToTxMessage to_tx;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).val_size() == 0) {
            std::string val;
            if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                if (tx.storages(i).key() == protos::kNormalTos) {
                    if (!to_tx.ParseFromString(val)) {
                        return;
                    }

                    if (to_tx.to_heights().sharding_id() == common::GlobalInfo::Instance()->network_id()) {
                        return;
                    }
                }

                auto kv = msg.mutable_sync()->add_items();
                kv->set_key(tx.storages(i).val_hash());
                kv->set_value(val);
            }
        }
    }

    if (!to_tx.has_to_heights()) {
        return;
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kPoolsMessage);
    dht::DhtKeyManager dht_key(to_tx.to_heights().sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    auto& cross_msg = *msg.mutable_cross_tos();
    *cross_msg.mutable_block() = *block_item;
    auto* brdcast = msg.mutable_broadcast();
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success broadcast cross tos height: %lu, sharding id: %u",
        block_item->height(), to_tx.to_heights().sharding_id());
}

int BftManager::LeaderCallCommit(
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr) {
    // check pre-commit multi sign and leader commit
    auto res = BftProto::LeaderCreateCommit(
        bft_ptr,
        true,
        msg_ptr->response->header);
    if (!res) {
        ZJC_ERROR("leader create commit message failed!");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommited);
    if (bft_ptr->prepare_block() != nullptr) {
        HandleLocalCommitBlock(msg_ptr->thread_idx, bft_ptr);
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
//         ZJC_DEBUG("leader should sync block now: %s.",
//             common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
        return kConsensusSuccess;
    }
    
    return kConsensusSuccess;
}

int BftManager::BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
//     ZJC_DEBUG("BackupCommit");
    auto& bft_msg = msg_ptr->header.zbft();
    if (!bft_msg.agree_commit()) {
        ZJC_ERROR("BackupCommit LeaderCallCommitOppose gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusSuccess;
    }
    
    if (bft_ptr->precommit_hash().empty()) {
        ZJC_ERROR("precommit hash empty.");
        return kConsensusError;
    }

    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < bft_msg.commit_bitmap_size(); ++i) {
        bitmap_data.push_back(bft_msg.commit_bitmap(i));
    }

    bft_ptr->set_precommit_bitmap(bitmap_data);
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusError;
    }

    bft_ptr->set_bls_commit_agg_sign(sign);
    bft_ptr->set_consensus_status(kConsensusCommited);
    if (bft_ptr->prepare_block() != nullptr) {
        HandleLocalCommitBlock(msg_ptr->thread_idx, bft_ptr);
    } else {
//         ZJC_DEBUG("should sync block now: %s.",
//             common::Encode::HexEncode(bft_ptr->local_prepare_hash()).c_str());
        return kConsensusError;
    }

    return kConsensusSuccess;
}

bool BftManager::IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info) {
    if (tx_info.step() != common::kConsensusCreateContract) {
        return false;
    }

    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.storages(i).key() == protos::kContractBytesCode) {
            if (zjcvm::IsContractBytesCode(tx_info.storages(i).val_hash())) {
                return true;
            }
        }
    }

    return false;
}

void BftManager::CheckTimeout(uint8_t thread_idx) {
    auto now_timestamp_us = common::TimeUtils::TimestampUs();
    if (prev_checktime_out_milli_ > now_timestamp_us / 1000) {
        return;
    }

    prev_checktime_out_milli_ = now_timestamp_us / 1000 + kCheckTimeoutPeriodMilli;
    while (!bft_queue_[thread_idx].empty()) {
        auto bft_ptr = bft_queue_[thread_idx].front();
        if (!bft_ptr->timeout(now_timestamp_us)) {
            break;
        }

        RemoveBft(thread_idx, bft_ptr->gid(), bft_ptr->this_node_is_leader());
        bft_queue_[thread_idx].pop();
    }
    
}

}  // namespace consensus

}  // namespace zjchain
