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

    if (!msg->header.has_c2c()) {
        return;
    }

    if (msg->header.c2c().has_sell()) {
        HandleNewSell(msg);
    }

    if (msg->header.c2c().has_order()) {
        HandleNewOrder(msg);
    }

    if (msg->header.c2c().has_report()) {
        HandleReport(msg);
    }

    if (msg->header.c2c().has_get_sell()) {
        HandleGetSells(msg);
    }

    if (msg->header.c2c().has_get_order()) {
        HandleGetOrders(msg);
    }
}

void Protocloler::HandleNewSell(const transport::MessagePtr& msg) {
    auto& sell = msg->header.c2c().sell();

}

void Protocloler::HandleNewOrder(const transport::MessagePtr& msg) {
}

void Protocloler::HandleReport(const transport::MessagePtr& msg) {
}

void Protocloler::HandleGetSells(const transport::MessagePtr& msg) {
}

void Protocloler::HandleGetOrders(const transport::MessagePtr& msg) {
}

}  // namespace c2c

}  // namespace zjchain
