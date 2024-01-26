#include <gtest/gtest.h>

#include <iostream>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#define private public
#include "websocket/websocket_server.h"

std::condition_variable cond;
std::mutex con_m;
zjchain::ws::WebSocketServer ws_server_;
typedef websocketpp::client<websocketpp::config::asio_client> client;
client c;
void run_cb() {
    std::cout << "start now." << std::endl;
    c.run();
    std::cout << "stop now." << std::endl;
}

void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
    cond.notify_all();
    std::cout << "get ws server data: " << msg->get_payload() << std::endl;
}

void ServerCallback(websocketpp::connection_hdl hdl, const std::string& msg) {
    std::cout << "server callback: " << msg << std::endl;
    ws_server_.Send(hdl, msg.c_str(), msg.size());
}

void CloseCallback(websocketpp::connection_hdl hdl) {
    std::cout << "client closed" << std::endl;
}

namespace zjchain {

namespace ws {

namespace test {

class TestWsServer : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestWsServer, InitAndPop) {
    ASSERT_EQ(ws_server_.Init("0.0.0.0", 9082, CloseCallback), 0);
    ws_server_.RegisterCallback("test", ServerCallback);
    ws_server_.Start();
    std::string uri = "ws://127.0.0.1:9082";
    c.init_asio();
    c.set_access_channels(websocketpp::log::alevel::none);
    c.set_error_channels(websocketpp::log::alevel::none);
    auto cb = [&](websocketpp::connection_hdl, ws::WsClient::message_ptr msg) {
        cond.notify_all();
        std::cout << "get ws server data: " << msg->get_payload() << std::endl;
    };
    c.set_message_handler(cb);
    websocketpp::lib::error_code ec;
    client::connection_ptr con = c.get_connection(uri, ec);
    ASSERT_EQ(ec.value(), 0);
    c.connect(con);
    std::thread t(run_cb);
    std::string test_data("4testhello world");
    test_data[0] = (char)4;
    while (true) {
        try {
            c.send(con->get_handle(), test_data.c_str(), test_data.size(), websocketpp::frame::opcode::value::text);
            break;
        } catch (std::exception& e) {
        }
        usleep(100000);
    }

    std::unique_lock<std::mutex> lock(con_m);
    cond.wait(lock);
    con->close(websocketpp::close::status::normal, "");
    c.stop();
    t.join();
    usleep(1000000);
    ws_server_.Stop();
}

}  // namespace test

}  // namespace http

}  // namespace zjchain
