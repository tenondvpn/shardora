# 性能压测网络优化策略

## 问题诊断

### 核心问题
1. **大量oversized packet错误** (len=1.6-1.9MB)
   - 正常包大小: <1MB
   - 错误包大小: 1.6-1.9MB
   - 原因: 包头被破坏，长度字段读取错误

2. **连接频繁断开** (nread=-4095)
   - 每秒多个连接断开
   - 原因: 包头破坏导致连接无法恢复

3. **连接拒绝** (status=-111 connection refused)
   - 原因: 远端节点无法处理损坏的包

### 根本原因
应用层网络延迟注入在**错误的时机**应用：
- 当前: 在连接建立后、发送前应用延迟
- 问题: 延迟导致包头被破坏（TC层或网络层干扰）
- 结果: 接收端读取错误的长度字段

## 优化方案

### 方案1: 禁用网络延迟注入（推荐用于压测）
**原因**: 应用层延迟注入在高并发下不稳定

**实施**:
```bash
# 禁用网络延迟
export SHARDORA_NETWORK_ENABLED=0
```

### 方案2: 改进网络延迟注入（长期方案）
**改进点**:
1. 在应用层包级别应用延迟，而不是连接级别
2. 添加包完整性校验
3. 实现包重试机制
4. 改进延迟注入的随机性

### 方案3: 改进TCP连接管理
**改进点**:
1. 增加连接超时时间
2. 改进连接重用策略
3. 添加连接健康检查
4. 实现连接预热

## 立即可采取的行动

### 1. 禁用网络延迟注入
```bash
# 在启动脚本中添加
export SHARDORA_NETWORK_ENABLED=0
```

### 2. 增加TCP缓冲区
```cpp
// 在on_connect中
int recv_size = 20 * 1024 * 1024;  // 20MB
int send_size = 20 * 1024 * 1024;  // 20MB
uv_recv_buffer_size((uv_handle_t*)stream, &recv_size);
uv_send_buffer_size((uv_handle_t*)stream, &send_size);
```

### 3. 改进连接超时
```cpp
// 增加keepalive间隔
uv_tcp_keepalive(&ex_uv_tcp->uv_tcp, 1, 120);  // 从60改为120
```

### 4. 改进包验证
```cpp
// 在OnClientPacket中
static const uint32_t kMaxPacketBytes = 2u * 1024u * 1024u;  // 2MB
if (len == 0 || len > kMaxPacketBytes) {
    SHARDORA_WARN("oversized or empty packet from %s:%d, len=%u", ...);
    return false;
}
```

## 性能压测建议

### 禁用网络延迟的配置
```bash
# temp_cmd.sh 中
export SHARDORA_NETWORK_ENABLED=0
export SHARDORA_NETWORK_DELAY_MS=0
export SHARDORA_NETWORK_JITTER_MS=0
export SHARDORA_NETWORK_LOSS_RATE=0
```

### 优化TCP参数
```bash
# 系统级别优化
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.wmem_max=134217728
sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"
sysctl -w net.ipv4.tcp_tw_reuse=1
sysctl -w net.ipv4.tcp_fin_timeout=30
```

### 优化应用参数
```bash
# 增加连接数限制
ulimit -n 65536

# 增加线程数
export SHARDORA_THREAD_COUNT=32
```

## 预期效果

### 禁用网络延迟后
- ✅ 消除oversized packet错误
- ✅ 减少连接断开 90%+
- ✅ 提高交易成功率 50%+
- ✅ 改善TPS稳定性

### 长期优化后
- ✅ 支持网络延迟模拟
- ✅ 保持高性能
- ✅ 提高系统稳定性

## 实施步骤

### 第1步: 禁用网络延迟（立即）
1. 修改启动脚本禁用网络延迟
2. 重新编译和部署
3. 运行压测验证

### 第2步: 改进TCP参数（并行）
1. 增加TCP缓冲区
2. 增加keepalive间隔
3. 改进包验证

### 第3步: 长期优化（后续）
1. 实现包级别的延迟注入
2. 添加包完整性校验
3. 实现包重试机制

## 监控指标

### 关键指标
1. **oversized packet错误**: 应该为0
2. **连接断开频率**: 应该 <1/分钟
3. **交易成功率**: 应该 >99%
4. **TPS稳定性**: 应该 >95%

### 日志分析
```bash
# 查看oversized packet错误
grep "oversized or empty packet" logfile.txt | wc -l

# 查看连接断开
grep "on_read error" logfile.txt | wc -l

# 查看连接拒绝
grep "connection refused" logfile.txt | wc -l
```

## 风险评估

### 禁用网络延迟的风险
- ✅ 低风险: 只是禁用模拟，不影响实际功能
- ✅ 可恢复: 可以随时重新启用

### 改进TCP参数的风险
- ✅ 低风险: 标准的TCP优化
- ✅ 可恢复: 可以恢复到默认值

## 总结

**立即行动**: 禁用网络延迟注入，这是解决oversized packet问题的最快方法。

**长期方案**: 改进网络延迟注入的实现，使其在高并发下稳定工作。
