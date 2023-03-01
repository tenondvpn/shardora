#include "consensus/zbft/bft_manager.h"

#include <cassert>

#include "consensus/zbft/bft_proto.h"
#include "consensus/zbft/root_zbft.h"
#include "consensus/zbft/zbft.h"
#include "consensus/zbft/zbft_utils.h"
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

namespace zjchain {

namespace consensus {

BftManager::BftManager() {
    network::Route::Instance()->RegisterMessage(
        common::kConsensusMessage,
        std::bind(&BftManager::HandleMessage, this, std::placeholders::_1));
    network::Route::Instance()->RegisterMessage(
        common::kConsensusTimerMessage,
        std::bind(&BftManager::ConsensusTimerMessage, this, std::placeholders::_1));
    //     CheckCommitBackupRecall();
}

BftManager::~BftManager() {
    if (bft_hash_map_ != nullptr) {
        delete []bft_hash_map_;
    }
}

int BftManager::Init(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count) {
    account_mgr_ = account_mgr;
    block_mgr_ = block_mgr;
    elect_mgr_ = elect_mgr;
    pools_mgr_ = pool_mgr;
    db_ = db;
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalFrom,
        std::bind(&BftManager::CreateFromTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalTo,
        std::bind(&BftManager::CreateToTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kConsensusLocalTos,
        std::bind(&BftManager::CreateToTxLocal, this, std::placeholders::_1));
    security_ptr_ = security_ptr;
    txs_pools_ = std::make_shared<WaitingTxsPools>(pools_mgr_);
    thread_count_ = thread_count;
    bft_hash_map_ = new std::unordered_map<std::string, ZbftPtr>[thread_count];
    for (uint8_t i = 0; i < thread_count_; ++i) {
        thread_set_[i] = nullptr;
        bft_gids_[i] = common::Hash::keccak256(common::Random::RandomString(1024));
        bft_gids_index_[i] = 0;
    }

    return kConsensusSuccess;
}

int BftManager::OnNewElectBlock(int32_t local_leader_index, int32_t leader_count) {
    if (local_leader_index < 0 || local_leader_index >= leader_count) {
        return kConsensusSuccess;
    }

    std::set<uint32_t> leader_pool_set;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        if (i % leader_count == local_leader_index) {
            leader_pool_set.insert(i);
        }
    }

    auto thread_item = std::make_shared<PoolTxIndexItem>();
    for (uint8_t j = 0; j < thread_count_; ++j) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            if (i % thread_count_ == j && leader_pool_set.find(i) != leader_pool_set.end()) {
                thread_item->pools.push_back(i);
            }
        }

        thread_item->prev_index = 0;
        thread_set_[j] = thread_item;  // ptr change, multi-thread safe
    }

    return kConsensusSuccess;
}

void BftManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
#ifndef ZJC_UNITTEST
    Start(msg_ptr->thread_index);
#endif
}

int BftManager::Start(uint8_t thread_index) {
    CheckTimeout(thread_index);
    auto thread_item = thread_set_[thread_index];
    if (thread_item == nullptr) {
        return kConsensusSuccess;
    }

    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    auto begin_index = thread_item->prev_index;
    for (; thread_item->prev_index < thread_item->pools.size(); ++thread_item->prev_index) {
        txs_ptr = txs_pools_->LeaderGetValidTxs(
            false,
            thread_item->pools[thread_item->prev_index]);
        if (txs_ptr != nullptr) {
            // now leader create zbft ptr and start consensus
            break;
        }
    }

    if (txs_ptr == nullptr) {
        for (thread_item->prev_index = 0;
                thread_item->prev_index < begin_index; ++thread_item->prev_index) {
            txs_ptr = txs_pools_->LeaderGetValidTxs(
                false,
                thread_item->pools[thread_item->prev_index]);
            if (txs_ptr != nullptr) {
                // now leader create zbft ptr and start consensus
                break;
            }
        }
    }

    if (txs_ptr == nullptr) {
        return kConsensusSuccess;
    }

    txs_ptr->thread_index = thread_index;
    return StartBft(txs_ptr);
}

