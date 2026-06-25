#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/bitmap.h"
#include "common/random.h"

namespace seth {

namespace common {

namespace test {

class TestBitmap : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

TEST_F(TestBitmap, AddAndContainClear) {
    Bitmap bitmap{ 4096 };
    for (uint32_t i = 0; i < 4096; ++i) {
        bitmap.Set(i);
    }

    for (uint32_t i = 0; i < 4096; ++i) {
        ASSERT_TRUE(bitmap.Valid(i));
    }

    bitmap.UnSet(10);
    bitmap.UnSet(190);
    bitmap.UnSet(899);
    bitmap.UnSet(4076);
    ASSERT_TRUE(!bitmap.Valid(10));
    ASSERT_TRUE(!bitmap.Valid(190));
    ASSERT_TRUE(!bitmap.Valid(899));
    ASSERT_TRUE(!bitmap.Valid(4076));
    for (uint32_t i = 0; i < 4096; ++i) {
        if (i == 10 || i == 190 || i == 899 || i == 4076) {
            continue;
        }
        ASSERT_TRUE(bitmap.Valid(i));
    }

    bitmap.inversion(4096);
    for (uint32_t i = 0; i < 4096; ++i) {
        if (i == 10 || i == 190 || i == 899 || i == 4076) {
            ASSERT_TRUE(bitmap.Valid(i));
            continue;
        }
        ASSERT_TRUE(!bitmap.Valid(i));
    }
}

TEST_F(TestBitmap, DefaultConstructor) {
    Bitmap b;
    ASSERT_EQ(b.data().size(), 0u);
    ASSERT_EQ(b.valid_count(), 0u);
}

TEST_F(TestBitmap, ConstructFromVector) {
    // data = {0xFFFFFFFFFFFFFFFF} means all 64 bits set
    std::vector<uint64_t> data = { 0xFFFFFFFFFFFFFFFFull };
    Bitmap b(data);
    ASSERT_EQ(b.valid_count(), 64u);
    for (uint32_t i = 0; i < 64; ++i) {
        ASSERT_TRUE(b.Valid(i));
    }
}

TEST_F(TestBitmap, ConstructFromVectorPartial) {
    // Only bit 0 set
    std::vector<uint64_t> data = { 1ull };
    Bitmap b(data);
    ASSERT_EQ(b.valid_count(), 1u);
    ASSERT_TRUE(b.Valid(0));
    ASSERT_FALSE(b.Valid(1));
}

TEST_F(TestBitmap, CopyConstructor) {
    Bitmap original(128);
    original.Set(0);
    original.Set(63);
    original.Set(127);

    Bitmap copy(original);
    ASSERT_EQ(copy.valid_count(), original.valid_count());
    ASSERT_TRUE(copy.Valid(0));
    ASSERT_TRUE(copy.Valid(63));
    ASSERT_TRUE(copy.Valid(127));
    ASSERT_FALSE(copy.Valid(1));
}

TEST_F(TestBitmap, AssignmentOperator) {
    Bitmap a(64);
    a.Set(5);
    a.Set(10);

    Bitmap b(64);
    b = a;
    ASSERT_TRUE(b.Valid(5));
    ASSERT_TRUE(b.Valid(10));
    ASSERT_FALSE(b.Valid(0));
    ASSERT_EQ(b.valid_count(), a.valid_count());
}

TEST_F(TestBitmap, EqualityOperator) {
    Bitmap a(64);
    a.Set(1);
    a.Set(2);

    Bitmap b(64);
    b.Set(1);
    b.Set(2);

    ASSERT_TRUE(a == b);

    b.Set(3);
    ASSERT_FALSE(a == b);
}

TEST_F(TestBitmap, ClearMethod) {
    Bitmap b(128);
    for (uint32_t i = 0; i < 128; ++i) {
        b.Set(i);
    }
    ASSERT_EQ(b.valid_count(), 128u);

    b.clear();
    ASSERT_EQ(b.valid_count(), 0u);
    for (uint32_t i = 0; i < 128; ++i) {
        ASSERT_FALSE(b.Valid(i));
    }
}

TEST_F(TestBitmap, ValidCountTracking) {
    Bitmap b(64);
    ASSERT_EQ(b.valid_count(), 0u);

    b.Set(0);
    ASSERT_EQ(b.valid_count(), 1u);

    b.Set(1);
    ASSERT_EQ(b.valid_count(), 2u);

    b.UnSet(0);
    ASSERT_EQ(b.valid_count(), 1u);

    b.UnSet(1);
    ASSERT_EQ(b.valid_count(), 0u);
}

TEST_F(TestBitmap, InversionPartialBits) {
    // Test inversion where max_idx is not a multiple of 64
    Bitmap b(128);
    b.Set(0);
    b.Set(1);
    // Invert first 70 bits
    b.inversion(70);
    // Bits 0 and 1 should now be unset
    ASSERT_FALSE(b.Valid(0));
    ASSERT_FALSE(b.Valid(1));
    // Bits 2..69 should now be set
    for (uint32_t i = 2; i < 70; ++i) {
        ASSERT_TRUE(b.Valid(i));
    }
}

TEST_F(TestBitmap, SetAndUnsetSameBit) {
    Bitmap b(64);
    b.Set(31);
    ASSERT_TRUE(b.Valid(31));
    ASSERT_EQ(b.valid_count(), 1u);

    b.UnSet(31);
    ASSERT_FALSE(b.Valid(31));
    ASSERT_EQ(b.valid_count(), 0u);

    // Set again
    b.Set(31);
    ASSERT_TRUE(b.Valid(31));
    ASSERT_EQ(b.valid_count(), 1u);
}

TEST_F(TestBitmap, DataAccessor) {
    Bitmap b(128);
    b.Set(0);
    const auto& data = b.data();
    ASSERT_EQ(data.size(), 2u);  // 128 bits / 64 = 2 uint64_t
    ASSERT_EQ(data[0], 1ull);    // bit 0 set
    ASSERT_EQ(data[1], 0ull);
}

}  // namespace test

}  // namespace common

}  // namespace seth
