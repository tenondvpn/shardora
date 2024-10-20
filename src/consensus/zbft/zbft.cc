#include "consensus/zbft/zbft.h"

#include "bls/bls_sign.h"
#include "block/account_manager.h"
#include "consensus/zbft/waiting_txs_pools.h"
#include "contract/contract_utils.h"
#include "elect/elect_manager.h"
#include "timeblock/time_block_manager.h"
#include "common/encode.h"
#include <common/log.h>

namespace shardora {

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

    pool_index_mod_num_ = leader_mem_ptr_->pool_index_mod_num;
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
    assert(leader_mem_ptr_ != nullptr);

    uint64_t pool_height = pools_mgr_->latest_height(txs_ptr_->pool_index);
    uint64_t preblock_time = pools_mgr_->latest_timestamp(txs_ptr_->pool_index);
    if (pool_height == common::kInvalidUint64) {
        ZJC_DEBUG("pool index not inited: %u", txs_ptr_->pool_index);
        return kConsensusError;
    }
    
    block_new_height_ = pool_height + 1;
    if (this_node_is_leader_) {
        uint64_t cur_time = common::TimeUtils::TimestampMs();
        if (preblock_time < cur_time) 
            block_new_timestamp_ = cur_time; 
        else
            block_new_timestamp_ = preblock_time;
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

int Zbft::Prepare(bool leader) {
    if (leader) {
        return LeaderCreatePrepare(leader);
    }

    consensus_prepare_tm_ms_ = common::TimeUtils::TimestampMs();
    if (txs_ptr_->txs.empty()) {
        ZJC_ERROR("pool index invalid[%d], tx empty!", pool_index());
        return kConsensusInvalidPackage;
    }

    if (pool_index() >= common::kInvalidPoolIndex) {
        ZJC_ERROR("pool index invalid[%d]!", pool_index());
        return kConsensusInvalidPackage;
    }

    int32_t invalid_tx_idx = -1;
    int res = BackupCheckPrepare(&invalid_tx_idx, leader);
    if (res != kConsensusSuccess) {
        ZJC_ERROR("backup prepare failed: %d", res);
        return res;
    }

    return kConsensusSuccess;
}

int Zbft::LeaderCreatePrepare(bool leader) {
    local_member_index_ = leader_index_;
    if (LeaderCallTransaction(leader) != kConsensusSuccess) {
        // assert(false);
        ZJC_ERROR("not all tx valid!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

int Zbft::BackupCheckPrepare(int32_t* invalid_tx_idx, bool leader) {
    if (DoTransaction(leader) != kConsensusSuccess) {
        return kConsensusInvalidPackage;
    }

    return kConsensusSuccess;
}

int Zbft::LeaderPrecommitOk(
        const std::string& prepare_hash,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
    assert(prepare_hash.size() == 32);
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
        prepare_hash,
        backup_sign);
    if ((uint32_t)valid_count >= min_aggree_member_count_) {
        int32_t res = kConsensusAgree;
        if (prepare_block_->hash() != prepare_hash) {
            prepare_block_ = nullptr;
            leader_waiting_prepare_hash_ = prepare_hash;
            set_prepare_hash(leader_waiting_prepare_hash_);
            CreatePrecommitVerifyHash();
            res =  kConsensusLeaderWaitingBlock;
            set_consensus_status(kConsensusLeaderWaitingBlock);
        } else {
            set_consensus_status(kConsensusPreCommit);
        }
        
        if (LeaderPrecommitAggSign(prepare_hash) != kConsensusSuccess) {
            ZJC_ERROR("create bls precommit agg sign failed!");
            return kConsensusOppose;
        }
        // times_[times_index_++] = common::TimeUtils::TimestampUs();
        //assert(times_[times_index_ - 1] - times_[times_index_ - 2] <= 10000);
        leader_handled_precommit_ = true;
        return res;
    }

    if (PrepareHashNotConsensus()) {
        ZJC_ERROR("prepare hash not consensus failed: %s", common::Encode::HexEncode(gid()).c_str());
//         assert(false);
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
    ZJC_DEBUG("commit_aggree_set_.size(), min_aggree_member_count_: %d, %d, id: %s, gid: %s",
        commit_aggree_set_.size(), min_aggree_member_count_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(gid()).c_str());
    if (commit_aggree_set_.size() >= min_aggree_member_count_) {
        leader_handled_commit_ = true;
        ZJC_DEBUG("gid precommit agg sign: %s", common::Encode::HexEncode(gid()).c_str());
        if (LeaderCreateCommitAggSign() != kConsensusSuccess) {
            ZJC_ERROR("leader create commit agg sign failed: %s",
                common::Encode::HexEncode(gid()).c_str());
            assert(false);
            return kConsensusOppose;
        }

        ZJC_DEBUG("gid aggree precommit agg sign: %s", common::Encode::HexEncode(gid()).c_str());
        return kConsensusAgree;
    }

    return kConsensusWaitingBackup;
}

void Zbft::RechallengePrecommitClear() {
    leader_handled_commit_ = false;
    precommit_bitmap_.clear();
    commit_aggree_set_.clear();
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
#if MOCK_SIGN        
        bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::random_element());
#else
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
        libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);        
        bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        bls_precommit_agg_sign_->to_affine_coordinates();
#endif
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
            //assert(false);
            return kConsensusError;
        }

#if MOCK_VERIFY
        sign_precommit_hash = precommit_bls_agg_verify_hash_;
#endif        

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
            common::Encode::HexEncode(gid()).c_str());
        if (prepare_block_ != nullptr) {
            LeaderResetPrepareBitmap(iter->second);
        }

        valid_index_ = iter->second->valid_index;
    } catch (std::exception& e) {
        ZJC_ERROR("catch bls exception: %s", e.what());
        return kConsensusError;
    }