int BftManager::InitZbftPtr(bool leader, ZbftPtr& bft_ptr) {
    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto network_id = common::GlobalInfo::Instance()->network_id();
    uint64_t elect_height = elect_mgr_->latest_height(network_id);
    auto members_ptr = elect_mgr_->GetNetworkMembersWithHeight(
        elect_height, network_id, &common_pk, &sec_key);
    if (members_ptr == nullptr) {
        ZJC_ERROR("bft init failed elect_height: %lu, network_id: %u",
            elect_height, network_id);
        return kConsensusError;
    }

    common::BftMemberPtr leader_mem_ptr = nullptr;
    if (leader) {
        leader_mem_ptr = elect_mgr_->local_mem_ptr(network_id);
    }

    if (bft_ptr->Init(
            elect_height,
            leader_mem_ptr,
            members_ptr,
            common_pk,
            sec_key) != kConsensusSuccess) {
        ZJC_ERROR("bft init failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

int BftManager::StartBft(std::shared_ptr<WaitingTxsItem>& txs_ptr) {
    ZbftPtr bft_ptr = nullptr;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        bft_ptr = std::make_shared<RootZbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            pools_mgr_);
    } else {
        bft_ptr = std::make_shared<Zbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            pools_mgr_);
    }

    if (InitZbftPtr(true, bft_ptr) != kConsensusSuccess) {
        return kConsensusError;
    }

    auto& gid = bft_gids_[txs_ptr->thread_index];
    uint64_t* tmp_gid = (uint64_t*)gid.data();
    tmp_gid[0] = bft_gids_index_[txs_ptr->thread_index]++;
    bft_ptr->set_gid(gid);
    bft_ptr->set_network_id(common::GlobalInfo::Instance()->network_id());
    // bft_ptr->set_randm_num(vss::VssManager::Instance()->EpochRandom());
    bft_ptr->set_member_count(elect_mgr_->GetMemberCount(
        common::GlobalInfo::Instance()->network_id()));
    int leader_pre = LeaderPrepare(bft_ptr);
    if (leader_pre != kConsensusSuccess) {
        ZJC_ERROR("leader prepare failed!");
        return leader_pre;
    }

    ZJC_DEBUG("this node is leader and start bft: %s, pool index: %d",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(), bft_ptr->pool_index());
    return kConsensusSuccess;
}

uint32_t BftManager::GetMemberIndex(uint32_t network_id, const std::string& node_id) {
    return elect_mgr_->GetMemberIndex(network_id, node_id);
}

void BftManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kConsensusMessage);
    BftItemPtr bft_item_ptr = std::make_shared<BftItem>();
    bft_item_ptr->msg_ptr = msg_ptr;
    auto& bft_msg = header.hotstuff_proto();
    assert(bft_msg.has_bft_step());
    if (!bft_msg.has_bft_step()) {
        ZJC_ERROR("bft message not has bft step failed!");
        return;
    }

    // leader 
    if (!bft_msg.leader()) {
        BackupHandleHotstuffMessage(msg_ptr->thread_idx, bft_item_ptr);
        return;
    }

    auto bft_ptr = GetBft(msg_ptr->thread_idx, bft_msg.gid(), true);
    if (bft_ptr == nullptr) {
        ZJC_DEBUG("leader get bft gid failed[%s]",
            common::Encode::HexEncode(bft_msg.gid()).c_str());
        return;
    }

    if (!bft_ptr->this_node_is_leader()) {
        ZJC_DEBUG("not valid leader get bft gid failed[%s]",
            common::Encode::HexEncode(bft_msg.gid()).c_str());
        return;
    }

    if (!bft_msg.agree()) {
        ZJC_DEBUG("not agree leader get bft gid failed[%s]",
            common::Encode::HexEncode(bft_msg.gid()).c_str());
        LeaderHandleBftOppose(bft_ptr, msg_ptr);
        return;
    }

    HandleHotstuffMessage(bft_ptr, msg_ptr);
}

void BftManager::SetBftGidPrepareInvalid(BftItemPtr& bft_item_ptr) {
//     bft_item_ptr->prepare_valid = false;
//     bft_gid_map_.Insert(bft_item_ptr->msg_ptr->header.hotstuff_proto().gid(), bft_item_ptr);
}

void BftManager::CacheBftPrecommitMsg(BftItemPtr& bft_item_ptr) {
//     bft_item_ptr->prepare_valid = true;
//     bft_gid_map_.Insert(bft_item_ptr->msg_ptr->header.hotstuff_proto().gid(), bft_item_ptr);
}

bool BftManager::VerifyLeaderIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr) {
    if (!msg_ptr->header.has_sign()) {
        return false;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_ptr_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        return false;
    }

    return true;
}

void BftManager::BackupHandleHotstuffMessage(uint8_t thread_index, BftItemPtr& bft_item_ptr) {
    // verify leader signature
    ZbftPtr bft_ptr = nullptr;
    auto& bft_msg = bft_item_ptr->msg_ptr->header.hotstuff_proto();
    if (bft_msg.bft_step() == kConsensusPrepare) {
        bft_ptr = CreateBftPtr(bft_item_ptr->msg_ptr);
        if (bft_ptr == nullptr || !bft_ptr->BackupCheckLeaderValid(bft_item_ptr->msg_ptr)) {
            SetBftGidPrepareInvalid(bft_item_ptr);
            // oppose
            BackupSendOppose(bft_item_ptr->msg_ptr, bft_ptr);
            ZJC_DEBUG("create bft ptr failed!");
            return;
        }
    } else {
        bft_ptr = GetBft(thread_index, bft_msg.gid(), false);
        if (bft_ptr == nullptr) {
            ZJC_DEBUG("get bft failed!");
            return;
        }
    }

    if (!VerifyLeaderIdValid(bft_item_ptr->msg_ptr, bft_ptr->leader_mem_ptr())) {
        ZJC_ERROR("leader invalid!");
        return;
    }

    if (!bft_msg.agree()) {
        bft_item_ptr->prepare_valid = false;
        bft_ptr->not_aggree();
    } else {
        HandleHotstuffMessage(bft_ptr, bft_item_ptr->msg_ptr);
    }
    
    if (!bft_ptr->aggree()) {
        RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), false);
    }
}

