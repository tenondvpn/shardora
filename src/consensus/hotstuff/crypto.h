#pragma once
#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <security/security.h>

namespace shardora {

namespace hotstuff {

class CryptoInfo {
public:
    explicit CryptoInfo(const std::shared_ptr<security::Security>& security) :
        members_(nullptr), local_member_(nullptr), elect_height_(0), security_ptr_(security) {
    }
    ~CryptoInfo() {};

    CryptoInfo(const CryptoInfo&) = delete;
    CryptoInfo& operator=(const CryptoInfo&) = delete;

    void OnNewElectBlock(
            uint32_t sharding_id,
            uint64_t elect_height,
            const common::MembersPtr& members,
            const libff::alt_bn128_G2& common_pk,
            const libff::alt_bn128_Fr& sk) {
        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            return;
        }

        if (elect_height_ >= elect_height) {
            return;
        }

        members_ = members;

        for (uint32_t i = 0; i < members->size(); i++) {
            if ((*members)[i]->id == security_ptr_->GetAddress()) {
                local_member_ = (*members)[i];
                if (local_member_->bls_publick_key != libff::alt_bn128_G2::zero()) {
                    bls_valid_ = true;
                }
                break;
            }
        }

        elect_height_ = elect_height;
        common_pk_ = common_pk;
        sk_ = sk;
    }

    inline common::MembersPtr Members() const {
        return members_;
    }

    inline common::BftMemberPtr LocalMember() const {
        return local_member_;
    }

private:
    common::MembersPtr members_;
    common::BftMemberPtr local_member_;
    uint64_t elect_height_;
    libff::alt_bn128_G2 common_pk_;
    libff::alt_bn128_Fr sk_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    bool bls_valid_{false};
};

class Crypto {
public:
    Crypto() = default;
    virtual ~Crypto() = 0;

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    void Sign(const std::string&);
    bool Verify();
    void RecoverSign();

private:
    
};

} // namespace consensus

} // namespace shardora


