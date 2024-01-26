#include "http/http_client.h"

#include <functional>

namespace zjchain {

namespace http {

static void
request_cb(evhtp_request_t * req, void * arg)
{
    evhtp_request_free(req);
}

static evhtp_res request_fini_cb(evhtp_request_t* req, void* arg) {
    return EVHTP_RES_OK;
}

static evhtp_res connection_fini_cb(evhtp_connection_t* connection, void* arg) {
    evhtp_connection_free(connection);
    std::cout << "connection freed" << std::endl;
    return EVHTP_RES_OK;
}

HttpClient::HttpClient() {
    evbase_ = event_base_new();
    http_thread_ = new std::thread(std::bind(&HttpClient::Start, this));
}

HttpClient::~HttpClient() {
    Destroy();
}

void HttpClient::Start() {
    while (!destroy_) {
        event_base_loop(evbase_, EVLOOP_NO_EXIT_ON_EMPTY);
        std::this_thread::sleep_for(std::chrono::microseconds(100000l));
    }
}

void HttpClient::Destroy() {
    destroy_ = true;
    event_base_loopbreak(evbase_);
    event_base_loopexit(evbase_, nullptr);
    http_thread_->join();
    if (evbase_ != nullptr) {
        event_base_free(evbase_);
        evbase_ = nullptr;
    }
}

int32_t HttpClient::Request(const char* ip, uint16_t port, const std::string& msg, evhtp_hook_read_cb cb) {
    evhtp_connection_t* conn = evhtp_connection_new(evbase_, ip, port);
    if (conn == nullptr) {
        std::cout << "get connection failed!" << std::endl;
        return 1;
    }

    evhtp_request_t* request = evhtp_request_new(request_cb, evbase_);
    evhtp_request_set_hook(request, evhtp_hook_on_read, (evhtp_hook)cb, evbase_);
    evhtp_request_set_hook(request, evhtp_hook_on_request_fini, (evhtp_hook)request_fini_cb, evbase_);
    evhtp_request_set_hook(request, evhtp_hook_on_connection_fini, (evhtp_hook)connection_fini_cb, evbase_);
    evhtp_headers_add_header(request->headers_out,
        evhtp_header_new("Host", "ieatfood.net", 0, 0));
    evhtp_headers_add_header(request->headers_out,
        evhtp_header_new("User-Agent", "libevhtp", 0, 0));
    evhtp_headers_add_header(request->headers_out,
        evhtp_header_new("Connection", "close", 0, 0));
    std::string req = std::string("/test?msg=") + msg;
    evhtp_make_request(conn, request, htp_method_GET, req.c_str());
    return kHttpSuccess;
}

};  // namespace http

};  // namespace zjchain