bool BftManager::VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr) {
    std::string msg_for_hash = msg_ptr->header.hotstuff_proto().bls_sign_x() +
        msg_ptr->header.hotstuff_proto().bls_sign_y();
    auto msg_hash = common::Hash::keccak256(msg_for_hash);
    std::string ecdh_key;
    if (security_ptr_->GetEcdhKey(
            mem_ptr->pubkey,
            &ecdh_key) != security::kSecuritySuccess) {
        ZJC_ERROR("get ecdh key failed peer pk: %s",
            common::Encode::HexEncode(mem_ptr->pubkey).c_str());
        return false;
    }

    std::string enc_out;
    if (security_ptr_->Encrypt(msg_hash, ecdh_key, &enc_out) != security::kSecuritySuccess) {
        ZJC_ERROR("encrypt key failed peer pk: %s",
            common::Encode::HexEncode(mem_ptr->pubkey).c_str());
        return false;
    }

    return enc_out == msg_ptr->header.hotstuff_proto().backup_enc_data();
}

void BftManager::LeaderHandleBftOppose(
        const ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    if (bft_msg.member_index() >= bft_ptr->members_ptr()->size()) {
        ZJC_ERROR("invalid bft message member index: %d", bft_msg.member_index());
        return;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (member_ptr == nullptr) {
        return;
    }

    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        return;
    }

    int32_t res = kConsensusSuccess;
    if (bft_msg.bft_step() == kConsensusPrepare) {
        res = bft_ptr->AddPrepareOpposeNode(member_ptr->id);
    }

    if (bft_msg.bft_step() == kConsensusPreCommit) {
        res = bft_ptr->AddPrecommitOpposeNode(member_ptr->id);
    }

    if (res == kConsensusOppose) {
        LeaderCallPrecommitOppose(bft_ptr);
        RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), true);
        pools_mgr_->SetTimeout(bft_ptr->pool_index());
    }
}

void BftManager::BackupSendOppose(
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr) {
    auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = res_msg_ptr->header;
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    bft_msg.set_error(kConsensusInvalidPackage);
    if (bft_ptr != nullptr && bft_ptr->handle_last_error_code() > 0) {
        bft_msg.set_error(bft_ptr->handle_last_error_code());
        // bft_msg.set_data(bft_ptr->handle_last_error_msg());
    }

    auto& from_bft_msg = msg_ptr->header.hotstuff_proto();
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dhtkey(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dhtkey.StrKey());
    msg.set_type(common::kConsensusMessage);
    msg.set_hop_count(0);
    bft_msg.set_leader(true);
    bft_msg.set_gid(from_bft_msg.gid());
    bft_msg.set_net_id(from_bft_msg.net_id());
    bft_msg.set_agree(false);
    bft_msg.set_bft_step(from_bft_msg.bft_step());
    bft_msg.set_epoch(from_bft_msg.epoch());
    bft_msg.set_member_index(elect_mgr_->local_node_member_index());
    std::string sign;
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    if (security_ptr_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return;
    }

    msg.set_sign(sign);
#ifndef ZJC_UNITTEST
    transport::TcpTransport::Instance()->Send(
        msg_ptr->thread_idx,
        msg_ptr->conn->PeerIp(),
        msg_ptr->conn->PeerPort(),
        msg);
#else
    bk_prepare_op_msg_ = res_msg_ptr;
#endif // !ZJC_UNITTEST

}

void BftManager::HandleHotstuffMessage(
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bft_msg = header.hotstuff_proto();
    int res = kConsensusSuccess;
    switch (bft_msg.bft_step()) {
    case kConsensusPrepare: {
        if (!bft_msg.leader()) {
            res = BackupPrepare(bft_ptr, msg_ptr);
        } else {
            LeaderPrecommit(bft_ptr, msg_ptr);
        }
        break;
    }
    case kConsensusPreCommit: {
        if (!bft_msg.leader()) {
            res = BackupPrecommit(bft_ptr, msg_ptr);
        } else {
            LeaderCommit(bft_ptr, msg_ptr);
        }
        break;
    }
    case kConsensusCommit: {
        if (!bft_msg.leader()) {
            BackupCommit(bft_ptr, msg_ptr);
        } else {
            assert(false);
        }
        break;
    }
    default:
        assert(false);
        break;
    }

    if (res != kConsensusSuccess) {
        BackupSendOppose(msg_ptr, bft_ptr);
        bft_ptr->not_aggree();
    }
}

