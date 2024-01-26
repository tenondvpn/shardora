#pragma once

#include <iostream>

#include "websocket/websocket_utils.h"
namespace zjchain {

namespace ws {

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    int32_t Init(WsClient::message_handler cb);
    WsClient::connection_ptr Connect(const std::string& uri);
    void Close(WsClient::connection_ptr con);
    int Send(WsClient::connection_ptr con, const std::string& type, const std::string& msg);
    int Send(WsClient::connection_ptr con, const std::string& type, const char* msg, size_t len);
    void Start();
    void Stop();

private:
    WsClient client_;
    std::thread* run_thread_{ nullptr };

    DISALLOW_COPY_AND_ASSIGN(WebSocketClient);
};

};  // namespace tcp

};  // namespace zjchain