    return kConsensusSuccess;
}

void Zbft::LeaderResetPrepareBitmap(const std::string& prepare_hash) {
    if (prepare_block_ == nullptr || bls_precommit_agg_sign_ == nullptr) {
        ZJC_DEBUG("set prepare bitmap failed: %d, %d",
            (prepare_block_ == nullptr), (bls_precommit_agg_sign_ == nullptr));
        return;
    }

    auto iter = prepare_block_map_.find(prepare_hash);
    if (iter == prepare_block_map_.end()) {
        return;
    }

    LeaderResetPrepareBitmap(iter->second);
}

void Zbft::LeaderResetPrepareBitmap(std::shared_ptr<LeaderPrepareItem>& prepare_item) {
    if (prepare_block_ == nullptr || bls_precommit_agg_sign_ == nullptr) {
        ZJC_DEBUG("set prepare bitmap failed: %d, %d",
            (prepare_block_ == nullptr), (bls_precommit_agg_sign_ == nullptr));
        return;
    }

    prepare_block_->set_bls_agg_sign_x(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->X)));
    prepare_block_->set_bls_agg_sign_y(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(bls_precommit_agg_sign_->Y)));
}

void Zbft::CreatePrecommitVerifyHash() {
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    if (bls_mgr_->GetVerifyHash(
            t,
            n,
            g1_prepare_hash_,
            common_pk_,
            &precommit_bls_agg_verify_hash_) != bls::kBlsSuccess) {
        ZJC_ERROR("get precommit hash failed!");
    }

    ZJC_DEBUG("block hash prepare hash: %s, g1 prepare: %s, gid: %s",
        common::Encode::HexEncode(prepare_hash_).c_str(),
        common::Encode::HexEncode(precommit_bls_agg_verify_hash_).c_str(),
        common::Encode::HexEncode(gid()).c_str());
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

    ZJC_DEBUG("block hash precommit hash: %s, g1 precommit: %s, gid: %s",
        common::Encode::HexEncode(prepare_block_->hash()).c_str(),
        common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
        common::Encode::HexEncode(gid()).c_str());
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

int BlsVerify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& pubkey,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        std::string* verify_hash) try {
    if (pubkey == libff::alt_bn128_G2::zero()) {
        auto sign_ptr = const_cast<libff::alt_bn128_G1*>(&sign);
        sign_ptr->to_affine_coordinates();
        auto sign_x = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->X);
        auto sign_y = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->Y);
        ZJC_ERROR("public key error: zero,msg sign x: %s, sign y: %s",
            sign_x.c_str(),
            sign_y.c_str());
        return kConsensusError;
    }

    auto bn_sign = sign;
    bn_sign.to_affine_coordinates();
    auto sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
    auto sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);
    ZJC_DEBUG("verify t: %u, n: %u, sign x: %s, sign y: %s, sign msg: %s,%s,%s",
        t, n, (sign_x).c_str(), (sign_y).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str());
    if (bls::BlsSign::Verify(t, n, sign, g1_hash, pubkey, verify_hash) == bls::kBlsSuccess) {
        return kConsensusSuccess;
    }

    return kConsensusError;
} catch (std::exception& e) {
    ZJC_ERROR("catch error: %s", e.what());
    return kConsensusError;
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
#if MOCK_SIGN
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::random_element()); 
#else
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
        libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
#endif
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

#if MOCK_VERIFY
        sign_commit_hash = commit_bls_agg_verify_hash_;
