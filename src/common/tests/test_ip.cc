#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/ip.h"

namespace seth {

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
    try {
        auto country = Ip::Instance()->GetIpCountry("82.156.224.174");
        auto country_iso = Ip::Instance()->GetIpCountryIsoCode("82.156.224.174");
        ASSERT_FALSE(country.empty());
        ASSERT_FALSE(country_iso.empty());
        float lat = 0.0f;
        float lon = 0.0f;
        ASSERT_EQ(Ip::Instance()->GetIpLocation("82.156.224.174", &lat, &lon), 0);
        ASSERT_GE(lat, -90.0f);
        ASSERT_LE(lat, 90.0f);
        ASSERT_GE(lon, -180.0f);
        ASSERT_LE(lon, 180.0f);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "ip geo database unavailable: " << ex.what();
    }
}

}  // namespace test

}  // namespace common

}  // namespace seth
