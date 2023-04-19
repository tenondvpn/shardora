#pragma once

#include <mutex>
#include <unordered_map>
#include <memory>
#include <queue>
#include <vector>

#include <libbls/bls/BLSPublicKey.h>

#include "bls/bls_manager.h"
#include "common/utils.h"
#include "db/db.h"
#include "elect/elect_node_detail.h"
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "protos/elect.pb.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace zjchain {

namespace elect {

class HeightWithElectBlock {
    struct HeightMembersItem {
        HeightMembersItem(common::MembersPtr& m, uint64_t h) : members_ptr(m), height(h) {
            // for (auto iter = m->begin(); iter != m->end(); ++iter) {
            //     ip_weight.AddIp((*iter)->public_ip);
            // }
        }

        common::MembersPtr members_ptr;
        uint64_t height;
        libff::alt_bn128_G2 common_bls_publick_key;
        libff::alt_bn128_Fr local_sec_key;
        // ip::IpWeight ip_weight;
    };

    typedef std::shared_ptr<HeightMembersItem> HeightMembersItemPtr;

public:
    HeightWithElectBlock(
            std::shared_ptr<security::Security>& security_ptr,
            std::shared_ptr<db::Db>& db)
            : security_ptr_(security_ptr), db_(db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }

    ~HeightWithElectBlock() {}

    // elect block is always coming in order or one time just one block, so no need to lock it
    void AddNewHeightBlock(
            uint64_t height,
            uint32_t network_id,
            common::MembersPtr& members_ptr,
            const libff::alt_bn128_G2& common_pk) {
        if (network_id >= network::kConsensusShardEndNetworkId) {
            return;
        }

        uint64_t min_height = common::kInvalidUint64;
        uint64_t min_index = 0;
        for (int32_t i = 0; i < 3; ++i) {
            if (members_ptrs_[network_id][i] == nullptr) {
                members_ptrs_[network_id][i] = std::make_shared<HeightMembersItem>(
                    members_ptr,
                    height);
                members_ptrs_[network_id][i]->common_bls_publick_key = common_pk;
                std::string bls_prikey;
                if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
                    members_ptrs_[network_id][i]->local_sec_key = libff::alt_bn128_Fr(bls_prikey.c_str());
                } else {
                    members_ptrs_[network_id][i]->local_sec_key = libff::alt_bn128_Fr::zero();
                }

                ZJC_DEBUG("0 save bls pk and secret key success.height: %lu, network_id: %u, %d, %d",
                    height, network_id,
                    (members_ptrs_[network_id][i]->common_bls_publick_key == libff::alt_bn128_G2::zero()),
                    (members_ptrs_[network_id][i]->local_sec_key == libff::alt_bn128_Fr::zero()));
                return;
            }

            if (members_ptrs_[network_id][i]->height < min_height) {
                min_height = members_ptrs_[network_id][i]->height;
                min_index = i;
            }
        }

        if (min_height >= height) {
            return;
        }

