# 网络模拟方案迁移 - 从 TC 层到应用层

## 问题描述

之前使用 Linux `tc (traffic control)` 在 TCP 层进行网络模拟，导致以下问题：

1. **报文破坏**: "oversized or empty packet" 错误
2. **TCP 层干扰**: TC 的 netem 模块对 TCP 报文处理不当
3. **配置复杂**: 需要 root 权限和复杂的 iptables/tc 规则

## 解决方案

采用**应用层延迟注入**方式，在 Shardora 应用的 Transport 层添加网络延迟和抖动模拟。

### 优势

| 方面 | TC 层方案 | 应用层方案 |
|------|---------|---------|
| 报文完整性 | ❌ 易破坏 | ✅ 完全保证 |
| 实现复杂度 | 高 (需要 root) | 低 (环境变量) |
| 灵活性 | 低 (需要重新配置) | 高 (动态调整) |
| 性能开销 | 中等 | 低 (仅在需要时) |
| 跨平台支持 | 仅 Linux | 全平台 |

## 实现方案

### 1. 环境变量配置

在启动 Shardora 节点时设置以下环境变量：

```bash
# 启用网络模拟
export SHARDORA_NETWORK_ENABLED=1

# 网络参数 (模拟公网 50ms RTT)
export SHARDORA_NETWORK_DELAY_MS=25      # 单向延迟 25ms
export SHARDORA_NETWORK_JITTER_MS=10     # 抖动 10ms
export SHARDORA_NETWORK_LOSS_RATE=0.0001 # 丢包率 0.01%
```

### 2. 修改 temp_cmd.sh

已完成以下修改：

- ✅ 移除所有 TC 层配置代码
- ✅ 移除 prio qdisc、tbf、netem 等 TC 规则
- ✅ 移除 IP 白名单过滤规则
- ✅ 添加应用层延迟注入说明
- ✅ 添加环境变量设置逻辑
- ✅ 简化网络模拟配置函数

### 3. Transport 层实现 (待完成)

需要在以下文件中添加延迟注入逻辑：

**文件**: `src/transport/uv_tcp_transport.cc` 或 `src/transport/tcp_transport.cc`

**实现步骤**:

1. 添加 `NetworkDelaySimulator` 类
2. 在 `Send()` 方法中应用延迟
3. 在消息处理线程中应用延迟
4. 添加丢包模拟逻辑

详见: `APP_LAYER_NETWORK_DELAY_IMPLEMENTATION.md`

## 使用方式

### 方式 1: 直接设置环境变量

```bash
# 启用网络模拟
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=25
export SHARDORA_NETWORK_JITTER_MS=10
export SHARDORA_NETWORK_LOSS_RATE=0.0001

# 启动部署脚本
./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm> 1
```

### 方式 2: 使用辅助脚本

```bash
# 使用提供的脚本设置环境变量
source setup_app_layer_network_delay.sh

# 启动部署脚本
./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm> 1
```

### 方式 3: 禁用网络模拟

```bash
# 禁用网络模拟 (正常运行)
export SHARDORA_NETWORK_ENABLED=0

# 或者不设置环境变量 (默认禁用)
./temp_cmd.sh <public_ip> <start_pos> <node_count> <bootstrap> <start_shard> <end_shard> <leader_init_tm>
```

## 配置参数说明

### SHARDORA_NETWORK_ENABLED

- **类型**: 整数 (0 或 1)
- **默认值**: 0 (禁用)
- **说明**: 是否启用网络模拟

### SHARDORA_NETWORK_DELAY_MS

- **类型**: 整数 (毫秒)
- **默认值**: 0
- **说明**: 单向网络延迟
- **示例**:
  - 25ms: 模拟公网 (50ms RTT)
  - 1ms: 模拟局域网
  - 50ms: 模拟高延迟网络 (100ms RTT)

### SHARDORA_NETWORK_JITTER_MS

- **类型**: 整数 (毫秒)
- **默认值**: 0
- **说明**: 网络抖动 (延迟的随机波动)
- **示例**:
  - 10ms: 中等抖动
  - 0ms: 无抖动 (固定延迟)
  - 20ms: 高抖动

