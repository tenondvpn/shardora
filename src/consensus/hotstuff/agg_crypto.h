#pragma once
#include <bls/agg_bls.h>
#include <consensus/hotstuff/elect_info.h>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

struct AggregateSignature {
    libff::alt_bn128_G1 sig_;
    std::unordered_set<uint32_t> participants_; // member indexes who submit signatures

    AggregateSignature(
            const libff::alt_bn128_G1& sig,
            const std::unordered_set<uint32_t>& parts) : sig_(sig), participants_(parts) {}

    inline std::unordered_set<uint32_t> participants() const {
        return participants_;
    }

    inline libff::alt_bn128_G1 signature() const {
        return sig_;
    }
};

class AggCrypto {
public:
    AggCrypto(
            const uint32_t pool_idx,
            const std::shared_ptr<ElectInfo>& elect_info) :
        pool_idx_(pool_idx), elect_info_(elect_info) {}
    ~AggCrypto() {};
    
    AggCrypto(const AggCrypto&) = delete;
    AggCrypto &operator=(const AggCrypto &) = delete;

    Status PartialSign(
            uint32_t sharding_id,
            uint64_t elect_height,
            const HashStr& msg_hash,
            AggregateSignature* partial_sig);

    // Verify partial sig and aggregate them to a agg sig 
    Status VerifyAndAggregateSig(
            uint64_t elect_height,
            View view,
            const HashStr& msg_hash,
            uint32_t member_idx,
            const AggregateSignature& partial_sig,
            AggregateSignature& agg_sig);

    Status VerifyQC(uint32_t sharding_id, const std::shared_ptr<QC>& qc);
    Status VerifyTC(uint32_t sharding_id, const std::shared_ptr<TC>& tc);

    Status SignMessage(transport::MessagePtr& msg_ptr);
    Status VerifyMessage(const transport::MessagePtr& msg_ptr);

    inline std::shared_ptr<ElectItem> GetElectItem(uint32_t sharding_id, uint64_t elect_height) {
        auto item = elect_info_->GetElectItem(sharding_id, elect_height);
        if (item != nullptr) {
            return item;
        }
        
        return nullptr;
    }

    inline std::shared_ptr<ElectItem> GetLatestElectItem(uint32_t sharding_id) {
        return elect_info_->GetElectItemWithShardingId(sharding_id);
    }    
    
private:
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;

    // Verify verifies the given quorum signature against the message.
    Status Verify(
            const AggregateSignature& sig,
            const HashStr& msg_hash,
            uint32_t sharding_id,
            uint64_t elect_height) {
        auto elect_item = GetElectItem(sharding_id, elect_height);
        if (!elect_item) {
            return Status::kError;
        }
        
        auto n = sig.participants().size();
        if (n == 1) {
            uint32_t member_idx = *sig.participants().begin();
            auto agg_bls_pk = elect_item->agg_bls_pk(member_idx);
            if (!agg_bls_pk) {
                return Status::kError;
            }
            auto verified = bls::AggBls().CoreVerify(
                    elect_item->t(),
                    elect_item->n(),
                    *agg_bls_pk,
                    msg_hash,
                    sig.signature());
            return verified ? Status::kSuccess : Status::kBlsVerifyFailed;
        }

        std::vector<libff::alt_bn128_G2> pks;
        for (uint32_t member_idx : sig.participants()) {
            auto agg_bls_pk = elect_item->agg_bls_pk(member_idx);
            if (!agg_bls_pk) {
                return Status::kError;
            }
            pks.push_back(*agg_bls_pk);
        }
        if (pks.size() != n) {
            return Status::kError;
        }

        auto verified = bls::AggBls().FastAggregateVerify(
                elect_item->t(),
                elect_item->n(),
                pks,
                msg_hash,
                sig.signature());
        return verified ? Status::kSuccess : Status::kBlsVerifyFailed; 
    }
    // BatchVerify verifies the given quorum signature against the batch of messages.
    Status BatchVerify(const AggregateSignature& sig, const std::unordered_map<uint32_t, HashStr> msg_hash_map);    
};

} // namespace hotstuff

} // namespace shardora