#endif

        if (prepare_block_->is_commited_block()) {
            if (sign_commit_hash != commit_bls_agg_verify_hash_) {
                for (uint32_t i = 0; i < bit_size; ++i) {
                    if (!precommit_bitmap_.Valid(i)) {
                        continue;
                    }

                    std::string tmp_hash;
                    ZJC_DEBUG("now check bls sign: %u, %s, gid: %s",
                        i, common::Encode::HexEncode((*members_ptr_)[i]->id).c_str(),
                        common::Encode::HexEncode(gid()).c_str());
                    BLS_DEBUG("now check bls sign sign t: %u, n: %u, sign x: %s, sign y: %s, sign msg: %s,%s,%s",
                        t, n,
                        libBLS::ThresholdUtils::fieldElementToString(backup_commit_signs_[i].X).c_str(),
                        libBLS::ThresholdUtils::fieldElementToString(backup_commit_signs_[i].Y).c_str(),
                        libBLS::ThresholdUtils::fieldElementToString(g1_precommit_hash_.X).c_str(),
                        libBLS::ThresholdUtils::fieldElementToString(g1_precommit_hash_.Y).c_str(),
                        libBLS::ThresholdUtils::fieldElementToString(g1_precommit_hash_.Z).c_str());
                    assert(BlsVerify(
                        t,
                        n,
                        (*members_ptr_)[i]->bls_publick_key,
                        backup_commit_signs_[i],
                        g1_precommit_hash_,
                        &tmp_hash) == kConsensusSuccess);
                }

                ZJC_ERROR("leader verify leader commit agg sign failed: %s, %s, gid: %s, t: %u, n: %u",
                    common::Encode::HexEncode(sign_commit_hash).c_str(),
                    common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
                    common::Encode::HexEncode(gid()).c_str(), t, n);
                assert(!commit_bls_agg_verify_hash_.empty());
                return kConsensusError;
            }
        } else {
            assert(false);
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
        ZJC_DEBUG("leader agg sign success! signx: %s, %s: %s, %s, gid: %s",
            common::Encode::HexEncode(prepare_block_->bls_agg_sign_x()).c_str(),
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
            common::Encode::HexEncode(prepare_block_->hash()).c_str(),
            common::Encode::HexEncode(gid()).c_str());
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

#if MOCK_VERIFY
    sign_commit_hash = sign_hash;
#endif    

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

#if MOCK_VERIFY
    sign_commit_hash = sign_hash;
#endif

    if (sign_commit_hash != sign_hash) {
        ZJC_DEBUG("backup verify leader precommit agg sign failed! %s: %s, "
            "prepare hash: %s, gid: %s",
            common::Encode::HexEncode(sign_commit_hash).c_str(),
            common::Encode::HexEncode(sign_hash).c_str(),
            common::Encode::HexEncode(prepare_hash_).c_str(),
            common::Encode::HexEncode(gid()).c_str());
        return false;
    }

    bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(agg_sign);
    return true;
}

