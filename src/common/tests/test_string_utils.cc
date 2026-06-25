#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "common/string_utils.h"

namespace seth {

namespace common {

namespace test {

class TestStringUtils : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// --- Trim Tests ---

TEST_F(TestStringUtils, TrimSpaces) {
    std::string s = "  hello world  ";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "hello world");
}

TEST_F(TestStringUtils, TrimTabs) {
    std::string s = "\t\thello\t\t";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "hello");
}

TEST_F(TestStringUtils, TrimNewlines) {
    std::string s = "\n\nhello\n\n";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "hello");
}

TEST_F(TestStringUtils, TrimMixed) {
    std::string s = " \t\n hello \n\t ";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "hello");
}

TEST_F(TestStringUtils, TrimEmpty) {
    std::string s = "";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "");
}

TEST_F(TestStringUtils, TrimAllWhitespace) {
    std::string s = "   \t\t\n\n   ";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "");
}

TEST_F(TestStringUtils, TrimNoWhitespace) {
    std::string s = "hello";
    StringUtil::Trim(s);
    ASSERT_EQ(s, "hello");
}

// --- IsNumeric Tests ---

TEST_F(TestStringUtils, IsNumericInteger) {
    ASSERT_TRUE(StringUtil::IsNumeric("123"));
    ASSERT_TRUE(StringUtil::IsNumeric("-456"));
    ASSERT_TRUE(StringUtil::IsNumeric("0"));
}

TEST_F(TestStringUtils, IsNumericFloat) {
    ASSERT_TRUE(StringUtil::IsNumeric("3.14"));
    ASSERT_TRUE(StringUtil::IsNumeric("-2.718"));
    ASSERT_TRUE(StringUtil::IsNumeric("0.0"));
}

TEST_F(TestStringUtils, IsNumericNotNumeric) {
    ASSERT_FALSE(StringUtil::IsNumeric("abc"));
    ASSERT_FALSE(StringUtil::IsNumeric("12abc"));
    ASSERT_FALSE(StringUtil::IsNumeric(""));
    ASSERT_FALSE(StringUtil::IsNumeric("hello"));
}

TEST_F(TestStringUtils, IsNumericString) {
    std::string s = "42";
    ASSERT_TRUE(StringUtil::IsNumeric(s));
    s = "not_a_number";
    ASSERT_FALSE(StringUtil::IsNumeric(s));
}

// --- ToBool Tests ---

TEST_F(TestStringUtils, ToBoolValid) {
    bool res = false;
    ASSERT_TRUE(StringUtil::ToBool("1", &res));
    ASSERT_TRUE(res);
    ASSERT_TRUE(StringUtil::ToBool("0", &res));
    ASSERT_FALSE(res);
}

TEST_F(TestStringUtils, ToBoolInvalid) {
    bool res = false;
    ASSERT_FALSE(StringUtil::ToBool("true", &res));
    ASSERT_FALSE(StringUtil::ToBool("false", &res));
    ASSERT_FALSE(StringUtil::ToBool("abc", &res));
}

TEST_F(TestStringUtils, NullCStringInputsReturnFalse) {
    const char* null_str = nullptr;
    bool b = false;
    int32_t i = 0;
    uint64_t u = 0;
    float f = 0.0f;
    double d = 0.0;
    ASSERT_FALSE(StringUtil::ToBool(null_str, &b));
    ASSERT_FALSE(StringUtil::ToInt32(null_str, &i));
    ASSERT_FALSE(StringUtil::ToUint64(null_str, &u));
    ASSERT_FALSE(StringUtil::ToFloat(null_str, &f));
    ASSERT_FALSE(StringUtil::ToDouble(null_str, &d));
    ASSERT_FALSE(StringUtil::IsNumeric(null_str));
}

// --- ToInt Tests ---

TEST_F(TestStringUtils, ToInt8Valid) {
    int8_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt8("127", &res));
    ASSERT_EQ(res, 127);
    ASSERT_TRUE(StringUtil::ToInt8("-128", &res));
    ASSERT_EQ(res, -128);
    ASSERT_TRUE(StringUtil::ToInt8("0", &res));
    ASSERT_EQ(res, 0);
}

TEST_F(TestStringUtils, ToInt8Overflow) {
    int8_t res = 0;
    ASSERT_FALSE(StringUtil::ToInt8("128", &res));
    ASSERT_FALSE(StringUtil::ToInt8("-129", &res));
}

