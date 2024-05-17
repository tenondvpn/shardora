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
    struct NodeConsensusStatistic { // 节点共识统计
        uint32_t success;
        uint32_t fail;
        uint32_t score;

        NodeConsensusStatistic() {
            success = 0;
            fail = 0;
            score = 100;
        }

        void IncrSucc() { success++; }
        void IncrFail() { fail++; }
        
        uint32_t Score() {
            return 0;
        }
    };
    
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

    // 统计 leader 的共识情况，更新分值
    std::shared_ptr<NodeConsensusStatistic> NodeStatistic(uint32_t member_idx) {
        auto it = node_statistic_map_.find(member_idx);
        if (it == node_statistic_map_.end()) {
            return nullptr;
        }
        return it->second;
    }
    
    void MarkSuccess(uint32_t member_idx) {
        auto stat = NodeStatistic(member_idx);
        if (stat) {
            stat->IncrSucc();
        }
    }
    void MarkFail(uint32_t member_idx) {
        auto stat = NodeStatistic(member_idx);
        if (stat) {
            stat->IncrFail();
        }
    }
    
    uint32_t NodeScore(uint32_t member_idx) {
        auto stat = NodeStatistic(member_idx);
        return stat == nullptr ? 0 : stat->Score();
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
    std::unordered_map<uint32_t, std::shared_ptr<NodeConsensusStatistic>> node_statistic_map_; // 用于保存该 elect_item 中每个节点的共识情况
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
        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            return;
        }
        if (elect_item_ != nullptr && elect_item_->ElectHeight() >= elect_height) {
            return;
        }
        auto elect_item = std::make_shared<ElectItem>(security_ptr_,
            sharding_id, elect_height, members, common_pk, sk);

        prev_elect_item_ = elect_item_;
        elect_item_ = elect_item;

        RefreshMemberAddrs();
    }

    std::shared_ptr<ElectItem> GetElectItem(const uint64_t elect_height) const {
        if (elect_item_ && elect_height == elect_item_->ElectHeight()) {
            return elect_item_;
        } else if (prev_elect_item_ && elect_height == prev_elect_item_->ElectHeight()) {
            return prev_elect_item_;
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

    inline std::shared_ptr<ElectItem> GetElectItem() const {
        return elect_item_ != nullptr ? elect_item_ : prev_elect_item_;
    }

    // 更新 elect_item members 的 addr
    void RefreshMemberAddrs() {
        if (!elect_item_) {
            ZJC_DEBUG("Leader pool elect item null");
            return;
        }
        for (auto& member : *(elect_item_->Members())) {
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

    void MarkSuccess(uint64_t elect_height, uint32_t member_idx) {
        auto elect_item = GetElectItem(elect_height);
        if (elect_item) {
            elect_item->MarkSuccess(member_idx);
        }
    }
    void MarkFail(uint64_t elect_height, uint32_t member_idx) {
        auto elect_item = GetElectItem(elect_height);
        if (elect_item) {
            elect_item->MarkFail(member_idx);
        }
    }
    
    uint32_t NodeScore(uint64_t elect_height, uint32_t member_idx) {
        auto elect_item = GetElectItem(elect_height);
        return elect_item != nullptr ? elect_item->NodeScore(member_idx) : 0;
    }

    std::shared_ptr<ElectItem::NodeConsensusStatistic> NodeStatistic(uint64_t elect_height, uint32_t member_idx) {
        auto elect_item = GetElectItem(elect_height);
        return elect_item != nullptr ? elect_item->NodeStatistic(member_idx) : 0;
    }
    
private:
    std::shared_ptr<ElectItem> prev_elect_item_ = nullptr; 
    std::shared_ptr<ElectItem> elect_item_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint32_t max_consensus_sharding_id_ = 3;
};

} // namespace consensus

} // namespace shardora


