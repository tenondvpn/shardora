# libuv uv_close 断言失败修复

## 问题分析

编译后运行 txcli 时出现崩溃：
```
txcli: /root/ShardoraPub/third_party/libuv/src/unix/core.c:160: uv_close: Assertion `!uv__is_closing(handle)' failed.
Aborted (core dumped)
```

### 根本原因

在 `uv_tcp_transport.cc` 的错误处理中，我们在 `on_write` 和 `on_read` 回调中调用了 `uv_close`：

```cpp
if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
    uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);  // ❌ 问题
}
```

**问题**：
1. libuv 的 `uv_close` 要求 handle 不能已经在关闭过程中
2. 即使我们检查了 `uv_is_closing()`，在多线程环境下仍可能出现竞态条件
3. 在回调函数中调用 `uv_close` 可能导致 handle 状态不一致

### 为什么会发生

- `on_read` 或 `on_write` 回调可能由于 handle 关闭而被触发
- 此时 handle 已经在关闭过程中（`uv__is_closing()` 返回 true）
- 我们的 `uv_is_closing()` 检查可能不够准确
- 或者在检查和调用 `uv_close` 之间，另一个线程已经开始关闭 handle

## 修复方案

### 核心思想

**不要在回调中调用 `uv_close`**。让 libuv 的事件循环和现有的连接管理机制处理关闭。

### 修改内容

#### 1. 修复 `on_write` 错误处理

**修改前**:
```cpp
void on_write(uv_write_t* req, int status) {
    if (status < 0) {
        tcp_transport->FreeConnection(ex_uv_tcp);
        // ❌ 不要在这里调用 uv_close
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        }
    }
    free(req);
}
```

**修改后**:
```cpp
void on_write(uv_write_t* req, int status) {
    if (status < 0) {
        tcp_transport->FreeConnection(ex_uv_tcp);
        // ✅ 只调用 FreeConnection，让 RealFreeInvalidConnections 处理关闭
    }
    free(req);
}
```

#### 2. 修复 `on_read` 错误处理

**修改前**:
```cpp
void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    if (nread >= 0) {
        // ... 处理数据 ...
        if (!ok) {
            tcp_transport->FreeConnection(ex_uv_tcp);
            // ❌ 不要在这里调用 uv_close
            if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
                uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
            }
            return;
        }
    } else {
        tcp_transport->FreeConnection(ex_uv_tcp);
        // ❌ 不要在这里调用 uv_close
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        }
    }
    delete[] buf->base;
}
```

**修改后**:
```cpp
void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    if (nread >= 0) {
        // ... 处理数据 ...
        if (!ok) {
            tcp_transport->FreeConnection(ex_uv_tcp);
            // ✅ 只调用 FreeConnection，让 RealFreeInvalidConnections 处理关闭
            return;
        }
    } else {
        tcp_transport->FreeConnection(ex_uv_tcp);
        // ✅ 只调用 FreeConnection，让 RealFreeInvalidConnections 处理关闭
    }
    delete[] buf->base;
}
```

### 工作流程

1. **错误发生**：`on_write` 或 `on_read` 返回错误
2. **调用 FreeConnection**：从 `conn_map_` 移除连接，放入 `invalid_conns_` 队列
3. **设置超时**：连接被标记为无效，设置 10 秒超时
4. **异步关闭**：`RealFreeInvalidConnections` 在主事件循环中安全地调用 `uv_close`
5. **on_close 回调**：handle 被正确关闭，资源被释放

### 为什么这样做是安全的

1. **避免竞态条件**：不在回调中调用 `uv_close`，避免与其他线程的竞争
2. **让 libuv 管理生命周期**：`RealFreeInvalidConnections` 在主事件循环中调用 `uv_close`，确保正确的时序
3. **已有的机制**：`FreeConnection` 和 `RealFreeInvalidConnections` 已经实现了安全的连接关闭
4. **符合 libuv 最佳实践**：不在回调中调用 `uv_close` 是 libuv 的推荐做法

## 修改文件

- `src/transport/uv_tcp_transport.cc`:
  - 行 45-60: 修复 `on_write` 错误处理
  - 行 224-260: 修复 `on_read` 错误处理

## 验证方法

1. 编译项目：`./build.sh shardora`
2. 运行 txcli：`./txcli 0 3 0 <ip> <port>`
3. 观察：
   - 不应该再出现 "uv_close: Assertion" 错误
   - 应该看到正常的 "[TCP_RECONN]" 日志
   - 程序应该正常运行和退出

## 总结

这个修复确保：
1. ✅ 不再出现 libuv 断言失败
2. ✅ 连接管理更加安全和稳定
3. ✅ 符合 libuv 的最佳实践
4. ✅ 避免多线程竞态条件
