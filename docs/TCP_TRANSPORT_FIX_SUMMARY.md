# TCP 传输层连接管理修复总结

## 问题分析

`uv_tcp_transport.cc` 对 TC 层丢包非常敏感，导致连接断裂且无法恢复。根本原因是**错误处理不完整**和**缓冲区验证缺陷**。

### 根本原因链

1. **TC 层报文破坏**
   - Linux `tc netem` 模块在丢包时可能破坏 TCP 报文结构
   - 导致 MsgDecoder 接收到畸形数据
   - PacketHeader 长度字段被破坏，导致 `len > kMaxPacketBytes`

2. **错误处理不完整**
   - `on_read` 和 `on_write` 错误时只调用 `FreeConnection`（从 conn_map 移除）
   - 但不调用 `uv_close`（关闭 libuv handle）
   - 导致 libuv 继续尝试读取已断开的连接

3. **连接状态不一致**
   - conn_map 中没有该连接（已被 FreeConnection 移除）
   - 但 libuv 内部仍有该 handle 的引用
   - 下次 Send 时，GetConnection 返回 nullptr，创建新连接
   - 但旧连接的 on_read 回调仍可能被触发

4. **无法恢复的原因**
   - FreeConnection 只是从 conn_map 移除，不关闭 handle
   - invalid_conns_ 队列中的连接需要等待 10 秒才能真正关闭
   - 期间如果有新的 Send 请求，会创建新连接
   - 但旧连接的 on_read 回调仍可能被触发，导致新连接也被 FreeConnection
   - 形成恶性循环

## 修复方案

### 1. 修复 `on_write` 错误处理 (行 45-60)

**问题**: 写入失败时只调用 FreeConnection，不关闭 libuv handle

**修复**:
```cpp
void on_write(uv_write_t* req, int status) {
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)req->handle;
    if (status < 0) {
        SHARDORA_WARN("[TCP_RECONN] on_write failed: %s:%d, status=%d (%s) — closing connection",
            ex_uv_tcp->ip, ex_uv_tcp->port, status, uv_strerror(status));
        tcp_transport->FreeConnection(ex_uv_tcp);
        // 新增: 正确关闭 libuv handle 以防止进一步的回调
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        }
    } else {
        SHARDORA_DEBUG("on_write called back.");
    }
    free(req);
}
```

**效果**: 确保 libuv handle 被正确关闭，防止进一步的回调触发

### 2. 修复 `on_read` 错误处理 (行 224-250)

**问题**: 读取失败或检测到坏报文时只调用 FreeConnection，不关闭 libuv handle

**修复**:
```cpp
void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    SHARDORA_DEBUG("get client data: %d", nread);
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)tcp;
    if (nread >= 0) {
        ex_uv_tcp->msg_decoder->Decode(buf->base, nread);
        auto packet = ex_uv_tcp->msg_decoder->GetPacket();
        SHARDORA_DEBUG("get packet data: %d", (packet != nullptr));
        while (packet != nullptr) {
            bool ok = OnClientPacket(ex_uv_tcp, *packet);
            packet->Free();
            if (!ok) {
                // 坏报文: 关闭连接
                delete[] buf->base;
                tcp_transport->FreeConnection(ex_uv_tcp);
                // 新增: 正确关闭 libuv handle
                if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
                    uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
                }
                return;
            }
            packet = ex_uv_tcp->msg_decoder->GetPacket();
        }
    } else {
        SHARDORA_WARN("[TCP_RECONN] on_read error: %s:%d, nread=%zd (%s) — closing connection",
            ex_uv_tcp->ip, ex_uv_tcp->port, nread, uv_strerror(nread));
        tcp_transport->FreeConnection(ex_uv_tcp);
        // 新增: 正确关闭 libuv handle
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        }
    }
    delete[] buf->base;
}
```

**效果**: 确保所有错误路径都正确关闭 libuv handle

### 3. 修复 `OnClientPacket` 缓冲区验证 (行 162-190)

**问题**: 检测到超大或空报文时没有关闭连接（代码被注释掉了）

**修复**:
```cpp
// Reject oversized packets — normal consensus messages are well under 1 MB.
// This can happen when TC layer corrupts packet headers, causing len to be garbage.
static const uint32_t kMaxPacketBytes = 1u * 1024u * 1024u + 1024  * 512;  // 1.5 MB hard limit
if (len == 0 || len > kMaxPacketBytes) {
    SHARDORA_WARN("oversized or empty packet from %s:%d, len=%u — closing connection",
              from_ip, from_port, len);
    // 修复: 启用返回 false 以信号 on_read 关闭连接
    return false;
}
```

**效果**: 当检测到 TC 层破坏的报文时，立即关闭连接而不是继续处理

### 4. 改进 `uv_async_cb` 中的连接重用逻辑 (行 560-575)

**问题**: 使用 `uv_is_active()` 检查连接有效性不够准确

**修复**:
```cpp
if (ex_uv_tcp == nullptr) {
    ex_uv_tcp = transport::TcpTransport::Instance()->GetConnection(des_ip, des_port);
    if (ex_uv_tcp != nullptr) {
        // 检查连接是否仍然有效:
        // 1. 未关闭 (uv_is_closing 返回 true 表示已调用 close)
        // 2. Handle 类型仍为 UV_TCP (未被破坏)
        if (uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp) ||
            ex_uv_tcp->uv_tcp.type != UV_TCP) {
            SHARDORA_WARN("[TCP_RECONN] stale connection detected: %s:%d (closing=%d, type=%d) — reconnecting",
                des_ip.c_str(), des_port, 
                uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
                ex_uv_tcp->uv_tcp.type);
            transport::TcpTransport::Instance()->FreeConnection(ex_uv_tcp);
            ex_uv_tcp = nullptr;
        }
    }
}
```

**效果**: 更准确地检测陈旧连接，避免重用已关闭的连接

## 修复效果

### 修复前的问题
- TC 丢包 → 报文破坏 → on_read 检测到错误 → FreeConnection（只移除，不关闭）
- libuv 继续尝试读取 → 触发 on_read 回调 → 再次 FreeConnection
- 新连接创建 → 旧连接的 on_read 仍可能被触发 → 新连接也被 FreeConnection
- 恶性循环，无法恢复

### 修复后的流程
- TC 丢包 → 报文破坏 → on_read 检测到错误 → FreeConnection + uv_close
- libuv handle 被正确关闭，不再触发回调
- 新连接创建时，旧连接已完全关闭
- 新连接可以正常工作

## 应用层网络延迟注入的优势

项目已实现 `NetworkDelaySimulator` (network_delay_simulator.h) 来避免 TC 层报文破坏：
- 在应用层而非 TCP 层进行丢包模拟
- 避免 TCP 报文被破坏
- 配合上述修复，实现真正的网络容错能力

## 文件修改

- `src/transport/uv_tcp_transport.cc`:
  - 行 45-60: 修复 on_write 错误处理
  - 行 224-250: 修复 on_read 错误处理
  - 行 162-190: 修复 OnClientPacket 缓冲区验证
  - 行 560-575: 改进连接重用逻辑

## 验证方法

1. 编译项目: `./build.sh shardora`
2. 启用应用层网络延迟: `export SHARDORA_NETWORK_ENABLED=1`
3. 运行测试: 观察连接是否能正确恢复
4. 检查日志: 确认 `[TCP_RECONN]` 消息显示正确的关闭行为
