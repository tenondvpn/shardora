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
#include <spdlog/fmt/bundled/printf.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/async.h"


#define SHARDORA_DEBUG(logfmt, ...)
#ifdef _WIN32
#define SHARDORA_LOG_FILE_NAME strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__
#else
#define SHARDORA_LOG_FILE_NAME strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__
#endif

#ifdef _WIN32

#ifdef NDEBUG
#define DEBUG(logfmt, ...)
#define SHARDORA_DEBUG(logfmt, ...)
#else
// #define DEBUG(logfmt, ...)
// #define SHARDORA_DEBUG(logfmt, ...)

#define DEBUG(logfmt, ...)  do {\
    spdlog::debug(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)
// #define SHARDORA_DEBUG(logfmt, ...)
#define SHARDORA_DEBUG(logfmt, ...)  do {\
    spdlog::debug(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)
#endif

#define SHARDORA_INFO(logfmt, ...)  do {\
    spdlog::info(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_WARN(logfmt, ...)  do {\
    spdlog::warn(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_ERROR(logfmt, ...)  do {\
    spdlog::error(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_FATAL(logfmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    spdlog::critical(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
    assert(false);\
    exit(0);\
} while (0)
#else

#ifdef NDEBUG
#define DEBUG(logfmt, ...)
#define SHARDORA_DEBUG(logfmt, ...)
#else
// #define DEBUG(logfmt, ...)
// #define SHARDORA_DEBUG(logfmt, ...)
#define DEBUG(logfmt, ...)  do {\
    spdlog::debug(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)
#define SHARDORA_DEBUG(logfmt, ...)  do {\
    spdlog::debug(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)
#endif
// #define SHARDORA_INFO(logfmt, ...)
// #define SHARDORA_WARN(logfmt, ...)
#define SHARDORA_INFO(logfmt, ...)  do {\
    spdlog::info(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_WARN(logfmt, ...)  do {\
    spdlog::warn(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_ERROR(logfmt, ...)  do {\
    spdlog::error(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
} while (0)

#define SHARDORA_FATAL(logfmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    spdlog::critical(spdlog::fmt::sprintf("[%s][%s][%d] " logfmt, SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__));\
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
#define DEBUG(logfmt, ...)
#define SHARDORA_DEBUG(logfmt, ...)
#else
 #define DEBUG(logfmt, ...)
 #define SHARDORA_DEBUG(logfmt, ...)
/*
#define DEBUG(logfmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define SHARDORA_DEBUG(logfmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
*/
#endif

#define SHARDORA_INFO(logfmt, ...)  do {\
    printf("[INFO][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_WARN(logfmt, ...)  do {\
    printf("[WARN][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_ERROR(logfmt, ...)  do {\
    printf("[ERROR][%s][%s][%d] " fmt "\n", SHARDORA_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define SHARDORA_FATAL(logfmt, ...)  do {\
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
