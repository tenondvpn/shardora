#include <bls/bls_utils.h>
#include <consensus/hotstuff/crypto.h>
#include <exception>

namespace shardora {

namespace hotstuff {

Status Crypto::Sign(const uint64_t& elect_height, const HashStr& msg_hash, std::string* sign_x, std::string* sign_y) {
    auto elect_item = GetElectItem(elect_height);
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

Status Crypto::ReconstructAndVerify(
        const uint64_t& elect_height,
        const View& view,
        const HashStr& msg_hash,
        const uint32_t& index,
        const std::string& partial_sign_x,
        const std::string& partial_sign_y,
        std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign,
        std::shared_ptr<std::vector<uint32_t>> participants) try {
    // old vote
    if (bls_collection_ && bls_collection_->view > view) {
        return Status::kInvalidArgument;
    }
        
    if (!bls_collection_ || bls_collection_->view < view) {
        bls_collection_ = std::make_shared<BlsCollection>();
        bls_collection_->view = view; 
    }

    if (bls_collection_->handled) {
        return Status::kSuccess;
    }

    auto partial_sign = std::make_shared<libff::alt_bn128_G1>();
    try {
        partial_sign->X = libff::alt_bn128_Fq(partial_sign_x.c_str());
        partial_sign->Y = libff::alt_bn128_Fq(partial_sign_y.c_str());
        partial_sign->Z = libff::alt_bn128_Fq::one();        
    } catch (std::exception& e) {
        return Status::kError;
    }

    // Reconstruct sign
    bls_collection_->ok_bitmap.Set(index);
    bls_collection_->partial_signs[index] = partial_sign;
    auto elect_item = GetElectItem(elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    
    if (bls_collection_->OkCount() < elect_item->t()) {
        return Status::kBlsVerifyWaiting;
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < elect_item->n(); i++) {
        if (!bls_collection_->ok_bitmap.Valid(i)) {
            continue;
        }

        all_signs.push_back(*bls_collection_->partial_signs[i]);
        idx_vec.push_back(i+1);
        if (idx_vec.size() >= elect_item->t()) {
            break;
        }
    }

    libBLS::Bls bls_instance = libBLS::Bls(elect_item->t(), elect_item->n());
    std::vector<libff::alt_bn128_Fr> lagrange_coeffs(elect_item->t());
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, elect_item->t(), lagrange_coeffs);
#ifdef HOTSTUFF_TEST
    bls_collection_->reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(all_signs, lagrange_coeffs));
#else
    bls_collection_->reconstructed_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
#endif
    bls_collection_->reconstructed_sign->to_affine_coordinates();
    // Verify
    std::string verify_hash_a = "";
    std::string verify_hash_b = "";
    Status s = GetVerifyHashA(elect_height, msg_hash, &verify_hash_a);
    if (s != Status::kSuccess) {
        return s;
    }
    s = GetVerifyHashB(elect_height, *bls_collection_->reconstructed_sign, &verify_hash_b);
    if (s != Status::kSuccess) {
        return s;
    }

    if (verify_hash_a != verify_hash_b) {
        return Status::kBlsVerifyFailed;
    }

    bls_collection_->handled = true;
    *reconstructed_sign = *bls_collection_->reconstructed_sign;
    for (uint32_t d : bls_collection_->ok_bitmap.data()) {
        participants->push_back(d);
    }
        
    return Status::kSuccess;
} catch (std::exception& e) {
    return Status::kBlsVerifyWaiting;
};

Status Crypto::CreateQC(
        const std::shared_ptr<ViewBlock>& view_block,
        const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign,
        std::shared_ptr<QC> qc) {    
    qc->bls_agg_sign = reconstructed_sign;
    qc->view = view_block->view;
    qc->view_block_hash = view_block->hash;
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

