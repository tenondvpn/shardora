#include "http/http_server.h"

#include <signal.h>
#include <functional>
#include <map>

#include "common/global_info.h"
#include "common/string_utils.h"

namespace shardora {

namespace http {

HttpServer::HttpServer() {
}

HttpServer::~HttpServer() {}

int32_t HttpServer::Init(
        const char* ip,
        uint16_t port,
        int32_t thread_count) {
    evbase_ = event_base_new();
    if (evbase_ == nullptr) {
        return kHttpError;
    }

    htp_ = evhtp_new(evbase_, NULL);
    if (htp_ == nullptr) {
        return kHttpError;
    }

    evhtp_use_threads_wexit(htp_, NULL, NULL, thread_count, NULL);
    evhtp_bind_socket(htp_, ip, port, 1024);
    ZJC_INFO("start http server: %s: %d", ip, port);
    return kHttpSuccess;
}

void HttpServer::AddCallback(const char* uri, evhtp_callback_cb cb) {
    evhtp_set_cb(
        htp_,
        uri,
        cb,
        NULL);
}

int32_t HttpServer::Start() {
    if (htp_ == nullptr) {
        return kHttpError;
    }

    http_thread_ = new std::thread(std::bind(&HttpServer::RunHttpServer, this));
    std::unique_lock<std::mutex> lock(mutex_);
    con_.wait_for(lock, std::chrono::milliseconds(1000));
    http_thread_->detach();
    return kHttpSuccess;
}

void HttpServer::RunHttpServer() {
    {
        auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
        std::unique_lock<std::mutex> lock(mutex_);
        con_.notify_one();
    }
    event_base_loop(evbase_, 0);
}

int32_t HttpServer::Stop() {
    if (evbase_ != nullptr) {
        event_base_loopexit(evbase_, NULL);
    }

    if (http_thread_ != nullptr) {
        delete http_thread_;
        http_thread_ = nullptr;
    }

    if (ev_sigint_ != nullptr) {
        evhtp_safe_free(ev_sigint_, event_free);
    }

    if (htp_ != nullptr) {
        evhtp_unbind_socket(htp_);
        evhtp_safe_free(htp_, evhtp_free);
    }

    if (evbase_ != nullptr) {
        evhtp_safe_free(evbase_, event_base_free);
    }

    return kHttpSuccess;
}

};  // namespace http

};  // namespace shardora
