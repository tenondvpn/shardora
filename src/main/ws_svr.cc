#include "websocket/websocket_server.h"

#include "common/time_utils.h"

using namespace zjchain;

int main(int argc, char* argv[]) {
    ws::WebSocketServer ws_server;
    std::atomic<uint32_t> recv_count = 0;
    std::mutex test_m;
    auto b_time = common::TimeUtils::TimestampMs();
    auto svr_cb = [&](websocketpp::connection_hdl hdl, const std::string& msg) {
        if (recv_count >= 1000000)
        {
            std::lock_guard<std::mutex> g(test_m);
            if (recv_count >= 1000000) {
                auto e_time = common::TimeUtils::TimestampMs();
                std::cout << "qps: " << (float(recv_count) / float(float(e_time - b_time) / 1000.0f)) << std::endl;
                recv_count = 0;
                b_time = e_time;
            }
        }

        ++recv_count;
        ws_server.Send(hdl, msg.c_str(), msg.size());
    };

    auto close_cb = [&](websocketpp::connection_hdl hdl) {
    };

    if (ws_server.Init("0.0.0.0", 9082, close_cb) != 0) {
        std::cout << "init ws server failed!" << std::endl;
        return 1;
    }

    ws_server.RegisterCallback("test", svr_cb);
    ws_server.Start();
    char stop;
    std::cin >> stop;
    return 0;
}