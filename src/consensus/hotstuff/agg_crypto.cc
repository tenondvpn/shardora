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

    auto collection_item = bls_collection_->GetItem(msg_hash);
    collection_item->ok_bitmap.Set(member_idx);
    collection_item->partial_sigs[member_idx] = partial_sig;
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
    
    return Status::kSuccess;
}
    
}
}
