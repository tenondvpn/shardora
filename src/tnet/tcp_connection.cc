#include "tnet/tcp_connection.h"

#include "common/time_utils.h"
#include "tnet/utils/cmd_packet.h"
#include "tnet/socket/client_socket.h"
#include <common/log.h>

namespace zjchain {

namespace tnet {

TcpConnection::TcpConnection(EventLoop& event_loop) : event_loop_(event_loop) {
    create_timestamp_ms_ = common::TimeUtils::TimestampMs();
}

TcpConnection::~TcpConnection() {
    if (socket_ != NULL) {
        socket_->Close();
        socket_->Free();
        socket_ = nullptr;
    }

    if (packet_encoder_ != NULL) {
        packet_encoder_->Free();
        packet_encoder_ = nullptr;
    }

    if (packet_decoder_ != NULL) {
        packet_decoder_->Free();
        packet_decoder_ = nullptr;
    }
}

void TcpConnection::SetPacketEncoder(PacketEncoder* encoder) {
    if (packet_encoder_ != NULL) {
        packet_encoder_->Free();
    }

    packet_encoder_ = encoder;
}

void TcpConnection::SetPacketDecoder(PacketDecoder* decoder) {
    if (packet_decoder_ != NULL) {
        packet_decoder_->Free();
    }

    packet_decoder_ = decoder;
}

void TcpConnection::SetPacketHandler(const PacketHandler& handler) {
    packet_handler_ = handler;
    packet_handler_(this, CmdPacketFactory::Create(CmdPacket::CT_TCP_NEW_CONNECTION));
}

uint64_t TcpConnection::GetBytesRecv() const {
    return bytes_recv_;
}

uint64_t TcpConnection::GetBytesSend() const {
    return bytes_sent_;
}

void TcpConnection::Destroy(bool closeSocketImmediately) {
    int16_t new_val = 0;
    int16_t old_val = 1;
    if (destroy_flag_.compare_exchange_strong(new_val, old_val)) {
        // if (closeSocketImmediately) {
            Close();
        // }
        // event_loop_.PostTask(std::bind(&TcpConnection::ReleaseByIOThread, this));
    }

    free_timeout_ms_ = common::TimeUtils::TimestampMs() + 10000lu;;
}

bool TcpConnection::SendPacket(Packet& packet) {
    bool rc = false;
    {
        common::AutoSpinLock l(spin_mutex_);
        rc = SendPacketWithoutLock(packet);
    }

    if (rc) {
        packet.Free();
    }

    return rc;
}

bool TcpConnection::SendPacketWithoutLock(Packet& packet) {
    if (tcp_state_ == kTcpNone || tcp_state_ == kTcpClosed) {
        ZJC_ERROR("bad state, %d", tcp_state_);
        return false;
    }

    ByteBufferPtr buf_ptr(new ByteBuffer);
    if (!packet_encoder_->Encode(packet, buf_ptr.get())) {
        ZJC_ERROR("encode packet failed");
        return false;
    }

    if (tcp_state_ != kTcpConnected || !out_buffer_list_.empty()) {
        if (out_buffer_list_.size() >= OUT_BUFFER_LIST_SIZE) {
            ZJC_ERROR("out_buffer_list_ out of size %d, %d", OUT_BUFFER_LIST_SIZE, out_buffer_list_.size());
            return false;
        }

        out_buffer_list_.push_back(buf_ptr);
        if (max_count_ < out_buffer_list_.size()) {
            max_count_ = out_buffer_list_.size();
        }

        return true;
    }

    bool rc = true;
    while (true) {
        size_t len = buf_ptr->length();
        if (len == 0) {
            break;
        }

        int n = socket_->Write(buf_ptr->data(), len);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ZJC_ERROR("write failed on [%d] [%s]", socket_->GetFd(), strerror(errno));
                rc = false;
            }

            break;
        }

        bytes_sent_.fetch_add(n);
        buf_ptr->AddOffset(n);
    }

