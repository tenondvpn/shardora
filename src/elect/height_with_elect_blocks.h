#pragma once

#include <bls/agg_bls.h>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <queue>
#include <vector>

#include <libbls/bls/BLSPublicKey.h>

#include "bls/bls_manager.h"
#include "common/utils.h"
#include "db/db.h"
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "protos/elect.pb.h"
#include "protos/get_proto_hash.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace shardora {

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
                auto new_item = std::make_shared<HeightMembersItem>(
                    members_ptr,
                    height);
                new_item->common_bls_publick_key = common_pk;
                std::string bls_prikey;
                if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
                    bls::protobuf::LocalBlsItem bls_item;
                    if (!bls_item.ParseFromString(bls_prikey)) {
                        new_item->local_sec_key = libff::alt_bn128_Fr::zero();
                        assert(false);
                    } else {
                        new_item->local_sec_key = libff::alt_bn128_Fr(bls_item.local_private_key().c_str());
                        assert(new_item->local_sec_key != libff::alt_bn128_Fr::zero());
                    }
                } else {
                    new_item->local_sec_key = libff::alt_bn128_Fr::zero();
                    // assert(false);
                }
                ZJC_DEBUG("0 save bls pk and secret key success.height: %lu, network_id: %u, %d, %d",
                    height, network_id,
                    (new_item->common_bls_publick_key == libff::alt_bn128_G2::zero()),
                    (new_item->local_sec_key == libff::alt_bn128_Fr::zero()));
                members_ptrs_[network_id][i] = new_item;
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

        auto new_item = std::make_shared<HeightMembersItem>(
            members_ptr,
            height);
        new_item->common_bls_publick_key = common_pk;

        std::string bls_prikey;
        if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
            bls::protobuf::LocalBlsItem bls_item;
            if (!bls_item.ParseFromString(bls_prikey)) {
                new_item->local_sec_key = libff::alt_bn128_Fr::zero();
                assert(false);
            } else {
                new_item->local_sec_key = libff::alt_bn128_Fr(bls_item.local_private_key().c_str());
                ZJC_DEBUG("2 success get local sec key.");
            }
        } else {
            new_item->local_sec_key = libff::alt_bn128_Fr::zero();
            assert(false);
        }

        members_ptrs_[network_id][min_index] = new_item;
        ZJC_DEBUG("1 save bls pk and secret key success.height: %lu, "
            "network_id: %u, local_sec_key: %s, is zero: %d",
            height, network_id,
            libBLS::ThresholdUtils::fieldElementToString(new_item->local_sec_key).c_str(),
            (new_item->local_sec_key == libff::alt_bn128_Fr::zero()));
    }

    // TODO: multi thread problem.
    common::MembersPtr GetMembersPtr(
            std::shared_ptr<security::Security>& security,
            uint64_t height,
            uint32_t network_id,
            libff::alt_bn128_G2* common_pk,
            libff::alt_bn128_Fr* local_sec_key) {
        if (height == 0) {
            assert(false);
            return nullptr;
        }
        
        ZJC_DEBUG("get bls pk and secret key success.height: %lu, "
            "network_id: %u, end net id: %u, offset: %u",
            height, 
            network_id, 
            network::kConsensusShardEndNetworkId, 
            network::kConsensusWaitingShardOffset);
        if (network_id >= network::kConsensusShardEndNetworkId) {
            network_id -= network::kConsensusWaitingShardOffset;
        }

        for (int32_t i = 0; i < 3; ++i) {
            auto item_ptr = members_ptrs_[network_id][i];
            if (item_ptr != nullptr && item_ptr->height == height) {
                if (common_pk != nullptr) {
                    *common_pk = item_ptr->common_bls_publick_key;
                    ZJC_DEBUG("0 get bls pk and secret key success.height: %lu, network_id: %u",
                        height, network_id);

                }

                if (local_sec_key != nullptr) {
                    *local_sec_key = item_ptr->local_sec_key;
                    ZJC_DEBUG("1 uccess get local sec key get bls pk and secret key success.height: %lu, network_id: %u",
                        height, network_id);
                }

                ZJC_DEBUG("success get bls pk and secret key success.height: %lu, "
                    "network_id: %u, end net id: %u, offset: %u, local sec key: %s",
                    height, 
                    network_id, 
                    network::kConsensusShardEndNetworkId, 
                    network::kConsensusWaitingShardOffset,
                    (local_sec_key == nullptr ? "0" : libBLS::ThresholdUtils::fieldElementToString(*local_sec_key).c_str()));
                return item_ptr->members_ptr;
            }
        }

        // lock it
        std::lock_guard<std::mutex> g(height_with_members_mutex_);
        auto iter = height_with_members_[network_id].find(height);
        if (iter != height_with_members_[network_id].end()) {
            if (iter->second->local_sec_key == libff::alt_bn128_Fr::zero()) {
                std::string bls_prikey;
                if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
                    bls::protobuf::LocalBlsItem bls_item;
                    if (!bls_item.ParseFromString(bls_prikey)) {
                        iter->second->local_sec_key = libff::alt_bn128_Fr::zero();
                        assert(false);
                    } else {
                        iter->second->local_sec_key = libff::alt_bn128_Fr(bls_item.local_private_key().c_str());
                        ZJC_DEBUG("1 success get local sec key.");
                    }
                }
            }

            if (iter->second->common_bls_publick_key == libff::alt_bn128_G2::zero()) {
                assert(false);
                return nullptr;
            }

            if (common_pk != nullptr) {
                *common_pk = iter->second->common_bls_publick_key;
            }

            if (local_sec_key != nullptr) {
                *local_sec_key = iter->second->local_sec_key;
                ZJC_DEBUG("1 0 success get local sec key.");
            }

            return iter->second->members_ptr;
        }

        auto shard_members = GetMembers(security, network_id, height);
        if (shard_members == nullptr) {
            ZJC_DEBUG("failed get members.");
            assert(false);
            return nullptr;
        }

        if (common_pk == nullptr) {
            return shard_members;
        }

        auto new_item = std::make_shared<HeightMembersItem>(shard_members, height);
        new_item->common_bls_publick_key = GetCommonPublicKey(network_id, height);
        if (new_item->common_bls_publick_key == libff::alt_bn128_G2::zero()) {
            ZJC_DEBUG("ew_item->common_bls_publick_key == libff::alt_bn128_G2::zero().");
            // assert(false);
            return shard_members;
        }

        height_with_members_[network_id][height] = new_item;
        CHECK_MEMORY_SIZE(height_with_members_[network_id]);
        std::string bls_prikey;
        if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
            bls::protobuf::LocalBlsItem bls_item;
            if (!bls_item.ParseFromString(bls_prikey)) {
                new_item->local_sec_key = libff::alt_bn128_Fr::zero();
                assert(false);
            } else {
                new_item->local_sec_key = libff::alt_bn128_Fr(bls_item.local_private_key().c_str());
                ZJC_DEBUG("success get local sec key.");
            }
        }

        if (common_pk != nullptr) {
            *common_pk = new_item->common_bls_publick_key;
        }

        if (local_sec_key != nullptr) {
            *local_sec_key = new_item->local_sec_key;
            ZJC_DEBUG("1 success get local sec key.");
        }

        if (height_with_members_[network_id].size() >= kMaxCacheElectBlockCount) {
            height_with_members_[network_id].erase(height_with_members_[network_id].begin());
            CHECK_MEMORY_SIZE(height_with_members_[network_id]);
        }

        return shard_members;
    }

