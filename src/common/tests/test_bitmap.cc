#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/bitmap.h"
#include "common/random.h"

namespace zjchain {

namespace common {

namespace test {

class TestBitmap : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:

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

}  // namespace test

}  // namespace common

}  // namespace zjchain
