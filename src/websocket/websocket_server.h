#pragma once

#include <limits>
#include <mutex>
#include <set>
#include <unordered_set>

#include "websocket/websocket_utils.h"
#include "common/utils.h"
#include "common/spin_mutex.h"

#include <websocketpp/server.hpp>

namespace zjchain {

namespace ws {

class WebSocketServer {
    typedef websocketpp::server<websocketpp::config::asio> server;

public:
    WebSocketServer();
    int32_t Init(const char* ip, uint16_t port, WebsocketCloseCallback close_cb);
    void Start();
    void Send(websocketpp::connection_hdl hdl, const char* msg, int32_t size);
    void Send(websocketpp::connection_hdl hdl, const std::string& msg);
    void Stop();
    // must init register all callback
    void RegisterCallback(const std::string& type, WebsocketServerCallback cb);

private:
    void OnOpen(websocketpp::connection_hdl hdl);
    void OnClose(websocketpp::connection_hdl hdl);
    void OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg);
    void Run();

    server server_;
    std::string ws_ip_;
    uint16_t ws_port_{ 0 };
    std::thread* run_thread_{ nullptr };
    std::unordered_map<std::string, WebsocketServerCallback> processors_;
    WebsocketCloseCallback close_cb_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(WebSocketServer);
};

};  // namespace tcp

};  // namespace zjchain
