#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/view_duration.h>
#include <db/db.h>
#include <gtest/gtest.h>
#include <consensus/hotstuff/pacemaker.h>
#include <security/ecdsa/ecdsa.h>
#include <consensus/hotstuff/tests/test_hotstuff.h>

namespace shardora {
namespace hotstuff {
namespace test {

static const uint32_t pool = 0;
extern std::shared_ptr<QC> GenQC(const View &view,
                                 const HashStr &view_block_hash);

std::shared_ptr<TC> GenTC(const View& view) {
    auto sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::random_element());
    return std::make_shared<TC>(sign, view);
}

class TestPacemaker : public testing::Test {
protected:
    void SetUp() {
        auto security_ptr = std::make_shared<security::Ecdsa>();
        security_ptr->SetPrivateKey(common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
        auto db_ptr = std::make_shared<db::Db>();
        auto bls_manager = std::make_shared<MockBlsManager>();
        auto elect_info = std::make_shared<ElectInfo>(security_ptr, nullptr);
        auto crypto = std::make_shared<Crypto>(pool, elect_info, bls_manager);


        auto view_block_chain = std::make_shared<ViewBlockChain>(db_ptr);
        auto leader_rotation = std::make_shared<LeaderRotation>(pool, view_block_chain, elect_info);
        auto view_duration = std::make_shared<ViewDuration>();
        
        pacemaker_ = std::make_shared<Pacemaker>(pool, crypto, leader_rotation, view_duration);
    }

    void TearDown() {}

    std::shared_ptr<Pacemaker> pacemaker_ = nullptr;
};

TEST_F(TestPacemaker, AdvanceView) {
    auto qc1 = GenQC(View(1), "block hash1");
    auto sync_info = std::make_shared<SyncInfo>();
    pacemaker_->AdvanceView(sync_info->WithQC(qc1));
    EXPECT_EQ(qc1->view+1, pacemaker_->CurView());
    EXPECT_EQ(qc1, pacemaker_->HighQC());

    auto qc2 = GenQC(View(2), "block hash2");
    pacemaker_->AdvanceView(sync_info->WithQC(qc2));
    EXPECT_EQ(qc2->view+1, pacemaker_->CurView());
    EXPECT_EQ(qc2, pacemaker_->HighQC());

    pacemaker_->AdvanceView(sync_info->WithQC(qc1));
    EXPECT_EQ(qc2->view+1, pacemaker_->CurView());
    EXPECT_EQ(qc2, pacemaker_->HighQC());

    auto qc3 = GenQC(View(3), "block hash3");
    auto tc4 = GenTC(View(4));
    pacemaker_->AdvanceView(sync_info->WithQC(qc3)->WithTC(tc4));
    EXPECT_EQ(tc4->view+1, pacemaker_->CurView());
    EXPECT_EQ(qc3, pacemaker_->HighQC());
    EXPECT_EQ(tc4, pacemaker_->HighTC());

    auto tc5 = GenTC(View(5));
    pacemaker_->AdvanceView(sync_info->WithQC(qc1)->WithTC(tc5));
    EXPECT_EQ(tc5->view+1, pacemaker_->CurView());
    EXPECT_EQ(qc3, pacemaker_->HighQC());
    EXPECT_EQ(tc5, pacemaker_->HighTC());
}
        
}
}
}
