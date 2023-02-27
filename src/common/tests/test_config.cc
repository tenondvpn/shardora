#include <gtest/gtest.h>

#include <iostream>

#define private public
#include "common/config.h"

namespace zjchain {

namespace common {

namespace test {

class TestConfig : public testing::Test {
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

TEST_F(TestConfig, NotExistsConf) {
    Config config;
    ASSERT_FALSE(config.Init("../conf.ut/notexist.conf"));
}

TEST_F(TestConfig, UnvalidConf) {
    Config config;
    ASSERT_FALSE(config.Init("../conf.ut/unvalid.conf"));
}

TEST_F(TestConfig, Init) {
    Config config;
    ASSERT_TRUE(config.Init("../conf.ut/bootstrap.conf"));
    std::string val;
    ASSERT_TRUE(config.Get("backbone", "string", val));
    ASSERT_EQ(val, "string");
    ASSERT_TRUE(config.Get("backbone", "string_e", val));
    ASSERT_EQ(val, "./test/string");
    ASSERT_TRUE(config.Get("backbone", "string_s", val));
    ASSERT_EQ(val, "test s  t  ring");

    bool bool_val;
    ASSERT_TRUE(config.Get("backbone", "bool_true", bool_val));
    ASSERT_TRUE(bool_val);
    ASSERT_TRUE(config.Get("backbone", "bool_false", bool_val));
    ASSERT_FALSE(bool_val);

    int16_t int16_val;
    ASSERT_TRUE(config.Get("backbone", "int16", int16_val));
    ASSERT_EQ(int16_val, 1);
    uint16_t uint16_val;
    ASSERT_TRUE(config.Get("backbone", "uint16", uint16_val));
    ASSERT_EQ(uint16_val, 1);

    int32_t int32_val;
    ASSERT_TRUE(config.Get("backbone", "int32", int32_val));
    ASSERT_EQ(int32_val, 1);
    uint32_t uint32_val;
    ASSERT_TRUE(config.Get("backbone", "uint32", uint32_val));
    ASSERT_EQ(uint32_val, 1);
    int64_t int64_val;
    ASSERT_TRUE(config.Get("backbone", "int64", int64_val));
    ASSERT_EQ(int64_val, 1);
    uint64_t uint64_val;
    ASSERT_TRUE(config.Get("backbone", "uint64", uint64_val));
    ASSERT_EQ(uint64_val, 1);
    float float_val;
    ASSERT_TRUE(config.Get("backbone", "float", float_val));
    ASSERT_EQ(float_val, 1.0f);  // problem 
    double double_val;
    ASSERT_TRUE(config.Get("backbone", "double", double_val));
    ASSERT_EQ(double_val, 1.0);  // problem 

    ASSERT_TRUE(config.Set("backbone", "bool_true", true));
    ASSERT_TRUE(config.Set("backbone", "bool_false", false));

    int16_val = 1;
    ASSERT_TRUE(config.Set("backbone", "int16", int16_val));
    uint16_val = 1;
    ASSERT_TRUE(config.Set("backbone", "uint16", uint16_val));

    int32_val = 1;
    ASSERT_TRUE(config.Set("backbone", "int32", int32_val));
    uint32_val = 1;
    ASSERT_TRUE(config.Set("backbone", "uint32", uint32_val));
    int64_val = 1;
    ASSERT_TRUE(config.Set("backbone", "int64", int64_val));
    uint64_val = 1;
    ASSERT_TRUE(config.Set("backbone", "uint64", uint64_val));
    float_val = 1.0f;
    ASSERT_TRUE(config.Set("backbone", "float", float_val));
    double_val = 1.0;
    ASSERT_TRUE(config.Set("backbone", "double", double_val));

    ASSERT_TRUE(config.DumpConfig("../conf.ut/dump.conf"));
}

TEST_F(TestConfig, InitError) {
    Config config;
    ASSERT_TRUE(config.Init("../conf.ut/bootstrap.conf"));
    std::string val;
    ASSERT_TRUE(config.Get("backbone", "string", val));
    ASSERT_EQ(val, "string");
    bool bool_val;
    ASSERT_FALSE(config.Get("backbone", "bool_true_e", bool_val));
    ASSERT_FALSE(config.Get("backbone", "bool_false_e", bool_val));
    int16_t int16_val;
    ASSERT_FALSE(config.Get("backbone", "int16_e", int16_val));
    uint16_t uint16_val;
    ASSERT_FALSE(config.Get("backbone", "uint16_e", uint16_val));
    int32_t int32_val;
    ASSERT_FALSE(config.Get("backbone", "int32_e", int32_val));
    uint32_t uint32_val;
    ASSERT_FALSE(config.Get("backbone", "uint32_e", uint32_val));
    int64_t int64_val;
    ASSERT_FALSE(config.Get("backbone", "int64_e", int64_val));
    uint64_t uint64_val;
    ASSERT_FALSE(config.Get("backbone", "uint64_e", uint64_val));
    float float_val;
    ASSERT_FALSE(config.Get("backbone", "float_e", float_val));
    double double_val;
    ASSERT_FALSE(config.Get("backbone", "double_e", double_val));
}

TEST_F(TestConfig, Set) {
    Config config;
    ASSERT_TRUE(config.Init("../conf.ut/dump.conf"));
    std::string val;
    ASSERT_TRUE(config.Get("backbone", "string", val));
    ASSERT_EQ(val, "string");
    ASSERT_FALSE(config.Get("backbone1", "string", val));
    ASSERT_FALSE(config.Get("backbone", "string1", val));

    bool bool_val;
    ASSERT_TRUE(config.Get("backbone", "bool_true", bool_val));
    ASSERT_FALSE(config.Get("backbone", "bool_true1", bool_val));
    ASSERT_TRUE(bool_val);
    ASSERT_TRUE(config.Get("backbone", "bool_false", bool_val));
    ASSERT_FALSE(bool_val);

    int16_t int16_val;
    ASSERT_TRUE(config.Get("backbone", "int16", int16_val));
    ASSERT_FALSE(config.Get("backbone", "int161", int16_val));
    ASSERT_EQ(int16_val, 1);
    uint16_t uint16_val;
    ASSERT_TRUE(config.Get("backbone", "uint16", uint16_val));
    ASSERT_FALSE(config.Get("backbone", "uint161", uint16_val));
    ASSERT_EQ(uint16_val, 1);

    int32_t int32_val;
    ASSERT_TRUE(config.Get("backbone", "int32", int32_val));
    ASSERT_FALSE(config.Get("backbone", "int321", int32_val));
    ASSERT_EQ(int32_val, 1);
    uint32_t uint32_val;
    ASSERT_TRUE(config.Get("backbone", "uint32", uint32_val));
    ASSERT_FALSE(config.Get("backbone", "uint321", uint32_val));
    ASSERT_EQ(uint32_val, 1);
    int64_t int64_val;
    ASSERT_TRUE(config.Get("backbone", "int64", int64_val));
    ASSERT_FALSE(config.Get("backbone", "int641", int64_val));
    ASSERT_EQ(int64_val, 1);
    uint64_t uint64_val;
    ASSERT_TRUE(config.Get("backbone", "uint64", uint64_val));
    ASSERT_FALSE(config.Get("backbone", "uint641", uint64_val));
    ASSERT_EQ(uint64_val, 1);
    float float_val;
    ASSERT_TRUE(config.Get("backbone", "float", float_val));
    ASSERT_FALSE(config.Get("backbone", "float1", float_val));
    ASSERT_EQ(float_val, 1.0f);  // problem 
    double double_val;
    ASSERT_TRUE(config.Get("backbone", "double", double_val));
    ASSERT_FALSE(config.Get("backbone", "double1", double_val));
    ASSERT_EQ(double_val, 1.0);  // problem 
    ASSERT_TRUE(config.DumpConfig("../conf.ut/dum.conf"));
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
