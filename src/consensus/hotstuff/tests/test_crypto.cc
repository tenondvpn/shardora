#include <bls/bls_dkg.h>
#include <common/node_members.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <gtest/gtest.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <network/network_utils.h>
#include <security/ecdsa/ecdsa.h>
#include <security/security.h>

namespace shardora {

namespace hotstuff {

namespace test {

static std::shared_ptr<security::Security> security_ptr = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static std::shared_ptr<bls::BlsManager> bls_manager = nullptr;
static const uint32_t sharding_id = network::kConsensusShardBeginNetworkId;

class TestCrypto : public testing::Test {
protected:
    std::shared_ptr<Crypto> crypto_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    
    void SetUp() {
        security_ptr = std::make_shared<security::Ecdsa>();
        security_ptr->SetPrivateKey(common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
        db_ptr = std::make_shared<db::Db>();
        bls_manager = std::make_shared<bls::BlsManager>(security_ptr, db_ptr);
        elect_info_ = std::make_shared<ElectInfo>(security_ptr);
        crypto_ = std::make_shared<Crypto>(elect_info_, bls_manager);

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
    // tested in test_bls.cc
}

TEST_F(TestCrypto, GetElectItem) {
    auto elect_item = elect_info_->GetElectItem(1);

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