ZbftPtr BftManager::CreateBftPtr(const transport::MessagePtr& msg_ptr) {
//     if (!pools_mgr_->PoolLocked(bft_msg.pool_index())) {
//         ZJC_ERROR("pool has locked[%d]", bft_msg.pool_index());
//         return nullptr;
//     }
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    std::vector<uint64_t> bloom_data;
    std::shared_ptr<WaitingTxsItem> txs_ptr = nullptr;
    auto& bloom_filter = bft_msg.tx_bft().ltx_prepare().bloom_filter();
    if (!bloom_filter.empty()) {
        for (int32_t i = 0; i < bloom_filter.size(); ++i) {
            bloom_data.push_back(bloom_filter[i]);
        }

        common::BloomFilter bf(bloom_data, kHashCount);
        txs_ptr = txs_pools_->FollowerGetTxs(bft_msg.pool_index(), bf, 0);
        ZJC_DEBUG("get tx count: %u, pool: %d", bloom_data.size(), bft_msg.pool_index());
    } else if (bft_msg.tx_bft().ltx_prepare().tx_hash_list_size() > 0) {
        // get txs direct
    } else {
        ZJC_ERROR("invalid consensus, tx empty.");
        return nullptr;
    }

    if (txs_ptr == nullptr) {
        ZJC_ERROR("invalid consensus, tx empty.");
        return nullptr;
    }
    
    txs_ptr->thread_index = msg_ptr->thread_idx;
    ZbftPtr bft_ptr = nullptr;
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        bft_ptr = std::make_shared<RootZbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            pools_mgr_);
    } else {
        bft_ptr = std::make_shared<Zbft>(
            account_mgr_,
            security_ptr_,
            bls_mgr_,
            txs_ptr,
            pools_mgr_);
    }

    if (InitZbftPtr(false, bft_ptr) != kConsensusSuccess) {
        return nullptr;
    }

    bft_ptr->set_gid(bft_msg.gid());
    bft_ptr->set_network_id(bft_msg.net_id());
    bft_ptr->set_consensus_status(kConsensusPrepare);
    bft_ptr->set_member_count(elect_mgr_->GetMemberCount(bft_msg.net_id()));
    return bft_ptr;
}

bool BftManager::VerifyAggSignWithMembers(
        const common::MembersPtr& members,
        const block::protobuf::Block& block) {
    auto hash = GetBlockHash(block);
//     for (int32_t i = 0; i < block.bitmap_size(); ++i) {
//         block_hash += std::to_string(block.bitmap(i));
//     }
// 
//     auto hash = common::Hash::Hash256(block_hash);
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(block.bls_agg_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(block.bls_agg_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusError;
    }

    uint32_t t = common::GetSignerCount(members->size());
    uint32_t n = members->size();
    if (bls_mgr_->Verify(
            t,
            n,
            elect_mgr_->GetCommonPublicKey(
            block.electblock_height(),
            block.network_id()),
            sign,
            hash) != bls::kBlsSuccess) {
        auto tmp_block_hash = GetBlockHash(block);
        ZJC_ERROR("VerifyBlsAggSignature agg sign failed!prepare hash: %s, agg sign hash: %s,"
            "t: %u, n: %u, elect height: %lu, network id: %u, agg x: %s, agg y: %s",
            common::Encode::HexEncode(tmp_block_hash).c_str(),
            common::Encode::HexEncode(hash).c_str(),
            t, n, block.electblock_height(), block.network_id(),
            block.bls_agg_sign_x().c_str(),
            block.bls_agg_sign_y().c_str());
        return false;
    }

    return true;
}

bool BftManager::AggSignValid(
        uint32_t thread_idx,
        uint32_t type,
        const block::protobuf::Block& block) {
    assert(thread_idx < common::kMaxThreadCount);
    if (!block.has_bls_agg_sign_x() ||
            !block.has_bls_agg_sign_y() ||
            block.precommit_bitmap_size() <= 0) {
        ZJC_ERROR("commit must have agg sign. block.has_bls_agg_sign_y(): %d,"
            "block.has_bls_agg_sign_y(): %d, block.bitmap_size(): %u",
            block.has_bls_agg_sign_x(), block.has_bls_agg_sign_y(), block.precommit_bitmap_size());
        return false;
    }

    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        block.network_id(),
        nullptr,
        nullptr);
    if (members == nullptr) {
        // The election block arrives later than the consensus block,
        // causing the aggregate signature verification to fail
        // add to waiting verify pool.
        auto block_ptr = std::make_shared<block::protobuf::Block>(block);
//         waiting_verify_block_queue_[thread_idx].push(
//             std::make_shared<WaitingBlockItem>(block_ptr, type));
        return false;
    }

    return VerifyAggSignWithMembers(members, block);
}

