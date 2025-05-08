#pragma once

#include <evhtp/evhtp.h>
#include <evhtp/internal.h>

#include "http/http_utils.h"

namespace shardora {

namespace http {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    int32_t Get(const char* ip, uint16_t port, const std::string& msg, evhtp_hook_read_cb cb);
    int32_t Post(
        const char* ip, 
        uint16_t port, 
        const std::string& url, 
        const std::string& post_data, 
        evhtp_hook_read_cb cb);
    void Destroy();

private:
    void Start();

    evbase_t* evbase_{ nullptr };
    std::thread* http_thread_{ nullptr };
    bool destroy_ = false;

    DISALLOW_COPY_AND_ASSIGN(HttpClient);
};

};  // namespace http

};  // namespace shardora
