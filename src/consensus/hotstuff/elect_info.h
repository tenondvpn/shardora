#pragma once
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <memory>
#include <security/security.h>

namespace shardora {

namespace consensus {

// ElectItem 
class ElectItem {
public:
    ElectItem(
            const std::shared_ptr<security::Security>& security,
            uint32_t sharding_id,
            uint64_t elect_height,
            const common::MembersPtr& members,
            const libff::alt_bn128_G2& common_pk,
            const libff::alt_bn128_Fr& sk) :
        members_(members), local_member_(nullptr), elect_height_(0), security_ptr_(security) {
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
        local_sk_ = sk;

        SetMemberCount(members->size());        
    }
    ~ElectItem() {};

    ElectItem(const ElectItem&) = delete;
    ElectItem& operator=(const ElectItem&) = delete;

    inline common::MembersPtr Members() const {
        return members_;
    }

    inline common::BftMemberPtr LocalMember() const {
        return local_member_;
    }

    inline uint32_t MemberCount() const {
        return member_count_;
    }

    inline uint64_t ElectHeight() const {
        return elect_height_;
    }

    inline uint32_t t() const {
        return bls_t_;
    }

    inline uint32_t n() const {
        return bls_n_;
    }

    inline const libff::alt_bn128_Fr& local_sk() const {
        return local_sk_;
    }

    inline libff::alt_bn128_G2 common_pk() const {
        return common_pk_;
    }

private:
    void SetMemberCount(uint32_t mem_cnt) {
        member_count_ = mem_cnt;
        bls_t_ = member_count_ * 2 / 3;
        if ((member_count_ * 2) % 3 > 0) {
            bls_t_ += 1;
        }        
    }
    
    common::MembersPtr members_;
    uint32_t member_count_{0};
    common::BftMemberPtr local_member_;
    uint64_t elect_height_;
    libff::alt_bn128_G2 common_pk_;
    libff::alt_bn128_Fr local_sk_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    bool bls_valid_{false};
    uint32_t bls_t_{0};
    uint32_t bls_n_{0};
};


class ElectInfo {
public:
    explicit ElectInfo(const std::shared_ptr<security::Security>& sec) : security_ptr_(sec) {}
    ~ElectInfo() {}

    ElectInfo(const ElectInfo&) = delete;
    ElectInfo& operator=(const ElectInfo&) = delete;
    
    void OnNewElectBlock(
            uint32_t sharding_id,
            uint64_t elect_height,
            const common::MembersPtr& members,
            const libff::alt_bn128_G2& common_pk,
            const libff::alt_bn128_Fr& sk) {
        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            return;
        }

        if (elect_item_ && elect_item_->ElectHeight() >= elect_height) {
            return;
        }

        auto elect_item = std::make_shared<ElectItem>(security_ptr_,
            sharding_id, elect_height, members, common_pk, sk);

        prev_elect_item_ = elect_item_;
        elect_item_ = elect_item;        
    }

    std::shared_ptr<ElectItem> GetElectItem(const uint64_t elect_height) const {
        if (elect_item_ && elect_height == elect_item_->ElectHeight()) {
            return elect_item_;
        } else if (prev_elect_item_ && elect_height == prev_elect_item_->ElectHeight()) {
            return prev_elect_item_;
        }
        return nullptr;
    }
    
private:
    std::shared_ptr<ElectItem> prev_elect_item_ = nullptr; 
    std::shared_ptr<ElectItem> elect_item_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
};

} // namespace consensus

} // namespace shardora


