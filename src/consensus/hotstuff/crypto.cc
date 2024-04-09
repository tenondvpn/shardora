#include <bls/bls_utils.h>
#include <consensus/hotstuff/crypto.h>

namespace shardora {
namespace hotstuff {


/******** Crypto *********/

Status Crypto::Sign(const uint64_t& elect_height, const HashStr& msg_hash, std::string* sign_x, std::string* sign_y) {
    auto elect_item = elect_info_->GetElectItem(elect_height);
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

}
}
