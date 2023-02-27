#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "common/string_utils.h"

namespace zjchain {

namespace common {

namespace test {

class TestStringUtils : public testing::Test {
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

TEST_F(TestStringUtils, ToFloat) {
//     const char* value = NULL;
//     try {
//         ASSERT_FALSE(StringUtil::ToFloat(value));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
// 
//     std::string min_str = std::to_string((std::numeric_limits<long>::min)());
//     min_str += min_str;
//     try {
//         ASSERT_FALSE(StringUtil::ToFloat(min_str));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
}

TEST_F(TestStringUtils, ToDouble) {
//     const char* value = NULL;
//     try {
//         ASSERT_FALSE(StringUtil::ToDouble(value));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
// 
//     std::string min_str = std::to_string((std::numeric_limits<long>::min)());
//     min_str += min_str;
//     try {
//         ASSERT_FALSE(StringUtil::ToDouble(min_str.c_str()));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
}

TEST_F(TestStringUtils, ToBool) {
//     const char* svalue = "1";
//     ASSERT_TRUE(StringUtil::ToBool(svalue));
//     svalue = "0";
//     ASSERT_FALSE(StringUtil::ToBool(svalue));
//     svalue = "true";
//     try {
//         ASSERT_FALSE(StringUtil::ToBool(svalue));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
//     svalue = "false";
//     try {
//         ASSERT_FALSE(StringUtil::ToBool(svalue));
//         ASSERT_FALSE(true);
//     } catch (ConvertException& ex) {
//         ASSERT_TRUE(true);
//     }
}
/*
TEST_F(TestStringUtils, StringToNum) {
    const char* value = "1234";
    ASSERT_EQ(StringUtil::ToInt16(value), 1234);
    ASSERT_EQ(StringUtil::ToUint16(value), 1234);
    ASSERT_EQ(StringUtil::ToInt32(value), 1234);
    ASSERT_EQ(StringUtil::ToUint32(value), 1234);
    ASSERT_EQ(StringUtil::ToInt64(value), 1234);
    ASSERT_EQ(StringUtil::ToUint64(value), 1234);
}

TEST_F(TestStringUtils, StringToNum6) {
    char value[5];
    memcpy(value, "1234", 4);
    value[4] = '\0';
    ASSERT_EQ(StringUtil::ToInt16(value), 1234);
    ASSERT_EQ(StringUtil::ToInt16(value), 1234);
    ASSERT_EQ(StringUtil::ToUint16(value), 1234);
    ASSERT_EQ(StringUtil::ToInt32(value), 1234);
    ASSERT_EQ(StringUtil::ToUint32(value), 1234);
    ASSERT_EQ(StringUtil::ToInt64(value), 1234);
    ASSERT_EQ(StringUtil::ToUint64(value), 1234);
    ASSERT_EQ(StringUtil::ToFloat(value), 1234);
    ASSERT_EQ(StringUtil::ToDouble(value), 1234);
}

TEST_F(TestStringUtils, InvalidStringToNum) {
    const char* value = "1234999999999999999999999999999999999999999999999999999";
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }

    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }

    try {
        ASSERT_EQ(StringUtil::ToInt32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToFloat(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
}

TEST_F(TestStringUtils, EmptyTest) {
    const char* value = "";
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 0);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }

    value = "\0";
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 0);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
}

TEST_F(TestStringUtils, InvalidStringToNum2) {
    const char* value = "1234fger";
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToFloat(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToDouble(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
}

TEST_F(TestStringUtils, InvalidStringToNum4) {
    const char* value = "1234.45ddfd";
    try {
        ASSERT_TRUE(fabs(StringUtil::ToFloat(value) - 1234.45) <=
            std::numeric_limits<float>::epsilon());
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }

    try {
        ASSERT_TRUE(fabs(StringUtil::ToDouble(value) - 1234.45) <=
            std::numeric_limits<double>::epsilon());
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
}

TEST_F(TestStringUtils, InvalidStringToNum3) {
    const char* value = "1234.45";
    ASSERT_TRUE(fabs(StringUtil::ToFloat(value) - 1234.45f) <=
        std::numeric_limits<float>::epsilon());
    ASSERT_TRUE(fabs(StringUtil::ToDouble(value) - 1234.45) <=
        std::numeric_limits<double>::epsilon());
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint16(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint32(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToInt64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
    try {
        ASSERT_EQ(StringUtil::ToUint64(value), 1234);
        ASSERT_TRUE(false);
    } catch (ConvertException& ex) {
        ASSERT_TRUE(true);
    }
}
*/
}  // namespace test

}  // namespace common

}  // namespace zjchain
