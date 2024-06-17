#pragma once
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/global_info.h>
#include <common/log.h>
#include <common/node_members.h>
#include <common/utils.h>
#include <consensus/hotstuff/types.h>
#include <elect/elect_manager.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <memory>
#include <network/dht_manager.h>
#include <network/network_utils.h>
#include <network/universal_manager.h>
#include <security/security.h>

namespace shardora {

namespace hotstuff {

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
        
        // 初始化共识统计
        for (uint32_t pool_idx; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
            std::vector<std::shared_ptr<MemberConsensusStat>> member_consen_stats(members->size());
            for (uint32_t i = 0; i < members->size(); i++) {
                member_consen_stats[(*members)[i]->index] = std::make_shared<MemberConsensusStat>();
            }
            pool_member_consen_stats_[pool_idx] = member_consen_stats;
        }
        
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

    // 本节点是否在该 epoch 的共识池中
    inline bool IsValid() const {
        return local_member_ != nullptr;
    }

    inline common::BftMemberPtr GetMemberByIdx(uint32_t member_idx) const {
        return (*members_)[member_idx];
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

    void SetMemberConsensusStat(
            uint32_t member_idx,
            uint32_t pool_idx,
            const std::shared_ptr<MemberConsensusStat>& member_consen_stat) {
        pool_member_consen_stats_[pool_idx][member_idx] = member_consen_stat;
    }

    inline std::vector<std::shared_ptr<MemberConsensusStat>> GetAllConsensusStats(uint32_t pool_idx) {
        return pool_member_consen_stats_[pool_idx];
    }

    inline std::shared_ptr<MemberConsensusStat> GetMemberConsensusStat(uint32_t member_idx, uint32_t pool_idx) {
        auto member_consen_stat = pool_member_consen_stats_[pool_idx];
        if (member_consen_stat.size() <= member_idx) {
            return nullptr;
        }
        return member_consen_stat[member_idx];
    }
    
private:
    void SetMemberCount(uint32_t mem_cnt) {
        bls_n_ = mem_cnt;
        bls_t_ = common::GetSignerCount(mem_cnt);
    }
    
    common::MembersPtr members_;
    common::BftMemberPtr local_member_;
    uint64_t elect_height_;
    libff::alt_bn128_G2 common_pk_;
    libff::alt_bn128_Fr local_sk_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    bool bls_valid_{false};
    uint32_t bls_t_{0};
    uint32_t bls_n_{0};

    std::unordered_map<uint32_t, std::vector<std::shared_ptr<MemberConsensusStat>>> pool_member_consen_stats_; // 所有节点共识情况统计
};


class ElectInfo {
public:
    ElectInfo(
            const std::shared_ptr<security::Security>& sec,
            const std::shared_ptr<elect::ElectManager>& elect_mgr) :
        security_ptr_(sec), elect_mgr_(elect_mgr) {}
    ~ElectInfo() {}

    ElectInfo(const ElectInfo&) = delete;
    ElectInfo& operator=(const ElectInfo&) = delete;
    
    void OnNewElectBlock(
            uint32_t sharding_id,
            uint64_t elect_height,
            const common::MembersPtr& members,
            const libff::alt_bn128_G2& common_pk,
            const libff::alt_bn128_Fr& sk) {
        if (sharding_id > max_consensus_sharding_id_) {
            max_consensus_sharding_id_ = sharding_id;
        }

        if (sharding_id > network::kConsensusShardEndNetworkId) {
            assert(false);
            return;
        }

        if (elect_items_[sharding_id] != nullptr &&
                elect_items_[sharding_id]->ElectHeight() >= elect_height) {
            return;
        }

        auto elect_item = std::make_shared<ElectItem>(security_ptr_,
            sharding_id, elect_height, members, common_pk, sk);
        prev_elect_items_[sharding_id] = elect_items_[sharding_id];
        elect_items_[sharding_id] = elect_item;
        RefreshMemberAddrs(sharding_id);
        ZJC_DEBUG("new elect coming sharding: %u, elect height: %lu",
            sharding_id, elect_item->ElectHeight());
    }

