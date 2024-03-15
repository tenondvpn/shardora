#pragma once

#include <vector>

#include "common/spin_mutex.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "tnet/tcp_interface.h"
#include "tnet/tnet_utils.h"
#include "tnet/socket/socket.h"
#include "tnet/event/event_loop.h"
#include "tnet/utils/bytes_buffer.h"
#include "tnet/utils/packet_decoder.h"
#include "tnet/utils/packet_encoder.h"
#include "tnet/utils/msg_packet.h"

namespace zjchain {

namespace tnet {

class TcpConnection : public EventHandler, public TcpInterface {
public:
    enum TcpState : int32_t {
        kTcpNone,
        kTcpConnecting,
        kTcpConnected,
        kTcpClosed
    };

    enum Action {
        kActionNone,
        kActionClose
    };

    TcpConnection(EventLoop& event_loop);
    virtual ~TcpConnection();

    virtual std::string PeerIp() {
        return peer_node_public_ip_;
    }

    virtual uint16_t PeerPort() {
        return peer_node_public_port_;
    }

    virtual void SetPeerIp(const std::string& ip) {
        peer_node_public_ip_ = ip;
    }

    virtual void SetPeerPort(uint16_t port) {
        peer_node_public_port_ = port;
    }

    virtual int Send(const std::string& data) {
        return Send(data.c_str(), data.size());
    }

    virtual int Send(const char* data, int32_t len) {
        MsgPacket* reply_packet = new MsgPacket(0, tnet::kEncodeWithHeader, false);
        // local message is thread safe and don't free memory
        reply_packet->SetMessage((char*)data, len);
        if (!SendPacket(*reply_packet)) {
            reply_packet->Free();
            return 1;
        }

        return 0;
    }

    void SetPacketEncoder(PacketEncoder* encoder);
    void SetPacketDecoder(PacketDecoder* decoder);
    void SetPacketHandler(const PacketHandler& handler);
    uint64_t GetBytesRecv() const;
    uint64_t GetBytesSend() const;
    void Destroy(bool closeSocketImmediately);
    virtual bool SendPacket(Packet& packet);
    virtual bool SendPacketWithoutLock(Packet& packet);
    virtual bool Connect(uint32_t timeout);
    virtual void Close();
    virtual void CloseWithoutLock();
    void SetTcpState(TcpState state) {
        tcp_state_ = state;
    }

    int32_t GetTcpState() {
        return tcp_state_;
    }

    void SetAction(int action) {
        action_ = action;
    }

    EventLoop& GetEventLoop() const {
        return event_loop_;
    }

    Socket* GetSocket() const {
        return socket_;
    }

    void SetSocket(Socket& socket) {
        socket_ = &socket;
    }

    uint32_t id() {
        return id_;
    }

    void set_id(uint32_t id) {
        id_ = id;
    }

    uint64_t free_timeout_ms() {
        return free_timeout_ms_;
    }
    
    bool ShouldReconnect() {
        auto now_tm_ms = common::TimeUtils::TimestampMs();
        if (now_tm_ms >= create_timestamp_ms_ + kConnectTimeoutMs) {
            ZJC_DEBUG("should remove connect timeout.");
            return true;
        }

        if (GetTcpState() == tnet::TcpConnection::kTcpClosed) {
            ZJC_DEBUG("should remove connect lost.");
            return true;
        }

        return false;
    }

    bool is_client() const { 
        return is_client_;
    }

    void set_client() {
        is_client_ = true;
    }

private:
    typedef std::deque<ByteBufferPtr> BufferList;
    typedef BufferList::const_iterator BufferListConstIter;
    typedef BufferList::iterator BufferListIter;
    typedef std::vector<WriteableHandler> WriteableHandlerList;
    typedef WriteableHandlerList::const_iterator WriteableHandlerListConstIter;
    typedef WriteableHandlerList::iterator WriteableHandlerListIter;

    void NotifyWriteable(bool needRelease, bool inLock);
    void ActionAfterPacketSent();
    virtual bool OnRead();
    virtual void OnWrite();
    bool ConnectWithoutLock(uint32_t timeout);
    bool ProcessConnecting();
    void OnConnectTimeout();
    void NotifyCmdPacketAndClose(int type);
    void ReleaseByIOThread();

    static const uint64_t kConnectTimeoutMs = 60000lu;
    static const int OUT_BUFFER_LIST_SIZE = 10240;

    common::SpinMutex spin_mutex_;
    BufferList out_buffer_list_;
    WriteableHandlerList writeable_handle_list_;
    volatile TcpState tcp_state_{ kTcpNone };
    int action_{ 0 };
    EventLoop& event_loop_;
    Socket* socket_{ nullptr };
    PacketEncoder* packet_encoder_{ nullptr };
    PacketDecoder* packet_decoder_{ nullptr };
    std::atomic<int64_t> bytes_recv_{ 0 };
    std::atomic<int64_t> bytes_sent_{ 0 };
    PacketHandler packet_handler_;
    std::atomic<int16_t> destroy_flag_{ 0 };
//     common::Tick connect_timeout_tick_;
    std::string peer_node_public_ip_;
    uint16_t peer_node_public_port_{ 0 };
    uint32_t id_{ 0 };
    uint64_t free_timeout_ms_{ 0 };
    uint32_t max_count_{ 0 };
    uint64_t create_timestamp_ms_ = 0;
    bool is_client_ = false;

    DISALLOW_COPY_AND_ASSIGN(TcpConnection);
};

}  // namespace tnet

}  // namespace zjchain
