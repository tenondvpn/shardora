#pragma once

namespace zjchain {

namespace tnet {

class TcpInterface {
public:
    virtual std::string PeerIp() = 0;
    virtual uint16_t PeerPort() = 0;
    virtual void SetPeerIp(const std::string& ip) = 0;
    virtual void SetPeerPort(uint16_t port) = 0;
    virtual int Send(const std::string& data) = 0;
    virtual int Send(const char* data, int32_t len) = 0;
    virtual int Send(uint64_t msg_id, const std::string& data) = 0;
    virtual int Send(const char* data, int32_t len, uint64_t msg_id) = 0;

public:
    TcpInterface() {}
    virtual ~TcpInterface() {}
};

};  // namespace tnet

};  // namespace zjchain
