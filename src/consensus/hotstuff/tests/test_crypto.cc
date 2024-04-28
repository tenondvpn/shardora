#include <bls/bls_dkg.h>
#include <bls/bls_manager.h>
#include <bls/bls_utils.h>
#include <common/global_info.h>
#include <common/node_members.h>
#include <common/utils.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <network/network_utils.h>
#include <security/ecdsa/ecdsa.h>
#include <security/security.h>
#include <consensus/hotstuff/tests/test_hotstuff.h>

// #define HOTSTUFF_TEST 1

namespace shardora {

namespace hotstuff {

namespace test {

extern std::shared_ptr<ViewBlock> GenViewBlock(const HashStr &parent_hash, const View &view);
extern std::shared_ptr<QC> GenQC(const View &view, const HashStr &view_block_hash);
extern std::shared_ptr<block::protobuf::Block> GenBlock();
extern uint32_t GenLeaderIdx();

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;


static std::shared_ptr<security::Security> security_ptr = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static std::shared_ptr<MockBlsManager> bls_manager = nullptr;
static const uint32_t sharding_id = network::kConsensusShardBeginNetworkId;

class TestCrypto : public testing::Test {
protected:
    std::shared_ptr<Crypto> crypto_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    
    void SetUp() {
        common::GlobalInfo::Instance()->set_network_id(sharding_id);
        security_ptr = std::make_shared<security::Ecdsa>();
        security_ptr->SetPrivateKey(common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
        db_ptr = std::make_shared<db::Db>();
        bls_manager = std::make_shared<MockBlsManager>();
        elect_info_ = std::make_shared<ElectInfo>(security_ptr, nullptr);
        crypto_ = std::make_shared<Crypto>(0, elect_info_, bls_manager);

        auto member = std::make_shared<common::BftMember>(1, "1", "pk1", 1, 0);
        auto members = std::make_shared<common::Members>();
        members->push_back(member);
        auto common_pk = libff::alt_bn128_G2::one();
        auto sk = libff::alt_bn128_Fr::one();
        elect_info_->OnNewElectBlock(sharding_id, 1, members, common_pk, sk);        
    }

    void TearDown() {}
};

TEST_F(TestCrypto, Sign_Verify) {
    uint32_t n = 10;
    auto members = std::make_shared<common::Members>();
    uint64_t elect_height = 2;

    for (uint32_t i = 0; i < n; i++) {
        auto member = std::make_shared<common::BftMember>(i, "1", "pk", i, i);
        members->push_back(member);
    }
    
    auto common_pk = libff::alt_bn128_G2::one();
    auto sk = libff::alt_bn128_Fr::one();
    elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sk);
    uint32_t t = elect_info_->GetElectItem()->t();

    auto sign = libff::alt_bn128_G1::one();
    sign.to_affine_coordinates();
    std::string x = libBLS::ThresholdUtils::fieldElementToString(sign.X);
    std::string y = libBLS::ThresholdUtils::fieldElementToString(sign.Y);
        
    EXPECT_CALL(*bls_manager, Sign(_, _, _, _, _, _))
        .WillRepeatedly(Invoke([&x, &y](uint32_t t, uint32_t n, const libff::alt_bn128_Fr& local_sec_key, const libff::alt_bn128_G1& g1_hash, std::string* sign_x, std::string* sign_y) {
            *sign_x = x;
            *sign_y = y;
            return bls::kBlsSuccess;
        }));

    EXPECT_CALL(*bls_manager, GetVerifyHash(_, _, _, _, _))
        .WillRepeatedly(Invoke([](uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& g1_hash,
                const libff::alt_bn128_G2& pkey,
                std::string* verify_hash
                ) {
            *verify_hash = "test_hash";
            return bls::kBlsSuccess;
        }));
    EXPECT_CALL(*bls_manager, GetVerifyHash(_, _, _, _))
        .WillRepeatedly(Invoke([](uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& sign,
                std::string* verify_hash
                ) {
            *verify_hash = "test_hash";
            return bls::kBlsSuccess;
        }));

    EXPECT_CALL(*bls_manager, GetLibffHash(_, _))
        .WillRepeatedly(Invoke([](const std::string& str_hash, libff::alt_bn128_G1* g1_hash) {
            *g1_hash = libff::alt_bn128_G1::one();
            return bls::kBlsSuccess;
        }));    

    std::string sign_x;
    std::string sign_y;
    std::string msg = "msg";
    HashStr msg_hash = common::Hash::keccak256(msg);
     
    Status s = crypto_->PartialSign(elect_height, msg_hash, &sign_x, &sign_y);
    EXPECT_EQ(Status::kSuccess, s);
    EXPECT_EQ(x, sign_x);
    EXPECT_EQ(y, sign_y);

    View old_view = 1;
    View view = 2;

    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;

    for (uint32_t i = 0; i < t-1; i++) {
        s = crypto_->ReconstructAndVerifyThresSign(elect_height, view, msg_hash, i, sign_x, sign_y, reconstructed_sign);
        EXPECT_FALSE(s == Status::kSuccess);
        EXPECT_TRUE(reconstructed_sign == nullptr);
    }

    s = crypto_->ReconstructAndVerifyThresSign(elect_height, view, msg_hash, t-1, sign_x, sign_y, reconstructed_sign);
    EXPECT_TRUE(s == Status::kSuccess);
    EXPECT_TRUE(reconstructed_sign != nullptr);

