#include "consensus/zbft/zbft.h"

#include "block/account_manager.h"
#include "consensus/zbft/waiting_txs_pools.h"
#include "contract/contract_utils.h"
#include "elect/elect_manager.h"
#include "timeblock/time_block_manager.h"

namespace zjchain {

namespace consensus {

Zbft::~Zbft() {}

Zbft::Zbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr)
        : account_mgr_(account_mgr),
        security_ptr_(security_ptr),
        bls_mgr_(bls_mgr),
        txs_ptr_(txs_ptr),
        pools_mgr_(pools_mgr),
        tm_block_mgr_(tm_block_mgr) {
    reset_timeout();
}

int Zbft::Init(
        uint64_t elect_height,
        common::BftMemberPtr& leader_mem_ptr,
        common::MembersPtr& members_ptr,
        libff::alt_bn128_G2& common_pk,
        libff::alt_bn128_Fr& local_sec_key) {
    if (members_ptr == nullptr) {
        ZJC_ERROR("elected memmbers is null;");
        return kConsensusError;
    }

    elect_height_ = elect_height;
    if (leader_mem_ptr != nullptr) {
        leader_mem_ptr_ = leader_mem_ptr;
        if (leader_mem_ptr_->pool_index_mod_num < 0) {
            ZJC_ERROR("leader: %s mem ptr pool_index_mod_num: %d, error",
                common::Encode::HexEncode(leader_mem_ptr_->id).c_str(),
                leader_mem_ptr_->pool_index_mod_num);
            return kConsensusError;
        }

        leader_index_ = leader_mem_ptr_->index;
    }

    members_ptr_ = members_ptr;
    common_pk_ = common_pk;
    local_sec_key_ = local_sec_key;
    if (leader_index_ >= members_ptr_->size() ||
            common_pk_ == libff::alt_bn128_G2::zero() ||
            local_sec_key_ == libff::alt_bn128_Fr::zero()) {
        ZJC_ERROR("leader_index_: %d, size: %d, common_pk_: %d, local_sec_key_: %d",
            leader_index_,
            members_ptr_->size(),
            (common_pk_ == libff::alt_bn128_G2::zero()),
            (local_sec_key_ == libff::alt_bn128_Fr::zero()));
        return kConsensusError;
    }

    // just leader call init
    if (leader_mem_ptr != nullptr && leader_mem_ptr->id == security_ptr_->GetAddress()) {
        this_node_is_leader_ = true;
    }

    return kConsensusSuccess;
}

void Zbft::Destroy() {
    if (txs_ptr_ == nullptr) {
        return;
    }

    if (consensus_status_ != kConsensusCommited) {
        auto ptr = shared_from_this();
        pools_mgr_->TxRecover(ptr);
    } else {
        auto ptr = shared_from_this();
        pools_mgr_->TxOver(ptr);
    }
}

int Zbft::Prepare(bool leader, zbft::protobuf::ZbftMessage* bft_msg) {
    if (leader) {
        return LeaderCreatePrepare(bft_msg);
    }

    if (pool_index() >= common::kInvalidPoolIndex) {
        ZJC_ERROR("pool index invalid[%d]!", pool_index());
        return kConsensusInvalidPackage;
    }

    int32_t invalid_tx_idx = -1;
    int res = BackupCheckPrepare(bft_msg, &invalid_tx_idx);
    if (res != kConsensusSuccess) {
        ZJC_ERROR("backup prepare failed: %d", res);
        return res;
    }

    return kConsensusSuccess;
}

int Zbft::LeaderCreatePrepare(zbft::protobuf::ZbftMessage* bft_msg) {
    local_member_index_ = leader_index_;
    LeaderCallTransaction(bft_msg);
    zbft::protobuf::TxBft& tx_bft = *bft_msg->mutable_tx_bft();
    auto ltxp = tx_bft.mutable_ltx_prepare();
    if (txs_ptr_->bloom_filter == nullptr) {
        auto& tx_map = txs_ptr_->txs;
        for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
            ltxp->add_tx_hash_list(iter->first);
        }
    } else {
        auto bloom_datas = txs_ptr_->bloom_filter->data();
        for (auto iter = bloom_datas.begin(); iter != bloom_datas.end(); ++iter) {
            ltxp->add_bloom_filter(*iter);
        }
    }

