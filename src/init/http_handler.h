#pragma once

#include "http/http_server.h"
#include "security/security.h"

namespace zjchain {

namespace init {

class HttpHandler {
public:
    HttpHandler();
    ~HttpHandler();
    void Init(
        std::shared_ptr<security::Security>& security_ptr,
        http::HttpServer& http_server);

    std::shared_ptr<security::Security> security_ptr() {
        return security_ptr_;
    }

private:
    std::shared_ptr<security::Security> security_ptr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HttpHandler);
};

};  // namespace init

};  // namespace zjchain
