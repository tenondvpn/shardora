#pragma once

#include <condition_variable>
#include <mutex>

#include <evhtp/evhtp.h>
#include <evhtp/internal.h>

#include "http/http_utils.h"

namespace shardora {

namespace http {

class HttpServer {
public:
    HttpServer();
    ~HttpServer();
    int32_t Init(const char* ip, uint16_t port, int32_t thread_count);
    int32_t Start();
    int32_t Stop();
    void AddCallback(const char* uri, evhtp_callback_cb cb);

private:
    void RunHttpServer();

    evbase_t* evbase_{ nullptr };
    evhtp_t* htp_{ nullptr };
    std::thread* http_thread_{ nullptr };
    struct event* ev_sigint_ {nullptr};
    std::condition_variable con_;
    std::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(HttpServer);
};

};  // namespace http

};  // namespace shardora
