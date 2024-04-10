#include <consensus/hotstuff/crypto.h>
#include <gtest/gtest.h>

namespace shardora {

namespace hotstuff {

namespace test {

class TestCrypto : public testing::Test {
protected:
    void SetUp() {
        
    }

    void TearDown() {}

    std::shared_ptr<Crypto> crypto_;
};

TEST_F(TestCrypto, Sign_Verify) {
    
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

