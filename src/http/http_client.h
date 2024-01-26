#pragma once

#include <evhtp/evhtp.h>
#include <evhtp/internal.h>

#include "http/http_utils.h"

namespace zjchain {

namespace http {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    int32_t Request(const char* ip, uint16_t port, const std::string& msg, evhtp_hook_read_cb cb);

private:
    void Destroy();
    void Start();

    evbase_t* evbase_{ nullptr };
    std::thread* http_thread_{ nullptr };
    bool destroy_ = false;

    DISALLOW_COPY_AND_ASSIGN(HttpClient);
};

};  // namespace http

};  // namespace zjchain