TEST_F(TestStringUtils, ToInt16Valid) {
    int16_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt16("32767", &res));
    ASSERT_EQ(res, 32767);
    ASSERT_TRUE(StringUtil::ToInt16("-32768", &res));
    ASSERT_EQ(res, -32768);
}

TEST_F(TestStringUtils, ToInt16Overflow) {
    int16_t res = 0;
    ASSERT_FALSE(StringUtil::ToInt16("32768", &res));
    ASSERT_FALSE(StringUtil::ToInt16("-32769", &res));
}

TEST_F(TestStringUtils, ToInt32Valid) {
    int32_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt32("2147483647", &res));
    ASSERT_EQ(res, 2147483647);
    ASSERT_TRUE(StringUtil::ToInt32("-2147483648", &res));
    ASSERT_EQ(res, -2147483648);
    ASSERT_TRUE(StringUtil::ToInt32("0", &res));
    ASSERT_EQ(res, 0);
    ASSERT_TRUE(StringUtil::ToInt32("1234", &res));
    ASSERT_EQ(res, 1234);
}

TEST_F(TestStringUtils, ToInt32Invalid) {
    int32_t res = 0;
    ASSERT_FALSE(StringUtil::ToInt32("abc", &res));
    ASSERT_FALSE(StringUtil::ToInt32("12.34", &res));
    ASSERT_FALSE(StringUtil::ToInt32("", &res));
}

TEST_F(TestStringUtils, ToInt64Valid) {
    int64_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt64("9223372036854775807", &res));
    ASSERT_EQ(res, 9223372036854775807LL);
    ASSERT_TRUE(StringUtil::ToInt64("-100", &res));
    ASSERT_EQ(res, -100);
}

TEST_F(TestStringUtils, ToInt64Invalid) {
    int64_t res = 0;
    ASSERT_FALSE(StringUtil::ToInt64("not_a_number", &res));
    ASSERT_FALSE(StringUtil::ToInt64("", &res));
}

TEST_F(TestStringUtils, ToInt64RangeAndPrefixCases) {
    int64_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt64("00000123", &res));
    ASSERT_EQ(res, 123);
    ASSERT_FALSE(StringUtil::ToInt64("9223372036854775808", &res));   // overflow
    ASSERT_FALSE(StringUtil::ToInt64("-9223372036854775809", &res));  // underflow
}

// --- ToUint Tests ---

TEST_F(TestStringUtils, ToUint8Valid) {
    uint8_t res = 0;
    ASSERT_TRUE(StringUtil::ToUint8("255", &res));
    ASSERT_EQ(res, 255);
    ASSERT_TRUE(StringUtil::ToUint8("0", &res));
    ASSERT_EQ(res, 0);
}

TEST_F(TestStringUtils, ToUint8Overflow) {
    uint8_t res = 0;
    ASSERT_FALSE(StringUtil::ToUint8("256", &res));
    ASSERT_FALSE(StringUtil::ToUint8("-1", &res));
}

TEST_F(TestStringUtils, ToUint16Valid) {
    uint16_t res = 0;
    ASSERT_TRUE(StringUtil::ToUint16("65535", &res));
    ASSERT_EQ(res, 65535);
    ASSERT_TRUE(StringUtil::ToUint16("0", &res));
    ASSERT_EQ(res, 0);
}

TEST_F(TestStringUtils, ToUint16Overflow) {
    uint16_t res = 0;
    ASSERT_FALSE(StringUtil::ToUint16("65536", &res));
    ASSERT_FALSE(StringUtil::ToUint16("-1", &res));
}

TEST_F(TestStringUtils, ToUint32Valid) {
    uint32_t res = 0;
    ASSERT_TRUE(StringUtil::ToUint32("4294967295", &res));
    ASSERT_EQ(res, 4294967295u);
    ASSERT_TRUE(StringUtil::ToUint32("0", &res));
    ASSERT_EQ(res, 0u);
    ASSERT_TRUE(StringUtil::ToUint32("1234", &res));
    ASSERT_EQ(res, 1234u);
}

TEST_F(TestStringUtils, ToUint32Invalid) {
    uint32_t res = 0;
    ASSERT_FALSE(StringUtil::ToUint32("-1", &res));
    ASSERT_FALSE(StringUtil::ToUint32("abc", &res));
    ASSERT_FALSE(StringUtil::ToUint32("", &res));
}

TEST_F(TestStringUtils, ToUint64Valid) {
    uint64_t res = 0;
    ASSERT_TRUE(StringUtil::ToUint64("18446744073709551615", &res));
    ASSERT_EQ(res, 18446744073709551615ULL);
    ASSERT_TRUE(StringUtil::ToUint64("0", &res));
    ASSERT_EQ(res, 0u);
}

