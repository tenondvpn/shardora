#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <gtest/gtest.h>

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
};

TEST_F(TestTypes, QCSerialization) {
    auto qc = GetGenesisQC();
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

} // namespace test

} // namespace hotstuff

} // namespace shardora

