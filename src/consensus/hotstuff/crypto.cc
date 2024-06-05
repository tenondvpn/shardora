#include <bls/bls_utils.h>
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
        return Status::kError;
    }
    return Status::kSuccess;
}

Status Crypto::ReconstructAndVerifyThresSign(
        uint64_t elect_height,
        View view,
        const HashStr& msg_hash,
        uint32_t index,
        const std::string& partial_sign_x,
        const std::string& partial_sign_y,
        std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign) try {
    // old vote
    if (bls_collection_ && bls_collection_->view > view) {
        ZJC_DEBUG("bls_collection_ && bls_collection_->view > view: %lu, %lu, "
            "index: %u, pool_idx_: %d", 
            bls_collection_->view, view, index, pool_idx_);
        return Status::kInvalidArgument;
    }
        
    if (!bls_collection_ || bls_collection_->view < view) {
        bls_collection_ = std::make_shared<BlsCollection>();
        bls_collection_->view = view; 
        ZJC_DEBUG("set bls_collection_ && bls_collection_->view > view: %lu, %lu, "
            "index: %u, pool_idx_: %d", 
            bls_collection_->view, view, index, pool_idx_);
    }

    // 已经处理过
    if (bls_collection_->handled) {
        auto collect_item = bls_collection_->GetItem(msg_hash);
        if (collect_item != nullptr && collect_item->reconstructed_sign != nullptr) {
            reconstructed_sign = collect_item->reconstructed_sign;
            return Status::kBlsHandled;
        }
        
        bls_collection_->handled = false;
    }

    auto partial_sign = std::make_shared<libff::alt_bn128_G1>();
    try {
        partial_sign->X = libff::alt_bn128_Fq(partial_sign_x.c_str());
        partial_sign->Y = libff::alt_bn128_Fq(partial_sign_y.c_str());
        partial_sign->Z = libff::alt_bn128_Fq::one();
    } catch (std::exception& e) {
        assert(false);
        return Status::kError;
    }

    auto collection_item = bls_collection_->GetItem(msg_hash);
    // Reconstruct sign
    // TODO(HT): 先判断是否已经处理过的index
    collection_item->ok_bitmap.Set(index);
    collection_item->partial_signs[index] = partial_sign;
    auto elect_item = GetElectItem(common::GlobalInfo::Instance()->network_id(), elect_height);
    if (!elect_item) {
        return Status::kError;
    }

    ZJC_DEBUG("msg hash: %s, ok count: %u, t: %u, index: %u",
        common::Encode::HexEncode(msg_hash).c_str(), 
        collection_item->OkCount(), 
        elect_item->t(),
        index);
    if (collection_item->OkCount() < elect_item->t()) {
        return Status::kBlsVerifyWaiting;
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < elect_item->n(); i++) {
        if (!collection_item->ok_bitmap.Valid(i)) {
            continue;
        }

        all_signs.push_back(*collection_item->partial_signs[i]);
        idx_vec.push_back(i+1);

        if (idx_vec.size() >= elect_item->t()) {
            break;
        }
    }

    std::vector<libff::alt_bn128_Fr> lagrange_coeffs(elect_item->t());
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, elect_item->t(), lagrange_coeffs);
#ifdef HOTSTUFF_TEST
    collection_item->reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
    collection_item->reconstructed_sign->to_affine_coordinates();   
#else
    libBLS::Bls bls_instance = libBLS::Bls(elect_item->t(), elect_item->n());
    collection_item->reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(all_signs, lagrange_coeffs));
    collection_item->reconstructed_sign->to_affine_coordinates();
#endif
    Status s = VerifyThresSign(
        common::GlobalInfo::Instance()->network_id(), 
        elect_height, 
        msg_hash, 
        collection_item->reconstructed_sign);
    if (s == Status::kSuccess) {
        reconstructed_sign = collection_item->reconstructed_sign;
        bls_collection_->handled = true;
    } else {
        assert(false);
    }

    return s;
} catch (std::exception& e) {
    ZJC_ERROR("crypto verify exception %s", e.what());
    return Status::kBlsVerifyWaiting;
};