private:
    libff::alt_bn128_G2 GetCommonPublicKey(uint32_t network_id, uint64_t height) {
        if (network_id >= network::kConsensusShardEndNetworkId) {
            return libff::alt_bn128_G2::zero();
        }

        elect::protobuf::PrevMembers prev_members;
        if (!prefix_db_->GetElectHeightCommonPk(network_id, height, &prev_members)) {
            // assert(false);
            return libff::alt_bn128_G2::zero();
        }

        if (!prev_members.has_common_pubkey()) {
            return libff::alt_bn128_G2::zero();
        }

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
        auto tmp_common_pk = *pkey.getPublicKey();
        if (tmp_common_pk == libff::alt_bn128_G2::zero()) {
            assert(false);
            return libff::alt_bn128_G2::zero();
        }

        return tmp_common_pk;
    }

    common::MembersPtr GetMembers(
            std::shared_ptr<security::Security>& security,
            uint32_t network_id,
            uint64_t height) {
        view_block::protobuf::ViewBlockItem view_block;
        if (!prefix_db_->GetBlockWithHeight(
                network::kRootCongressNetworkId,
                network_id % common::kImmutablePoolSize,
                height,
                &view_block)) {
            ZJC_INFO("failed get block with height net: %u, pool: %u, height: %lu",
                network::kRootCongressNetworkId, network_id, height);
            //             assert(false);
            return nullptr;
        }

        auto& block = view_block.block_info();
        if (!block.has_elect_block()) {
            assert(false);
            return nullptr;
        }

        assert(block.tx_list_size() > 0);
        auto& elect_block = block.elect_block();
        auto shard_members_ptr = std::make_shared<common::Members>();
        auto& in = elect_block.in();
        uint32_t member_index = 0;
        for (int32_t i = 0; i < in.size(); ++i) {
            auto id = security->GetAddress(in[i].pubkey());
            auto agg_bls_pk = bls::Proto2BlsPublicKey(in[i].agg_bls_pk());
            auto agg_bls_pk_proof = bls::Proto2BlsPopProof(in[i].agg_bls_pk_proof());
            shard_members_ptr->push_back(std::make_shared<common::BftMember>(
                elect_block.shard_network_id(),
                id,
                in[i].pubkey(),
                member_index++,
                in[i].pool_idx_mod_num(),
                *agg_bls_pk,
                *agg_bls_pk_proof));
        }

        return shard_members_ptr;
    }

    static const uint32_t kMaxKeepElectBlockCount = 3u;
    static const uint32_t kMaxCacheElectBlockCount = 7u;
    std::map<uint64_t, std::shared_ptr<HeightMembersItem>, std::less<uint64_t>> height_with_members_[network::kConsensusShardEndNetworkId];
    std::mutex height_with_members_mutex_;
    HeightMembersItemPtr members_ptrs_[network::kConsensusShardEndNetworkId][kMaxKeepElectBlockCount];
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HeightWithElectBlock);
};

}  // namespace elect

}  // namespace shardora
