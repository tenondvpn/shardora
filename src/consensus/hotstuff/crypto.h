#pragma once
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <security/security.h>
#include <consensus/hotstuff/elect_info.h>
#include <transport/transport_utils.h>
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

// Every pool has a Crypto
class Crypto {
public:
    struct BlsCollectionItem {
        HashStr msg_hash;
        common::Bitmap ok_bitmap{ common::kEachShardMaxNodeCount };
        std::shared_ptr<libff::alt_bn128_G1> partial_signs[common::kEachShardMaxNodeCount];
        std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;

        inline uint32_t OkCount() const {
            return ok_bitmap.valid_count();
        }    
    };
    // Bls vote collection
    struct BlsCollection {
        BlsCollection() : view(0), handled(false), count(0), max_hash_count(0) {
        }

        std::shared_ptr<BlsCollectionItem> GetItem(const HashStr& msg_hash, uint32_t index) {
            if (!index_with_hash[index].empty()) {
                ZJC_INFO("new hash coming index: %d, %s, %s", 
                    index, 
                    common::Encode::HexEncode(msg_hash).c_str(),
                    common::Encode::HexEncode(index_with_hash[index]).c_str());
                // assert(msg_hash == index_with_hash[index]);
                auto it = msg_collection_map.find(index_with_hash[index]);
                return it->second;
            }
            
            ++count;
            index_with_hash[index] = msg_hash;
            std::shared_ptr<BlsCollectionItem> collection_item = nullptr;
            auto it = msg_collection_map.find(msg_hash);
            if (it == msg_collection_map.end()) {
                collection_item = std::make_shared<BlsCollectionItem>();
                collection_item->msg_hash = msg_hash;
                msg_collection_map[msg_hash] = collection_item;
            } else {
                collection_item = it->second;
            }
    
            collection_item->ok_bitmap.Set(index);
            if (max_hash_count < collection_item->OkCount()) {
                max_hash_count = collection_item->OkCount();
            }

            ZJC_INFO("hash: %s, all count: %u, ok count: %d, index: %d",
                common::Encode::HexEncode(msg_hash).c_str(), 
                count, 
                collection_item->OkCount(), 
                index);
            return collection_item;
        }

        uint32_t invalid_diff_count() {
            return count - max_hash_count;
        }

        View view;
        // may receive different msg_hashs per view because of unexpected inconsistency.
        std::unordered_map<HashStr, std::shared_ptr<BlsCollectionItem>> msg_collection_map;
        HashStr index_with_hash[1024];
        bool handled;
        uint32_t count;
        uint32_t max_hash_count;
    };
    
    Crypto(
            const uint32_t& pool_idx,
            const std::shared_ptr<ElectInfo>& elect_info,
            const std::shared_ptr<bls::IBlsManager>& bls_mgr) :
            pool_idx_(pool_idx), elect_info_(elect_info), bls_mgr_(bls_mgr) {
        LoadInitGenesisCommonPk();
    };

    ~Crypto() {};

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    Status PartialSign(
            uint32_t sharding_id,
            uint64_t elect_height,
            const HashStr& msg_hash,
            std::string* sign_x,
            std::string* sign_y);
    
    Status ReconstructAndVerifyThresSign(
            const transport::MessagePtr& msg_ptr,
            uint64_t elect_height,
            View view,
            const HashStr& msg_hash,
            uint32_t member_idx,
            const std::string& partial_sign_x,
            const std::string& partial_sign_y,
            std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign);
    Status VerifyQC(
            uint32_t sharding_id,
            const QC& qc);
    Status VerifyTC(
            uint32_t sharding_id,
            const TC& tc);    
    Status SignMessage(transport::MessagePtr& msg_ptr);
    Status VerifyMessage(const transport::MessagePtr& msg_ptr);
    void RecoverBlsCollection() {
        bls_collection_ = nullptr;
    }
    
    inline std::shared_ptr<ElectItem> GetElectItem(uint32_t sharding_id, uint64_t elect_height) {
        auto item = elect_info_->GetElectItem(sharding_id, elect_height);
        if (item != nullptr) {
            return item;
        }

        if (genesis_elect_items_[sharding_id] != nullptr && 
                genesis_elect_items_[sharding_id]->ElectHeight() == elect_height) {
            return genesis_elect_items_[sharding_id];
        }

        return nullptr;
    }

    inline std::shared_ptr<ElectItem> GetLatestElectItem(uint32_t sharding_id) {
        return elect_info_->GetElectItemWithShardingId(sharding_id);
    }
    
    inline std::shared_ptr<security::Security> security() const {
        return bls_mgr_->security();
    }

