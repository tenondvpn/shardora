#pragma once

#include "common/utils.h"
#include "common/log.h"

namespace zjchain {

namespace c2c {

Protocloler::Protocloler(std::shared_ptr<protos::PrefixDb> prefix_db) : prefix_db_(prefix_db) {}

Protocloler::~Protocloler() {}

void Protocloler::HandleMessage(const transport::MessagePtr& msg) {
    if (msg->header.type() != kC2cMessage) {
        return;
    }
}

void Protocloler::HandleNewSell(const transport::MessagePtr& msg) {
}

}  // namespace c2c

}  // namespace zjchain
