#include "tnet/utils/cmd_packet.h"

namespace zjchain {

namespace tnet {

namespace {

static CmdPacket None(CmdPacket::CT_NONE);
static CmdPacket ReadError(CmdPacket::CT_READ_ERROR);
static CmdPacket WriteError(CmdPacket::CT_WRITE_ERROR);
static CmdPacket ConnectError(CmdPacket::CT_CONNECT_ERROR);
static CmdPacket InvalidPacket(CmdPacket::CT_INVALID_PACKET);
static CmdPacket TlsReadError(CmdPacket::CT_TLS_READ_ERROR);
static CmdPacket TlsWriteError(CmdPacket::CT_TLS_WRITE_ERROR);
static CmdPacket TlsHandshakeError(CmdPacket::CT_TLS_HANDSHAKE_ERROR);
static CmdPacket ConnectionClosed(CmdPacket::CT_CONNECTION_CLOSED);
static CmdPacket ConnectTimeout(CmdPacket::CT_CONNECT_TIMEOUT);
static CmdPacket PacketTimeout(CmdPacket::CT_PACKET_TIMEOUT);
static CmdPacket HttpKeepAliveTimeout(CmdPacket::CT_HTTP_KEEPALIVE_TIMEOUT);
static CmdPacket HttpIdleTimeout(CmdPacket::CT_HTTP_IDLE_TIMEOUT);
static CmdPacket WebSocketProtocolError(CmdPacket::CT_WS_PROTOCOL_ERROR);
static CmdPacket WebSocketNonUtf8(CmdPacket::CT_WS_NON_UTF8);
static CmdPacket WebSocketUnexpectedError(CmdPacket::CT_WS_UNEXPECTED_ERROR);
static CmdPacket TcpNewConnectionPacket(CmdPacket::CT_TCP_NEW_CONNECTION);

}

CmdPacket& CmdPacketFactory::Create(int type) {
    switch (type) {
        case CmdPacket::CT_READ_ERROR:
            return ReadError;
        case CmdPacket::CT_WRITE_ERROR:
            return WriteError;
        case CmdPacket::CT_CONNECT_ERROR:
            return ConnectError;
        case CmdPacket::CT_INVALID_PACKET:
            return InvalidPacket;
        case CmdPacket::CT_TLS_READ_ERROR:
            return TlsReadError;
        case CmdPacket::CT_TLS_WRITE_ERROR:
            return TlsWriteError;
        case CmdPacket::CT_TLS_HANDSHAKE_ERROR:
            return TlsHandshakeError;
        case CmdPacket::CT_CONNECTION_CLOSED:
            return ConnectionClosed;
        case CmdPacket::CT_CONNECT_TIMEOUT:
            return ConnectTimeout;
        case CmdPacket::CT_PACKET_TIMEOUT:
            return PacketTimeout;
        case CmdPacket::CT_HTTP_KEEPALIVE_TIMEOUT:
            return HttpKeepAliveTimeout;
        case CmdPacket::CT_HTTP_IDLE_TIMEOUT:
            return HttpIdleTimeout;
        case CmdPacket::CT_WS_PROTOCOL_ERROR:
            return WebSocketProtocolError;
        case CmdPacket::CT_WS_NON_UTF8:
            return WebSocketNonUtf8;
        case CmdPacket::CT_WS_UNEXPECTED_ERROR:
            return WebSocketUnexpectedError;
        case CmdPacket::CT_TCP_NEW_CONNECTION:
            return TcpNewConnectionPacket;
        default:
            assert(false);
            break;
    }

    return None;
}

}  // namespace tnet

}  // namespace zjchain
