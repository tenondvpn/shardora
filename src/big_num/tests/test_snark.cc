#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/encode.h"

#define private public
#include "big_num/snark.h"

namespace seth {

namespace bignum {

namespace test {

class TestSnark : public testing::Test {
public:
    static void SetUpTestCase() {
        // Ensure libsnark is initialized
        Snark::Instance();
    }
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// --- AltBn128G1Add Tests ---

TEST_F(TestSnark, G1AddIdentity) {
    // Adding zero point to zero point should give zero point
    // Zero point is encoded as 64 bytes of zeros (two 32-byte coordinates)
    std::string zero_point(64, '\0');
    std::string input = zero_point + zero_point;  // p1 = 0, p2 = 0

    auto result = Snark::Instance()->AltBn128G1Add(input);
    ASSERT_EQ(result.size(), 64u);
    // Result should be zero point
    ASSERT_EQ(result, std::string(64, '\0'));
}

TEST_F(TestSnark, G1AddGeneratorPlusZero) {
    // Generator point G1 = (1, 2) in alt_bn128
    // Encoding: x as 32-byte big-endian, y as 32-byte big-endian
    std::string g1_x(32, '\0');
    g1_x[31] = 1;  // x = 1
    std::string g1_y(32, '\0');
    g1_y[31] = 2;  // y = 2
    std::string generator = g1_x + g1_y;
    std::string zero_point(64, '\0');

    std::string input = generator + zero_point;  // G + 0 = G
    auto result = Snark::Instance()->AltBn128G1Add(input);
    ASSERT_EQ(result.size(), 64u);
    ASSERT_EQ(result, generator);
}

TEST_F(TestSnark, G1AddZeroPlusGenerator) {
    // 0 + G = G
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;
    std::string zero_point(64, '\0');

    std::string input = zero_point + generator;
    auto result = Snark::Instance()->AltBn128G1Add(input);
    ASSERT_EQ(result.size(), 64u);
    ASSERT_EQ(result, generator);
}

TEST_F(TestSnark, G1AddCommutative) {
    // G + 2G should equal 2G + G
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;

    // First compute 2G = G + G
    std::string input_gg = generator + generator;
    auto two_g = Snark::Instance()->AltBn128G1Add(input_gg);
    ASSERT_EQ(two_g.size(), 64u);
    ASSERT_NE(two_g, std::string(64, '\0'));

    // G + 2G
    std::string input_g_2g = generator + two_g;
    auto result1 = Snark::Instance()->AltBn128G1Add(input_g_2g);

    // 2G + G
    std::string input_2g_g = two_g + generator;
    auto result2 = Snark::Instance()->AltBn128G1Add(input_2g_g);

    ASSERT_EQ(result1, result2);
}

TEST_F(TestSnark, G1AddAssociative) {
    // (G + G) + G should equal G + (G + G)
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;

    // 2G = G + G
    auto two_g = Snark::Instance()->AltBn128G1Add(generator + generator);
    // 3G via (G + G) + G
    auto three_g_1 = Snark::Instance()->AltBn128G1Add(two_g + generator);
    // 3G via G + (G + G)
    auto three_g_2 = Snark::Instance()->AltBn128G1Add(generator + two_g);

    ASSERT_EQ(three_g_1, three_g_2);
}

TEST_F(TestSnark, G1AddInvalidInput) {
    // Empty input should return empty
    auto result = Snark::Instance()->AltBn128G1Add("");
    ASSERT_TRUE(result.empty());
}

// --- AltBn128G1Mul Tests ---

TEST_F(TestSnark, G1MulByZero) {
    // G * 0 = 0 (zero point)
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;
    std::string scalar_zero(32, '\0');

    std::string input = generator + scalar_zero;
    auto result = Snark::Instance()->AltBn128G1Mul(input);
    ASSERT_EQ(result.size(), 64u);
    ASSERT_EQ(result, std::string(64, '\0'));
}

TEST_F(TestSnark, G1MulByOne) {
    // G * 1 = G
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;
    std::string scalar_one(32, '\0');
    scalar_one[31] = 1;

    std::string input = generator + scalar_one;
    auto result = Snark::Instance()->AltBn128G1Mul(input);
    ASSERT_EQ(result.size(), 64u);
    ASSERT_EQ(result, generator);
}

TEST_F(TestSnark, G1MulByTwo) {
    // G * 2 should equal G + G
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;

    std::string scalar_two(32, '\0');
    scalar_two[31] = 2;

    // G * 2
    std::string mul_input = generator + scalar_two;
    auto mul_result = Snark::Instance()->AltBn128G1Mul(mul_input);

    // G + G
    std::string add_input = generator + generator;
    auto add_result = Snark::Instance()->AltBn128G1Add(add_input);

    ASSERT_EQ(mul_result.size(), 64u);
    ASSERT_EQ(mul_result, add_result);
}

TEST_F(TestSnark, G1MulByThree) {
    // G * 3 should equal G + G + G
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;

    std::string scalar_three(32, '\0');
    scalar_three[31] = 3;

    // G * 3
    auto mul_result = Snark::Instance()->AltBn128G1Mul(generator + scalar_three);

    // G + G + G
    auto two_g = Snark::Instance()->AltBn128G1Add(generator + generator);
    auto three_g = Snark::Instance()->AltBn128G1Add(two_g + generator);

    ASSERT_EQ(mul_result, three_g);
}

TEST_F(TestSnark, G1MulDistributive) {
    // G * (a + b) should equal G*a + G*b
    std::string g1_x(32, '\0');
    g1_x[31] = 1;
    std::string g1_y(32, '\0');
    g1_y[31] = 2;
    std::string generator = g1_x + g1_y;

    std::string scalar_a(32, '\0');
    scalar_a[31] = 5;
    std::string scalar_b(32, '\0');
    scalar_b[31] = 7;
    std::string scalar_ab(32, '\0');
    scalar_ab[31] = 12;  // 5 + 7

    // G * 12
    auto result_combined = Snark::Instance()->AltBn128G1Mul(generator + scalar_ab);

    // G * 5
    auto result_a = Snark::Instance()->AltBn128G1Mul(generator + scalar_a);
    // G * 7
    auto result_b = Snark::Instance()->AltBn128G1Mul(generator + scalar_b);
    // G*5 + G*7
    auto result_sum = Snark::Instance()->AltBn128G1Add(result_a + result_b);

    ASSERT_EQ(result_combined, result_sum);
}

TEST_F(TestSnark, G1MulZeroPoint) {
    // 0 * scalar = 0
    std::string zero_point(64, '\0');
    std::string scalar(32, '\0');
    scalar[31] = 42;

    std::string input = zero_point + scalar;
    auto result = Snark::Instance()->AltBn128G1Mul(input);
    ASSERT_EQ(result.size(), 64u);
    ASSERT_EQ(result, std::string(64, '\0'));
}

// --- AltBn128PairingProduct Tests ---

TEST_F(TestSnark, PairingEmptyInput) {
    // Empty input (0 pairs) should return 1 (true)
    auto result = Snark::Instance()->AltBn128PairingProduct("");
    ASSERT_EQ(result.size(), 32u);
    // Last byte should be 1 (pairing of empty set is identity = true)
    ASSERT_EQ((uint8_t)result[31], 1u);
}

TEST_F(TestSnark, PairingInvalidLength) {
    // Input not a multiple of pair_size (192 bytes) should return empty
    std::string invalid(100, '\0');
    auto result = Snark::Instance()->AltBn128PairingProduct(invalid);
    ASSERT_TRUE(result.empty());
}

TEST_F(TestSnark, PairingZeroPoints) {
    // Pairing with zero G1 point should return 1 (true)
    // pair_size = 2*32 + 2*64 = 192 bytes
    std::string zero_pair(192, '\0');
    auto result = Snark::Instance()->AltBn128PairingProduct(zero_pair);
    ASSERT_EQ(result.size(), 32u);
    ASSERT_EQ((uint8_t)result[31], 1u);
}

}  // namespace test

}  // namespace bignum

}  // namespace seth