### SHARDORA_NETWORK_LOSS_RATE

- **类型**: 浮点数 (0-1)
- **默认值**: 0
- **说明**: 丢包率 (0 = 0%, 1 = 100%)
- **示例**:
  - 0.0001: 0.01% (1/10000)
  - 0.001: 0.1% (1/1000)
  - 0.01: 1% (1/100)

## 常见配置场景

### 场景 1: 模拟公网 (推荐用于压测)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=25
export SHARDORA_NETWORK_JITTER_MS=10
export SHARDORA_NETWORK_LOSS_RATE=0.0001
```

**特点**: 50ms RTT, 10ms 抖动, 0.01% 丢包率

### 场景 2: 模拟局域网 (快速测试)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=1
export SHARDORA_NETWORK_JITTER_MS=0
export SHARDORA_NETWORK_LOSS_RATE=0
```

**特点**: 2ms RTT, 无抖动, 无丢包

### 场景 3: 模拟高延迟网络 (极限测试)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=50
export SHARDORA_NETWORK_JITTER_MS=20
export SHARDORA_NETWORK_LOSS_RATE=0.001
```

**特点**: 100ms RTT, 20ms 抖动, 0.1% 丢包率

### 场景 4: 禁用网络模拟 (正常运行)

```bash
export SHARDORA_NETWORK_ENABLED=0
# 或不设置任何环境变量
```

## 迁移清单

- [x] 修改 temp_cmd.sh 移除 TC 层配置
- [x] 添加应用层延迟注入说明
- [x] 创建环境变量配置脚本
- [x] 编写实现指南文档
- [ ] 在 Transport 层实现 NetworkDelaySimulator 类
- [ ] 在 Send() 方法中集成延迟注入
- [ ] 在消息处理中集成延迟注入
- [ ] 添加日志输出验证延迟注入
- [ ] 进行功能测试验证
- [ ] 性能基准测试

## 测试验证

### 1. 验证环境变量设置

```bash
echo $SHARDORA_NETWORK_ENABLED
echo $SHARDORA_NETWORK_DELAY_MS
echo $SHARDORA_NETWORK_JITTER_MS
echo $SHARDORA_NETWORK_LOSS_RATE
```

### 2. 查看日志中的延迟信息

```bash
grep "Network simulation" /root/shardoras/s*/log/shardora.log
```

### 3. 监控网络延迟

```bash
# 使用 ping 测试
ping <remote_node_ip>

# 使用 iperf 测试带宽
iperf -c <remote_node_ip>
```

### 4. 性能指标对比

- 启用网络模拟前后的 TPS 对比
- 消息延迟分布对比
- CPU 占用率对比

## 注意事项

1. **延迟精度**: 系统调度可能导致实际延迟与设置值有偏差 (±5ms)
2. **CPU 占用**: 频繁的 sleep 操作可能增加 CPU 占用，建议在测试环境使用
3. **丢包处理**: 应用层丢包需要上层协议处理重传逻辑
4. **多线程**: 确保延迟注入在正确的线程上执行，避免阻塞关键路径
5. **性能影响**: 启用网络模拟会降低吞吐量，这是预期行为

## 后续优化方向

1. **自适应延迟**: 根据网络状况动态调整延迟
2. **带宽限制**: 在应用层实现带宽限制
3. **丢包恢复**: 实现智能重传机制
4. **性能监控**: 添加详细的网络模拟性能指标
5. **配置文件**: 支持从配置文件读取网络模拟参数

## 相关文件

- `temp_cmd.sh`: 已修改，移除 TC 层配置
- `setup_app_layer_network_delay.sh`: 新增，环境变量设置脚本
- `APP_LAYER_NETWORK_DELAY_IMPLEMENTATION.md`: 新增，实现指南
- `NETWORK_SIMULATION_MIGRATION.md`: 本文件，迁移说明

## 参考资源

- Linux tc 命令文档: https://man7.org/linux/man-pages/man8/tc.8.html
- netem 模块文档: https://man7.org/linux/man-pages/man8/tc-netem.8.html
- C++ 随机数生成: https://en.cppreference.com/w/cpp/numeric/random
