#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/tick.h"

namespace zjchain {

namespace common {

namespace test {

class TestTick : public testing::Test, std::enable_shared_from_this<TestTick> {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
        called_times_ = 0;
        called_times1_ = 0;
        called_times2_ = 0;
        called_times3_ = 0;
        called_times4_ = 0;
        called_times5_ = 0;
        called_times6_ = 0;
        const uint32_t kPeriod = 1000;
        for (int32_t i = 0; i < 100; ++i) {
            tick_.CutOff(kPeriod, std::bind(&TestTick::TimerCall, this));
            tick1_.CutOff(kPeriod, std::bind(&TestTick::TimerCall1, this));
            tick2_.CutOff(kPeriod, std::bind(&TestTick::TimerCall2, this));
            tick3_.CutOff(kPeriod, std::bind(&TestTick::TimerCall3, this));
            tick4_.CutOff(kPeriod, std::bind(&TestTick::TimerCall4, this));
            tick5_.CutOff(kPeriod, std::bind(&TestTick::TimerCall5, this));
            tick6_.CutOff(kPeriod, std::bind(&TestTick::TimerCall6, this));
        }
    }

    virtual void TearDown() {
        std::this_thread::sleep_for(std::chrono::microseconds(600 * 1000));
        ASSERT_EQ(called_times_, 5);
    }

    void TimerCall() {
        ++called_times_;
        if (called_times_ < 5) {
            tick_ .CutOff(15 * 1000, std::bind(&TestTick::TimerCall, this));
        }
    }

    void TimerCall1() {
        ++called_times1_;
        if (called_times1_ < 5) {
            tick1_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall1, this));
        }
    }

    void TimerCall2() {
        ++called_times2_;
        if (called_times2_ < 5) {
            tick2_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall2, this));
        }
    }

    void TimerCall3() {
        ++called_times3_;
        if (called_times3_ < 5) {
            tick3_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall3, this));
        }
    }

    void TimerCall4() {
        ++called_times4_;
        if (called_times4_ < 5) {
            tick4_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall4, this));
        }
    }

    void TimerCall5() {
        ++called_times5_;
        if (called_times5_ < 5) {
            tick5_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall5, this));
        }
    }

    void TimerCall6() {
        ++called_times6_;
        if (called_times6_ < 5) {
            tick6_.CutOff(15 * 1000, std::bind(&TestTick::TimerCall6, this));
        }
    }

private:
    uint32_t called_times_;
    uint32_t called_times1_;
    uint32_t called_times2_;
    uint32_t called_times3_;
    uint32_t called_times4_;
    uint32_t called_times5_;
    uint32_t called_times6_;
    Tick tick_;
    Tick tick1_;
    Tick tick2_;
    Tick tick3_;
    Tick tick4_;
    Tick tick5_;
    Tick tick6_;
};

TEST_F(TestTick, All) {
    ASSERT_TRUE(true);
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
