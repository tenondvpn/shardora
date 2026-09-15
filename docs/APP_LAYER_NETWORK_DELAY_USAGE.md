# 应用层网络延迟注入 - 使用指南

## 概述

已在 Transport 层实现应用层网络延迟注入，避免 TCP 层报文破坏。通过环境变量控制网络模拟参数。

## 实现文件

### 新增文件

- `src/transport/network_delay_simulator.h`: 网络延迟模拟器类

### 修改文件

- `src/transport/uv_tcp_transport.h`: 添加 NetworkDelaySimulator 成员和 getter 方法
- `src/transport/uv_tcp_transport.cc`: 在发送和接收路径中集成延迟注入

## 环境变量配置

| 变量 | 说明 | 默认值 | 范围 | 示例 |
|------|------|--------|------|------|
| SHARDORA_NETWORK_ENABLED | 启用/禁用 | 0 | 0-1 | 1 |
| SHARDORA_NETWORK_DELAY_MS | 单向延迟 (ms) | 0 | 0-1000 | 25 |
| SHARDORA_NETWORK_JITTER_MS | 抖动 (ms) | 0 | 0-100 | 10 |
| SHARDORA_NETWORK_LOSS_RATE | 丢包率 | 0 | 0-1 | 0.0001 |

## 使用方式

### 启用网络模拟 (公网模拟: 50ms RTT)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=25      # 单向 25ms = 往返 50ms
export SHARDORA_NETWORK_JITTER_MS=10     # 抖动 10ms
export SHARDORA_NETWORK_LOSS_RATE=0.0001 # 丢包率 0.01%

# 启动节点
./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm>
```

### 启用网络模拟 (局域网模拟: 2ms RTT)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=1       # 单向 1ms
export SHARDORA_NETWORK_JITTER_MS=0      # 无抖动
export SHARDORA_NETWORK_LOSS_RATE=0      # 无丢包

./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm>
```

### 禁用网络模拟 (正常运行)

```bash
export SHARDORA_NETWORK_ENABLED=0
# 或不设置环境变量 (默认禁用)

./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm>
```

## 实现细节

### NetworkDelaySimulator 类

位置: `src/transport/network_delay_simulator.h`

**主要方法**:

1. `ShouldDropPacket()`: 检查是否应该丢弃报文
   - 根据 SHARDORA_NETWORK_LOSS_RATE 随机决定
   - 返回 true 表示丢弃，false 表示保留

2. `GetDelayMs()`: 获取实际延迟时间
   - 延迟 = 基础延迟 + 随机抖动
   - 返回延迟时间 (毫秒)

3. `ApplyDelay()`: 应用网络延迟
   - 通过 std::this_thread::sleep_for() 实现
   - 阻塞当前线程指定时间

### 集成点

#### 1. 发送路径 (uv_tcp_transport.cc)

**位置 1**: on_connect 回调 (连接建立后发送初始消息)
```cpp
// 应用层网络延迟注入
if (tcp_transport->GetNetworkDelaySimulator().ShouldDropPacket()) {
    // 丢弃报文，关闭连接
    return;
}
tcp_transport->GetNetworkDelaySimulator().ApplyDelay();
uv_write(...);  // 发送消息
```

**位置 2**: Output() 函数 (批量发送消息)
```cpp
// 应用层网络延迟注入
if (network_delay_simulator_.ShouldDropPacket()) {
    // 丢弃报文
    continue;
}
network_delay_simulator_.ApplyDelay();
uv_write(...);  // 发送消息
```

#### 2. 接收路径 (uv_tcp_transport.cc)

**位置**: OnClientPacket() 函数 (处理接收到的报文)
```cpp
// 应用层网络延迟注入 (接收端)
if (tcp_transport->GetNetworkDelaySimulator().ShouldDropPacket()) {
    // 丢弃报文
    return false;
}
tcp_transport->GetNetworkDelaySimulator().ApplyDelay();
// 处理报文
```

## 工作原理

### 延迟注入流程

1. **发送端**:
   - 消息准备好后，检查是否应该丢弃
   - 如果不丢弃，应用延迟 (sleep)
   - 然后通过 uv_write 发送

2. **接收端**:
   - 收到报文后，检查是否应该丢弃
   - 如果不丢弃，应用延迟 (sleep)
   - 然后解析和处理报文

### 延迟计算

```
实际延迟 = 基础延迟 + 随机抖动
随机抖动 = [-SHARDORA_NETWORK_JITTER_MS, +SHARDORA_NETWORK_JITTER_MS]

例如:
SHARDORA_NETWORK_DELAY_MS=25
SHARDORA_NETWORK_JITTER_MS=10
实际延迟范围 = [15ms, 35ms]
```

### 丢包模拟

```
丢包概率 = SHARDORA_NETWORK_LOSS_RATE

例如:
SHARDORA_NETWORK_LOSS_RATE=0.0001 (0.01%)
平均每 10000 个报文丢弃 1 个
```

## 性能影响

### CPU 占用

- 启用网络模拟会增加 CPU 占用 (sleep 操作)
- 建议仅在测试环境使用

### 吞吐量

- 启用网络模拟会降低吞吐量
- 这是网络模拟的预期行为

### 延迟精度

- 系统调度可能导致实际延迟与设置值有偏差 (±5ms)
- 这是操作系统调度的正常行为

## 日志输出

启用网络模拟时，会输出以下日志:

```
[DEBUG] Network simulation: dropping packet to 192.168.26.172:13017
[DEBUG] Network simulation: dropping received packet from 192.168.26.173:13018
```

## 验证方法

### 1. 检查环境变量

```bash
echo $SHARDORA_NETWORK_ENABLED
echo $SHARDORA_NETWORK_DELAY_MS
echo $SHARDORA_NETWORK_JITTER_MS
echo $SHARDORA_NETWORK_LOSS_RATE
```

### 2. 查看日志

```bash
grep "Network simulation" /root/shardoras/s*/log/shardora.log
```

### 3. 测试网络延迟

```bash
# 使用 ping 测试
ping <remote_node_ip>

# 观察 RTT 是否增加
# 启用网络模拟前: ~0.1ms
# 启用网络模拟后: ~50ms (如果设置 SHARDORA_NETWORK_DELAY_MS=25)
```

### 4. 性能对比

- 启用网络模拟前的 TPS
- 启用网络模拟后的 TPS
- 观察性能下降幅度

## 常见问题

### Q: 为什么要在应用层而不是 TC 层实现?

A: TC 层会破坏 TCP 报文，导致 "oversized or empty packet" 错误。应用层方案避免了这个问题。

### Q: 延迟精度如何?

A: 系统调度可能导致 ±5ms 的偏差，这是正常的。

### Q: 如何禁用网络模拟?

A: 设置 `SHARDORA_NETWORK_ENABLED=0` 或不设置任何环境变量。

### Q: 支持哪些平台?

A: 支持所有平台 (Linux, macOS, Windows)。

### Q: 如何修改网络参数?

A: 修改环境变量后重新启动节点即可。

## 后续优化

1. **自适应延迟**: 根据网络状况动态调整延迟
2. **带宽限制**: 在应用层实现带宽限制
3. **丢包恢复**: 实现智能重传机制
4. **性能监控**: 添加详细的网络模拟性能指标

## 相关文件

- `src/transport/network_delay_simulator.h`: 网络延迟模拟器
- `src/transport/uv_tcp_transport.h`: Transport 层头文件
- `src/transport/uv_tcp_transport.cc`: Transport 层实现
- `cleanup_tc_rules.sh`: 清理 TC 规则脚本
- `temp_cmd.sh`: 部署脚本
