#include <bls/bls_utils.h>
#include <common/defer.h>
#include <common/log.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <exception>

namespace shardora {

namespace hotstuff {

Status Crypto::PartialSign(
        uint32_t sharding_id, 
        uint64_t elect_height, 
        const HashStr& msg_hash, 
        std::string* sign_x, 
        std::string* sign_y) {
    auto elect_item = GetElectItem(sharding_id, elect_height);
    if (!elect_item) {
        assert(false);
        return Status::kError;
    }
    
    if (elect_item->local_sk() == libff::alt_bn128_Fr::zero()) {
        // assert(false);
        return Status::kError;
    }

    if (elect_item->LocalMember()->bls_publick_key == libff::alt_bn128_G2::zero()) {
        // assert(false);
        return Status::kError;
    }

    libff::alt_bn128_G1 g1_hash;
    GetG1Hash(msg_hash, &g1_hash);
    auto ret = bls_mgr_->Sign(
            elect_item->t(),
            elect_item->n(),
            elect_item->local_sk(),
            g1_hash,
            sign_x,
            sign_y);
    if (ret != bls::kBlsSuccess) {
        assert(false);
        return Status::kError;
    }

// #ifndef NDEBUG
//     auto member_bls_pk = libBLS::ThresholdUtils::fieldElementToString(
//             elect_item->LocalMember()->bls_publick_key.X.c0);
//     ZJC_DEBUG("bls parial sign t: %u, n: %u, member index: %u"
//         "bls pk: %s, sign x: %s, y: %s, hash: %s, elect height: %lu",
//         elect_item->t(),
//         elect_item->n(),
//         elect_item->LocalMember()->index,
//         member_bls_pk.c_str(),
//         sign_x->c_str(),
//         sign_y->c_str(),
//         common::Encode::HexEncode(msg_hash).c_str(),
//         elect_height);
// #endif
    return Status::kSuccess;
}

Status Crypto::ReconstructAndVerifyThresSign(
        const transport::MessagePtr& msg_ptr,
        uint64_t elect_height,
        View view,
        const HashStr& msg_hash,
        uint32_t index,
        const std::string& partial_sign_x,
        const std::string& partial_sign_y,
        std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign) try {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = GetElectItem(common::GlobalInfo::Instance()->network_id(), elect_height);
    if (!elect_item) {
        ZJC_DEBUG("get elect item failed bls_collection_ && bls_collection_->view > view: %lu, %lu, "
            "index: %u, pool_idx_: %d", 
            vote_view_, view, index, pool_idx_);
        return Status::kError;
    }

    if ((*elect_item->Members())[index]->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_DEBUG("bls public key failed bls_collection_ && bls_collection_->view > view: %lu, %lu, "
            "index: %u, pool_idx_: %d", 
            vote_view_, view, index, pool_idx_);
        assert(false);
        return Status::kError;
    }

    if (view > vote_view_) {
        vote_view_ = view;
        hash_with_vote_index_.clear();
    }

    auto map_iter = hash_with_vote_index_.find(msg_hash);
    if (map_iter != hash_with_vote_index_.end()) {
        if (map_iter->second.find(index) != map_iter->second.end()) {
            return Status::kBlsHandled;
        }
    } else {
        hash_with_vote_index_[msg_hash] = std::map<uint32_t, std::shared_ptr<libff::alt_bn128_G1>>();
        map_iter = hash_with_vote_index_.find(msg_hash);
    }

    if (map_iter->second.size() >= elect_item->t()) {
        return Status::kBlsHandled;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto partial_sign = std::make_shared<libff::alt_bn128_G1>();
    try {
        partial_sign->X = libff::alt_bn128_Fq(partial_sign_x.c_str());
        partial_sign->Y = libff::alt_bn128_Fq(partial_sign_y.c_str());
        partial_sign->Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        assert(false);
        return Status::kError;
    }

    // Reconstruct sign
    // TODO(HT): 先判断是否已经处理过的index
    map_iter->second[index] = partial_sign;
    ZJC_DEBUG("msg hash: %s, ok count: %u, t: %u, index: %u, elect_height: %lu, pool: %u",
        common::Encode::HexEncode(msg_hash).c_str(), 
        map_iter->second.size(), 
        elect_item->t(),
        index,
        elect_height,
        pool_idx_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (map_iter->second.size() < elect_item->t()) {
        return Status::kBlsVerifyWaiting;
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec;
    for (auto index_iter = map_iter->second.begin(); index_iter != map_iter->second.end(); ++index_iter) {
        all_signs.push_back(*index_iter->second);
        idx_vec.push_back(index_iter->first + 1);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::vector<libff::alt_bn128_Fr> lagrange_coeffs(elect_item->t());
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, elect_item->t(), lagrange_coeffs);
#ifdef HOTSTUFF_TEST
    reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
    reconstructed_sign->to_affine_coordinates();   
#else
    libBLS::Bls bls_instance = libBLS::Bls(elect_item->t(), elect_item->n());
    reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(all_signs, lagrange_coeffs));
    reconstructed_sign->to_affine_coordinates();
#endif
    ADD_DEBUG_PROCESS_TIMESTAMP();
    Status s = VerifyThresSign(
        common::GlobalInfo::Instance()->network_id(), 
        elect_height, 
        msg_hash, 
        *reconstructed_sign);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        // TODO: check each partial sign
        ZJC_ERROR("verify thresh sign failed!");
        assert(false);
        return s;
    }

// #ifndef NDEBUG
//     auto val = libBLS::ThresholdUtils::fieldElementToString(
//         elect_item->common_pk().X.c0);
//     auto agg_sign_str = libBLS::ThresholdUtils::fieldElementToString(
//         reconstructed_sign->X);
//     ZJC_DEBUG("success construct agg msg_hash: %s, net: %u, pool: %u, "
//             "elect height: %lu, common PK: %s, agg sign: %s", 
//             common::Encode::HexEncode(msg_hash).c_str(),
//             common::GlobalInfo::Instance()->network_id(), 
//             pool_idx_,
//             elect_height,
//             val.c_str(),
//             agg_sign_str.c_str());
// #endif
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return s;
} catch (std::exception& e) {
    ZJC_ERROR("crypto verify exception %s", e.what());
    return Status::kBlsVerifyWaiting;
};

Status Crypto::VerifyThresSign(
        uint32_t sharding_id, 
        uint64_t elect_height, 
        const HashStr &msg_hash,
        const libff::alt_bn128_G1& reconstructed_sign) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        ZJC_DEBUG("sharding_id: %d VerifyThresSign duration: %lu ms", sharding_id, e-b);
    });

#ifdef HOTSTUFF_TEST
    return Status::kSuccess;
#endif
    if (reconstructed_sign == libff::alt_bn128_G1::zero()) {
        ZJC_DEBUG("reconstructed_sign == nullptr");
        assert(false);
        return Status::kBlsVerifyFailed;
    }    
    std::string verify_hash_a;
    std::string verify_hash_b;
    Status s = GetVerifyHashA(sharding_id, elect_height, msg_hash, &verify_hash_a);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("GetVerifyHashA faile net: %u, pool: %u, height: %lu, hash: %s",
            sharding_id, sharding_id, elect_height,
            common::Encode::HexEncode(msg_hash).c_str());
        // assert(false);
        return s;
    }

    s = GetVerifyHashB(sharding_id, elect_height, reconstructed_sign, &verify_hash_b);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("GetVerifyHashB failed!");
        assert(false);
        return s;
    }

    if (verify_hash_a != verify_hash_b) {
        auto elect_item = GetElectItem(sharding_id, elect_height);
        auto val = libBLS::ThresholdUtils::fieldElementToString(
            elect_item->common_pk().X.c0);
        auto agg_sign_str = libBLS::ThresholdUtils::fieldElementToString(
            reconstructed_sign.X);
        ZJC_DEBUG("verify_hash_a != verify_hash_b %s, %s, msg_hash: %s, "
            "net: %u, pool: %u, elect height: %lu, common PK: %s, agg sign: %s", 
            common::Encode::HexEncode(verify_hash_a).c_str(),
            common::Encode::HexEncode(verify_hash_b).c_str(),
            common::Encode::HexEncode(msg_hash).c_str(),
            sharding_id, 
            pool_idx_,
            elect_height,
            val.c_str(),
            agg_sign_str.c_str());
        assert(false);
        return Status::kBlsVerifyFailed;
    }

