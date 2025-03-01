#include <bls/agg_bls.h>
#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <consensus/hotstuff/agg_crypto.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <tools/utils.h>

namespace shardora {
namespace hotstuff {

Status AggCrypto::PartialSign(
        uint32_t sharding_id,
        uint64_t elect_height,
        const HashStr& msg_hash,
        AggregateSignature* partial_sig) {
    auto elect_item = GetElectItem(sharding_id, elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    libff::alt_bn128_G1 g1_sig;
    
    bls::AggBls::Sign(
            elect_item->local_sk(),
            msg_hash,
            &g1_sig);

    ZJC_DEBUG("partial sign sk: %s, real sk: %s, msg_hash: %s, sig: %s",
        libBLS::ThresholdUtils::fieldElementToString(elect_item->local_sk()).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(bls::AggBls::Instance()->agg_sk()).c_str(),
        common::Encode::HexEncode(msg_hash).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(g1_sig.X).c_str());
    partial_sig->set_signature(g1_sig);
    partial_sig->add_participant(elect_item->LocalMember()->index);
    
    return Status::kSuccess;
}

Status AggCrypto::VerifyAndAggregateSig(
        uint64_t elect_height,
        View view,
        const HashStr& msg_hash,
        const AggregateSignature& partial_sig,
        AggregateSignature& agg_sig) {
    // auto s = Verify(partial_sig, msg_hash, common::GlobalInfo::Instance()->network_id(), elect_height);
    // if (s != Status::kSuccess) {
    //     assert(false);
    //     return s;
    // }

    // old vote
    if (bls_collection_ && bls_collection_->view > view) {
        return Status::kInvalidArgument;
    }
        
    if (!bls_collection_ || bls_collection_->view < view) {
        bls_collection_ = std::make_shared<BlsCollection>();
        bls_collection_->view = view;
    }

    if (bls_collection_->handled) {
        auto collect_item = bls_collection_->GetItem(msg_hash);
        if (collect_item != nullptr && collect_item->agg_sig != nullptr) {
            agg_sig = *collect_item->agg_sig;
            return Status::kBlsHandled;
        }
        
        bls_collection_->handled = false;
    }

    auto elect_item = GetElectItem(common::GlobalInfo::Instance()->network_id(), elect_height);
    if (!elect_item) {
        assert(false);
        return Status::kError;
    }

    if (!partial_sig.IsValid()) {
        assert(false);
        return Status::kError;
    }
    uint32_t member_idx = *partial_sig.participants().begin();
    
    auto collection_item = bls_collection_->GetItem(msg_hash);
    collection_item->ok_bitmap.Set(member_idx);
    collection_item->partial_sigs[member_idx] = partial_sig;

    // ZJC_WARN("====7.0 msg_hash: %s, member_idx: %d", common::Encode::HexEncode(msg_hash).c_str(), member_idx);
    
    if (collection_item->OkCount() < elect_item->t()) {
        return Status::kBlsVerifyWaiting;
    }

    std::vector<std::shared_ptr<AggregateSignature>> partial_sigs;
    for (auto partial_sig : collection_item->partial_sigs) {
        partial_sigs.push_back(std::make_shared<AggregateSignature>(partial_sig));
    }
    auto s = AggregateSigs(partial_sigs, &agg_sig);
    if (s == Status::kSuccess) {
        collection_item->agg_sig = &agg_sig;
        bls_collection_->handled = true;
    } else {
        assert(false);
    }
    return s;
}

Status AggCrypto::VerifyQC(uint32_t sharding_id, const QC& qc) {    
    auto agg_sig = std::make_shared<AggregateSignature>();
    if (!agg_sig->LoadFromProto(qc.agg_sig())) {
        return Status::kError;
    }
    if (!agg_sig->IsValid()) {
        return Status::kError;
    }

    // Check if QC participants length is enougth
    auto elect_item = elect_info_->GetElectItem(sharding_id, qc.elect_height());
    if (!elect_item) {
        return Status::kError;
    }

    if (agg_sig->participants().size() < elect_item->t()) {
        return Status::kError;
    }

    auto qc_msg_hash = GetQCMsgHash(qc);
    return Verify(*agg_sig, qc_msg_hash, sharding_id, qc.elect_height());
}

Status AggCrypto::VerifyTC(uint32_t sharding_id, const TC& tc) {    
    auto agg_sig = std::make_shared<AggregateSignature>();
    if (!agg_sig->LoadFromProto(tc.agg_sig())) {
        return Status::kError;
    }
    if (!agg_sig->IsValid()) {
        return Status::kError;
    }

    // Check if QC participants length is enougth
    auto elect_item = elect_info_->GetElectItem(sharding_id, tc.elect_height());
    if (!elect_item) {
        return Status::kError;
    }

    if (agg_sig->participants().size() < elect_item->t()) {
        return Status::kError;
    }    

    auto tc_msg_hash = GetTCMsgHash(tc);
    return Verify(*agg_sig, tc_msg_hash, sharding_id, tc.elect_height());
}

std::shared_ptr<AggregateQC> AggCrypto::CreateAggregateQC(
        uint32_t sharding_id,
        uint64_t elect_height,        
        View view,
        const std::unordered_map<uint32_t, std::shared_ptr<QC>>& high_qcs,
        const std::vector<std::shared_ptr<AggregateSignature>>& high_qc_sigs) {
    auto elect_item = GetElectItem(sharding_id, elect_height);
    if (!elect_item) {
        return nullptr;
    }
    
    AggregateSignature agg_high_qc_sig;
    Status s = AggregateSigs(high_qc_sigs, &agg_high_qc_sig);
    if (s != Status::kSuccess) {
        return nullptr;
    }

    return std::make_shared<AggregateQC>(
            high_qcs,
            std::make_shared<AggregateSignature>(agg_high_qc_sig),
            view);
}

Status AggCrypto::VerifyAggregateQC(
        uint32_t sharding_id,
        const std::shared_ptr<AggregateQC>& agg_qc,
        std::shared_ptr<QC> high_qc) {
    if (agg_qc->QCs().size() == 0) {
        return Status::kError;
    }
    // TODO 默认取 agg_qc 中第一个 qc 对应的 elect_item
    auto elect_height = agg_qc->QCs()[0]->elect_height();
    auto elect_item = GetElectItem(sharding_id, elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    
    std::unordered_map<uint32_t, HashStr> msg_hashes;
    auto qc_map = agg_qc->QCs();
    for (auto iter = qc_map.begin(); iter != qc_map.end(); iter++) {
        auto member_idx = iter->first;
        auto qc = iter->second;

        if (high_qc->view() < qc->view()) {
            high_qc = qc;
        }

        msg_hashes[member_idx] = GetQCMsgHash(*qc);
    }

    if (agg_qc->Sig()->participants().size() < elect_item->t()) {
        return Status::kError;
    }

    return BatchVerify(sharding_id, elect_height, *agg_qc->Sig(), msg_hashes);
}

Status AggCrypto::SignMessage(transport::MessagePtr& msg_ptr) {
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string sign;
    if (security() && security()->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return Status::kError;
    }
    
    msg_ptr->header.set_sign(sign);
    return Status::kSuccess;
}

Status AggCrypto::VerifyMessage(const transport::MessagePtr& msg_ptr) {
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
    auto mem_ptr = elect_item->GetMemberByIdx(msg_ptr->header.hotstuff().pro_msg().view_item().qc().leader_idx());
    if (mem_ptr->bls_publick_key == libff::alt_bn128_G2::zero()) {
        ZJC_WARN("verify sign failed, backup invalid bls pk: %s",
            common::Encode::HexEncode(mem_ptr->id).c_str());
        return Status::kError;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security() && security()->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_WARN("verify leader sign failed: %s", common::Encode::HexEncode(mem_ptr->id).c_str());
        return Status::kError;
    }

    return Status::kSuccess;
}

}
}
