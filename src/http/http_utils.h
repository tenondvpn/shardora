#pragma once

#include <map>

#include "common/utils.h"

namespace zjchain {

namespace http {

enum HttpStatusCode: int32_t {
    kHttpSuccess = 0,
    kHttpError = 1,
    kAccountNotExists = 2,
    kBalanceInvalid = 3,
    kShardIdInvalid = 4,
    kSignatureInvalid = 5,
    kFromEqualToInvalid = 6,
};

static const std::string kHttpParamTaskId = "tid";

static const std::map<int, const char*> kStatusMap = {
    {kHttpSuccess, "kHttpSuccess"},
    {kHttpError, "kHttpError"},
    {kAccountNotExists, "kAccountNotExists"},
    {kBalanceInvalid, "kBalanceInvalid"},
    {kShardIdInvalid, "kShardIdInvalid"},
    {kSignatureInvalid, "kSignatureInvalid"},
    {kFromEqualToInvalid, "kFromEqualToInvalid"},
};

inline static const char* GetStatus(int status) {
    auto iter = kStatusMap.find(status);
    if (iter != kStatusMap.end()) {
        return iter->second;
    }

    return "unknown";
}

};  // namespace zjchain

};  // namespace zjchain