    bft_msg->mutable_tx_bft()->mutable_ltx_prepare()->release_block();
    return kConsensusSuccess;
}

int Zbft::BackupCheckPrepare(
        zbft::protobuf::ZbftMessage* bft_msg,
        int32_t* invalid_tx_idx) {
    auto& tx_bft = *bft_msg->mutable_tx_bft();
    auto ltx_msg = tx_bft.mutable_ltx_prepare();
    if (DoTransaction(*ltx_msg) != kConsensusSuccess) {
        return kConsensusInvalidPackage;
    }

    ltx_msg->release_block();
    assert(!ltx_msg->has_block());
    return kConsensusSuccess;
}

int Zbft::InitZjcTvmContext() {
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host_.tx_context_.tx_gas_price,
        common::GlobalInfo::Instance()->gas_price());
    zjc_host_.tx_context_.tx_origin = evmc::address{};
    zjc_host_.tx_context_.block_coinbase = evmc::address{};
    zjc_host_.tx_context_.block_number = pools_mgr_->latest_height(txs_ptr_->pool_index) + 1;
    zjc_host_.tx_context_.block_timestamp = common::TimeUtils::TimestampSeconds();
    zjc_host_.tx_context_.block_gas_limit = 0;
    uint64_t chanin_id = (((uint64_t)common::GlobalInfo::Instance()->network_id()) << 32 |
        (uint64_t)pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host_.tx_context_.chain_id,
        chanin_id);
    return kConsensusSuccess;
}

bool Zbft::BackupCheckLeaderValid(const zbft::protobuf::ZbftMessage* bft_msg) {
    auto local_elect_height = elect_height_;
    auto members = members_ptr_;
    if (members == nullptr ||
            common_pk_ == libff::alt_bn128_G2::zero() ||
            local_sec_key_ == libff::alt_bn128_Fr::zero()) {
        if (members != nullptr) {
            ZJC_ERROR("get members failed!. bft_msg.member_index(): %d, members->size(): %d, "
                "common_pk_ == libff::alt_bn128_G2::zero(), local_sec_key_ == libff::alt_bn128_Fr::zero()",
                bft_msg->member_index(), members->size(),
                (common_pk_ == libff::alt_bn128_G2::zero()),
                (local_sec_key_ == libff::alt_bn128_Fr::zero()));
        } else {
            ZJC_ERROR("get members failed!: %lu, net id: %d", local_elect_height, common::GlobalInfo::Instance()->network_id());
        }
        return false;
    }

    for (uint32_t i = 0; i < members->size(); ++i) {
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            local_member_index_ = i;
            break;
        }
    }

    if (local_member_index_ == elect::kInvalidMemberIndex) {
        ZJC_ERROR("get local member failed!.");
        return false;
    }

    leader_mem_ptr_ = (*members)[bft_msg->member_index()];
    if (!leader_mem_ptr_) {
        ZJC_ERROR("get leader failed!.");
        return false;
    }

    elect_height_ = local_elect_height;
    members_ptr_ = members;
//     ZJC_DEBUG("backup check leader success elect height: %lu, local_member_index_: %lu, gid: %s",
//         elect_height_, local_member_index_, common::Encode::HexEncode(gid_).c_str());
    return true;
}

int Zbft::LeaderPrecommitOk(
        const zbft::protobuf::LeaderTxPrepare& tx_prepare,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
    if (leader_handled_precommit_) {
//         ZJC_DEBUG("leader_handled_precommit_: %d", leader_handled_precommit_);
        return kConsensusHandled;
    }

    // TODO: check back hash eqal to it's signed hash
    auto valid_count = SetPrepareBlock(
        id,
        index,
        tx_prepare.prepare_final_hash(),
        tx_prepare.height(),
        backup_sign);
    if ((uint32_t)valid_count >= min_aggree_member_count_) {
        if (LeaderCreatePreCommitAggChallenge(
                tx_prepare.prepare_final_hash()) != kConsensusSuccess) {
            ZJC_ERROR("create bls precommit agg sign failed!");
            return kConsensusOppose;
        }

        leader_handled_precommit_ = true;
        return kConsensusAgree;
    }

    return kConsensusWaitingBackup;
}

