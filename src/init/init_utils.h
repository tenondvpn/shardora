#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/encode.h"

#define INIT_DEBUG(fmt, ...) ZJC_DEBUG("[init]" fmt, ## __VA_ARGS__)
#define INIT_INFO(fmt, ...) ZJC_INFO("[init]" fmt, ## __VA_ARGS__)
#define INIT_WARN(fmt, ...) ZJC_WARN("[init]" fmt, ## __VA_ARGS__)
#define INIT_ERROR(fmt, ...) ZJC_ERROR("[init]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace init {

enum InitErrorCode {
    kInitSuccess = 0,
    kInitError = 1,
};

}  // namespace init

}  // namespace zjchain
