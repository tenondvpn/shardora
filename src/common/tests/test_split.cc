#include <gtest/gtest.h>

#include <iostream>

#define private public
#include "common/split.h"

namespace zjchain {

namespace common {

namespace test {

class TestSplit : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestSplit, StrSplit) {
    std::string test_str("Hello world!    dddd  ddd");
    Split<> line_split(test_str.c_str(), ' ');
    ASSERT_EQ(line_split.Count(), 8);
    ASSERT_EQ(line_split[0], std::string("Hello"));
    ASSERT_EQ(line_split[1], std::string("world!"));
    ASSERT_EQ(line_split[2], std::string(""));
    ASSERT_EQ(line_split[3], std::string(""));
    ASSERT_EQ(line_split[4], std::string(""));
    ASSERT_EQ(line_split[5], std::string("dddd"));
    ASSERT_EQ(line_split[6], std::string(""));
    ASSERT_EQ(line_split[7], std::string("ddd"));

    ASSERT_EQ(line_split.SubLen(0), strlen("Hello"));
    ASSERT_EQ(line_split.SubLen(1), strlen("world!"));
    ASSERT_EQ(line_split.SubLen(2), strlen(""));
    ASSERT_EQ(line_split.SubLen(3), strlen(""));
    ASSERT_EQ(line_split.SubLen(4), strlen(""));
    ASSERT_EQ(line_split.SubLen(5), strlen("dddd"));
    ASSERT_EQ(line_split.SubLen(6), strlen(""));
    ASSERT_EQ(line_split.SubLen(7), strlen("ddd"));
}

TEST_F(TestSplit, SplitWithLen) {
    std::string test_str("Hello world!    dddd  ddd");
    Split<> line_split(test_str.c_str(), ' ', test_str.size());
    ASSERT_EQ(line_split.Count(), 8);
    ASSERT_EQ(line_split[0], std::string("Hello"));
    ASSERT_EQ(line_split[1], std::string("world!"));
    ASSERT_EQ(line_split[2], std::string(""));
    ASSERT_EQ(line_split[3], std::string(""));
    ASSERT_EQ(line_split[4], std::string(""));
    ASSERT_EQ(line_split[5], std::string("dddd"));
    ASSERT_EQ(line_split[6], std::string(""));
    ASSERT_EQ(line_split[7], std::string("ddd"));

    ASSERT_EQ(line_split.SubLen(0), strlen("Hello"));
    ASSERT_EQ(line_split.SubLen(1), strlen("world!"));
    ASSERT_EQ(line_split.SubLen(2), strlen(""));
    ASSERT_EQ(line_split.SubLen(3), strlen(""));
    ASSERT_EQ(line_split.SubLen(4), strlen(""));
    ASSERT_EQ(line_split.SubLen(5), strlen("dddd"));
    ASSERT_EQ(line_split.SubLen(6), strlen(""));
    ASSERT_EQ(line_split.SubLen(7), strlen("ddd"));
}

TEST_F(TestSplit, SplitWithBadChar) {
    std::string test_str("Hel\t\nlo$world!\0$$$$dd\0dd$$ddd", 29);
    Split<> line_split(test_str.c_str(), '$', test_str.size());
    ASSERT_EQ(line_split.Count(), 8);
    ASSERT_EQ(line_split[0], std::string("Hel\t\nlo"));
    ASSERT_EQ(std::string(line_split[1], line_split.SubLen(1)), std::string("world!\0", 7));
    ASSERT_EQ(line_split[2], std::string(""));
    ASSERT_EQ(line_split[3], std::string(""));
    ASSERT_EQ(line_split[4], std::string(""));
    ASSERT_EQ(std::string(line_split[5], line_split.SubLen(5)), std::string("dd\0dd", 5));
    ASSERT_EQ(line_split[6], std::string(""));
    ASSERT_EQ(line_split[7], std::string("ddd"));

    ASSERT_EQ(line_split.SubLen(0), strlen("Hel\t\nlo"));
    ASSERT_EQ(line_split.SubLen(1), strlen("world!") + 1);
    ASSERT_EQ(line_split.SubLen(2), strlen(""));
    ASSERT_EQ(line_split.SubLen(3), strlen(""));
    ASSERT_EQ(line_split.SubLen(4), strlen(""));
    ASSERT_EQ(line_split.SubLen(5), strlen("dddd") + 1);
    ASSERT_EQ(line_split.SubLen(6), strlen(""));
    ASSERT_EQ(line_split.SubLen(7), strlen("ddd"));
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