    std::shared_ptr<ElectItem> GetElectItem(uint32_t sharding_id, const uint64_t elect_height) const {
        if (elect_items_[sharding_id] &&
                elect_height == elect_items_[sharding_id]->ElectHeight()) {
            return elect_items_[sharding_id];
        } else if (prev_elect_items_[sharding_id] &&
                elect_height == prev_elect_items_[sharding_id]->ElectHeight()) {
            return prev_elect_items_[sharding_id];
        }
        // 内存中没有从 ElectManager 获取
        auto net_id = common::GlobalInfo::Instance()->network_id(); 
        libff::alt_bn128_G2 common_pk = libff::alt_bn128_G2::zero();
        libff::alt_bn128_Fr sec_key;

        if (!elect_mgr_) {
            return nullptr;
        }
        
        auto members = elect_mgr_->GetNetworkMembersWithHeight(
            elect_height,
            net_id,
            &common_pk,
            &sec_key);
        if (members == nullptr || common_pk == libff::alt_bn128_G2::zero()) {
            ZJC_ERROR("failed get elect members or common pk: %u, %lu, %d",
                net_id,
                elect_height,
                (common_pk == libff::alt_bn128_G2::zero()));            
            return nullptr;
        }
        
        return std::make_shared<ElectItem>(
            security_ptr_,
            net_id,
            elect_height,
            members,
            common_pk,
            sec_key);
    }

    inline std::shared_ptr<ElectItem> GetElectItemWithShardingId(uint32_t sharding_id) const {
        if (sharding_id > network::kConsensusShardEndNetworkId) {
            return nullptr;
        }

        return elect_items_[sharding_id] != nullptr ? elect_items_[sharding_id] : prev_elect_items_[sharding_id];
    }

    // 更新 elect_item members 的 addr
    void RefreshMemberAddrs(uint32_t sharding_id) {
        if (!elect_items_[sharding_id]) {
            ZJC_DEBUG("Leader pool elect item null");
            return;
        }
        for (auto& member : *(elect_items_[sharding_id]->Members())) {
            ZJC_DEBUG("get Leader pool %s failed: %d, %u %d", 
                common::Encode::HexEncode(member->id).c_str(), 
                common::GlobalInfo::Instance()->network_id(),
                member->public_ip,
                member->public_port);
            if (member->public_ip == 0 || member->public_port == 0) {
                auto dht_ptr = network::DhtManager::Instance()->GetDht(common::GlobalInfo::Instance()->network_id());
                if (dht_ptr != nullptr) {
                    auto nodes = dht_ptr->readonly_hash_sort_dht();
                    for (auto iter = nodes->begin(); iter != nodes->end(); ++iter) {
                        ZJC_DEBUG("Leader pool dht node: %s", common::Encode::HexEncode((*iter)->id).c_str());
                        if ((*iter)->id == member->id) {
                            member->public_ip = common::IpToUint32((*iter)->public_ip.c_str());
                            member->public_port = (*iter)->public_port;
                            ZJC_DEBUG("set member %s ip port %s:%d",
                                common::Encode::HexEncode((*iter)->id).c_str(), 
                                (*iter)->public_ip.c_str(), 
                                (*iter)->public_port);
                        }
                    }
                } else {
                    ZJC_DEBUG("Leader pool dht failed: %d", common::GlobalInfo::Instance()->network_id());
                }
            } else {
                ZJC_DEBUG("Leader pool %s failed: %d", 
                    common::Encode::HexEncode(member->id).c_str(), 
                    common::GlobalInfo::Instance()->network_id());
            }
        }        
    }

    inline uint32_t max_consensus_sharding_id() const {
        return max_consensus_sharding_id_;
    }
    
private:
    std::shared_ptr<ElectItem> prev_elect_items_[network::kConsensusShardEndNetworkId + 1] = { nullptr };
    std::shared_ptr<ElectItem> elect_items_[network::kConsensusShardEndNetworkId + 1] = { nullptr };
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint32_t max_consensus_sharding_id_ = 3;
};

} // namespace consensus

} // namespace shardora