TEST_F(TestStringUtils, ToUint64Invalid) {
    uint64_t res = 0;
    ASSERT_FALSE(StringUtil::ToUint64("-1", &res));
    ASSERT_FALSE(StringUtil::ToUint64("abc", &res));
}

TEST_F(TestStringUtils, ToUint64RejectsNegativeAndOverflow) {
    uint64_t res = 0;
    ASSERT_FALSE(StringUtil::ToUint64("-42", &res));
    ASSERT_FALSE(StringUtil::ToUint64("18446744073709551616", &res));
}

// --- ToFloat Tests ---

TEST_F(TestStringUtils, ToFloatValid) {
    float res = 0.0f;
    ASSERT_TRUE(StringUtil::ToFloat("3.14", &res));
    ASSERT_NEAR(res, 3.14f, 0.001f);
    ASSERT_TRUE(StringUtil::ToFloat("-2.5", &res));
    ASSERT_NEAR(res, -2.5f, 0.001f);
    ASSERT_TRUE(StringUtil::ToFloat("0", &res));
    ASSERT_NEAR(res, 0.0f, 0.001f);
}

TEST_F(TestStringUtils, ToFloatInvalid) {
    float res = 0.0f;
    ASSERT_FALSE(StringUtil::ToFloat("abc", &res));
    ASSERT_FALSE(StringUtil::ToFloat("12.34abc", &res));
    ASSERT_FALSE(StringUtil::ToFloat("", &res));
}

TEST_F(TestStringUtils, ToFloatAndDoubleRangeErrors) {
    float f = 0.0f;
    double d = 0.0;
    ASSERT_FALSE(StringUtil::ToFloat("1e50", &f));    // float ERANGE
    ASSERT_FALSE(StringUtil::ToDouble("1e500", &d));  // double ERANGE
}

// --- ToDouble Tests ---

TEST_F(TestStringUtils, ToDoubleValid) {
    double res = 0.0;
    ASSERT_TRUE(StringUtil::ToDouble("3.14159265358979", &res));
    ASSERT_NEAR(res, 3.14159265358979, 1e-10);
    ASSERT_TRUE(StringUtil::ToDouble("-100.5", &res));
    ASSERT_NEAR(res, -100.5, 1e-10);
    ASSERT_TRUE(StringUtil::ToDouble("0", &res));
    ASSERT_NEAR(res, 0.0, 1e-10);
}

TEST_F(TestStringUtils, ToDoubleInvalid) {
    double res = 0.0;
    ASSERT_FALSE(StringUtil::ToDouble("abc", &res));
    ASSERT_FALSE(StringUtil::ToDouble("12.34xyz", &res));
    ASSERT_FALSE(StringUtil::ToDouble("", &res));
}

// --- Format Tests ---

TEST_F(TestStringUtils, FormatBasic) {
    auto result = StringUtil::Format("Hello %s, you are %d years old", "World", 25);
    ASSERT_EQ(result, "Hello World, you are 25 years old");
}

TEST_F(TestStringUtils, FormatNoArgs) {
    auto result = StringUtil::Format("Hello World");
    ASSERT_EQ(result, "Hello World");
}

TEST_F(TestStringUtils, FormatNumbers) {
    auto result = StringUtil::Format("%d + %d = %d", 1, 2, 3);
    ASSERT_EQ(result, "1 + 2 = 3");
}

TEST_F(TestStringUtils, FormatFloat) {
    auto result = StringUtil::Format("%.2f", 3.14159);
    ASSERT_EQ(result, "3.14");
}

// --- Leading zeros handling ---

TEST_F(TestStringUtils, LeadingZeros) {
    int32_t res = 0;
    ASSERT_TRUE(StringUtil::ToInt32("007", &res));
    ASSERT_EQ(res, 7);
}

// --- String overload tests ---

TEST_F(TestStringUtils, StringOverloads) {
    std::string s;

    int32_t i32 = 0;
    s = "42";
    ASSERT_TRUE(StringUtil::ToInt32(s, &i32));
    ASSERT_EQ(i32, 42);

    uint64_t u64 = 0;
    s = "123456789";
    ASSERT_TRUE(StringUtil::ToUint64(s, &u64));
    ASSERT_EQ(u64, 123456789u);

    float f = 0.0f;
    s = "1.5";
    ASSERT_TRUE(StringUtil::ToFloat(s, &f));
    ASSERT_NEAR(f, 1.5f, 0.001f);

    double d = 0.0;
    s = "2.718";
    ASSERT_TRUE(StringUtil::ToDouble(s, &d));
    ASSERT_NEAR(d, 2.718, 0.001);
}

