#pragma once
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <security/security.h>
#include <consensus/hotstuff/elect_info.h>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

// Every pool has a Crypto
class Crypto {
public:
    struct BlsCollectionItem {
        HashStr msg_hash;
        common::Bitmap ok_bitmap{ common::kEachShardMaxNodeCount };
        std::shared_ptr<libff::alt_bn128_G1> partial_signs[common::kEachShardMaxNodeCount];
        std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;

        inline uint32_t OkCount() const {
            return ok_bitmap.valid_count();
        }    
    };
    // Bls vote collection
    struct BlsCollection {
        View view;
        // may receive different msg_hashs per view because of unexpected inconsistency.
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
    
    Crypto(
            const uint32_t& pool_idx,
            const std::shared_ptr<ElectInfo>& elect_info,
            const std::shared_ptr<bls::IBlsManager>& bls_mgr) :
        pool_idx_(pool_idx), elect_info_(elect_info), bls_mgr_(bls_mgr) {};
    ~Crypto() {};

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    Status Sign(
            const uint64_t& elect_height,
            const HashStr& msg_hash,
            std::string* sign_x,
            std::string* sign_y);
    
    Status ReconstructAndVerify(
            const uint64_t& elect_height,
            const View& view,
            const HashStr& msg_hash,
            const uint32_t& member_idx,
            const std::string& partial_sign_x,
            const std::string& partial_sign_y,
            std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign);    
    
    Status Verify(
            const uint64_t& elect_height,
            const HashStr& msg_hash,
            const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign);
    
    Status CreateQC(
            const std::shared_ptr<ViewBlock>& view_block,
            const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign,
            std::shared_ptr<QC>& qc);

    Status CreateTC(
            const View& view,
            const std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign,
            std::shared_ptr<TC>& tc);

    Status SignMessage(transport::MessagePtr& msg_ptr);
    Status VerifyMessage(const transport::MessagePtr& msg_ptr);
    
    
    inline std::shared_ptr<ElectItem> GetElectItem(const uint64_t& elect_height) {
        return elect_info_->GetElectItem(elect_height);
    }

    inline std::shared_ptr<ElectItem> GetLatestElectItem() {
        return elect_info_->GetElectItem();
    }
    
private:
    // 保留上一次 elect_item，避免 epoch 切换的影响
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<bls::IBlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<BlsCollection> bls_collection_ = nullptr;
    
    void GetG1Hash(const HashStr& msg_hash, libff::alt_bn128_G1* g1_hash) {
        bls_mgr_->GetLibffHash(msg_hash, g1_hash);
    }

    Status GetVerifyHashA(const uint64_t& elect_height, const HashStr& msg_hash, std::string* verify_hash) {
        auto elect_item = GetElectItem(elect_height);
        if (!elect_item) {
            ZJC_ERROR("elect_item not found, elect_height: %lu", elect_height);
            return Status::kError;
        }        
        libff::alt_bn128_G1 g1_hash;
        GetG1Hash(msg_hash, &g1_hash);
        
        if (bls_mgr_->GetVerifyHash(
                    elect_item->t(),
                    elect_item->n(),
                    g1_hash,
                    elect_item->common_pk(),
                    verify_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("get verify hash a failed!");
            return Status::kError;
        }

        return Status::kSuccess;
    }

    Status GetVerifyHashB(const uint64_t& elect_height, const libff::alt_bn128_G1& reconstructed_sign, std::string* verify_hash) {
        auto elect_item = GetElectItem(elect_height);
        if (!elect_item) {
            return Status::kError;
        }

        if (bls_mgr_->GetVerifyHash(
                    elect_item->t(),
                    elect_item->n(),
                    reconstructed_sign,
                    verify_hash) != bls::kBlsSuccess) {
            elect_item->common_pk().to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(elect_item->common_pk());
            auto cpk_strs = cpk->toString();
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, ",
                elect_item->t(), elect_item->n(), cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height);
            return Status::kError;
        }

        return Status::kSuccess;
    }

    inline std::shared_ptr<security::Security> security() const {
        return bls_mgr_->security();
    }
};

} // namespace consensus

} // namespace shardora


