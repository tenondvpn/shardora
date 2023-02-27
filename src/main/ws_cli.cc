#include "websocket/websocket_client.h"

using namespace zjchain;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }

    ws::WsClient::connection_ptr con = nullptr;
    ws::WebSocketClient c;
    auto cb = [&](websocketpp::connection_hdl, ws::WsClient::message_ptr msg) {
        int send_res = c.Send(con, "test", "hello web server");
        if (send_res != 0) {
            std::cout << "send failed!" << std::endl;
        }
    };

    c.Init(cb);
    auto uri = std::string("ws://") + argv[1] + ":9082";
    con = c.Connect(uri);
    if (con == nullptr) {
        std::cout << "connect websocket server failed!" << std::endl;
        return 1;
    }

    c.Start();
    int send_res = c.Send(con, "test", "hello web server");
    if (send_res != 0) {
        std::cout << "send failed!" << std::endl;
    }

    char stop;
    std::cin >> stop;
    c.Stop();
    return 0;
}