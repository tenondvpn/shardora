#include "consensus/zbft/zbft.h"

#include "block/account_manager.h"
#include "consensus/zbft/waiting_txs_pools.h"
#include "contract/contract_utils.h"
#include "elect/elect_manager.h"
#include "timeblock/time_block_manager.h"

namespace zjchain {

namespace consensus {

Zbft::~Zbft() {
}

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

int Zbft::ChangeLeader(
        int32_t leader_idx,
        int32_t leader_count,
        uint64_t elect_height,
        const common::MembersPtr& members_ptr,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& local_sec_key) {
    if (members_ptr == nullptr) {
        ZJC_ERROR("elected memmbers is null;");
        return kConsensusError;
    }

    if (leader_idx >= (int32_t)members_ptr->size()) {
        ZJC_ERROR("leader_idx >= (int32_t)members_ptr->size(), %u, %u",
            leader_idx, members_ptr->size());
        return kConsensusError;
    }

    elect_height_ = elect_height;
    leader_mem_ptr_ = (*members_ptr)[leader_idx];
    if ((pool_index() % leader_count) != (uint32_t)leader_mem_ptr_->pool_index_mod_num) {
        ZJC_ERROR("pool_index() leader_count != (uint32_t)leader_mem_ptr_->pool_index_mod_num: %u, %u",
            (pool_index() % leader_count), leader_mem_ptr_->pool_index_mod_num);
        return kConsensusError;
    }

    leader_index_ = leader_idx;
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

    if (leader_mem_ptr_->id == security_ptr_->GetAddress()) {
        this_node_is_leader_ = true;
    }

    if (prepare_block_ != nullptr) {
        auto& zjc_block = *prepare_block_;
        zjc_block.set_timestamp(common::TimeUtils::TimestampMs());
        if (txs_ptr_->tx_type != pools::protobuf::kNormalFrom) {
            zjc_block.set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
        }

        zjc_block.set_electblock_height(elect_height_);
        assert(elect_height_ >= 1);
        zjc_block.set_leader_index(leader_index_);
        zjc_block.set_hash(GetBlockHash(zjc_block));
    }

    reset_timeout();
    return kConsensusSuccess;
}

int Zbft::Init(
        int32_t leader_idx,
        int32_t leader_count,
        uint64_t elect_height,
        const common::MembersPtr& members_ptr,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& local_sec_key) {
    if (members_ptr == nullptr) {
        ZJC_ERROR("elected memmbers is null;");
        return kConsensusError;
    }

    if (leader_idx >= (int32_t)members_ptr->size()) {
        ZJC_ERROR("leader_idx >= (int32_t)members_ptr->size(), %u, %u",
            leader_idx, members_ptr->size());
        return kConsensusError;
    }

    elect_height_ = elect_height;
    leader_mem_ptr_ = (*members_ptr)[leader_idx];
    if ((pool_index() % leader_count) != (uint32_t)leader_mem_ptr_->pool_index_mod_num) {
        ZJC_ERROR("pool_index() leader_count != (uint32_t)leader_mem_ptr_->pool_index_mod_num: %u, %u",
            (pool_index() % leader_count), leader_mem_ptr_->pool_index_mod_num);
        return kConsensusError;
    }

    leader_index_ = leader_idx;
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

    if (leader_mem_ptr_->id == security_ptr_->GetAddress()) {
        this_node_is_leader_ = true;
    }

    db_batch_ = std::make_shared<db::DbWriteBatch>();
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

    if (txs_ptr_->txs.empty()) {
        ZJC_ERROR("pool index invalid[%d], tx empty!", pool_index());
        return kConsensusInvalidPackage;
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
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    if (LeaderCallTransaction(bft_msg) != kConsensusSuccess) {
        return kConsensusError;
    }
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);

    zbft::protobuf::TxBft& tx_bft = *bft_msg->mutable_tx_bft();
    auto& tx_map = txs_ptr_->txs;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        tx_bft.add_tx_hash_list(iter->first);
    }
    
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
    bft_msg->mutable_tx_bft()->release_block();
    return kConsensusSuccess;
}

int Zbft::BackupCheckPrepare(
        zbft::protobuf::ZbftMessage* bft_msg,
        int32_t* invalid_tx_idx) {
    auto& tx_bft = *bft_msg->mutable_tx_bft();
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    if (DoTransaction(tx_bft) != kConsensusSuccess) {
        return kConsensusInvalidPackage;
    }

    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
    tx_bft.release_block();
    //assert(!tx_bft.has_block());
    return kConsensusSuccess;
}

bool Zbft::BackupCheckLeaderValid(const zbft::protobuf::ZbftMessage* bft_msg) {
//     if (members_ptr_ == nullptr ||
//             common_pk_ == libff::alt_bn128_G2::zero() ||
//             local_sec_key_ == libff::alt_bn128_Fr::zero()) {
//         if (members != nullptr) {
//             ZJC_ERROR("get members failed!. bft_msg.member_index(): %d, members->size(): %d, "
//                 "common_pk_ == libff::alt_bn128_G2::zero(), local_sec_key_ == libff::alt_bn128_Fr::zero()",
//                 bft_msg->member_index(), members->size(),
//                 (common_pk_ == libff::alt_bn128_G2::zero()),
//                 (local_sec_key_ == libff::alt_bn128_Fr::zero()));
//         } else {
//             ZJC_ERROR("get members failed!: %lu, net id: %d",
//                 local_elect_height,
//                 common::GlobalInfo::Instance()->network_id());
//         }
//         return false;
//     }
// 
//     for (uint32_t i = 0; i < members->size(); ++i) {
//         if ((*members)[i]->id == security_ptr_->GetAddress()) {
//             local_member_index_ = i;
//             break;
//         }
//     }
// 
//     if (local_member_index_ == elect::kInvalidMemberIndex) {
//         ZJC_ERROR("get local member failed!.");
//         return false;
//     }
// 
//     leader_mem_ptr_ = (*members)[bft_msg->member_index()];
//     if (!leader_mem_ptr_) {
//         ZJC_ERROR("get leader failed!.");
//         return false;
//     }

//     ZJC_DEBUG("backup check leader success elect height: %lu, local_member_index_: %lu, gid: %s",
//         elect_height_, local_member_index_, common::Encode::HexEncode(gid_).c_str());
    return true;
}

int Zbft::LeaderPrecommitOk(
        const zbft::protobuf::TxBft& tx_prepare,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
//     ZJC_DEBUG("leader precommit hash: %s, index: %d, gid: %s",
//         common::Encode::HexEncode(tx_prepare.prepare_final_hash()).c_str(),
//         index,
//         common::Encode::HexEncode(gid_).c_str());
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
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
    // times_[times_index_++] = common::TimeUtils::TimestampUs();
    //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
    if ((uint32_t)valid_count >= min_aggree_member_count_) {
        if (LeaderPrecommitAggSign(tx_prepare.prepare_final_hash()) != kConsensusSuccess) {
            ZJC_ERROR("create bls precommit agg sign failed!");
            return kConsensusOppose;
        }

        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
        leader_handled_precommit_ = true;
        return kConsensusAgree;
    }

    if (PrepareHashNotConsensus()) {
        ZJC_ERROR("prepare hash not consensus failed!");
        return kConsensusOppose;
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

void Zbft::RechallengePrecommitClear() {
    leader_handled_commit_ = false;
    init_precommit_timeout();
    precommit_bitmap_.clear();
    commit_aggree_set_.clear();
//     precommit_aggree_set_.clear();
    precommit_oppose_set_.clear();
    commit_oppose_set_.clear();
}

int Zbft::LeaderPrecommitAggSign(const std::string& prpare_hash) {
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

        //assert(iter->second->backup_precommit_signs_[i] != libff::alt_bn128_G1::zero());
        all_signs.push_back(iter->second->backup_precommit_signs_[i]);
        idx_vec.push_back(i + 1);
        if (idx_vec.size() >= t) {
            break;
        }
    }

    try {
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);

        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);

        std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
        libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);

        bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        bls_precommit_agg_sign_->to_affine_coordinates();
        prepare_block_->set_bls_agg_sign_x(
            common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->X)));
        prepare_block_->set_bls_agg_sign_y(
            common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->Y)));
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
//         if (times_[times_index_ - 1] - times_[times_index_ - 2] >= 10000) {
//             ZJC_DEBUG("SignatureRecover use time %lu us", (times_[times_index_ - 1] - times_[times_index_ - 2]));
//         }

        // times_[times_index_ - 2] = times_[times_index_ - 1];
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);


        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
        std::string sign_precommit_hash;
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
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
            //assert(false);
            return kConsensusError;
        }

        // times_[times_index_++] = common::TimeUtils::TimestampUs();
//         if (times_[times_index_ - 1] - times_[times_index_ - 2] > 10000) {
//             ZJC_DEBUG("get verify hash use time: %lu", (times_[times_index_ - 1] - times_[times_index_ - 2]));
//         }
        // times_[times_index_ - 2] = times_[times_index_ - 1];
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);

        if (sign_precommit_hash != precommit_bls_agg_verify_hash_) {
            common_pk_.to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(common_pk_);
            auto cpk_strs = cpk->toString();
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, "
                "network id: %u, prepare hash: %s, pk hash: %s, sign hash: %s",
                t, n, cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height_, network_id_, common::Encode::HexEncode(prpare_hash).c_str(),
                common::Encode::HexEncode(sign_precommit_hash).c_str(),
                common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str());
            //assert(false);
            return kConsensusError;
        }

        ZJC_DEBUG("leader precommit agg sign success! signx: %s, %s: %s, %s",
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->X).c_str(),
            common::Encode::HexEncode(sign_precommit_hash).c_str(),
            common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str(),
            common::Encode::HexEncode(prepare_hash_).c_str());

        // times_[times_index_++] = common::TimeUtils::TimestampUs();