common::MembersPtr BftManager::GetNetworkMembers(uint32_t network_id) {
    return elect_mgr_->GetNetworkMembers(network_id);
}

void BftManager::RootCommitAddNewAccount(
        const block::protobuf::Block& block,
        db::DbWriteBach& db_batch) {
    auto& tx_list = block.tx_list();
    if (tx_list.empty()) {
        ZJC_ERROR("to has no transaction info!");
        return;
    }

    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() != 0) {
            continue;
        }

        // db::DbWriteBach db_batch;
        // if (block::AccountManager::Instance()->AddNewAccount(
        //         tx_list[i],
        //         block.height(),
        //         block.hash(),
        //         db_batch) != block::kBlockSuccess) {
        //     ZJC_ERROR("add new account failed");
        //     continue;
        // }
    }
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
//     ZJC_DEBUG("add bft and now size: %d", bft_hash_map_.size());
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
            bft_hash_map_[thread_idx].erase(iter);
            ZJC_DEBUG("remove bft gid: %s", common::Encode::HexEncode(gid).c_str());
        }
    }
}

int BftManager::LeaderPrepare(ZbftPtr& bft_ptr) {
    hotstuff::protobuf::HotstuffMessage bft_msg;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    int res = bft_ptr->Prepare(true, msg_ptr);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }

    res = AddBft(bft_ptr);
    if (res != kConsensusSuccess) {
        ZJC_ERROR("AddBft failed[%u].", res);
        return res;
    }

    auto msg_res = BftProto::LeaderCreatePrepare(
        security_ptr_,
        bft_ptr,
        msg_ptr->header);
    if (!msg_res) {
        return kConsensusError;
    }
    
    bft_ptr->init_prepare_timeout();
    // (TODO): just for test
#ifdef ZJC_UNITTEST
    leader_prepare_msg_ = msg_ptr;
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
    return kConsensusSuccess;
}

