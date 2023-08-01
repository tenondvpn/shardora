#pragma once

#include "http/http_server.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace init {

class HttpHandler {
public:
    HttpHandler();
    ~HttpHandler();
    void Init(
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security>& security_ptr,
        http::HttpServer& http_server);

    std::shared_ptr<security::Security> security_ptr() {
        return security_ptr_;
    }

    transport::MultiThreadHandler* net_handler() {
        return net_handler_;
    }

private:
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    transport::MultiThreadHandler* net_handler_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HttpHandler);
};

};  // namespace init

};  // namespace zjchain
