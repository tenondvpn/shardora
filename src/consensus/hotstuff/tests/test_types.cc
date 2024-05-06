#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <gtest/gtest.h>
#include <protos/view_block.pb.h>

namespace shardora {

namespace hotstuff {

namespace test {

class TestTypes : public testing::Test {
protected:
    void SetUp() {
        
    }

    void TearDown() {
        
    }

    static void CreateQC(std::shared_ptr<QC>& qc) {
        auto fake_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
        qc->bls_agg_sign = fake_sign;
        qc->view = 1;
        qc->view_block_hash = "hash str";        
    }

    static void CreateTC(std::shared_ptr<TC>& tc) {
        auto fake_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
        tc->bls_agg_sign = fake_sign;
        tc->view = 1;
    }

    static std::shared_ptr<ViewBlock> CreateViewBlock() {
        return std::make_shared<hotstuff::ViewBlock>(
                "parent hash",
                GetQCWrappedByGenesis(),
                nullptr,
                0,
                0);
    }
};

TEST_F(TestTypes, QCSerialization) {
    auto qc = GetQCWrappedByGenesis();
    auto qc_str = qc->Serialize();

    auto qc2 = std::make_shared<QC>();
    qc2->Unserialize(qc_str);

    auto qc2_str = qc2->Serialize();

    EXPECT_EQ(qc_str, qc2_str);

    
    qc = std::make_shared<hotstuff::QC>();
    CreateQC(qc);

    qc_str = qc->Serialize();
    
    qc2 = std::make_shared<QC>();
    qc2->Unserialize(qc_str);

    qc2_str = qc2->Serialize();

    EXPECT_EQ(qc_str, qc2_str);
}

TEST_F(TestTypes, TCSerialization) {
    auto tc = std::make_shared<hotstuff::TC>();
    CreateTC(tc);

    auto tc_str = tc->Serialize();
    
    auto tc2 = std::make_shared<TC>();
    tc2->Unserialize(tc_str);

    auto tc2_str = tc2->Serialize();

    EXPECT_EQ(tc_str, tc2_str);
}

TEST_F(TestTypes, ViewBlock2Proto) {
    auto view_block = CreateViewBlock();
    EXPECT_EQ(view_block->DoHash(), view_block->hash);

    view_block::protobuf::ViewBlockItem vb_proto;
    ViewBlock2Proto(view_block, &vb_proto);
    EXPECT_EQ(view_block->DoHash(), vb_proto.hash());

    auto view_block2 = std::make_shared<ViewBlock>();
    Status s = Proto2ViewBlock(vb_proto, view_block2);
    
    EXPECT_EQ(s, Status::kSuccess);
    EXPECT_EQ(view_block2->DoHash(), view_block2->hash);
    EXPECT_EQ(view_block2->DoHash(), view_block->hash);
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

