#pragma once

#include <time.h>

#ifndef _WIN32
#include <sys/time.h>
#endif

#include <cstdint>
#include <chrono>
#include <string>

#include "common/utils.h"

namespace zjchain {

namespace common {

class TimeUtils {
public:
    static uint32_t PeriodSeconds(std::chrono::system_clock::time_point &start) {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - start).count();
    }

    static uint64_t PeriodMs(std::chrono::system_clock::time_point &start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - start).count();
    }

    static uint32_t TimestampDays() {
        uint32_t hours = std::chrono::duration_cast<std::chrono::hours>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return hours / 24;
    }

    static uint32_t TimestampHours() {
        uint32_t hours = std::chrono::duration_cast<std::chrono::hours>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return hours;
    }

    static uint32_t TimestampSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static uint64_t TimestampMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static uint64_t TimestampUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static uint64_t ToTimestampMs(const std::chrono::system_clock::time_point &time) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                time.time_since_epoch()).count();
    }

    // "%Y-%m-%d %H:%M:%S"
//     static std::string DatetimeStr(const std::chrono::system_clock::time_point &time) {
//         char tmp_time[25] = { 0 };
//         time_t tt = std::chrono::system_clock::to_time_t(time);
//         struct tm local_time;
//         localtime_r(&tt, &local_time);
//         strftime(tmp_time, 22, "%Y-%m-%d %H:%M:%S", &local_time);
//         return std::string(tmp_time);
//     }
// 
//     // "%d-%02d-%02d %02d:%02d:%02d.%03d"
//     static std::string DatetimeMs(const std::chrono::system_clock::time_point &time) {
//         uint64_t mill = std::chrono::duration_cast<std::chrono::milliseconds>(
//                 time.time_since_epoch()).count() - 
//                 std::chrono::duration_cast<std::chrono::seconds>(
//                 time.time_since_epoch()).count() * 1000;
// 
//         char tmp_time[25] = { 0 };
//         time_t tt = std::chrono::system_clock::to_time_t(time);
//         struct tm local_time;
//         localtime_r(&tt, &local_time);
//         sprintf(tmp_time, "%d-%02d-%02d %02d:%02d:%02d.%03d", local_time.tm_year + 1900,
//             local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_hour,
//             local_time.tm_min, local_time.tm_sec, mill);
// 
//         return std::string(tmp_time);
//     }

private:
    TimeUtils();
    ~TimeUtils();

    DISALLOW_COPY_AND_ASSIGN(TimeUtils);
};

}  // namespace common

}  // namespace zjchain
