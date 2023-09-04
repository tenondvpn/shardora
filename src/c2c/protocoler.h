#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "protos/prefix_db.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace c2c {

class Protocloler {
public:
    Protocloler(std::shared_ptr<protos::PrefixDb> prefix_db);
    ~Protocloler();

private:
    void HandleMessage(const transport::MessagePtr& msg);
    void HandleNewSell(const transport::MessagePtr& msg);
    void HandleNewOrder(const transport::MessagePtr& msg);
    void HandleReport(const transport::MessagePtr& msg);
    void HandleGetSells(const transport::MessagePtr& msg);
    void HandleGetOrders(const transport::MessagePtr& msg);

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Protocloler);
};

}  // namespace c2c

}  // namespace zjchain