//         if (times_[times_index_ - 1] - times_[times_index_ - 2] > 10000) {
//             ZJC_DEBUG("bls_precommit_agg_sign_->to_affine_coordinates use time: %lu", (times_[times_index_ - 1] - times_[times_index_ - 2]));
//         }
        // times_[times_index_ - 2] = times_[times_index_ - 1];
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
        prepare_bitmap_ = iter->second->prepare_bitmap_;
        prepare_bitmap_.inversion(n);
        assert(prepare_bitmap_.valid_count() == member_count_ - min_aggree_member_count_);
        auto& bitmap_data = prepare_bitmap_.data();
        prepare_block_->clear_precommit_bitmap();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            prepare_block_->add_precommit_bitmap(bitmap_data[i]);
        }

        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
        set_consensus_status(kConsensusPreCommit);
        valid_index_ = iter->second->valid_index;
    } catch (std::exception& e) {
        ZJC_ERROR("catch bls exception: %s", e.what());
        return kConsensusError;
    }

    return kConsensusSuccess;
}

void Zbft::CreatePrecommitVerifyHash() {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
//     ZJC_DEBUG("precommit get pk verify hash begin.");
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            g1_prepare_hash_,
            common_pk_,
            &precommit_bls_agg_verify_hash_) != bls::kBlsSuccess) {
        ZJC_ERROR("get precommit hash failed!");
    }
//     ZJC_DEBUG("precommit get pk verify hash end: %s, hash: %s",
//         common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str(),
//         common::Encode::HexEncode(prepare_hash_).c_str());
}

void Zbft::CreateCommitVerifyHash() {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            g1_precommit_hash_,
            common_pk_,
            &commit_bls_agg_verify_hash_) != bls::kBlsSuccess) {
        ZJC_ERROR("get commit hash failed!");
    }
}