// #ifndef NDEBUG
//     auto elect_item = GetElectItem(sharding_id, elect_height);
//     auto val = libBLS::ThresholdUtils::fieldElementToString(
//         elect_item->common_pk().X.c0);
//     auto agg_sign_str = libBLS::ThresholdUtils::fieldElementToString(
//         reconstructed_sign.X);
//     ZJC_DEBUG("success verify agg sign %s, %s, msg_hash: %s, net: %u, pool: %u, "
//             "elect height: %lu, common PK: %s, agg sign: %s", 
//             common::Encode::HexEncode(verify_hash_a).c_str(),
//             common::Encode::HexEncode(verify_hash_b).c_str(),
//             common::Encode::HexEncode(msg_hash).c_str(),
//             sharding_id, 
//             pool_idx_,
//             elect_height,
//             val.c_str(),
//             agg_sign_str.c_str());
// #endif
    return Status::kSuccess;
}

Status Crypto::VerifyQC(
        uint32_t sharding_id,
        const QC& qc) {
    auto msg_hash = GetQCMsgHash(qc);
    libff::alt_bn128_G1 bls_sign;
    try {
        bls_sign.X = libff::alt_bn128_Fq(qc.sign_x().c_str());
        bls_sign.Y = libff::alt_bn128_Fq(qc.sign_y().c_str());
        bls_sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        assert(false);
        return Status::kError;
    }

    Status s = VerifyThresSign(
        sharding_id, 
        qc.elect_height(), 
        msg_hash, 
        bls_sign);
    if (s != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error sharding id: %u, elect height: %lu, msg hash: %s",
            sharding_id, qc.elect_height(), common::Encode::HexEncode(msg_hash).c_str());
        return s;
    }
    
    return Status::kSuccess;
}