    if (rc) {
        if (buf_ptr->length() > 0) {
            out_buffer_list_.push_back(buf_ptr);
        } else if (action_ != kActionNone) {
            event_loop_.PostTask(std::bind(
                    &TcpConnection::ActionAfterPacketSent, this));
            event_loop_.Wakeup();
        } else if (!writeable_handle_list_.empty()) {
            event_loop_.PostTask(std::bind(
                    &TcpConnection::NotifyWriteable, this, true, true));
            event_loop_.Wakeup();
        }
    }

    return rc;
}

bool TcpConnection::Connect(uint32_t timeout) {
    common::AutoSpinLock l(spin_mutex_);
    return ConnectWithoutLock(timeout);
}

void TcpConnection::Close() {
    common::AutoSpinLock l(spin_mutex_);
    CloseWithoutLock();
}

void TcpConnection::CloseWithoutLock() {
    ZJC_DEBUG("connection socket closed tcp_state_: %d", tcp_state_);
    event_loop_.DisableIoEvent(socket_->GetFd(), kEventRead, *this);
    event_loop_.DisableIoEvent(socket_->GetFd(), kEventWrite, *this);
    if (tcp_state_ != kTcpClosed) {
        tcp_state_ = kTcpClosed;
        if (socket_ != NULL) {
            socket_->Close();
            socket_->Free();
            socket_ = nullptr;
        }
    }

    ZJC_DEBUG("connection socket closed tcp_state_: %d", tcp_state_);
}

void TcpConnection::NotifyWriteable(bool need_release, bool lock) {
    if (tcp_state_ != kTcpConnected) {
        return;
    }

    WriteableHandlerList tmpList;
    if (lock) {
        common::AutoSpinLock l(spin_mutex_);
        writeable_handle_list_.swap(tmpList);
    } else {
        writeable_handle_list_.swap(tmpList);
    }

//     while (!tmpList.empty()) {
//         auto& item = tmpList.front();
//         item();
//         tmpList.pop_front();
//     }
    for (WriteableHandlerListConstIter iter = tmpList.begin();
        iter != tmpList.end(); ++iter) {
        (*iter)();
    }
}

void TcpConnection::ActionAfterPacketSent() {
    bool good = false;
    {
        common::AutoSpinLock l(spin_mutex_);
        good = tcp_state_ == kTcpConnected;
    }

    if (good) {
        ZJC_ERROR("close connection after packet sent");
        NotifyCmdPacketAndClose(CmdPacket::CT_CONNECTION_CLOSED);
    }
}

bool TcpConnection::OnRead() {
    int type = CmdPacket::CT_NONE;
    volatile bool userBreak = false;
    char buf[10 * 1024];

    spin_mutex_.lock();
    if (tcp_state_ == kTcpConnecting && !ProcessConnecting()) {
        type = CmdPacket::CT_CONNECT_ERROR;
        spin_mutex_.unlock();
        return false;
    }

    while (tcp_state_ == kTcpConnected) {
        int n = socket_->Read(buf, sizeof(buf));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                type = CmdPacket::CT_READ_ERROR;
            }

            break;
        }

        if (!packet_decoder_->Decode(buf, n)) {
            type = CmdPacket::CT_INVALID_PACKET;
            break;
        }

        while (!userBreak) {
            Packet* packet = packet_decoder_->GetPacket();
            if (packet == NULL) {
                break;
            }

            spin_mutex_.unlock();
            create_timestamp_ms_ = common::TimeUtils::TimestampMs();
            if (!packet_handler_(this, *packet)) {
                userBreak = true;
            }

            spin_mutex_.lock();
        }

        if (userBreak) {
            break;
        }

        if (n == 0) {
            type = CmdPacket::CT_CONNECTION_CLOSED;
            break;
        }
    }

    if (userBreak) {
        assert(type == CmdPacket::CT_NONE);
        CloseWithoutLock();
    }

    spin_mutex_.unlock();
    if (type != CmdPacket::CT_NONE) {
        NotifyCmdPacketAndClose(type);
        return false;
    }

    return !userBreak;
}