int Zbft::LeaderCommitOk(
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
    if (leader_handled_commit_) {
//         ZJC_DEBUG("leader_handled_commit_");
        return kConsensusHandled;
    }
//     if (!prepare_bitmap_.Valid(index)) {
//         ZJC_DEBUG("index invalid: %d", index);
//         return kConsensusWaitingBackup;
//     }

//     auto mem_ptr = elect::ElectManager::Instance()->GetMember(network_id_, index);
    commit_aggree_set_.insert(id);
    precommit_bitmap_.Set(index);
    backup_commit_signs_[index] = backup_sign;
//     ZJC_DEBUG("commit_aggree_set_.size() >= min_aggree_member_count_: %d, %d",
//         commit_aggree_set_.size(), min_aggree_member_count_);
    if (commit_aggree_set_.size() >= min_aggree_member_count_) {
        leader_handled_commit_ = true;
        if (LeaderCreateCommitAggSign() != kConsensusSuccess) {
            ZJC_ERROR("leader create commit agg sign failed!");
            return kConsensusOppose;
        }

        return kConsensusAgree;
    }

    return kConsensusWaitingBackup;
}

int Zbft::CheckTimeout() {
    auto now_timestamp = std::chrono::steady_clock::now();
    if (timeout_ <= now_timestamp) {
//         ZJC_DEBUG("%lu, %lu, Timeout %s,",
//             timeout_.time_since_epoch().count(),
//             now_timestamp.time_since_epoch().count(),
//             common::Encode::HexEncode(gid()).c_str());
        return kTimeout;
    }

    if (!this_node_is_leader_) {
        return kConsensusSuccess;
    }

    if (!leader_handled_precommit_) {
//         if (precommit_aggree_set_.size() >= min_prepare_member_count_ ||
//                 (precommit_aggree_set_.size() >= min_aggree_member_count_ &&
//                 now_timestamp >= prepare_timeout_)) {
//             LeaderCreatePreCommitAggChallenge("");
//             leader_handled_precommit_ = true;
//             ZJC_ERROR("kTimeoutCallPrecommit %s,", common::Encode::HexEncode(gid()).c_str());
//             return kTimeoutCallPrecommit;
//        
        return kTimeoutWaitingBackup;
    }

    if (!leader_handled_commit_) {
        if (now_timestamp >= precommit_timeout_) {
            if (precommit_bitmap_.valid_count() < min_aggree_member_count_) {
//                 ZJC_ERROR("precommit_bitmap_.valid_count() failed!");
                return kTimeoutWaitingBackup;
            }

            prepare_bitmap_ = precommit_bitmap_;
            LeaderCreatePreCommitAggChallenge("");
            RechallengePrecommitClear();
            ZJC_ERROR("kTimeoutCallReChallenge %s,", common::Encode::HexEncode(gid()).c_str());
            return kTimeoutCallReChallenge;
        }

        return kTimeoutWaitingBackup;
    }

    return kTimeoutNormal;
}

void Zbft::RechallengePrecommitClear() {
    leader_handled_commit_ = false;
    init_precommit_timeout();
    precommit_bitmap_.clear();
    commit_aggree_set_.clear();
//     precommit_aggree_set_.clear();
    precommit_oppose_set_.clear();
    commit_oppose_set_.clear();
}

