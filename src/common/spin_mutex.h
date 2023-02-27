#pragma once

#include <array>
#include <thread>
#include <atomic>
#include <emmintrin.h>

#include "common/utils.h"

namespace zjchain {

namespace common {

class SpinMutex {
public:
    void lock() noexcept {
        constexpr std::array<uint32_t, 3> iterations = { 5, 10, 3000 };

        for (int i = 0; i < iterations[0]; ++i) {
            if (try_lock()) {
                return;
            }
        }

        for (int i = 0; i < iterations[1]; ++i) {
            if (try_lock()) {
                return;
            }

            _mm_pause();
        }

        while (true) {
            for (int i = 0; i < iterations[2]; ++i) {
                if (try_lock()) {
                    return;
                }

                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
            }

            std::this_thread::yield();
        }
    }

    bool try_lock() noexcept {
        return !flag.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

class AutoSpinLock {
public:
    AutoSpinLock(SpinMutex& spin_mutex) : spin_mutex_(spin_mutex) {
        spin_mutex_.lock();
    }

    ~AutoSpinLock() {
        spin_mutex_.unlock();
    }

private:
    SpinMutex& spin_mutex_;

    DISALLOW_COPY_AND_ASSIGN(AutoSpinLock);
};

};  // namespace common

};  // namespace zjchain
