#pragma once

#include <string.h>

// #include "log4cpp/Category.hh"
// #include "log4cpp/Appender.hh"
// #include "log4cpp/FileAppender.hh"
// #include "log4cpp/OstreamAppender.hh"
// #include "log4cpp/Layout.hh"
// #include "log4cpp/BasicLayout.hh"
// #include "log4cpp/Priority.hh"
// #include "log4cpp/PropertyConfigurator.hh"
#include <google/protobuf/util/json_util.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/async.h"


#define SHARDORA_DEBUG(fmt, ...)
#ifdef _WIN32
#define SHARDORA_LOG_FILE_NAME strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__
#else
#define SHARDORA_LOG_FILE_NAME strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__
#endif

static std::shared_ptr<spdlog::logger> LOG_INS = nullptr;
static inline void GlobalInitSpdlog() {
    // 1. 先初始化异步线程池（必须在 create_async 前调用）
    spdlog::init_thread_pool(8192, 1);  // 队列大小 8192，一个后台线程

    // 2. 创建异步文件 logger
    LOG_INS = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "async_file", "log/shardora.log", true);  // true 表示自动 flush_on debug/error 等

    // 3. 【关键！】设置为默认 logger，让全局宏（如 spdlog::debug）能用
    spdlog::set_default_logger(LOG_INS);

    // 4. 设置日志级别和格式
    spdlog::set_level(spdlog::level::debug);  // 或 trace，根据需要
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [thread %t] %v");

    // 可选：出错时立即刷新
    spdlog::flush_on(spdlog::level::err);
}

#ifdef _WIN32

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)
#else
// #define DEBUG(fmt, ...)
// #define SHARDORA_DEBUG(fmt, ...)

#define DEBUG(fmt, ...)  do {\
    LOG_INS->debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
// #define SHARDORA_DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)  do {\
    LOG_INS->debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif

#define SHARDORA_INFO(fmt, ...)  do {\
    LOG_INS->info("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(fmt, ...)  do {\
    LOG_INS->warn("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(fmt, ...)  do {\
    LOG_INS->error("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    LOG_INS->critical("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)
#else

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)
#else
// #define DEBUG(fmt, ...)
// #define SHARDORA_DEBUG(fmt, ...)
#define DEBUG(fmt, ...)  do {\
    LOG_INS->debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define SHARDORA_DEBUG(fmt, ...)  do {\
    LOG_INS->debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif
// #define SHARDORA_INFO(fmt, ...)
// #define SHARDORA_WARN(fmt, ...)
#define SHARDORA_INFO(fmt, ...)  do {\
    LOG_INS->info("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(fmt, ...)  do {\
    LOG_INS->warn("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(fmt, ...)  do {\
    LOG_INS->error("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    LOG_INS->critical("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)

#endif // _WIN32

// #ifdef LOG
// #undef LOG
// #endif // LOG
// #define LOG(level) LOG_INS << level << "[" << SHARDORA_LOG_FILE_NAME << ": " << __LINE__ << "]" 

#ifdef FOR_CONSOLE_DEBUG
#undef DEBUG
#undef SHARDORA_INFO
#undef SHARDORA_WARN
#undef SHARDORA_ERROR

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)
#else
 #define DEBUG(fmt, ...)
 #define SHARDORA_DEBUG(fmt, ...)
/*
#define DEBUG(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define SHARDORA_DEBUG(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
*/
#endif

#define SHARDORA_INFO(fmt, ...)  do {\
    printf("[INFO][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(fmt, ...)  do {\
    printf("[WARN][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(fmt, ...)  do {\
    printf("[ERROR][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(fmt, ...)  do {\
    printf("[FATAL][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)

#endif

static std::string ProtobufToJson(const google::protobuf::Message& message, bool pretty_print = false) {
    // return "";
#ifdef NDEBUG
    return "";
#endif
    std::string json_str;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = pretty_print;
    auto status = google::protobuf::util::MessageToJsonString(message, &json_str, options);
    if (!status.ok()) {
        return "";
    }
    return json_str;
}