int Zbft::LeaderCreatePreCommitAggChallenge(const std::string& prpare_hash) {
    auto iter = prepare_block_map_.find(prpare_hash);
    if (iter == prepare_block_map_.end()) {
        return kConsensusError;
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t bit_size = iter->second->prepare_bitmap_.data().size() * 64;
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < n; ++i) {
        if (!iter->second->prepare_bitmap_.Valid(i)) {
            continue;
        }

        assert(iter->second->backup_precommit_signs_[i] != libff::alt_bn128_G1::zero());
        all_signs.push_back(iter->second->backup_precommit_signs_[i]);
        idx_vec.push_back(i + 1);
        if (idx_vec.size() >= t) {
            break;
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        leader_tbft_prepare_hash_ = prpare_hash;
        std::string msg_hash_src;
        msg_hash_src.reserve(32 + 128);
        msg_hash_src.append(prpare_hash);
        for (uint32_t i = 0; i < iter->second->prepare_bitmap_.data().size(); ++i) {
            auto& data = iter->second->prepare_bitmap_.data()[i];
            msg_hash_src.append((char*)&data, sizeof(data));
        }

        set_precoimmit_hash(common::Hash::keccak256(msg_hash_src));
        ZJC_INFO("bls_mgr_->Verify start.");
        std::string sign_precommit_hash;
        if (bls_mgr_->GetVerifyHash(
                t,
                n,
                *bls_precommit_agg_sign_,
                &sign_precommit_hash) != bls::kBlsSuccess) {
            common_pk_.to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(common_pk_);
            auto cpk_strs = cpk->toString();
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, "
                "network id: %u, prepare hash: %s",
                t, n, cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height_, network_id_, common::Encode::HexEncode(prpare_hash).c_str());
            assert(false);
            return kConsensusError;
        }

        if (sign_precommit_hash != precommit_bls_agg_verify_hash_) {
            common_pk_.to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(common_pk_);
            auto cpk_strs = cpk->toString();
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, "
                "network id: %u, prepare hash: %s",
                t, n, cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height_, network_id_, common::Encode::HexEncode(prpare_hash).c_str());
            assert(false);
            return kConsensusError;
        }

        ZJC_INFO("bls_mgr_->Verify over.");
        bls_precommit_agg_sign_->to_affine_coordinates();
        prepare_bitmap_ = iter->second->prepare_bitmap_;
        uint64_t max_height = 0;
        uint64_t max_count = 0;
        for (auto hiter = iter->second->height_count_map.begin();
                hiter != iter->second->height_count_map.end(); ++hiter) {
            if (hiter->second > max_count) {
                max_height = hiter->first;
                max_count = hiter->second;
            }
        }
    } catch (std::exception& e) {
        ZJC_ERROR("catch bls exception: %s", e.what());
        return kConsensusError;
    }

    return kConsensusSuccess;
}

void Zbft::CreatePrecommitVerifyHash() {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    ZJC_DEBUG("precommit get pk verify hash begin.");
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            g1_prepare_hash_,
            common_pk_,
            &precommit_bls_agg_verify_hash_) != bls::kBlsSuccess) {
        ZJC_ERROR("get precommit hash failed!");
    }
    ZJC_DEBUG("precommit get pk verify hash end: %s, hash: %s",
        common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str(),
        common::Encode::HexEncode(prepare_hash_).c_str());
}

void Zbft::CreateCommitVerifyHash() {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    ZJC_DEBUG("commit get pk verify hash begin.");
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            g1_precommit_hash_,
            common_pk_,
            &commit_bls_agg_verify_hash_) != bls::kBlsSuccess) {
        ZJC_ERROR("get commit hash failed!");
    }
    ZJC_DEBUG("commit get pk verify hash end: %s, hash: %s",
        common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
        common::Encode::HexEncode(precommit_hash_).c_str());
}

void Zbft::AfterNetwork() {
    if (consensus_status_ == kConsensusPreCommit) {
        CreatePrecommitVerifyHash();
    }

    if (consensus_status_ == kConsensusCommit) {
        CreateCommitVerifyHash();
    }
}

int Zbft::LeaderCreateCommitAggSign() {
    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t bit_size = precommit_bitmap_.data().size() * 64;
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < bit_size; ++i) {
        if (!precommit_bitmap_.Valid(i)) {
            continue;
        }

        all_signs.push_back(backup_commit_signs_[i]);
        idx_vec.push_back(i + 1);
        if (idx_vec.size() >= min_aggree_member_count_) {
            break;
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        std::string msg_hash_src;
        msg_hash_src.reserve(32 + 128);
        msg_hash_src.append(precommit_hash());
        for (uint32_t i = 0; i < precommit_bitmap_.data().size(); ++i) {
            auto& data = precommit_bitmap_.data()[i];
            msg_hash_src.append((char*)&data, sizeof(data));// precommit_bitmap_.data()[i]);
        }

        commit_hash_ = common::Hash::Hash256(msg_hash_src);
        ZJC_INFO("commit verify start,");
        std::string sign_commit_hash;
        if (bls_mgr_->GetVerifyHash(
                t,
                n,
                *bls_commit_agg_sign_,
                &sign_commit_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("verify leader precommit agg sign failed!");
            return kConsensusError;
        }

        if (sign_commit_hash != commit_bls_agg_verify_hash_) {
            ZJC_ERROR("leader verify leader commit agg sign failed!");
            return kConsensusError;
        }

        ZJC_INFO("commit verify end,");
        bls_commit_agg_sign_->to_affine_coordinates();
    } catch (...) {
        return kConsensusError;
    }

    return kConsensusSuccess;
}

bool Zbft::set_bls_precommit_agg_sign(
        const libff::alt_bn128_G1& agg_sign,
        const std::string& sign_hash) {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::string sign_commit_hash;
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            agg_sign,
            &sign_commit_hash) != bls::kBlsSuccess) {
        ZJC_ERROR("verify leader precommit agg sign failed!");
        return false;
    }

    if (sign_commit_hash != sign_hash) {
        ZJC_ERROR("backup verify leader precommit agg sign failed! %s: %s",
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(sign_hash).c_str());
        return false;
    }

    bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(agg_sign);
    return true;
}

