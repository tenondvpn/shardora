#include "http/http_server.h"

#include <atomic>
#include <mutex>

#include "common/time_utils.h"

using namespace zjchain;
std::atomic<uint32_t> receive_count = 0;
std::mutex test_m;
int64_t b_time = 0;

static void HttpTestCallback(evhtp_request_t* req, void* data) {
    const char* msg = evhtp_kv_find(req->uri->query, "msg");
    if (msg == nullptr) {
        std::string res("param msg missing.");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_OK);
        return;
    }

    ++receive_count;
    if (receive_count >= 100000) {
        std::lock_guard<std::mutex> g(test_m);
        if (receive_count >= 100000) {
            int64_t e_time = common::TimeUtils::TimestampMs();
            std::cout << "qps: " << (float(receive_count) / (float(e_time - b_time) / 1000.0f)) << std::endl;
            receive_count = 0;
            b_time = e_time;
        }
    }

    std::string res(msg);
    evbuffer_add(req->buffer_out, res.c_str(), res.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
}

int main(int argc, char* argv[]) {
    std::string http_ip = "0.0.0.0";
    uint16_t http_port = 8080;
    http::HttpServer http_server;
    if (http_server.Init("0.0.0.0", 8080, 2) != 0) {
        printf("init http server failed! %s:%d\n", http_ip.c_str(), 8080);
        return 1;
    }

    http_server.AddCallback("/test", HttpTestCallback);
    http_server.Start();
    b_time = common::TimeUtils::TimestampMs();
    char stop;
    std::cin >> stop;
    return 0;
}
