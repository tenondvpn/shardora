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

struct RotatitionLeaders {
    RotatitionLeaders() : now_rotation_idx(0), invalid_pool_count(0), now_leader_idx(0) {}
    std::vector<uint32_t> rotation_leaders;
    uint32_t now_rotation_idx;
    uint32_t invalid_pool_count;
    uint32_t now_leader_idx;
};

struct LeaderRotationInfo {
    LeaderRotationInfo() : elect_height(0), members(nullptr) {}
    uint64_t elect_height;
    uint32_t member_count;
    std::vector<RotatitionLeaders> rotations;
    std::set<uint32_t> invalid_leaders;
    common::MembersPtr members;
};

}  // namespace init

}  // namespace zjchain
