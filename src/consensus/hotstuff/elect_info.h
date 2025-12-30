#pragma once
#include <bls/agg_bls.h>
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/global_info.h>
#include <common/log.h>
#include <common/node_members.h>
#include <common/utils.h>
#include <consensus/hotstuff/consensus_statistic.h>
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
            const libff::alt_bn128_G2& common_pk, // useless for aggbls
            const libff::alt_bn128_Fr& sk) :
            members_(members), local_member_(nullptr), elect_height_(0), security_ptr_(security) {
        for (uint32_t i = 0; i < members->size(); i++) {
            if ((*members)[i]->id == security_ptr_->GetAddress()) {
                local_member_ = (*members)[i];
                // assert(local_member_->bls_publick_key != libff::alt_bn128_G2::zero());
                if (local_member_->bls_publick_key != libff::alt_bn128_G2::zero()) {
                    bls_valid_ = true;
                } 
                break;
            }
        }
#ifdef USE_AGG_BLS
        for (uint32_t i = 0; i < members->size(); i++) {
            auto agg_bls_pk = (*members)[i]->agg_bls_pk;
            auto agg_bls_pk_proof = (*members)[i]->agg_bls_pk_proof;            
            // 检查 agg bls 的 Proof of Posession，确保公钥不是假的，规避密钥消除攻击
            if (bls::AggBls::PopVerify(agg_bls_pk, agg_bls_pk_proof)) {
                member_aggbls_pk_map_[(*members)[i]->index] = std::make_shared<libff::alt_bn128_G2>(agg_bls_pk);
                SHARDORA_INFO("pop verify succ, member: %lu, elect_height: %lu, shard: %d, pk: %s",
                    (*members)[i]->index,
                    elect_height,
                    sharding_id,
                    libBLS::ThresholdUtils::G2ToString(agg_bls_pk)[0].c_str());
            }
            member_aggbls_pk_proof_map_[(*members)[i]->index] = std::make_shared<libff::alt_bn128_G1>(agg_bls_pk_proof);
        }
#endif
        
        elect_height_ = elect_height;
        common_pk_ = common_pk;
        assert(common_pk_ != libff::alt_bn128_G2::zero());
#ifdef USE_AGG_BLS
        local_sk_ = bls::AggBls::Instance()->agg_sk();
#else
        local_sk_ = sk;
        // assert(local_sk_ != libff::alt_bn128_Fr::zero());
#endif
        SetMemberCount(members->size());
        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
            pool_consen_stat_map_[pool_idx] = std::make_shared<ConsensusStat>(pool_idx, members);
        }
    }

    ElectItem(
            uint32_t sharding_id,
            uint64_t elect_height,
            uint32_t n,
            const libff::alt_bn128_G2& common_pk) :
        members_(nullptr), local_member_(nullptr), elect_height_(elect_height), common_pk_(common_pk), security_ptr_(nullptr) {
        SetMemberCount(n);
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

    inline std::shared_ptr<ConsensusStat> consensus_stat(uint32_t pool_idx) {
        return pool_consen_stat_map_[pool_idx];
    }

    std::shared_ptr<libff::alt_bn128_G2> agg_bls_pk(uint32_t member_idx) {
        if (member_aggbls_pk_map_.find(member_idx) != member_aggbls_pk_map_.end()) {
            return member_aggbls_pk_map_[member_idx];
        }
        SHARDORA_ERROR("cannot find agg pk, member: %lu, elect: %lu, right: %d", member_idx, elect_height_, member_aggbls_pk_map_[member_idx] != nullptr);
        assert(false);
        return nullptr;
    }

    std::shared_ptr<libff::alt_bn128_G1> agg_bls_pk_proof(uint32_t member_idx) {
        if (member_aggbls_pk_proof_map_.find(member_idx) != member_aggbls_pk_proof_map_.end()) {
            return member_aggbls_pk_proof_map_[member_idx];
        }
        return nullptr;
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
    std::unordered_map<uint32_t, std::shared_ptr<libff::alt_bn128_G2>> member_aggbls_pk_map_;
    std::unordered_map<uint32_t, std::shared_ptr<libff::alt_bn128_G1>> member_aggbls_pk_proof_map_;
    
    std::unordered_map<uint32_t, std::shared_ptr<ConsensusStat>> pool_consen_stat_map_; 
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

        if (sharding_id >= network::kConsensusShardEndNetworkId) {
            assert(false);
            return;
        }

        auto elect_items_ptr = elect_items_[sharding_id].load();
        if (elect_items_ptr != nullptr &&
                elect_items_ptr->ElectHeight() >= elect_height) {
            return;
        }

        auto elect_item = std::make_shared<ElectItem>(security_ptr_,
            sharding_id, elect_height, members, common_pk, sk);
        prev_elect_items_[sharding_id].store(elect_items_ptr);
        elect_items_[sharding_id].store(elect_item);
        RefreshMemberAddrs(sharding_id);
    #ifndef NDEBUG
        auto val = libBLS::ThresholdUtils::fieldElementToString(
            elect_item->common_pk().X.c0);
        SHARDORA_DEBUG("new elect coming sharding: %u, elect height: %lu, common pk: %s",
            sharding_id, elect_item->ElectHeight(), val.c_str());
    #endif
    }

    std::shared_ptr<ElectItem> GetElectItem(uint32_t sharding_id, const uint64_t elect_height) const {
        std::shared_ptr<ElectItem> res_ptr = nullptr;
        do {
            auto prev_elect_items_ptr = prev_elect_items_[sharding_id].load();
            auto elect_items_ptr = elect_items_[sharding_id].load();
            if (elect_items_ptr &&
                    elect_height == elect_items_ptr->ElectHeight()) {
                res_ptr = elect_items_ptr;
                break;
            } else if (prev_elect_items_ptr &&
                    elect_height == prev_elect_items_ptr->ElectHeight()) {
                res_ptr = prev_elect_items_ptr;
                break;
            }
            
            // 内存中没有从 ElectManager 获取
            libff::alt_bn128_G2 common_pk = libff::alt_bn128_G2::zero();
            libff::alt_bn128_Fr sec_key;
            if (!elect_mgr_) {
                break;
            }
            
            auto members = elect_mgr_->GetNetworkMembersWithHeight(
                elect_height,
                sharding_id,
                &common_pk,
                &sec_key);
            if (members == nullptr || common_pk == libff::alt_bn128_G2::zero()) {
                SHARDORA_ERROR("failed get elect members or common pk: %u, %lu, %d",
                    sharding_id,
                    elect_height,
                    (common_pk == libff::alt_bn128_G2::zero()));
                // assert(false);      
                break;
            }
            
            SHARDORA_DEBUG("new elect coming sharding: %u, elect height: %lu, common pk: %d",
                sharding_id, elect_height, (common_pk != libff::alt_bn128_G2::zero()));
    // #ifndef NDEBUG
    //         if (sharding_id == common::GlobalInfo::Instance()->network_id())
    //             for (auto iter = members->begin(); iter != members->end(); ++iter) {
    //                 assert((*iter)->bls_publick_key != libff::alt_bn128_G2::zero());
    //             }
    // #endif
            res_ptr = std::make_shared<ElectItem>(
                security_ptr_,
                sharding_id,
                elect_height,
                members,
                common_pk,
                sec_key);
        } while (0);
#ifndef NDEBUG
        if (res_ptr) {
            auto val = libBLS::ThresholdUtils::fieldElementToString(
                res_ptr->common_pk().X.c0);
            SHARDORA_DEBUG("success get elect sharding: %u, des elect_height: %lu, "
                "elect height: %lu, common pk: %s",
                sharding_id, elect_height, res_ptr->ElectHeight(), val.c_str());
        }
#endif
        return res_ptr;
    }

    inline std::shared_ptr<ElectItem> GetElectItemWithShardingId(uint32_t sharding_id) const {
        if (sharding_id > network::kConsensusShardEndNetworkId) {
            SHARDORA_DEBUG("get elect item failed sharding id: %u", sharding_id);
            return nullptr;
        }

        auto prev_elect_items_ptr = prev_elect_items_[sharding_id].load();
        auto elect_items_ptr = elect_items_[sharding_id].load();
        return elect_items_ptr != nullptr ? elect_items_ptr : prev_elect_items_ptr;
    }

    // 更新 elect_item members 的 addr
    void RefreshMemberAddrs(uint32_t sharding_id) {
        auto elect_items_ptr = elect_items_[sharding_id].load();
        if (!elect_items_ptr) {
            SHARDORA_DEBUG("Leader pool elect item null");
            return;
        }
        for (auto& member : *(elect_items_ptr->Members())) {
            // SHARDORA_DEBUG("get Leader pool %s failed: %d, %u %d", 
            //     common::Encode::HexEncode(member->id).c_str(), 
            //     common::GlobalInfo::Instance()->network_id(),
            //     member->public_ip,
            //     member->public_port);
            if (member->public_ip == 0 || member->public_port == 0) {
                auto dht_ptr = network::DhtManager::Instance()->GetDht(common::GlobalInfo::Instance()->network_id());
                if (dht_ptr != nullptr) {
                    auto nodes = dht_ptr->readonly_hash_sort_dht();
                    for (auto iter = nodes->begin(); iter != nodes->end(); ++iter) {
                        if ((*iter)->id == member->id) {
                            member->public_ip = common::IpToUint32((*iter)->public_ip.c_str());
                            member->public_port = (*iter)->public_port;
                            SHARDORA_DEBUG("set member %s ip port %s:%d",
                                common::Encode::HexEncode((*iter)->id).c_str(), 
                                (*iter)->public_ip.c_str(), 
                                (*iter)->public_port);
                        }
                    }
                }
            }
        }        
    }

    inline uint32_t max_consensus_sharding_id() const {
        return max_consensus_sharding_id_;
    }
    
private:
    std::atomic<std::shared_ptr<ElectItem>> prev_elect_items_[network::kConsensusShardEndNetworkId + 1] = { nullptr };
    std::atomic<std::shared_ptr<ElectItem>> elect_items_[network::kConsensusShardEndNetworkId + 1] = { nullptr };
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint32_t max_consensus_sharding_id_ = 3;
};

} // namespace consensus

} // namespace shardora


