#pragma once
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <security/security.h>
#include <consensus/hotstuff/elect_info.h>

namespace shardora {

namespace hotstuff {

// Bls vote collection
struct BlsCollection {
    HashStr msg_hash;
    View view;
    common::Bitmap ok_bitmap{ common::kEachShardMaxNodeCount };
    libff::alt_bn128_G1 partial_signs[common::kEachShardMaxNodeCount];

    inline uint32_t OkCount() const {
        return ok_bitmap.valid_count();
    }
};

// Every ViewBlockChain's hotstuff has a Crypto
class Crypto {
public:
    Crypto() {};
    ~Crypto() {};

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    Status Sign(const uint64_t& elect_height, const HashStr& msg_hash, std::string* sign_x, std::string* sign_y);
    bool Verify(const uint64_t& elect_height, const View& view, const HashStr& msg_hash, const uint32_t& index, const libff::alt_bn128_G1& partial_sign) {
        // old vote
        if (bls_collection_ && bls_collection_->view > view) {
            return false;
        }
        
        if (!bls_collection_ || bls_collection_->view < view) {
            bls_collection_ = std::make_shared<BlsCollection>();
            bls_collection_->view = view; 
        }

        bls_collection_->ok_bitmap.Set(index);
        bls_collection_->partial_signs[index] = partial_sign;
        
        auto elect_item = elect_info_->GetElectItem(elect_height);
        if (!elect_item) {
            return false;
        }
        
        if (bls_collection_->OkCount() < elect_item->t()) {
            return false;
        }

        // TODO Restore sig and verify
        
        return true;
    };
    
private:
    void GetG1Hash(const HashStr& msg_hash, libff::alt_bn128_G1* g1_hash) {
        bls_mgr_->GetLibffHash(msg_hash, g1_hash);
    }

    void GetVerifyHash(const uint64_t& elect_height, const HashStr& msg_hash, std::string* verify_hash) {
        libff::alt_bn128_G1 g1_hash;
        GetG1Hash(msg_hash, &g1_hash);
        auto elect_item = elect_info_->GetElectItem(elect_height);
        if (!elect_item) {
            return;
        }
        
        if (bls_mgr_->GetVerifyHash(
                elect_item->t(),
                elect_item->n(),
                g1_hash,
                elect_item->common_pk(),
                verify_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("get verify hash failed!");
        }
    }

    

    // 保留上一次 elect_item，避免 epoch 切换的影响
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<BlsCollection> bls_collection_ = nullptr;
};

} // namespace consensus

} // namespace shardora