void Zbft::LeaderCallTransaction(zbft::protobuf::ZbftMessage* bft_msg) {
    auto& res_tx_bft = *bft_msg->mutable_tx_bft();
    auto ltx_msg = res_tx_bft.mutable_ltx_prepare();
    if (DoTransaction(*ltx_msg) != kConsensusSuccess) {
        ZJC_ERROR("leader do transaction failed!");
        return;
    }

    libff::alt_bn128_G1 bn_sign;
    if (bls_mgr_->Sign(
            min_aggree_member_count(),
            member_count(),
            local_sec_key(),
            g1_prepare_hash_,
            &bn_sign) != bls::kBlsSuccess) {
        ZJC_ERROR("leader do transaction sign data failed!");
        return;
    }

    if (LeaderPrecommitOk(
            *ltx_msg,
            leader_index_,
            bn_sign,
            leader_mem_ptr_->id) != bls::kBlsSuccess) {
        ZJC_ERROR("leader call LeaderPrecommitOk failed!");
        return;
    }
}

int Zbft::DoTransaction(zbft::protobuf::LeaderTxPrepare& ltx_prepare) {
    if (InitZjcTvmContext() != kConsensusSuccess) {
        return kConsensusError;
    }

    std::string pool_hash = pools_mgr_->latest_hash(txs_ptr_->pool_index);
    uint64_t pool_height = pools_mgr_->latest_height(txs_ptr_->pool_index);
    block::protobuf::Block& zjc_block = *(ltx_prepare.mutable_block());
    DoTransactionAndCreateTxBlock(zjc_block);
    if (zjc_block.tx_list_size() <= 0) {
        ZJC_ERROR("all choose tx invalid!");
        return kConsensusNoNewTxs;
    }

    zjc_block.set_pool_index(txs_ptr_->pool_index);
    zjc_block.set_prehash(pool_hash);
    zjc_block.set_version(common::kTransactionVersion);
    zjc_block.set_network_id(common::GlobalInfo::Instance()->network_id());
    zjc_block.set_consistency_random(0);
    zjc_block.set_height(pool_height + 1);
    zjc_block.set_timestamp(common::TimeUtils::TimestampMs());
    zjc_block.set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    zjc_block.set_electblock_height(elect_height_);
    zjc_block.set_leader_index(leader_index_);
    zjc_block.set_hash(GetBlockHash(zjc_block));
    prpare_block_ = std::make_shared<block::protobuf::Block>(zjc_block);
    ltx_prepare.set_prepare_final_hash(zjc_block.hash());
    ltx_prepare.set_height(zjc_block.height());
    ltx_prepare.set_tx_type(txs_ptr_->tx_type);
    set_prepare_hash(zjc_block.hash());
    return kConsensusSuccess;
}

void Zbft::DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx_map = txs_ptr_->txs;
    std::unordered_map<std::string, int64_t> acc_balance_map;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx_info = iter->second->msg_ptr->header.tx_proto();
        auto& block_tx = *tx_list->Add();
        int res = iter->second->TxToBlockTx(tx_info, &block_tx);
        if (res != kConsensusSuccess) {
            continue;
        }

        block_tx.set_status(kConsensusSuccess);
        int do_tx_res = iter->second->HandleTx(
            txs_ptr_->thread_index, acc_balance_map, block_tx);
        if (do_tx_res != kConsensusSuccess) {
            continue;
        }
    }
}

};  // namespace consensus

};  // namespace zjchain