#include <gtest/gtest.h>
#include "bls/agg_bls.h"
#define private public

namespace shardora {

namespace bls {
    
namespace test {

class TestAggBls : public testing::Test {
protected:
    std::shared_ptr<AggBls> agg_bls_ = nullptr;

    void SetUp() {
        agg_bls_ = std::make_shared<AggBls>();
    }
};

} // namespace test

} // namespace bls

} // namespace shardora
        