        members_ptrs_[network_id][min_index] = std::make_shared<HeightMembersItem>(
            members_ptr,
            height);
        members_ptrs_[network_id][min_index]->common_bls_publick_key = common_pk;
        std::string bls_prikey;
        if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
            members_ptrs_[network_id][min_index]->local_sec_key = libff::alt_bn128_Fr(bls_prikey.c_str());
        } else {
            members_ptrs_[network_id][min_index]->local_sec_key = libff::alt_bn128_Fr::zero();
            assert(false);
        }
        ZJC_DEBUG("1 save bls pk and secret key success.height: %lu, network_id: %u",
            height, network_id);
    }

    // ip::IpWeight GetIpWeight(uint64_t height, uint32_t network_id) {
    //     if (network_id >= network::kConsensusShardEndNetworkId) {
    //         return ip::IpWeight();
    //     }

    //     for (int32_t i = 0; i < 3; ++i) {
    //         if (members_ptrs_[network_id][i] != nullptr &&
    //                 members_ptrs_[network_id][i]->height == height) {
    //             return members_ptrs_[network_id][i]->ip_weight;
    //         }
    //     }

    //     auto members = GetMembersPtr(height, network_id, nullptr, nullptr);
    //     if (members == nullptr) {
    //         return ip::IpWeight();
    //     }

    //     ip::IpWeight weight;
    //     for (auto iter = members->begin(); iter != members->end(); ++iter) {
    //         weight.AddIp((*iter)->public_ip);
    //     }

    //     return weight;
    // }

    libff::alt_bn128_G2 GetCommonPublicKey(uint64_t height, uint32_t network_id) {
        if (network_id >= network::kConsensusShardEndNetworkId) {
            return libff::alt_bn128_G2::zero();
        }

        for (int32_t i = 0; i < 3; ++i) {
            if (members_ptrs_[network_id][i] != nullptr &&
                members_ptrs_[network_id][i]->height == height) {
                return members_ptrs_[network_id][i]->common_bls_publick_key;
            }
        }

        return height_with_common_pks_[network_id][height];
    }

    common::MembersPtr GetMembersPtr(
            std::shared_ptr<security::Security>& security,
            uint64_t height,
            uint32_t network_id,
            libff::alt_bn128_G2* common_pk,
            libff::alt_bn128_Fr* local_sec_key) {
        ZJC_DEBUG("get bls pk and secret key success.height: %lu, network_id: %u",
            height, network_id);
        if (network_id >= network::kConsensusShardEndNetworkId) {
            return nullptr;
        }

        for (int32_t i = 0; i < 3; ++i) {
            if (members_ptrs_[network_id][i] != nullptr &&
                    members_ptrs_[network_id][i]->height == height) {
                if (common_pk != nullptr) {
                    *common_pk = members_ptrs_[network_id][i]->common_bls_publick_key;
                    ZJC_DEBUG("0 get bls pk and secret key success.height: %lu, network_id: %u",
                        height, network_id);

                }

                if (local_sec_key != nullptr) {
                    *local_sec_key = members_ptrs_[network_id][i]->local_sec_key;
                    ZJC_DEBUG("1 get bls pk and secret key success.height: %lu, network_id: %u",
                        height, network_id);
                }

                return members_ptrs_[network_id][i]->members_ptr;
            }
        }

        // get from cache map
        {
            if (common_pk != nullptr) {
                auto pk_iter = height_with_common_pks_[network_id].find(height);
                if (pk_iter != height_with_common_pks_[network_id].end()) {
                    *common_pk = pk_iter->second;
                    ZJC_DEBUG("2 get bls pk and secret key success.height: %lu, network_id: %u",
                        height, network_id);

                }
            }

            if (local_sec_key != nullptr) {
                std::string bls_prikey;
                if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
                    *local_sec_key = libff::alt_bn128_Fr(bls_prikey.c_str());
                } else {
                    *local_sec_key = libff::alt_bn128_Fr::zero();
                }
                ZJC_DEBUG("3 get bls pk and secret key success.height: %lu, network_id: %u",
                    height, network_id);

            }

            auto iter = height_with_members_[network_id].find(height);
            if (iter != height_with_members_[network_id].end()) {
                return iter->second;
            }
        }

        block::protobuf::Block block;
        if (!prefix_db_->GetBlockWithHeight(
                network::kRootCongressNetworkId,
                common::kRootChainPoolIndex,
                height,
                &block)) {
            assert(false);
            return nullptr;
        }

        if (block.tx_list_size() != 1) {
            assert(false);
            return nullptr;
        }

        bool eb_valid = false;
        elect::protobuf::ElectBlock elect_block;
        for (int32_t i = 0; i < block.tx_list(0).storages_size(); ++i) {
            if (block.tx_list(0).storages(i).key() == protos::kElectNodeAttrElectBlock) {
                if (!elect_block.ParseFromString(block.tx_list(0).storages(i).val_hash())) {
                    assert(false);
                    return nullptr;
                }

                eb_valid = true;
                break;
            }
        }

        if (!eb_valid) {
            assert(false);
            return nullptr;
        }

        auto shard_members_ptr = std::make_shared<common::Members>();
        auto& in = elect_block.in();
        uint32_t member_index = 0;
        for (int32_t i = 0; i < in.size(); ++i) {
            auto id = security->GetAddress(in[i].pubkey());
            shard_members_ptr->push_back(std::make_shared<common::BftMember>(
                elect_block.shard_network_id(),
                id,
                in[i].pubkey(),
                member_index++,
                in[i].public_ip(),
                in[i].pool_idx_mod_num()));
        }

        libff::alt_bn128_G2 tmp_common_pk = libff::alt_bn128_G2::zero();
        auto& prev_members = elect_block.prev_members();
        std::vector<std::string> pkey_str = {
            prev_members.common_pubkey().x_c0(),
            prev_members.common_pubkey().x_c1(),
            prev_members.common_pubkey().y_c0(),
            prev_members.common_pubkey().y_c1()
        };

        auto n = prev_members.bls_pubkey_size();
        auto t = n * 2 / 3;
        if ((n * 2) % 3 > 0) {
            t += 1;
        }

        BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
        tmp_common_pk = *pkey.getPublicKey();
        if (tmp_common_pk == libff::alt_bn128_G2::zero()) {
            assert(false);
            return nullptr;
        }

        ZJC_DEBUG("4 get bls pk and secret key success.height: %lu, network_id: %u",
            height, network_id);

        if (common_pk != nullptr) {
            *common_pk = tmp_common_pk;
        }

        height_queue_.push(height);
        height_with_members_[network_id][height] = shard_members_ptr;
        height_with_common_pks_[network_id][height] = tmp_common_pk;
        if (height_queue_.size() > kMaxCacheElectBlockCount) {
            auto min_height = height_queue_.top();
            auto iter = height_with_members_[network_id].find(min_height);
            if (iter != height_with_members_[network_id].end()) {
                height_with_members_[network_id].erase(iter);
            }

            auto pk_iter = height_with_common_pks_[network_id].find(min_height);
            if (pk_iter != height_with_common_pks_[network_id].end()) {
                height_with_common_pks_[network_id].erase(pk_iter);
            }

            height_queue_.pop();
        }

        return shard_members_ptr;
    }

private:
    static const uint32_t kMaxKeepElectBlockCount = 3u;
    static const uint32_t kMaxCacheElectBlockCount = 7u;
    std::map<uint64_t, common::MembersPtr> height_with_members_[network::kConsensusShardEndNetworkId];
    std::map<uint64_t, libff::alt_bn128_G2> height_with_common_pks_[network::kConsensusShardEndNetworkId];
    std::map<uint64_t, libff::alt_bn128_Fr> height_with_local_sec_key_[network::kConsensusShardEndNetworkId];
    std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> height_queue_;
    HeightMembersItemPtr members_ptrs_[network::kConsensusShardEndNetworkId][kMaxKeepElectBlockCount];
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HeightWithElectBlock);
};

}  // namespace elect

}  // namespace zjchain
