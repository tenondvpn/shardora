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
};

TEST_F(TestTypes, QC) {
    auto qc = GetGenesisQC();
    auto qc_str = qc->Serialize();

    auto qc2 = std::make_shared<QC>();
    qc2->Unserialize(qc_str);

    auto qc2_str = qc2->Serialize();

    EXPECT_EQ(qc_str, qc2_str);
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

