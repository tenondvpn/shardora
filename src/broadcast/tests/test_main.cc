#include <gtest/gtest.h>
#include <gmock/gmock.h>

int main(int argc, char *argv[]) {
    testing::GTEST_FLAG(output) = "xml:";
    testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
