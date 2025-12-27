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

static inline void GlobalInitSpdlog() {
    // 1. Initialize the asynchronous thread pool first (must be called before create_async)
    spdlog::init_thread_pool(8192, 1);  // 队列大小 8192，一个后台线程

    // 2. Create asynchronous file logger
    auto LOG_INS = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "async_file", "log/shardora.log", true);  // true 表示自动 flush_on debug/error 等

    // 3. [Key!] Set as the default logger, so that global macros (such as spdlog::debug) can be used
    spdlog::set_default_logger(LOG_INS);

    // 4. Set log level and format
    spdlog::set_level(spdlog::level::debug);  // 或 trace，根据需要
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] [%s:%#] [%!] %v%$");

    // Optional: Flush immediately on error
    spdlog::flush_on(spdlog::level::err);
    spdlog::debug("init spdlog success.");
}

#ifdef _WIN32

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)
#else
// #define DEBUG(fmt, ...)
// #define SHARDORA_DEBUG(fmt, ...)

#define DEBUG(fmt, ...)  do {\
    spdlog::debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
// #define SHARDORA_DEBUG(fmt, ...)
#define SHARDORA_DEBUG(fmt, ...)  do {\
    spdlog::debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif

#define SHARDORA_INFO(fmt, ...)  do {\
    spdlog::info("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(fmt, ...)  do {\
    spdlog::warn("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(fmt, ...)  do {\
    spdlog::error("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    spdlog::critical("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
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
    spdlog::debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define SHARDORA_DEBUG(fmt, ...)  do {\
    spdlog::debug("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif
// #define SHARDORA_INFO(fmt, ...)
// #define SHARDORA_WARN(fmt, ...)
#define SHARDORA_INFO(fmt, ...)  do {\
    spdlog::info("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(fmt, ...)  do {\
    spdlog::warn("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(fmt, ...)  do {\
    spdlog::error("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    spdlog::critical("[%s][%s][%d] " fmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
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