    // old view not valid
    s = crypto_->ReconstructAndVerifyThresSign(elect_height, old_view, msg_hash, 0, sign_x, sign_y, reconstructed_sign);
    EXPECT_TRUE(s == Status::kInvalidArgument);
}

TEST_F(TestCrypto, Sign_Verify_Change_Epoch) {
    uint32_t n = 10;
    auto members = std::make_shared<common::Members>();
    uint64_t elect_height = 2;

    for (uint32_t i = 0; i < n; i++) {
        auto member = std::make_shared<common::BftMember>(i, "1", "pk", i, i);
        members->push_back(member);
    }
    
    auto common_pk = libff::alt_bn128_G2::one();
    auto sk = libff::alt_bn128_Fr::one();
    elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sk);
    uint32_t t = elect_info_->GetElectItem()->t();

    auto sign = libff::alt_bn128_G1::one();
    sign.to_affine_coordinates();
    std::string x = libBLS::ThresholdUtils::fieldElementToString(sign.X);
    std::string y = libBLS::ThresholdUtils::fieldElementToString(sign.Y);
        
    EXPECT_CALL(*bls_manager, Sign(_, _, _, _, _, _))
        .WillRepeatedly(Invoke([&x, &y](uint32_t t, uint32_t n, const libff::alt_bn128_Fr& local_sec_key, const libff::alt_bn128_G1& g1_hash, std::string* sign_x, std::string* sign_y) {
            *sign_x = x;
            *sign_y = y;
            return bls::kBlsSuccess;
        }));

    EXPECT_CALL(*bls_manager, GetVerifyHash(_, _, _, _, _))
        .WillRepeatedly(Invoke([](uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& g1_hash,
                const libff::alt_bn128_G2& pkey,
                std::string* verify_hash
                ) {
            *verify_hash = "test_hash";
            return bls::kBlsSuccess;
        }));
    EXPECT_CALL(*bls_manager, GetVerifyHash(_, _, _, _))
        .WillRepeatedly(Invoke([](uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& sign,
                std::string* verify_hash
                ) {
            *verify_hash = "test_hash";
            return bls::kBlsSuccess;
        }));

    EXPECT_CALL(*bls_manager, GetLibffHash(_, _))
        .WillRepeatedly(Invoke([](const std::string& str_hash, libff::alt_bn128_G1* g1_hash) {
            *g1_hash = libff::alt_bn128_G1::one();
            return bls::kBlsSuccess;
        }));    

    std::string sign_x;
    std::string sign_y;
    std::string msg = "msg";
    HashStr msg_hash = common::Hash::keccak256(msg);
     
    Status s = crypto_->PartialSign(elect_height, msg_hash, &sign_x, &sign_y);
    EXPECT_EQ(Status::kSuccess, s);
    EXPECT_EQ(x, sign_x);
    EXPECT_EQ(y, sign_y);

    View old_view = 1;
    View view = 2;

    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;

    for (uint32_t i = 0; i < t-3; i++) {
        s = crypto_->ReconstructAndVerifyThresSign(elect_height, view, msg_hash, i, sign_x, sign_y, reconstructed_sign);
        EXPECT_FALSE(s == Status::kSuccess);
        EXPECT_TRUE(reconstructed_sign == nullptr);
    }
    
    elect_info_->OnNewElectBlock(sharding_id, elect_height+1, members, common_pk, sk);
    
    for (uint32_t i = t-3; i < t-1; i++) {
        s = crypto_->ReconstructAndVerifyThresSign(elect_height, view, msg_hash, i, sign_x, sign_y, reconstructed_sign);
        EXPECT_FALSE(s == Status::kSuccess);
        EXPECT_TRUE(reconstructed_sign == nullptr);
    }
    
    s = crypto_->ReconstructAndVerifyThresSign(elect_height, view, msg_hash, t-1, sign_x, sign_y, reconstructed_sign);
    EXPECT_TRUE(s == Status::kSuccess);
    EXPECT_TRUE(reconstructed_sign != nullptr);
}


TEST_F(TestCrypto, GetElectItem) {
    auto elect_item = crypto_->GetElectItem(1);

    EXPECT_TRUE(elect_item != nullptr);
    EXPECT_EQ(1, elect_item->ElectHeight());
    EXPECT_EQ("pk1", (*elect_item->Members())[0]->pubkey);

    auto member = std::make_shared<common::BftMember>(2, "2", "pk2", 2, 0);
    auto members = std::make_shared<common::Members>();
    members->push_back(member);
    auto common_pk = libff::alt_bn128_G2::one();
    auto sk = libff::alt_bn128_Fr::one();
    elect_info_->OnNewElectBlock(sharding_id, 2, members, common_pk, sk);

    auto elect_item2 = crypto_->GetElectItem(2);
    EXPECT_TRUE(elect_item2 != nullptr);
    EXPECT_EQ(2, elect_item2->ElectHeight());
    EXPECT_EQ("pk2", (*elect_item2->Members())[0]->pubkey);

    auto elect_item1 = crypto_->GetElectItem(1);
    EXPECT_TRUE(elect_item1 != nullptr);
    EXPECT_EQ(1, elect_item1->ElectHeight());
    EXPECT_EQ("pk1", (*elect_item1->Members())[0]->pubkey);
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