int BftManager::BackupPrepare(
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
    auto backup_msg_ptr = std::make_shared<transport::TransportMessage>();
    int prepare_res = bft_ptr->Prepare(false, backup_msg_ptr);
#ifdef ZJC_UNITTEST
    if (test_for_prepare_evil_) {
        ZJC_ERROR("1 bft backup prepare failed! not agree bft gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }
#endif
    if (prepare_res != kConsensusSuccess) {
        ZJC_ERROR("1 bft backup prepare failed! not agree bft gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusError;
    }

    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    bool res = BftProto::BackupCreatePrepare(
        security_ptr_,
        bls_mgr_,
        msg_ptr->header,
        bft_msg,
        bft_ptr,
        true,
        backup_msg_ptr->header);
    ZJC_DEBUG("bft backup prepare success! agree bft gid: %s, from: %s:%d",
        common::Encode::HexEncode(bft_ptr->gid()).c_str(),
        bft_msg.node_ip().c_str(), bft_msg.node_port());
    if (!res) {
        ZJC_ERROR("message set data failed!");
        return kConsensusError;
    }

    AddBft(bft_ptr);
    bft_ptr->set_consensus_status(kConsensusPreCommit);
#ifdef ZJC_UNITTEST
    backup_prepare_msg_ = backup_msg_ptr;
#else
    transport::TcpTransport::Instance()->Send(
        msg_ptr->thread_idx,
        msg_ptr->conn->PeerIp(),
        msg_ptr->conn->PeerPort(),
        backup_msg_ptr->header);
#endif
    return kConsensusSuccess;
}

int BftManager::LeaderPrecommit(
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    if (bft_ptr->members_ptr()->size() <= bft_msg.member_index()) {
        ZJC_ERROR("backup message member index invalid. %d", bft_msg.member_index());
        return kConsensusError;
    }

    auto& member_ptr = (*bft_ptr->members_ptr())[bft_msg.member_index()];
    if (member_ptr->public_ip == 0) {
        member_ptr->public_ip = common::IpToUint32(bft_msg.node_ip().c_str());
        member_ptr->public_port = bft_msg.node_port();
    }

    if (!VerifyBackupIdValid(msg_ptr, member_ptr)) {
        ZJC_ERROR("verify backup valid error!");
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

    auto& tx_bft = bft_msg.tx_bft();
    int res = bft_ptr->LeaderPrecommitOk(
        tx_bft.ltx_prepare(),
        bft_msg.member_index(),
        sign,
        member_ptr->id);
    if (res == kConsensusAgree) {
        LeaderCallPrecommit(bft_ptr);
    }

    return kConsensusSuccess;
}

int BftManager::LeaderCallPrecommitOppose(const ZbftPtr& bft_ptr) {
    // check pre-commit multi sign
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    auto res = BftProto::LeaderCreatePreCommit(security_ptr_, bft_ptr, false, msg);
    if (!res) {
        return kConsensusError;
    }

    ZJC_ERROR("LeaderCallPrecommitOppose gid: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
#ifdef ZJC_UNITTEST
    leader_precommit_msg_ = msg_ptr;
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
    return kConsensusSuccess;
}

int BftManager::LeaderCallPrecommit(ZbftPtr& bft_ptr) {
    // check pre-commit multi sign
    bft_ptr->init_precommit_timeout();
    libff::alt_bn128_G1 sign;
    if (bls_mgr_->Sign(
            bft_ptr->min_aggree_member_count(),
            bft_ptr->member_count(),
            bft_ptr->local_sec_key(),
            bft_ptr->precommit_hash(),
            &sign) != bls::kBlsSuccess) {
        ZJC_ERROR("leader signature error.");
        return kConsensusError;
    }

    if (bft_ptr->LeaderCommitOk(
            elect_mgr_->local_node_member_index(),
            sign,
            security_ptr_->GetAddress()) != kConsensusWaitingBackup) {
        ZJC_ERROR("leader commit failed!");
        RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), true);
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommit);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& precommit_msg = msg_ptr->header;  // msg;
    auto res = BftProto::LeaderCreatePreCommit(
        security_ptr_, bft_ptr, true, precommit_msg);
    if (!res) {
        return kConsensusError;
    }

    ZJC_ERROR("LeaderCallPrecommit gid: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
#ifdef ZJC_UNITTEST
    leader_precommit_msg_ = msg_ptr;
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
    return kConsensusSuccess;
}

int BftManager::BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    if (!bft_msg.agree()) {
        ZJC_INFO("BackupPrecommit LeaderCallCommitOppose gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusSuccess;
    }

    if (VerifyBlsAggSignature(bft_ptr, bft_msg, bft_ptr->local_prepare_hash()) != kConsensusSuccess) {
        ZJC_INFO("VerifyBlsAggSignature error gid: %s",
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

    std::string msg_hash_src = bft_ptr->local_prepare_hash();
    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < bft_msg.bitmap_size(); ++i) {
        bitmap_data.push_back(bft_msg.bitmap(i));
        msg_hash_src += std::to_string(bft_msg.bitmap(i));
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

    bft_ptr->set_bls_precommit_agg_sign(sign);
    // check prepare multi sign
    auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = res_msg_ptr->header;
    std::string precommit_data;
    auto res = BftProto::BackupCreatePreCommit(
        security_ptr_,
        bls_mgr_,
        msg,
        bft_msg,
        bft_ptr,
        true,
        bft_ptr->precommit_hash(),
        msg);
    if (!res) {
        ZJC_ERROR("BackupCreatePreCommit not has data.");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommit);
    // send pre-commit to leader
#ifdef ZJC_UNITTEST
    backup_precommit_msg_ = res_msg_ptr;
#else
    transport::TcpTransport::Instance()->Send(
        msg_ptr->thread_idx,
        msg_ptr->conn->PeerIp(),
        msg_ptr->conn->PeerPort(),
        msg);
#endif
    ZJC_DEBUG("BackupPrecommit success.");
    return kConsensusSuccess;
}

int BftManager::LeaderCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
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
    auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    res_msg_ptr->thread_idx = msg_ptr->thread_idx;
    auto res = BftProto::LeaderCreateCommit(security_ptr_, bft_ptr, false, msg);
    if (!res) {
        ZJC_ERROR("leader create commit message failed!");
        return kConsensusError;
    }

    bft_ptr->set_consensus_status(kConsensusCommited);
    LeaderBroadcastToAcc(bft_ptr, true);
    ZJC_ERROR("LeaderCallCommitOppose gid: %s", common::Encode::HexEncode(bft_ptr->gid()).c_str());
#ifdef ZJC_UNITTEST
    leader_commit_msg_ = res_msg_ptr;
#else
    network::Route::Instance()->Send(res_msg_ptr);
#endif
    return kConsensusSuccess;
}

void BftManager::HandleLocalCommitBlock(int32_t thread_idx, ZbftPtr& bft_ptr) {
    auto& zjc_block = bft_ptr->prpare_block();
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
    account_mgr_->AddBlockItemToCache(
        thread_idx,
        queue_item_ptr->block_ptr,
        queue_item_ptr->db_batch);
    block_mgr_->ConsensusAddBlock(thread_idx, queue_item_ptr);
    bft_ptr->set_consensus_status(kConsensusCommited);
    ZJC_DEBUG("add new block network: %d, height: %lu", zjc_block->network_id(), zjc_block->height());
    assert(bft_ptr->prpare_block()->precommit_bitmap_size() == zjc_block->precommit_bitmap_size());
}

int BftManager::LeaderCallCommit(
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr) {
    // check pre-commit multi sign and leader commit
    auto leader_msg_ptr = std::make_shared<transport::TransportMessage>();
    leader_msg_ptr->thread_idx = msg_ptr->thread_idx;
    transport::protobuf::Header& msg = leader_msg_ptr->header;
    auto res = BftProto::LeaderCreateCommit(security_ptr_, bft_ptr, true, msg);
    if (!res) {
        ZJC_ERROR("leader create commit message failed!");
        return kConsensusError;
    }

    if (bft_ptr->local_prepare_hash() == bft_ptr->leader_tbft_prepare_hash()) {
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
    }
    
#ifdef ZJC_UNITTEST
    leader_commit_msg_ = leader_msg_ptr;
#else
    network::Route::Instance()->Send(leader_msg_ptr);
#endif
    ZJC_DEBUG("LeaderCommit success waiting pool_index: %u, bft gid: %s",
        bft_ptr->pool_index(), common::Encode::HexEncode(bft_ptr->gid()).c_str());
    RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), true);
    return kConsensusSuccess;
}

int BftManager::BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    if (!bft_msg.agree()) {
        ZJC_ERROR("BackupCommit LeaderCallCommitOppose gid: %s",
            common::Encode::HexEncode(bft_ptr->gid()).c_str());
        return kConsensusSuccess;
    }
    
    if (bft_ptr->precommit_hash().empty()) {
        return kConsensusError;
    }

    if (VerifyBlsAggSignature(bft_ptr, bft_msg, bft_ptr->precommit_hash()) != kConsensusSuccess) {
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
    if (bft_ptr->local_prepare_hash() == bft_msg.prepare_hash()) {
        HandleLocalCommitBlock(msg_ptr->thread_idx, bft_ptr);
    } else {
        // sync block from neighbor nodes
        auto& tx_bft = bft_msg.tx_bft();
        // if (bft_ptr->pool_index() == common::kImmutablePoolSize) {
        //     sync::KeyValueSync::Instance()->AddSyncHeight(
        //         network::kRootCongressNetworkId,
        //         bft_ptr->pool_index(),
        //         tx_bft.ltx_commit().latest_hegight(),
        //         sync::kSyncHighest);
        // } else {
        //     sync::KeyValueSync::Instance()->AddSyncHeight(
        //         bft_ptr->network_id(),
        //         bft_ptr->pool_index(),
        //         tx_bft.ltx_commit().latest_hegight(),
        //         sync::kSyncHighest);
        // }
    }

    // start new bft
    RemoveBft(bft_ptr->thread_index(), bft_ptr->gid(), false);
    ZJC_DEBUG("BackupCommit success waiting pool_index: %u, bft gid: %s",
        bft_ptr->pool_index(), common::Encode::HexEncode(bft_ptr->gid()).c_str());
    return kConsensusSuccess;
}

void BftManager::LeaderBroadcastToAcc(ZbftPtr& bft_ptr, bool is_bft_leader) {
    // broadcast to this consensus shard and waiting pool shard
    const std::shared_ptr<block::protobuf::Block>& block_ptr = bft_ptr->prpare_block();
    // consensus pool sync by pull in bft step commit
    //
    // waiting pool sync by push
    {
        auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = res_msg_ptr->header;
        res_msg_ptr->thread_idx = -1;
        auto res = BftProto::CreateLeaderBroadcastToAccount(
            common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset,
            common::kConsensusMessage,
            kConsensusSyncBlock,
            false,
            block_ptr,
            bft_ptr->local_member_index(),
            msg);
        if (res) {
            network::Route::Instance()->Send(res_msg_ptr);
        }
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        if (block_ptr->tx_list_size() == 1 &&
                block_ptr->tx_list(0).step() == common::kConsensusFinalStatistic) {
            return;
        }

        auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = res_msg_ptr->header;
        res_msg_ptr->thread_idx = -1;
        auto res = BftProto::CreateLeaderBroadcastToAccount(
            network::kNodeNetworkId,
            common::kConsensusMessage,
            kConsensusRootBlock,
            true,
            block_ptr,
            bft_ptr->local_member_index(),
            msg);
        if (res) {
            network::Route::Instance()->Send(res_msg_ptr);
        }

        return;
    }

    std::set<uint32_t> broadcast_nets;
    auto tx_list = block_ptr->tx_list();
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() == kConsensusSuccess &&
                tx_list[i].step() == common::kConsensusFinalStatistic) {
            broadcast_nets.insert(network::kRootCongressNetworkId);
            continue;
        }

        // contract must unlock caller
        if (tx_list[i].status() != kConsensusSuccess &&
                (tx_list[i].step() != common::kConsensusCreateContract &&
                tx_list[i].step() != common::kConsensusCallContract)) {
            continue;
        }

        // if (tx_list[i].has_to() && !tx_list[i].to_add() &&
        //         tx_list[i].step() != common::kConsensusCallContract &&
        //         tx_list[i].step() != common::kConsensusCreateContract) {
            // auto account_ptr = block::AccountManager::Instance()->GetAcountInfo(
            //     tx_list[i].to());
            // uint32_t network_id = network::kRootCongressNetworkId;
            // if (account_ptr != nullptr) {
            //     account_ptr->GetConsensuseNetId(&network_id);
            // }

        //     broadcast_nets.insert(network_id);
        // }

        // if (tx_list[i].step() == common::kConsensusCallContract ||
        //         tx_list[i].step() == common::kConsensusCreateContract) {
        //     std::string id = "";
        //     if (tx_list[i].step() == contract::kCallStepCallerInited) {
        //         id = tx_list[i].to();
        //     } else if (tx_list[i].step() == contract::kCallStepContractCalled) {
        //         id = tx_list[i].from();
        //     } else if (tx_list[i].step() == contract::kCallStepContractFinal) {
        //         if (IsCreateContractLibraray(tx_list[i])) {
        //             for (uint32_t i = 0;
        //                     i < common::GlobalInfo::Instance()->consensus_shard_count(); ++i) {
        //                 if ((network::kConsensusShardBeginNetworkId + i) !=
        //                         common::GlobalInfo::Instance()->network_id()) {
        //                     broadcast_nets.insert(network::kConsensusShardBeginNetworkId + i);
        //                 }
        //             }
        //         }

        //         continue;
        //     } else {
        //         continue;
        //     }
             
        //     if (id.empty()) {
        //         continue;
        //     }

        //     auto account_ptr = block::AccountManager::Instance()->GetAcountInfo(id);
        //     uint32_t network_id = network::kRootCongressNetworkId;
        //     if (account_ptr != nullptr) {
        //         account_ptr->GetConsensuseNetId(&network_id);
        //     }

        //     broadcast_nets.insert(network_id);
        //     ZJC_DEBUG("block message broadcast to network: %d, gid: %s",
        //         network_id, common::Encode::HexEncode(tx_list[i].gid()).c_str());
        // }
    }

    for (auto iter = broadcast_nets.begin(); iter != broadcast_nets.end(); ++iter) {
        auto res_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = res_msg_ptr->header;
        res_msg_ptr->thread_idx = -1;
        auto res = BftProto::CreateLeaderBroadcastToAccount(
            *iter,
            common::kConsensusMessage,
            kConsensusToTxInit,
            false,
            block_ptr,
            bft_ptr->local_member_index(),
            msg);
        if (res) {
            network::Route::Instance()->Send(res_msg_ptr);
        }
    }
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
    auto now_timestamp_milli = common::TimeUtils::TimestampMs();
    if (prev_checktime_out_milli_ > now_timestamp_milli) {
        return;
    }

    prev_checktime_out_milli_ = now_timestamp_milli + kCheckTimeoutPeriodMilli;
    std::vector<ZbftPtr> timeout_vec;
    auto& bft_hash_map = bft_hash_map_[thread_idx];
    auto iter = bft_hash_map.begin();
    while (iter != bft_hash_map.end()) {
        int timeout_res = iter->second->CheckTimeout();
        if (timeout_res == kTimeout) {
            ZJC_DEBUG("timeout remove bft gid: %s", common::Encode::HexEncode(iter->first).c_str());
            bft_hash_map.erase(iter++);
            continue;
        }

        if (timeout_res == kTimeoutCallPrecommit) {
            iter->second->AddBftEpoch();
            LeaderCallPrecommit(iter->second);
        }

        ++iter;
    }
}

int BftManager::VerifyBlsAggSignature(
        ZbftPtr& bft_ptr,
        const hotstuff::protobuf::HotstuffMessage& bft_msg,
        const std::string& sign_hash) {
    libff::alt_bn128_G1 sign;
    try {
        sign.X = libff::alt_bn128_Fq(bft_msg.bls_sign_x().c_str());
        sign.Y = libff::alt_bn128_Fq(bft_msg.bls_sign_y().c_str());
        sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        ZJC_ERROR("get invalid bls sign.");
        return kConsensusError;
    }

    uint32_t t = common::GetSignerCount(bft_ptr->members_ptr()->size());
    uint32_t n = bft_ptr->members_ptr()->size();
    if (bls_mgr_->Verify(
            t,
            n,
            elect_mgr_->GetCommonPublicKey(
            bft_ptr->elect_height(),
            bft_ptr->network_id()),
            sign,
            sign_hash) != bls::kBlsSuccess) {
        ZJC_ERROR("VerifyBlsAggSignature agg sign failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

}  // namespace consensus

}  // namespace zjchain
