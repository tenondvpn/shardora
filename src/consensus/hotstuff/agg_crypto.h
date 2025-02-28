#pragma once
#include <bls/agg_bls.h>
#include <common/bitmap.h>
#include <common/encode.h>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <tools/utils.h>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

// AggCrypto is hotstuff crypto module supported by bls aggregation signature
// It works when USE_AGG_BLS is defined
class AggCrypto {
public:
    struct BlsCollectionItem {
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
        std::unordered_map<HashStr, std::shared_ptr<BlsCollectionItem>> msg_collection_map;
        bool handled;

        std::shared_ptr<BlsCollectionItem> GetItem(const HashStr& msg_hash) {
            std::shared_ptr<BlsCollectionItem> collection_item = nullptr;
            auto it = msg_collection_map.find(msg_hash);
            if (it == msg_collection_map.end()) {
                collection_item = std::make_shared<BlsCollectionItem>();
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

    Status VerifyQC(uint32_t sharding_id, const QC& qc);
    Status VerifyTC(uint32_t sharding_id, const TC& tc);
    std::shared_ptr<AggregateQC> CreateAggregateQC(
            uint32_t sharding_id,
            uint64_t elect_height,
            View view,
            const std::unordered_map<uint32_t, std::shared_ptr<QC>>& high_qcs,
            const std::vector<std::shared_ptr<AggregateSignature>>& high_qc_sigs);
    Status VerifyAggregateQC(
            uint32_t sharding_id,
            const std::shared_ptr<AggregateQC>& agg_qc,
            std::shared_ptr<QC> high_qc);
    
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

    // Verify verifies a sig of agg bls against the message.
    Status Verify(
            const AggregateSignature& sig,
            const HashStr& msg_hash,
            uint32_t sharding_id,
            uint64_t elect_height) {
        auto elect_item = GetElectItem(sharding_id, elect_height);
        if (!elect_item) {
            assert(false);
            return Status::kError;
        }
        
        auto n = sig.participants().size();
        // non aggregated sig
        if (n == 1) {
            uint32_t member_idx = *sig.participants().begin();
            auto agg_bls_pk = elect_item->agg_bls_pk(member_idx);
            if (!agg_bls_pk) {
                // 不在本次共识池或 POP 验证失败都会导致 elect_item 找不到 pk
                assert(false);
                return Status::kError;
            }
            
            auto verified = bls::AggBls::CoreVerify(
                    *agg_bls_pk,
                    msg_hash,
                    sig.signature());
            if (verified) {
                return Status::kSuccess;
            }
            auto sig_x_str = libBLS::ThresholdUtils::fieldElementToString(sig.signature().X);
            ZJC_WARN("agg sig verify failed, sig.x is %s, msg_hash: %s, member: %d", sig_x_str.c_str(), common::Encode::HexEncode(msg_hash).c_str(), member_idx);
            assert(false);
            return Status::kBlsVerifyFailed;
        }

        // aggregated sig
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

        if (!bls::AggBls::FastAggregateVerify(
                pks,
                msg_hash,
                sig.signature())) {
            assert(false);
            return Status::kBlsVerifyFailed;
        }
        
        return Status::kSuccess;
    }    
    
private:
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<bls::IBlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<BlsCollection> bls_collection_ = nullptr;
    // BatchVerify verifies the given quorum signature against the batch of messages.
    Status BatchVerify(
            uint32_t sharding_id,
            uint64_t elect_height,
            const AggregateSignature& sig,
            const std::unordered_map<uint32_t, HashStr> msg_hash_map) {
        if (sig.participants().size() != msg_hash_map.size()) {
            return Status::kError;
        }

        auto elect_item = GetElectItem(sharding_id, elect_height);
        if (!elect_item) {
            return Status::kError;
        }

        std::vector<libff::alt_bn128_G2> pks;
        std::vector<std::string> str_hashes;
        for (auto iter = msg_hash_map.begin(); iter != msg_hash_map.end(); iter++) {
            auto member_idx = iter->first;
            auto str_hash = iter->second;
            auto pk = elect_item->agg_bls_pk(member_idx);
            if (!pk) {
                ZJC_ERROR("pool: %d, batch verify failed, pk not found, member_idx: %d",
                    pool_idx_, member_idx);
                return Status::kError;
            }
            pks.push_back(*pk);
            str_hashes.push_back(str_hash);
        }
        
        if (msg_hash_map.size() == 1) {
            bool ok = bls::AggBls::Instance()->CoreVerify(pks[0], str_hashes[0], sig.signature());
            return ok ? Status::kSuccess : Status::kError;
        }

        bool ok = bls::AggBls::Instance()->AggregateVerify(pks, str_hashes, sig.signature());
        return ok ? Status::kSuccess : Status::kError;
    }

    Status AggregateSigs(
            const std::vector<std::shared_ptr<AggregateSignature>>& sigs,
            AggregateSignature* agg_sig) {
        std::vector<libff::alt_bn128_G1> g1_sigs;
        for (const auto& sig : sigs) {
            if (!sig->IsValid()) {
                continue;
            }
            g1_sigs.push_back(sig->signature());
            for (const uint32_t member_id : sig->participants()) {
                agg_sig->add_participant(member_id);
            }
        }

        libff::alt_bn128_G1 agg_g1_sig;
        bls::AggBls::Aggregate(g1_sigs, &agg_g1_sig);
        agg_sig->set_signature(agg_g1_sig);

        return Status::kSuccess;
    }
};

} // namespace hotstuff

} // namespace shardora