void TcpConnection::OnWrite() {
    spin_mutex_.lock();
    if (tcp_state_ == kTcpConnecting && !ProcessConnecting()) {
        spin_mutex_.unlock();
        NotifyCmdPacketAndClose(CmdPacket::CT_CONNECT_ERROR);
        return;
    }

    if (tcp_state_ != kTcpConnected) {
        spin_mutex_.unlock();
        return;
    }

    if (out_buffer_list_.empty()) {
        NotifyWriteable(false, false);
        spin_mutex_.unlock();
        return;
    }

    bool ioError = false;
    bool writeAble = true;
    while (!out_buffer_list_.empty()) {
        ByteBufferPtr& bufferPtr = out_buffer_list_.front();
        while (true) {
            size_t len = bufferPtr->length();
            if (len == 0) {
                out_buffer_list_.pop_front();
                break;
            }

            int n = socket_->Write(bufferPtr->data(), len);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ZJC_ERROR("write failed on fd[%d] [%s]", 
                                 socket_->GetFd(), 
                                 strerror(errno));
                    ioError = true;
                } else {
                    ZJC_ERROR("writeAble false, [%d] [%s], n: %d", socket_->GetFd(), strerror(errno), n);
                    writeAble = false;
                }

                break;
            }

            bufferPtr->AddOffset(n);
        }

        if (!writeAble || ioError) {
            break;
        }
    }

    int cmd_type = CmdPacket::CT_NONE;
    if (ioError) {
        cmd_type = CmdPacket::CT_WRITE_ERROR;
    } else if (writeAble) {
        if (action_ == kActionClose) {
            ZJC_ERROR("close connection after packet sent");
            cmd_type = CmdPacket::CT_CONNECTION_CLOSED;
        } else {
            NotifyWriteable(false, false);
        }
    }

    spin_mutex_.unlock();
    if (cmd_type != CmdPacket::CT_NONE) {
        NotifyCmdPacketAndClose(cmd_type);
    }
}

bool TcpConnection::ConnectWithoutLock(uint32_t timeout) {
    if (tcp_state_ != kTcpNone) {
        ZJC_ERROR("bad state");
        return false;
    }

    if (!packet_handler_) {
        ZJC_ERROR("packet handler must be set");
        return false;
    }

    if (socket_ == NULL) {
        ZJC_ERROR("socket must be set");
        return false;
    }

    ClientSocket* socket = dynamic_cast<ClientSocket*>(socket_);
    if (socket == NULL) {
        ZJC_ERROR("cast to TcpClientSocket failed");
        return false;
    }

    if (!event_loop_.EnableIoEvent(socket->GetFd(), kEventRead | kEventWrite, *this)) {
        ZJC_ERROR("enable read or write event failed");
        return false;
    }

//     int optval = 1;
//     socket->SetOption(SO_REUSEPORT, &optval, sizeof(optval));
    int rc = socket->Connect();
    if (rc < 0) {
        ZJC_ERROR("connect failed");
        socket->Close();
        return false;
    }

    if (rc == 0) {
        tcp_state_ = kTcpConnected;
    } else {
        tcp_state_ = kTcpConnecting;
//         if (timeout != 0) {
//             connect_timeout_tick_.CutOff(
//                     timeout,
//                     std::bind(&TcpConnection::OnConnectTimeout, this));
//         }
    }

    create_timestamp_ms_ = common::TimeUtils::TimestampMs();
    return true;
}

bool TcpConnection::ProcessConnecting() {
    if (tcp_state_ != kTcpConnecting) {
//         ZJC_ERROR("bad state [%d]", tcp_state_);
        return false;
    }

    int code = 0;
    if (!socket_->GetSoError(&code) || code != 0) {
//         ZJC_ERROR("connect error");
        return false;
    }

    tcp_state_ = kTcpConnected;
//     connect_timeout_tick_.Destroy();
    return true;
}

void TcpConnection::OnConnectTimeout() {
    NotifyCmdPacketAndClose(CmdPacket::CT_CONNECT_TIMEOUT);
}

void TcpConnection::NotifyCmdPacketAndClose(int type) {
    packet_handler_(this, CmdPacketFactory::Create(type));
    Close();
}

void TcpConnection::ReleaseByIOThread() {
    assert(false);
    common::AutoSpinLock l(spin_mutex_);
    tcp_state_ = kTcpClosed;
}

}  // namespace tnet

}  // namespace zjchain
