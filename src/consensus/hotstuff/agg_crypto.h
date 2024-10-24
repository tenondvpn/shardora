#pragma once
#include <bls/agg_bls.h>
#include <common/bitmap.h>
#include <common/utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

class AggCrypto {
public:
    struct AggBlsCollectionItem {
        HashStr msg_hash;
        common::Bitmap ok_bitmap{ common::kEachShardMaxNodeCount };
        AggregateSignature partial_sigs[common::kEachShardMaxNodeCount];
        AggregateSignature* agg_sig;

        inline uint32_t OkCount() const {
            return ok_bitmap.valid_count();
        }
    };

    struct BlsCollection {
        View view;
        std::unordered_map<HashStr, std::shared_ptr<AggBlsCollectionItem>> msg_collection_map;
        bool handled;

        std::shared_ptr<AggBlsCollectionItem> GetItem(const HashStr& msg_hash) {
            std::shared_ptr<AggBlsCollectionItem> collection_item = nullptr;
            auto it = msg_collection_map.find(msg_hash);
            if (it == msg_collection_map.end()) {
                collection_item = std::make_shared<AggBlsCollectionItem>();
                collection_item->msg_hash = msg_hash;
                msg_collection_map[msg_hash] = collection_item; 
            } else {
                collection_item = it->second;
            }
            return collection_item;
        }        
    };
    
    AggCrypto(
            const uint32_t pool_idx,
            const std::shared_ptr<ElectInfo>& elect_info,
            const std::shared_ptr<bls::IBlsManager>& bls_mgr) :
        pool_idx_(pool_idx), elect_info_(elect_info), bls_mgr_(bls_mgr) {}
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
            const AggregateSignature& partial_sig,
            AggregateSignature& agg_sig);

    Status VerifyQC(uint32_t sharding_id, const std::shared_ptr<QC>& qc);
    Status VerifyTC(uint32_t sharding_id, const std::shared_ptr<TC>& tc);
    std::shared_ptr<AggregateQC> CreateAggregateQC(
            uint32_t sharding_id,
            uint64_t elect_height,
            View view,
            const std::unordered_map<uint32_t, std::shared_ptr<QC>>& high_qcs,
            const std::vector<AggregateSignature*>& high_qc_sigs);

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

    inline std::shared_ptr<security::Security> security() const {
        return bls_mgr_->security();
    }    
    
private:
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<bls::IBlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<BlsCollection> bls_collection_ = nullptr;

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

    Status AggregateSigs(
            const std::shared_ptr<ElectItem>& elect_item,
            const std::vector<AggregateSignature*>& sigs,
            AggregateSignature* agg_sig) {
        std::vector<libff::alt_bn128_G1> g1_sigs;
        for (const auto sig : sigs) {
            if (!sig->IsValid()) {
                continue;
            }
            g1_sigs.push_back(sig->signature());
            for (const uint32_t member_id : sig->participants()) {
                agg_sig->add_participant(member_id);
            }
        }

        libff::alt_bn128_G1* agg_g1_sig;
        bls::AggBls().Aggregate(elect_item->t(), elect_item->n(), g1_sigs, agg_g1_sig);
        agg_sig->set_signature(*agg_g1_sig);

        return Status::kSuccess;
    }
};

} // namespace hotstuff

} // namespace shardora