    // std::string serializedPartialSigns(uint64_t elect_height, const HashStr& msg_hash) {
    //     std::string ret = "";
    //     auto elect_item = GetElectItem(common::GlobalInfo::Instance()->network_id(), elect_height);
    //     if (!elect_item) {
    //         return ret;
    //     }        

    //     auto partial_signs = bls_collection_item(msg_hash)->partial_signs;
    //     for (uint32_t i = 0; i < elect_item->n(); i++) {
    //         auto sign = partial_signs[i];
    //         if (!sign) {
    //             continue;
    //         }
    //         ret += serializedSign(*sign);
    //     }

    //     return ret;
    // }

    std::string serializedSign(const libff::alt_bn128_G1& sign) {
        auto x = libBLS::ThresholdUtils::fieldElementToString(sign.X);
        auto y = libBLS::ThresholdUtils::fieldElementToString(sign.Y);
        return "("+x+","+y+")";
    }

    // std::shared_ptr<BlsCollectionItem> bls_collection_item(const HashStr& msg_hash) {
    //     return bls_collection_->GetItem(msg_hash);
    // }    
    
    Status VerifyThresSign(
        uint32_t sharding_id,
        uint64_t elect_height,
        const HashStr& msg_hash,
        const libff::alt_bn128_G1& reconstructed_sign);

private:
    
    void GetG1Hash(const HashStr& msg_hash, libff::alt_bn128_G1* g1_hash) {
        bls_mgr_->GetLibffHash(msg_hash, g1_hash);
    }

    Status GetVerifyHashA(
            uint32_t sharding_id, 
            uint64_t elect_height, 
            const HashStr& msg_hash, 
            std::string* verify_hash) {
        auto elect_item = GetElectItem(sharding_id, elect_height);
        if (!elect_item || elect_item->common_pk() == libff::alt_bn128_G2::zero()) {
            ZJC_ERROR("elect_item not found, elect_height: %lu", elect_height);
            return Status::kElectItemNotFound;
        }

        libff::alt_bn128_G1 g1_hash;
        GetG1Hash(msg_hash, &g1_hash);
        if (bls_mgr_->GetVerifyHash(
                    elect_item->t(),
                    elect_item->n(),
                    g1_hash,
                    elect_item->common_pk(),
                    verify_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("get verify hash a failed!");
            return Status::kError;
        }

        return Status::kSuccess;
    }

    Status GetVerifyHashB(
            uint32_t sharding_id,
            uint64_t elect_height, 
            const libff::alt_bn128_G1& reconstructed_sign, 
            std::string* verify_hash) {
        auto elect_item = GetElectItem(sharding_id, elect_height);
        if (!elect_item) {
            return Status::kError;
        }

        if (bls_mgr_->GetVerifyHash(
                    elect_item->t(),
                    elect_item->n(),
                    reconstructed_sign,
                    verify_hash) != bls::kBlsSuccess) {
            elect_item->common_pk().to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(elect_item->common_pk());
            auto cpk_strs = cpk->toString();
            auto agg_sign_str = libBLS::ThresholdUtils::fieldElementToString(
                reconstructed_sign.X);
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, sign x: %s",
                elect_item->t(), elect_item->n(), cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height,
                agg_sign_str.c_str());
            return Status::kError;
        }

        return Status::kSuccess;
    }

    void LoadInitGenesisCommonPk() {
        FILE* fd = fopen("./conf/bls_pk", "r");
        if (fd == NULL) {
            return;
        }

        char data[1024*1024] = {0};
        fread(data, 1, sizeof(data), fd);
        fclose(fd);
        nlohmann::json bls_pk_json = nlohmann::json::parse(data);
        for (auto item: bls_pk_json) {
            uint32_t shard_id = item["shard_id"];
            if (shard_id >= network::kConsensusShardEndNetworkId) {
                assert(false);
                return;
            }

            std::vector<std::string> pkey_str = {
                item["x_c0"],
                item["x_c1"],
                item["y_c0"],
                item["y_c1"]
            };

            BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
            genesis_elect_items_[shard_id] = std::make_shared<ElectItem>(
                shard_id, item["prev_height"], item["n"], *pkey.getPublicKey());
            ZJC_DEBUG("success load genesis item: %s", item.dump().c_str());
        }
    }

    // 保留上一次 elect_item，避免 epoch 切换的影响
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<bls::IBlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<BlsCollection> bls_collection_ = nullptr;
    std::shared_ptr<ElectItem> genesis_elect_items_[network::kConsensusShardEndNetworkId] = { nullptr };
};

} // namespace consensus

} // namespace shardora


