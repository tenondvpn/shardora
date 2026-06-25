#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "common/global_info.h"
#include "common/config.h"

int main(int argc, char *argv[]) {
    testing::GTEST_FLAG(output) = "xml:";
    testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    seth::common::Config conf;
    conf.Init("../conf/sethhain.conf");
    seth::common::GlobalInfo::Instance()->Init(conf);
    int ret = RUN_ALL_TESTS();
    return ret;
}