void Zbft::AfterNetwork() {
    if (prepare_block_ == nullptr) {
        return;
    }

    ZJC_DEBUG("AfterNetwork consensus_status_: %d, gid: %s, hash: %s",
        consensus_status_,
        common::Encode::HexEncode(gid_).c_str(),
        common::Encode::HexEncode(prepare_block_->hash()).c_str());
    if (consensus_status_ == kConsensusPrepare) {
        CreatePrecommitVerifyHash();
    }

    if (consensus_status_ == kConsensusPreCommit) {
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
        std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
        libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
//         ZJC_INFO("commit verify start,");
        std::string sign_commit_hash;
        if (bls_mgr_->GetVerifyHash(
                t,
                n,
                *bls_commit_agg_sign_,
                &sign_commit_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("verify leader precommit agg sign failed!");
            return kConsensusError;
        }

        if (prepare_block_->is_cross_block()) {
            if (sign_commit_hash != commit_bls_agg_verify_hash_) {
                ZJC_ERROR("leader verify leader commit agg sign failed!");
                assert(!commit_bls_agg_verify_hash_.empty());
                return kConsensusError;
            }
        } else {
            if (sign_commit_hash != precommit_bls_agg_verify_hash_) {
                ZJC_ERROR("leader verify leader commit agg sign failed!");
                assert(!precommit_bls_agg_verify_hash_.empty());
                return kConsensusError;
            }
        }

//         ZJC_INFO("commit verify end,");
        bls_commit_agg_sign_->to_affine_coordinates();
        prepare_block_->set_bls_agg_sign_x(
            common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(bls_commit_agg_sign_->X)));
        prepare_block_->set_bls_agg_sign_y(
            common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(bls_commit_agg_sign_->Y)));
        set_consensus_status(kConsensusCommit);
        ZJC_DEBUG("leader agg sign success! signx: %s, %s: %s, %s",
            common::Encode::HexEncode(prepare_block_->bls_agg_sign_x()).c_str(),
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
            common::Encode::HexEncode(precommit_hash_).c_str());
    } catch (...) {
        return kConsensusError;
    }

    return kConsensusSuccess;
}

bool Zbft::set_bls_precommit_agg_sign(
        const libff::alt_bn128_G1& agg_sign,
        const std::string& sign_hash) {
    if (prepare_block_ == nullptr) {
        return false;
    }

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

    bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(agg_sign);
    if (sign_commit_hash != sign_hash) {
        ZJC_ERROR("backup verify leader precommit agg sign failed! %s: %s",
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(sign_hash).c_str());
        return false;
    }

    bls_precommit_agg_sign_->to_affine_coordinates();
    prepare_block_->set_bls_agg_sign_x(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->X)));
    prepare_block_->set_bls_agg_sign_y(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->Y)));
    ZJC_DEBUG("precommit success set bls sign, hash: %s, sign x: %s, sign y: %s",
        common::Encode::HexEncode(sign_hash).c_str(),
        common::Encode::HexEncode(prepare_block_->bls_agg_sign_x()).c_str(),
        common::Encode::HexEncode(prepare_block_->bls_agg_sign_y()).c_str());
    set_consensus_status(kConsensusPreCommit);
    return true;
}

bool Zbft::verify_bls_precommit_agg_sign(
        const libff::alt_bn128_G1& agg_sign,
        const std::string& sign_hash) {
    if (prepare_block_ == nullptr) {
        return false;
    }

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

    bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(agg_sign);
    if (sign_commit_hash != sign_hash) {
        ZJC_ERROR("backup verify leader precommit agg sign failed! %s: %s",
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(sign_hash).c_str());
        return false;
    }

    return true;
}

