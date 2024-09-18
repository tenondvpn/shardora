#include <bls/agg_bls.h>
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
    
    return Status::kSuccess;
}
    
}
}
