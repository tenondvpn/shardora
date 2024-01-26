#pragma once

#include "common/utils.h"

namespace zjchain {

namespace common {

int32_t RunShellCmdToGetOutput(const std::string& cmd, std::string* res) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp == NULL) {
        return 1;
    }

    char data[2048] = { 0 };
    while (fgets(data, sizeof(data), fp) != nullptr) {
        *res += data;
    }

    return 0;
}

};  // namespace common

};  // namespace zjchain
