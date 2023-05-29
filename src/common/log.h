#pragma once

#include <string.h>

#include "log4cpp/Category.hh"
#include "log4cpp/Appender.hh"
#include "log4cpp/FileAppender.hh"
#include "log4cpp/OstreamAppender.hh"
#include "log4cpp/Layout.hh"
#include "log4cpp/BasicLayout.hh"
#include "log4cpp/Priority.hh"
#include "log4cpp/PropertyConfigurator.hh"

#ifdef _WIN32
#define ZJC_LOG_FILE_NAME strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__
#else
#define ZJC_LOG_FILE_NAME strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__
#endif

#define LOG_INS log4cpp::Category::getInstance(std::string("sub1"))
#ifdef _WIN32

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define ZJC_DEBUG(fmt, ...)
#else
#define DEBUG(fmt, ...)  do {\
    LOG_INS.debug("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define ZJC_DEBUG(fmt, ...)  do {\
    LOG_INS.debug("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif

#define ZJC_INFO(fmt, ...)  do {\
    LOG_INS.info("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_WARN(fmt, ...)  do {\
    LOG_INS.warn("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_ERROR(fmt, ...)  do {\
    LOG_INS.error("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    LOG_INS.fatal("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)
#else

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define ZJC_DEBUG(fmt, ...)
#else
#define DEBUG(fmt, ...)  do {\
    LOG_INS.info("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define ZJC_DEBUG(fmt, ...)  do {\
    LOG_INS.debug("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif

#define ZJC_INFO(fmt, ...)  do {\
    LOG_INS.info("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_WARN(fmt, ...)  do {\
    LOG_INS.warn("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_ERROR(fmt, ...)  do {\
    LOG_INS.error("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_FATAL(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    LOG_INS.fatal("[%s][%s][%d] " fmt, ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)

#endif // _WIN32

#ifdef LOG
#undef LOG
#endif // LOG
#define LOG(level) LOG_INS << level << "[" << ZJC_LOG_FILE_NAME << ": " << __LINE__ << "]" 

#ifdef FOR_CONSOLE_DEBUG
#undef DEBUG
#undef ZJC_INFO
#undef ZJC_WARN
#undef ZJC_ERROR

#ifdef NDEBUG
#define DEBUG(fmt, ...)
#define ZJC_DEBUG(fmt, ...)
#else
#define DEBUG(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#define ZJC_DEBUG(fmt, ...)  do {\
    printf("[DEBUG][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)
#endif

#define ZJC_INFO(fmt, ...)  do {\
    printf("[INFO][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_WARN(fmt, ...)  do {\
    printf("[WARN][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_ERROR(fmt, ...)  do {\
    printf("[ERROR][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
} while (0)

#define ZJC_FATAL(fmt, ...)  do {\
    printf("[FATAL][%s][%s][%d] " fmt "\n", ZJC_LOG_FILE_NAME, __FUNCTION__, __LINE__, ## __VA_ARGS__);\
    assert(false);\
    exit(0);\
} while (0)

#endif
