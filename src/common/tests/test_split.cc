#include <gtest/gtest.h>

#include <iostream>

#define private public
#include "common/split.h"

namespace shardora {

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

TEST_F(TestSplit, EmptyInputAndOutOfRangeAccess) {
    Split<> empty("", ',');
    ASSERT_EQ(empty.Count(), 0u);
    ASSERT_EQ(empty[0], nullptr);
    ASSERT_EQ(empty.SubLen(0), -1);
}

TEST_F(TestSplit, MaxSplitLimitStopsParsingFurtherDelimiters) {
    // kMaxSplitNum=3 means at most 4 chunks are tracked.
    Split<3> s("a,b,c,d,e", ',');
    ASSERT_EQ(s.Count(), 4u);
    ASSERT_EQ(std::string(s[0]), "a");
    ASSERT_EQ(std::string(s[1]), "b");
    ASSERT_EQ(std::string(s[2]), "c");
    // parser breaks when delimiter count exceeds limit at next delimiter.
    ASSERT_EQ(std::string(s[3]), "d");
}

TEST_F(TestSplit, ExplicitLenZeroBehavesAsCStringLength) {
    const char* text = "x|y|z";
    Split<> s(text, '|', 0);
    ASSERT_EQ(s.Count(), 3u);
    ASSERT_EQ(std::string(s[0]), "x");
    ASSERT_EQ(std::string(s[1]), "y");
    ASSERT_EQ(std::string(s[2]), "z");
}

TEST_F(TestSplit, NullInputWithExplicitLengthProducesZeroCount) {
    Split<> s(nullptr, ',', 1);
    ASSERT_EQ(s.Count(), 0u);
    ASSERT_EQ(s[0], nullptr);
    ASSERT_EQ(s.SubLen(0), -1);
}

TEST_F(TestSplit, ExplicitLengthTruncatesInput) {
    Split<> s("left,right", ',', 4);  // only "left" is parsed
    ASSERT_EQ(s.Count(), 1u);
    ASSERT_EQ(std::string(s[0]), "left");
}

}  // namespace test

}  // namespace common

}  // namespace shardora
