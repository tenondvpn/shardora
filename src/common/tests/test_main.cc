#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "common/global_info.h"
#include "common/config.h"

int main(int argc, char *argv[]) {
    testing::GTEST_FLAG(output) = "xml:";
    testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    zjchain::common::Config conf;
    conf.Init("../conf/zjchain.conf");
    zjchain::common::GlobalInfo::Instance()->Init(conf);
    int ret = RUN_ALL_TESTS();
    return ret;
}