Status Crypto::VerifyThresSign(
        uint32_t sharding_id, 
        uint64_t elect_height, 
        const HashStr &msg_hash,
        const std::shared_ptr<libff::alt_bn128_G1> &reconstructed_sign) {
#ifdef HOTSTUFF_TEST
    return Status::kSuccess;
#endif
    if (reconstructed_sign == nullptr) {
        ZJC_DEBUG("reconstructed_sign == nullptr");
        assert(false);
        return Status::kBlsVerifyFailed;
    }    
    std::string verify_hash_a;
    std::string verify_hash_b;
    Status s = GetVerifyHashA(sharding_id, elect_height, msg_hash, &verify_hash_a);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("GetVerifyHashA failed!");
        assert(false);
        return s;
    }

    s = GetVerifyHashB(sharding_id, elect_height, *reconstructed_sign, &verify_hash_b);
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
            reconstructed_sign->X);
        ZJC_DEBUG("verify_hash_a != verify_hash_b %s, %s, msg_hash: %s, "
            "net: %u, elect height: %lu, common PK: %s, agg sign: %s", 
            common::Encode::HexEncode(verify_hash_a).c_str(),
            common::Encode::HexEncode(verify_hash_b).c_str(),
            common::Encode::HexEncode(msg_hash).c_str(),
            sharding_id, 
            elect_height,
            val.c_str(),
            agg_sign_str.c_str());
        assert(false);
        return Status::kBlsVerifyFailed;
    }

#ifndef NDEBUG
    auto elect_item = GetElectItem(sharding_id, elect_height);
    auto val = libBLS::ThresholdUtils::fieldElementToString(
        elect_item->common_pk().X.c0);
    auto agg_sign_str = libBLS::ThresholdUtils::fieldElementToString(
        reconstructed_sign->X);
    ZJC_DEBUG("success verify agg sign %s, %s, msg_hash: %s, net: %u, "
            "elect height: %lu, common PK: %s, agg sign: %s", 
            common::Encode::HexEncode(verify_hash_a).c_str(),
            common::Encode::HexEncode(verify_hash_b).c_str(),
            common::Encode::HexEncode(msg_hash).c_str(),
            sharding_id, 
            elect_height,
            val.c_str(),
            agg_sign_str.c_str());
#endif
    return Status::kSuccess;
}

Status Crypto::CreateQC(
        const HashStr& view_block_hash,
        const HashStr& commit_view_block_hash,
        View view,
        uint64_t elect_height,
        uint32_t leader_idx,
        const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign,
        std::shared_ptr<QC>& qc) {
    if (!reconstructed_sign) {
        return Status::kInvalidArgument;
    }
    qc->bls_agg_sign = reconstructed_sign;
    qc->view = view;
    qc->elect_height = elect_height;
    qc->view_block_hash = view_block_hash;
    qc->commit_view_block_hash = commit_view_block_hash;
    qc->leader_idx = leader_idx;
    return Status::kSuccess;
}

Status Crypto::CreateTC(
        View view,
        uint64_t elect_height,
        uint32_t leader_idx,
        const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign,
        std::shared_ptr<TC>& tc) {
    if (!reconstructed_sign) {
        tc = nullptr;
        return Status::kInvalidArgument;
    }
    tc->bls_agg_sign = reconstructed_sign;
    tc->view = view;
    tc->elect_height = elect_height;
    tc->leader_idx = leader_idx;
    return Status::kSuccess;
}

Status Crypto::VerifyQC(
        uint32_t sharding_id,
        const std::shared_ptr<QC>& qc) {
    if (!qc) {
        return Status::kError;
    }
    if (qc->view == GenesisView) {
        return Status::kSuccess;
    }
    Status s = VerifyThresSign(sharding_id, qc->elect_height, qc->msg_hash(), qc->bls_agg_sign);
    if (s != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error.");
        return s;
    }
    return Status::kSuccess;
}

Status Crypto::VerifyTC(
        uint32_t sharding_id,
        const std::shared_ptr<TC>& tc) {
    if (!tc) {
        return Status::kError;
    }
    if (VerifyThresSign(sharding_id, tc->elect_height, tc->msg_hash(), tc->bls_agg_sign) != Status::kSuccess) {
        ZJC_ERROR("Verify tc is error.");
        return Status::kError; 
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
    auto mem_ptr = elect_item->GetMemberByIdx(msg_ptr->header.hotstuff().pro_msg().view_item().leader_idx());
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