bool Zbft::set_bls_commit_agg_sign(const libff::alt_bn128_G1& agg_sign) {
    if (prepare_block_ == nullptr) {
        assert(false);
        return false;
    }

    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::string sign_commit_hash;
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            agg_sign,
            &sign_commit_hash) != bls::kBlsSuccess) {
        ZJC_ERROR("verify leader commit agg sign failed!");
        return false;
    }


    if (prepare_block_->is_cross_block()) {
        if (sign_commit_hash != commit_bls_agg_verify_hash_) {
            ZJC_ERROR("backup verify leader precommit agg sign failed! signx: %s, %s: %s, %s",
                libBLS::ThresholdUtils::fieldElementToString(agg_sign.X).c_str(),
                common::Encode::HexEncode(sign_commit_hash).c_str(),
                common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
                common::Encode::HexEncode(precommit_hash_).c_str());
            assert(!commit_bls_agg_verify_hash_.empty());
            return false;
        }
    } else {
        if (sign_commit_hash != precommit_bls_agg_verify_hash_) {
            ZJC_ERROR("backup verify leader precommit agg sign failed! signx: %s, %s: %s, %s",
                libBLS::ThresholdUtils::fieldElementToString(agg_sign.X).c_str(),
                common::Encode::HexEncode(sign_commit_hash).c_str(),
                common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str(),
                common::Encode::HexEncode(prepare_hash_).c_str());
            assert(!precommit_bls_agg_verify_hash_.empty());
            return false;
        }
    }
    
    auto tmp_sign = agg_sign;
    tmp_sign.to_affine_coordinates();
    prepare_block_->set_bls_agg_sign_x(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(tmp_sign.X)));
    prepare_block_->set_bls_agg_sign_y(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(tmp_sign.Y)));
    ZJC_DEBUG("commit success set bls sign, hash: %s, sign x: %s, sign y: %s",
        common::Encode::HexEncode(prepare_block_->hash()).c_str(),
        common::Encode::HexEncode(prepare_block_->bls_agg_sign_x()).c_str(),
        common::Encode::HexEncode(prepare_block_->bls_agg_sign_y()).c_str());
    set_consensus_status(kConsensusCommit);
    return true;
}

