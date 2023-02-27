#pragma once

#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include "common/utils.h"

namespace zjchain {

namespace ws {

    typedef websocketpp::client<websocketpp::config::asio_client> WsClient;

typedef std::function<void(
    websocketpp::connection_hdl hdl,
    const std::string& msg)> WebsocketServerCallback;
typedef std::function<void(
    websocketpp::connection_hdl hdl)> WebsocketCloseCallback;

static const std::string kInvalidType = "kInvalidType";
static const std::string kInvalidMessage = "kInvalidMessage";

};  // namespace tcp

};  // namespace zjchain