TEST_F(TestStringUtils, IsNumericSpecialFormats) {
    ASSERT_TRUE(StringUtil::IsNumeric("0x10"));   // base autodetect path
    ASSERT_TRUE(StringUtil::IsNumeric("1e3"));
    ASSERT_FALSE(StringUtil::IsNumeric("1e3x"));
}

TEST_F(TestStringUtils, StringOverloadFailurePaths) {
    int8_t i8 = 0;
    uint8_t u8 = 0;
    int16_t i16 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    bool b = false;

    ASSERT_FALSE(StringUtil::ToInt8(std::string("128"), &i8));
    ASSERT_FALSE(StringUtil::ToUint8(std::string("-1"), &u8));
    ASSERT_FALSE(StringUtil::ToInt16(std::string("40000"), &i16));
    ASSERT_FALSE(StringUtil::ToUint16(std::string("70000"), &u16));
    ASSERT_FALSE(StringUtil::ToUint32(std::string("abc"), &u32));
    ASSERT_FALSE(StringUtil::ToBool(std::string("true"), &b));
}

TEST_F(TestStringUtils, CStringOverloadsAndWhitespaceCases) {
    const char* with_spaces = " 42 ";
    int32_t i32 = 0;
    ASSERT_FALSE(StringUtil::ToInt32(with_spaces, &i32));

    const char* plus_sign = "+123";
    ASSERT_TRUE(StringUtil::ToInt32(plus_sign, &i32));
    ASSERT_EQ(i32, 123);

    const char* hex = "0xff";
    uint32_t u32 = 0;
    ASSERT_FALSE(StringUtil::ToUint32(hex, &u32));
}

TEST_F(TestStringUtils, BoolAndUint64AdditionalEdgeCases) {
    bool b = false;
    ASSERT_FALSE(StringUtil::ToBool("2", &b));
    ASSERT_FALSE(StringUtil::ToBool("-1", &b));

    uint64_t u64 = 0;
    ASSERT_TRUE(StringUtil::ToUint64("+42", &u64));
    ASSERT_EQ(u64, 42u);
    ASSERT_TRUE(StringUtil::ToUint64(" 42", &u64));
    ASSERT_EQ(u64, 42u);
}

TEST_F(TestStringUtils, IsNumericWhitespaceAndSignedScientific) {
    ASSERT_TRUE(StringUtil::IsNumeric(" 1"));
    ASSERT_FALSE(StringUtil::IsNumeric("1 "));
    ASSERT_TRUE(StringUtil::IsNumeric("+1.5e2"));
}

TEST_F(TestStringUtils, TrimCarriageReturnOnlyAndTrailingCR) {
    std::string only_cr = "\r\n\r";
    StringUtil::Trim(only_cr);
    ASSERT_TRUE(only_cr.empty());

    std::string word = "text\r";
    StringUtil::Trim(word);
    ASSERT_EQ(word, "text");
}

TEST_F(TestStringUtils, ToUint64HexOctalAndTrailingJunk) {
    uint64_t u = 0;
    ASSERT_TRUE(StringUtil::ToUint64("0xff", &u));
    ASSERT_EQ(u, 255u);
    u = 0;
    ASSERT_TRUE(StringUtil::ToUint64("010", &u));
    ASSERT_EQ(u, 8u);
    ASSERT_FALSE(StringUtil::ToUint64("99zz", &u));
}

TEST_F(TestStringUtils, ToUint64StringOverloadHex) {
    uint64_t u = 0;
    ASSERT_TRUE(StringUtil::ToUint64(std::string("0x10"), &u));
    ASSERT_EQ(u, 16u);
}

TEST_F(TestStringUtils, ToInt32StringOverloadLeadingZeros) {
    int32_t i = 0;
    ASSERT_TRUE(StringUtil::ToInt32(std::string("0000099"), &i));
    ASSERT_EQ(i, 99);
}

TEST_F(TestStringUtils, IsNumericRejectsExtremeExponent) {
    ASSERT_FALSE(StringUtil::IsNumeric("1e10000"));
}

TEST_F(TestStringUtils, ToDoubleLargeFinite) {
    double d = 0.0;
    ASSERT_TRUE(StringUtil::ToDouble("1e100", &d));
    ASSERT_GT(d, 1e99);
}

}  // namespace test

}  // namespace common

}  // namespace seth
