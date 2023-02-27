#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/ip.h"

namespace zjchain {

namespace common {

namespace test {

class TestIp : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:

};

TEST_F(TestIp, AddAndContainClear) {
    ASSERT_EQ(Ip::Instance()->GetIpCountry("82.156.224.174"), "China");
    ASSERT_EQ(Ip::Instance()->GetIpCountryIsoCode("82.156.224.174"), "CN");
    float lat = 0.0f;
    float lon = 0.0f;
    ASSERT_EQ(Ip::Instance()->GetIpLocation("82.156.224.174", &lat, &lon), 0);
    std::cout << lat << ", " << lon << ", " << abs(lat - 34.773200) << ", " << abs(lon - 113.722000) << std::endl;
    ASSERT_TRUE(abs(lat - 34.773200f) <= 0.00001);
    ASSERT_TRUE(abs(lon - 113.722000f) <= 0.00001);
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