int Zbft::LeaderCallTransaction(zbft::protobuf::ZbftMessage* bft_msg) {
    auto& res_tx_bft = *bft_msg->mutable_tx_bft();
    if (DoTransaction(res_tx_bft) != kConsensusSuccess) {
        ZJC_ERROR("leader do transaction failed!");
        return kConsensusError;
    }

    libff::alt_bn128_G1 bn_sign;
    if (bls_mgr_->Sign(
            min_aggree_member_count(),
            member_count(),
            local_sec_key(),
            g1_prepare_hash_,
            &bn_sign) != bls::kBlsSuccess) {
        ZJC_ERROR("leader do transaction sign data failed!");
        return kConsensusError;
    }

    if (LeaderPrecommitOk(
            res_tx_bft,
            leader_index_,
            bn_sign,
            leader_mem_ptr_->id) != bls::kBlsSuccess) {
        ZJC_ERROR("leader call LeaderPrecommitOk failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

int Zbft::DoTransaction(zbft::protobuf::TxBft& tx_bft) {
    std::string pool_hash = pools_mgr_->latest_hash(txs_ptr_->pool_index);
    uint64_t pool_height = pools_mgr_->latest_height(txs_ptr_->pool_index);
    if (pool_hash.empty() || pool_height == common::kInvalidUint64) {
        ZJC_DEBUG("pool index not inited: %u", txs_ptr_->pool_index);
        return kConsensusError;
    }

    auto prepare_block = std::make_shared<block::protobuf::Block>(*(tx_bft.mutable_block()));
    block::protobuf::Block& zjc_block = *prepare_block;
    zjc_block.set_pool_index(txs_ptr_->pool_index);
    zjc_block.set_prehash(pool_hash);
    if (pipeline_prev_zbft_ptr_ != nullptr &&
            pipeline_prev_zbft_ptr_->pool_index() == pool_index()) {
        zjc_block.set_prehash(pipeline_prev_zbft_ptr_->prepare_block()->hash());
    } else if (!leader_pre_hash_.empty()) {
        zjc_block.set_prehash(leader_pre_hash_);
    }

    zjc_block.set_version(common::kTransactionVersion);
    zjc_block.set_network_id(common::GlobalInfo::Instance()->network_id());
    zjc_block.set_consistency_random(0);
    zjc_block.set_height(pool_height + 1);
    if (pipeline_prev_zbft_ptr_ != nullptr &&
            pipeline_prev_zbft_ptr_->pool_index() == pool_index()) {
        zjc_block.set_height(pipeline_prev_zbft_ptr_->prepare_block()->height() + 1);
    } else if (leader_pre_height_ != 0) {
        zjc_block.set_height(leader_pre_height_ + 1);
    }

    assert(zjc_block.height() > 0);
//     ZJC_DEBUG("add new block: %lu", zjc_block.height());
    zjc_block.set_timestamp(common::TimeUtils::TimestampMs());
    if (txs_ptr_->tx_type != pools::protobuf::kNormalFrom) {
        zjc_block.set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    }

    zjc_block.set_electblock_height(elect_height_);
    assert(elect_height_ >= 1);
    zjc_block.set_leader_index(leader_index_);
    DoTransactionAndCreateTxBlock(zjc_block);
    if (zjc_block.tx_list_size() <= 0) {
        ZJC_ERROR("all choose tx invalid!");
        return kConsensusNoNewTxs;
    }

    if (pipeline_prev_zbft_ptr_ != nullptr) {
        zjc_block.set_commit_pool_index(pipeline_prev_zbft_ptr_->prepare_block()->pool_index());
        zjc_block.set_commit_height(pipeline_prev_zbft_ptr_->prepare_block()->height());
    }

    if (txs_ptr_->tx_type != pools::protobuf::kNormalFrom ||
            txs_ptr_->pool_index == common::kRootChainPoolIndex) {
        zjc_block.set_is_cross_block(true);
    }

    zjc_block.set_hash(GetBlockHash(zjc_block));
    tx_bft.set_prepare_final_hash(zjc_block.hash());
    tx_bft.set_height(zjc_block.height());
    tx_bft.set_tx_type(txs_ptr_->tx_type);
    ZJC_DEBUG("has prepool: %d, has preheight: %d, prepool index: %d,"
        "pre height: %lu, pool index: %d, height: %lu, prehash: %s, hash: %s",
        zjc_block.has_commit_pool_index(),
        zjc_block.has_commit_height(),
        zjc_block.commit_pool_index(),
        zjc_block.commit_height(),
        pool_index(),
        zjc_block.height(),
        common::Encode::HexEncode(zjc_block.prehash()).c_str(),
        common::Encode::HexEncode(zjc_block.hash()).c_str());
    set_prepare_hash(zjc_block.hash());
    prepare_block_ = prepare_block;
    return kConsensusSuccess;
}

void Zbft::DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx_map = txs_ptr_->txs;
    tx_list->Reserve(tx_map.size());
    std::unordered_map<std::string, int64_t> acc_balance_map;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = zjc_block.height();
    zjc_host.tx_context_.block_timestamp = zjc_block.timestamp() / 1000;
    uint64_t chanin_id = (((uint64_t)zjc_block.network_id()) << 32 | (uint64_t)zjc_block.pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.thread_idx_ = txs_ptr_->thread_index;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) { 
        auto& tx_info = iter->second->msg_ptr->header.tx_proto();
        auto& block_tx = *tx_list->Add();
        int res = iter->second->TxToBlockTx(tx_info, db_batch_, &block_tx);
        if (res != kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        if (block_tx.step() == pools::protobuf::kContractExcute) {
            block_tx.set_from(security_ptr_->GetAddress(
                iter->second->msg_ptr->header.tx_proto().pubkey()));
        } else {
            block_tx.set_from(iter->second->msg_ptr->address_info->addr());
        }

        block_tx.set_status(kConsensusSuccess);
        int do_tx_res = iter->second->HandleTx(
            txs_ptr_->thread_index,
            zjc_block,
            db_batch_,
            zjc_host,
            acc_balance_map,
            block_tx);
        if (do_tx_res != kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }
    }
}

};  // namespace consensus

};  // namespace zjchain