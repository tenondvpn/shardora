#include "websocket/websocket_server.h"

#include <evhtp/internal.h>

namespace zjchain {

namespace ws {

WebSocketServer::WebSocketServer() {}

int32_t WebSocketServer::Init(const char* ip, uint16_t port, WebsocketCloseCallback close_cb) try {
    ws_ip_ = ip;
    ws_port_ = port;
    close_cb_ = close_cb;
    server_.set_access_channels(websocketpp::log::alevel::none);
    server_.set_error_channels(websocketpp::log::alevel::none);
    server_.set_reuse_addr(true);
    server_.init_asio();
    server_.set_open_handler(websocketpp::lib::bind(
        &WebSocketServer::OnOpen,
        this,
        websocketpp::lib::placeholders::_1));
    server_.set_close_handler(websocketpp::lib::bind(
        &WebSocketServer::OnClose,
        this,
        websocketpp::lib::placeholders::_1));
    server_.set_message_handler(websocketpp::lib::bind(
        &WebSocketServer::OnMessage,
        this,
        websocketpp::lib::placeholders::_1,
        websocketpp::lib::placeholders::_2));
    return 0;
} catch (std::exception& e) {
    ZJC_ERROR("catch error: %s", e.what());
    return 1;
}

void WebSocketServer::Send(websocketpp::connection_hdl hdl, const char* msg, int32_t size) {
    server_.send(hdl, msg, size, websocketpp::frame::opcode::value::binary);
}

void WebSocketServer::Send(websocketpp::connection_hdl hdl, const std::string& msg) {
    Send(hdl, msg.c_str(), msg.size());
}

void WebSocketServer::Start() {
    run_thread_ = new std::thread(&WebSocketServer::Run, this);
}

void WebSocketServer::RegisterCallback(const std::string& type, WebsocketServerCallback cb) {
    processors_[type] = cb;
}

void WebSocketServer::Run() {
    try {
        server_.listen(websocketpp::lib::asio::ip::tcp::endpoint(
            boost::asio::ip::address_v4::from_string(ws_ip_.c_str()),
            ws_port_));
        server_.start_accept();
        server_.run();
    } catch (const std::exception & e) {
        ZJC_ERROR("start websocket server failed: %s, ip port: %s:%d", e.what(), ws_ip_.c_str(), ws_port_);
        assert(false);
        exit(1);
    }
}

void WebSocketServer::OnOpen(websocketpp::connection_hdl hdl) {}

void WebSocketServer::OnClose(websocketpp::connection_hdl hdl) {
    if (close_cb_ != nullptr) {
        close_cb_(hdl);
    }
}

void WebSocketServer::OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg) {
    const std::string& payload = msg->get_payload();
    int32_t type_len = int32_t(payload[0]);
    auto type = payload.substr(1, type_len);
    auto iter = processors_.find(type);
    if (iter == processors_.end()) {
        server_.send(hdl, kInvalidType, websocketpp::frame::opcode::text);
        return;
    }

    if (int32_t(payload.size()) < type_len + 1) {
        server_.send(hdl, kInvalidMessage, websocketpp::frame::opcode::text);
        return;
    }

    iter->second(hdl, payload.substr(type_len + 1, payload.size() - type_len - 1));
}

void WebSocketServer::Stop() {
    server_.stop();
    if (run_thread_ != nullptr) {
        run_thread_->join();
        delete run_thread_;
        run_thread_ = nullptr;
    }
}

};  // namespace ws

};  // namespace zjchain
