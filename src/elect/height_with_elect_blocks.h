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
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "protos/elect.pb.h"
#include "protos/get_proto_hash.h"
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
        }
        ZJC_DEBUG("1 save bls pk and secret key success.height: %lu, network_id: %u",
            height, network_id);
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
            network_id -= network::kConsensusWaitingShardOffset;
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

        auto iter = height_with_members_[network_id].find(height);
        if (iter != height_with_members_[network_id].end()) {
            if (iter->second->local_sec_key == libff::alt_bn128_Fr::zero()) {
                std::string bls_prikey;
                if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
                    iter->second->local_sec_key = libff::alt_bn128_Fr(bls_prikey.c_str());
                }
            }

            assert(iter->second->common_bls_publick_key != libff::alt_bn128_G2::zero());
            if (common_pk != nullptr) {
                *common_pk = iter->second->common_bls_publick_key;
            }

            if (local_sec_key != nullptr) {
                *local_sec_key = iter->second->local_sec_key;
            }

            return iter->second->members_ptr;
        }

        auto shard_members = GetMembers(security, network_id, height);
        if (shard_members == nullptr) {
            return nullptr;
        }

        auto new_item = std::make_shared<HeightMembersItem>(shard_members, height);
        new_item->common_bls_publick_key = GetCommonPublicKey(network_id, height);
        if (new_item->common_bls_publick_key == libff::alt_bn128_G2::zero()) {
            return nullptr;
        }

        height_with_members_[network_id][height] = new_item;
        std::string bls_prikey;
        if (prefix_db_->GetBlsPrikey(security_ptr_, height, network_id, &bls_prikey)) {
            new_item->local_sec_key = libff::alt_bn128_Fr(bls_prikey.c_str());
        }

        if (common_pk != nullptr) {
            *common_pk = new_item->common_bls_publick_key;
        }

        if (local_sec_key != nullptr) {
            *local_sec_key = new_item->local_sec_key;
        }

        if (height_with_members_[network_id].size() >= kMaxCacheElectBlockCount) {
            height_with_members_[network_id].erase(height_with_members_[network_id].begin());
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
        block::protobuf::Block block;
        if (!prefix_db_->GetBlockWithHeight(
                network::kRootCongressNetworkId,
                network_id % common::kImmutablePoolSize,
                height,
                &block)) {
            ZJC_INFO("failed get block with height net: %u, pool: %u, height: %lu",
                network::kRootCongressNetworkId, network_id, height);
            //             assert(false);
            return nullptr;
        }

        bool eb_valid = false;
        elect::protobuf::ElectBlock elect_block;
        for (int32_t tx_idx = 0; tx_idx < block.tx_list_size(); ++tx_idx) {
            if (block.tx_list(tx_idx).step() != pools::protobuf::kConsensusRootElectShard) {
                continue;
            }

            for (int32_t i = 0; i < block.tx_list(tx_idx).storages_size(); ++i) {
                if (block.tx_list(tx_idx).storages(i).key() == protos::kElectNodeAttrElectBlock) {
                    std::string val;
                    if (!prefix_db_->GetTemporaryKv(block.tx_list(0).storages(i).val_hash(), &val)) {
                        ZJC_FATAL("elect block get temp kv from db failed!");
                        return nullptr;
                    }

                    if (!elect_block.ParseFromString(val)) {
                        assert(false);
                        return nullptr;
                    }

                    std::string ec_hash = protos::GetElectBlockHash(elect_block);
                    if (ec_hash != block.tx_list(tx_idx).storages(i).val_hash()) {
                        ZJC_FATAL("elect block get temp kv from db failed!");
                        return nullptr;
                    }

                    eb_valid = true;
                    break;
                }
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
                in[i].pool_idx_mod_num()));
        }

        return shard_members_ptr;
    }

    static const uint32_t kMaxKeepElectBlockCount = 3u;
    static const uint32_t kMaxCacheElectBlockCount = 7u;
    std::map<uint64_t, std::shared_ptr<HeightMembersItem>, std::less<uint64_t>> height_with_members_[network::kConsensusShardEndNetworkId];
    HeightMembersItemPtr members_ptrs_[network::kConsensusShardEndNetworkId][kMaxKeepElectBlockCount];
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HeightWithElectBlock);
};

}  // namespace elect

}  // namespace zjchain
