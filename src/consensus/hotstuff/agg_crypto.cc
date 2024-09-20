#include <bls/agg_bls.h>
#include <common/global_info.h>
#include <consensus/hotstuff/agg_crypto.h>

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
    
    bls::AggBls().Sign(
            elect_item->t(),
            elect_item->n(),
            elect_item->local_sk(),
            msg_hash,
            &partial_sig->sig_);
    partial_sig->add_participant(elect_item->LocalMember()->index);
    
    return Status::kSuccess;
}

Status AggCrypto::VerifyAndAggregateSig(
        uint64_t elect_height,
        View view,
        const HashStr& msg_hash,
        uint32_t member_idx,
        const AggregateSignature& partial_sig,
        AggregateSignature& agg_sig) {
    auto s = Verify(partial_sig, msg_hash, common::GlobalInfo::Instance()->network_id(), elect_height);
    if (s != Status::kSuccess) {
        return s;
    }

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
        return Status::kError;
    }    

    std::unordered_set<uint32_t> participant_set;
    auto collection_item = bls_collection_->GetItem(msg_hash);
    collection_item->ok_bitmap.Set(member_idx);
    collection_item->partial_sigs[member_idx] = partial_sig;
    participant_set.insert(member_idx);
    
    if (collection_item->OkCount() < elect_item->t()) {
        return Status::kBlsVerifyWaiting;
    }

    // Aggregate partial signatures
    std::vector<libff::alt_bn128_G1> partial_g1_sigs;
    for (auto partial_sig : collection_item->partial_sigs) {
        partial_g1_sigs.push_back(partial_sig.signature());
    }

    libff::alt_bn128_G1* agg_g1_sig;
    bls::AggBls().Aggregate(elect_item->t(), elect_item->n(), partial_g1_sigs, agg_g1_sig);
    agg_sig.sig_ = *agg_g1_sig;
    agg_sig.participants_ = participant_set;
    
    return Status::kSuccess;
}

Status AggCrypto::VerifyQC(uint32_t sharding_id, const std::shared_ptr<QC>& qc) {
    if (!qc || !qc->agg_bls_agg_sign()) {
        return Status::kError;
    }

    return Verify(*qc->agg_bls_agg_sign(), qc->msg_hash(), sharding_id, qc->elect_height());
}

Status AggCrypto::VerifyTC(uint32_t sharding_id, const std::shared_ptr<TC>& tc) {
    if (!tc || !tc->agg_bls_agg_sign()) {
        return Status::kError;
    }

    return Verify(*tc->agg_bls_agg_sign(), tc->msg_hash(), sharding_id, tc->elect_height());
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

}
}
