#include "websocket/websocket_client.h"

namespace zjchain {

namespace ws {

WebSocketClient::WebSocketClient() {}
WebSocketClient::~WebSocketClient() {

}

int32_t WebSocketClient::Init(WsClient::message_handler cb) {
    client_.init_asio();
    client_.set_access_channels(websocketpp::log::alevel::none);
    client_.set_error_channels(websocketpp::log::alevel::none);
    client_.set_message_handler(cb);
    return 0;
}

void WebSocketClient::Start() {
    run_thread_ = new std::thread([&]() { 
        client_.run();
    });
}

WsClient::connection_ptr WebSocketClient::Connect(const std::string& uri) {
    websocketpp::lib::error_code ec;
    WsClient::connection_ptr con = client_.get_connection(uri, ec);
    if (ec.value() != 0) {
        return nullptr;
    }

    return client_.connect(con);
}

void WebSocketClient::Close(WsClient::connection_ptr con) {
    if (con == nullptr) {
        return;
    }

    con->close(websocketpp::close::status::normal, "");
}

int WebSocketClient::Send(
        WsClient::connection_ptr con,
        const std::string& type,
        const std::string& msg) {
    return Send(con, type, msg.c_str(), msg.size());
}

int WebSocketClient::Send(
        WsClient::connection_ptr con,
        const std::string& type,
        const char* msg,
        size_t len) {
    assert(con != nullptr);
    assert(type.size() < std::numeric_limits<char>::max());
    std::shared_ptr<char> send_buf(new char[len + type.size() + 1], [](char* p) { delete[]p; });
    char* data = send_buf.get();
    data[0] = type.size();
    memcpy(data + 1, type.c_str(), type.size());
    memcpy(data + 1 + type.size(), msg, len);
    int32_t try_times = 0;
    while (try_times < 10) {
        try {
            client_.send(
                con->get_handle(),
                data,
                len + type.size() + 1,
                websocketpp::frame::opcode::value::text);
            break;
        } catch (std::exception& e) {
            ++try_times;
            std::this_thread::sleep_for(std::chrono::microseconds(50000ull));
        }
    }

    return 0;
}

void WebSocketClient::Stop() {
    client_.stop();
    if (run_thread_ != nullptr) {
        run_thread_->join();
        delete run_thread_;
        run_thread_ = nullptr;
    }
}

};  // namespace tcp

};  // namespace zjchain