bool Zbft::set_bls_commit_agg_sign(const libff::alt_bn128_G1& agg_sign) {
    if (prepare_block_ == nullptr) {
        ZJC_ERROR("prepare_block_ == nullptr: gid: %s",
            common::Encode::HexEncode(gid()).c_str());
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

#if MOCK_VERIFY
    sign_commit_hash = commit_bls_agg_verify_hash_;
#endif    

    if (prepare_block_->is_commited_block()) {
        if (sign_commit_hash != commit_bls_agg_verify_hash_) {
            ZJC_ERROR("backup verify leader precommit agg sign failed! signx: %s, %s: %s, %s",
                libBLS::ThresholdUtils::fieldElementToString(agg_sign.X).c_str(),
                common::Encode::HexEncode(sign_commit_hash).c_str(),
                common::Encode::HexEncode(commit_bls_agg_verify_hash_).c_str(),
                common::Encode::HexEncode(prepare_block_->hash()).c_str());
            assert(!commit_bls_agg_verify_hash_.empty());
            return false;
        }
    } else {
        assert(false);
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

int Zbft::LeaderCallTransaction(bool leader) {
    if (DoTransaction(leader) != kConsensusSuccess) {
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

    auto res = LeaderPrecommitOk(
        prepare_hash_,
        leader_index_,
        bn_sign,
        leader_mem_ptr_->id);
    if (res != kConsensusWaitingBackup) {
        ZJC_ERROR("leader call LeaderPrecommitOk failed!");
        return kConsensusError;
    }

    return kConsensusSuccess;
}

int Zbft::DoTransaction(bool leader) {
    std::string pool_hash = pools_mgr_->latest_hash(txs_ptr_->pool_index);
    uint64_t pool_height = pools_mgr_->latest_height(txs_ptr_->pool_index);
    uint64_t preblock_time = pools_mgr_->latest_timestamp(txs_ptr_->pool_index);
    if (pool_hash.empty() || pool_height == common::kInvalidUint64) {
        ZJC_DEBUG("pool index not inited: %u", txs_ptr_->pool_index);
        return kConsensusError;
    }

    auto prepare_block = std::make_shared<block::protobuf::Block>();
    block::protobuf::Block& zjc_block = *prepare_block;
    zjc_block.set_pool_index(txs_ptr_->pool_index);
    zjc_block.set_prehash(pool_hash);
    zjc_block.set_version(common::kTransactionVersion);
    zjc_block.set_network_id(common::GlobalInfo::Instance()->network_id());
    zjc_block.set_consistency_random(0);
    zjc_block.set_height(pool_height + 1);
    assert(zjc_block.height() > 0);
//     ZJC_DEBUG("add new block: %lu", zjc_block.height());
    if (leader) {
        zjc_block.set_timestamp(block_new_timestamp_); 
    } else {
        // todo backup节点使用leader节点时间
        uint64_t cur_time = common::TimeUtils::TimestampMs();
        if (leader_timestamp_ < preblock_time || (cur_time > leader_timestamp_ ? cur_time - leader_timestamp_ > 10000 : leader_timestamp_ - cur_time > 10000)) { // leader节点的时间戳必须大于上一个区块时间戳；leader节点的时间戳跟当前时间戳绝对值差值小于10s
            ZJC_ERROR("timestamp is error. cur_time = %lu, preblock_time = %lu, leader_timestamp = %lu ", cur_time, preblock_time, leader_timestamp_);
            return kConsensusError;
        }

        zjc_block.set_timestamp(leader_timestamp_);
    }
    
    // if (txs_ptr_->tx_type != pools::protobuf::kNormalFrom) {
        assert(tm_block_mgr_->LatestTimestampHeight() > 0);
        zjc_block.set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    // } else {
    //     assert(false);
    // }

    zjc_block.set_electblock_height(elect_height_);
    assert(elect_height_ >= 1);
    zjc_block.set_leader_index(leader_index_);
    DoTransactionAndCreateTxBlock(zjc_block);
    if (zjc_block.tx_list_size() <= 0) {
        ZJC_ERROR("all choose tx invalid!");
        return kConsensusNoNewTxs;
    }

    zjc_block.set_is_commited_block(false);
    // 没用 change_leader_invalid_hashs，永远是空
    std::vector<std::string> change_leader_invalid_hashs;
    pools_mgr_->GetHeightInvalidChangeLeaderHashs(
        zjc_block.pool_index(),
        zjc_block.height(),
        change_leader_invalid_hashs);
    for (uint32_t i = 0; i < change_leader_invalid_hashs.size(); ++i) {
        zjc_block.add_change_leader_invalid_hashs(change_leader_invalid_hashs[i]);
    }

    // 重新计算 hash 值
    zjc_block.set_hash(GetBlockHash(zjc_block));
    ZJC_DEBUG("pool index: %d, height: %lu, prehash: %s, hash: %s",
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
	// acc_balance_map 没有用到？
    std::unordered_map<std::string, int64_t> acc_balance_map;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = zjc_block.height();
    zjc_host.tx_context_.block_timestamp = zjc_block.timestamp() / 1000;
    uint64_t chanin_id = (((uint64_t)zjc_block.network_id()) << 32 | (uint64_t)zjc_block.pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) { 
        auto& tx_info = iter->second->tx_info;
        auto& block_tx = *tx_list->Add();
        int res = iter->second->TxToBlockTx(tx_info, db_batch_, &block_tx);
        if (res != kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        if (block_tx.step() == pools::protobuf::kContractExcute) {
            block_tx.set_from(security_ptr_->GetAddress(
                iter->second->tx_info.pubkey()));
        } else {
            block_tx.set_from(iter->second->address_info->addr());
        }

        block_tx.set_status(kConsensusSuccess);

        ZJC_DEBUG("tx_item type: %d ", ProtobufToJson(iter->second->tx_info));
        int do_tx_res = iter->second->HandleTx(
            zjc_block,
            db_batch_,
            zjc_host,
            acc_balance_map,
            block_tx);
        if (do_tx_res != kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        for (auto event_iter = zjc_host.recorded_logs_.begin();
                event_iter != zjc_host.recorded_logs_.end(); ++event_iter) {
            auto log = block_tx.add_events();
            log->set_data((*event_iter).data);
            for (auto topic_iter = (*event_iter).topics.begin();
                    topic_iter != (*event_iter).topics.end(); ++topic_iter) {
                log->add_topics(std::string((char*)(*topic_iter).bytes, sizeof((*topic_iter).bytes)));
            }
        }

        zjc_host.recorded_logs_.clear();
    }
}

};  // namespace consensus

};  // namespace shardora
