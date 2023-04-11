#pragma once

#include <memory>
#include <unordered_map>

#include "common/bitmap.h"
#include "bls/bls_dkg.h"
#include "bls/bls_utils.h"
#include "security/security.h"

namespace zjchain {

namespace bls {

class BlsManager {
public:
    BlsManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db);
    ~BlsManager();
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    void SetUsedElectionBlock(
        uint64_t elect_height,
        uint32_t network_id,
        uint32_t member_count,
        const libff::alt_bn128_G2& common_public_key);
    int Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const libff::alt_bn128_G1& g1_hash,
        std::string* sign_x,
        std::string* sign_y);
    int Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& sec_key,
        const libff::alt_bn128_G1& g1_hash,
        libff::alt_bn128_G1* bn_sign);
    int Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& pubkey,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        std::string* verify_hash);
    int GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash);
    static int GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        std::string* verify_hash);
    static int GetLibffHash(const std::string& str_hash, libff::alt_bn128_G1* g1_hash);

//     int AddBlsConsensusInfo(elect::protobuf::ElectBlock& ec_block, common::Bitmap* bitmap);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void HandleFinish(const transport::MessagePtr& msg_ptr);
    void CheckAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        uint32_t member_idx);
    bool VerifyAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& all_signs,
        std::vector<size_t>& idx_vec);
    bool CheckAndVerifyAll(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& all_signs,
        std::vector<size_t>& idx_vec);

    std::shared_ptr<bls::BlsDkg> waiting_bls_{ nullptr };
    uint64_t max_height_{ common::kInvalidUint64 };
    std::unordered_map<uint32_t, BlsFinishItemPtr> finish_networks_map_;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<TimeBlockItem> latest_timeblock_info_ = nullptr;
    uint64_t latest_elect_height_ = 0;

    DISALLOW_COPY_AND_ASSIGN(BlsManager);
};


};  // namespace bls

};  // namespace zjchain
