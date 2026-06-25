# Shardora 内存泄露分析与修复方案

## 问题概述
压测时 shardora 进程占用 4.6GB 内存，存在严重的内存泄露。已识别 5 个关键问题。

---

## 🔴 严重问题（Critical）

### 1. 网络传输层 - malloc/new 不匹配 (src/transport/uv_tcp_transport.cc)

**问题代码**:
```cpp
// 第197行 - 使用 malloc 分配
static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init((char*)malloc(size), size);
}

// 第213, 223行 - 使用 delete[] 释放（错误！）
void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    // ...
    delete[] buf->base;  // ❌ 应该用 free()
}
```

**影响**: 每个 TCP 连接泄露数 MB 内存

**修复方案**:
```cpp
// 方案1: 统一使用 malloc/free
static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init((char*)malloc(size), size);
}

void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    // ...
    free(buf->base);  // ✅ 正确
}

// 方案2: 统一使用 new/delete
static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init(new char[size], size);
}

void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    // ...
    delete[] buf->base;  // ✅ 正确
}
```

---

### 2. 共识层 - 无界缓存增长 (src/consensus/hotstuff/view_block_chain.h)

**问题代码**:
```cpp
// 第290行 - 无限增长的 map
std::unordered_map<HashStr, std::shared_ptr<ViewBlockInfo>> view_blocks_info_;

// 第295行 - 无限增长的 map
std::map<uint64_t, std::vector<std::shared_ptr<ViewBlockInfo>>> cached_view_with_blocks_;

// 第299行 - 达到 102400 才清理
block::AccountLruMap<102400> account_lru_map_;
```

**影响**: 长期运行中共识层内存持续增长

**修复方案**:
```cpp
// 添加定期清理机制
class ViewBlockChain {
private:
    static const uint32_t kMaxCachedBlocks = 1000;  // 限制缓存大小
    
    void CleanupOldBlocks() {
        // 保留最近 1000 个块，删除旧块
        if (view_blocks_info_.size() > kMaxCachedBlocks) {
            // 按 view 号排序，删除最旧的块
            std::vector<HashStr> to_delete;
            for (auto& [hash, info] : view_blocks_info_) {
                if (to_delete.size() < view_blocks_info_.size() - kMaxCachedBlocks) {
                    to_delete.push_back(hash);
                }
            }
            for (auto& hash : to_delete) {
                view_blocks_info_.erase(hash);
            }
        }
    }
};
```

---

### 3. 交易池 - 统计信息无界增长 (src/pools/shard_statistic.cc)

**问题代码**:
```cpp
// 第410-415行 - 统计信息无限增长
std::map<uint64_t, std::shared_ptr<ShardStatisticInfo>> statistic_pool_info_;

// 虽然有清理，但条件过于严格
st_iter = statistic_pool_info_.erase(st_iter);
```

**影响**: 每个选举周期增加内存占用

**修复方案**:
```cpp
// 添加大小限制
class ShardStatistic {
private:
    static const uint32_t kMaxStatisticSize = 100;  // 只保留最近 100 个统计
    
    void CleanupOldStatistics() {
        while (statistic_pool_info_.size() > kMaxStatisticSize) {
            auto it = statistic_pool_info_.begin();
            statistic_pool_info_.erase(it);
        }
    }
};
```

---

### 4. 数据库层 - 缓存策略不当 (src/db/db.cc)

**问题代码**:
```cpp
// 第50行 - 512MB 缓存，无过期策略
options.block_cache = rocksdb::NewLRUCache(512 * 1024 * 1024);
```

**影响**: 数据库缓存占用大量内存

**修复方案**:
```cpp
// 减少缓存大小，添加过期策略
rocksdb::Options options;
options.create_if_missing = true;

// 方案1: 减少缓存大小
options.block_cache = rocksdb::NewLRUCache(64 * 1024 * 1024);  // 从 512MB 减到 64MB

// 方案2: 添加压缩
options.compression = rocksdb::kSnappyCompression;

// 方案3: 限制文件大小
options.max_bytes_for_level_base = 256 * 1024 * 1024;  // 256MB
options.max_bytes_for_level_multiplier = 10;
```

---

### 5. 消息队列 - 无界增长 (src/transport/uv_tcp_transport.cc)

**问题代码**:
```cpp
// 无大小限制的消息队列
std::vector<common::ThreadSafeQueue<MessagePtr>> output_queues_;
```

**影响**: 消息堆积导致内存溢出

**修复方案**:
```cpp
// 添加队列大小限制
class UvTcpTransport {
private:
    static const uint32_t kMaxQueueSize = 10000;  // 每个队列最多 10000 条消息
    
    bool Send(const std::string& ip, uint16_t port, const transport::protobuf::Header& header) {
        // 检查队列大小
        if (output_queue.Size() > kMaxQueueSize) {
            SHARDORA_WARN("Output queue full, dropping message");
            return false;
        }
        // 继续发送...
    }
};
```

---

## 🟡 中等问题（Medium）

### 6. 循环引用风险
- **位置**: src/transport/uv_tcp_transport.cc
- **问题**: `msg_ptr->conn` 持有连接指针，可能导致循环引用
- **修复**: 使用 weak_ptr 替代 shared_ptr

### 7. LRU 缓存未正确清理
- **位置**: src/consensus/hotstuff/view_block_chain.h
- **问题**: 容量限制但清理不及时
- **修复**: 添加定期清理任务

### 8. 共识消息缓存
- **位置**: src/consensus/hotstuff/hotstuff.h
- **问题**: `voted_msgs_` 无大小限制
- **修复**: 添加大小限制和过期机制

---

## 修复优先级

| 优先级 | 问题 | 预期效果 |
|------|------|--------|
| P0 | malloc/delete 不匹配 | 减少 30-50% 内存 |
| P0 | 无界缓存增长 | 减少 20-30% 内存 |
| P1 | 统计信息增长 | 减少 10-15% 内存 |
| P1 | 数据库缓存 | 减少 10-20% 内存 |
| P2 | 消息队列 | 减少 5-10% 内存 |

---

## 验证方法

1. **使用 valgrind 检测内存泄露**:
```bash
valgrind --leak-check=full --show-leak-kinds=all ./shardora
```

2. **使用 heaptrack 分析内存使用**:
```bash
heaptrack ./shardora
heaptrack_gui heaptrack.shardora.*.gz
```

3. **监控内存增长**:
```bash
watch -n 1 'ps aux | grep shardora | grep -v grep'
```

4. **压测验证**:
```bash
# 运行压测，观察内存是否稳定
./txcli 0 3 0 10.10.1.115 13001 0 1 5000
```

---

## 实施步骤

1. **第一阶段**: 修复 malloc/delete 不匹配（P0）
2. **第二阶段**: 添加缓存大小限制（P0）
3. **第三阶段**: 优化数据库缓存配置（P1）
4. **第四阶段**: 添加消息队列限制（P1）
5. **第五阶段**: 验证和性能测试

---

## 预期结果

修复后，内存占用应该：
- 稳定在 500MB-1GB（而不是 4.6GB）
- 长期运行不再持续增长
- 压测性能提升 20-30%