Status Crypto::VerifyTC(
        uint32_t sharding_id,
        const TC& tc) {
    libff::alt_bn128_G1 bls_sign;
    try {
        bls_sign.X = libff::alt_bn128_Fq(tc.sign_x().c_str());
        bls_sign.Y = libff::alt_bn128_Fq(tc.sign_y().c_str());
        bls_sign.Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        assert(false);
        return Status::kError;
    }

    auto msg_hash = GetTCMsgHash(tc);
    Status s = VerifyThresSign(
        sharding_id, 
        tc.elect_height(), 
        msg_hash, 
        bls_sign);
    if (s != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error sharding id: %u, elect height: %lu, msg hash: %s",
            sharding_id, tc.elect_height(), common::Encode::HexEncode(msg_hash).c_str());
        return s;
    }
    
    return Status::kSuccess;
}

Status Crypto::SignMessage(transport::MessagePtr& msg_ptr) {
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string sign;
    if (security() && security()->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return Status::kError;
    }
    
    msg_ptr->header.set_sign(sign);
    return Status::kSuccess;
}

Status Crypto::VerifyMessage(const transport::MessagePtr& msg_ptr) {
    if (!msg_ptr->header.has_sign()) {
        return Status::kError;
    }

    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return Status::kError;
    }

    if (!msg_ptr->header.has_hotstuff() ||
            !msg_ptr->header.hotstuff().has_pro_msg() ||
            !msg_ptr->header.hotstuff().pro_msg().has_view_item()) {
        return Status::kInvalidArgument;
    }

    auto mem_ptr = elect_item->GetMemberByIdx(
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().leader_idx());
    if (mem_ptr->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_DEBUG("verify sign failed, backup invalid bls pk: %s",
            common::Encode::HexEncode(mem_ptr->id).c_str());
        return Status::kError;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security() && security()->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_DEBUG("verify leader sign failed: %s", common::Encode::HexEncode(mem_ptr->id).c_str());
        return Status::kError;
    }

    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

