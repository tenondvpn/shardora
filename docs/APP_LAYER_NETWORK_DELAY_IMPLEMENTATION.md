# 应用层网络延迟注入实现指南

## 概述

为避免 TCP 层报文被破坏，采用应用层延迟注入方式模拟网络延迟和抖动。

## 问题背景

之前使用 `tc (traffic control)` 在 TCP 层进行网络模拟，导致报文被破坏，出现"oversized or empty packet"错误。

## 解决方案

在应用层（Transport 层）添加延迟注入逻辑，通过环境变量控制：

### 环境变量

| 变量名 | 说明 | 默认值 | 示例 |
|--------|------|--------|------|
| `SHARDORA_NETWORK_ENABLED` | 是否启用网络模拟 | 0 | 1 |
| `SHARDORA_NETWORK_DELAY_MS` | 单向延迟 (ms) | 0 | 25 |
| `SHARDORA_NETWORK_JITTER_MS` | 抖动 (ms) | 0 | 10 |
| `SHARDORA_NETWORK_LOSS_RATE` | 丢包率 (0-1) | 0 | 0.0001 |

## 实现步骤

### 1. 修改 Transport 层代码

在 `src/transport/uv_tcp_transport.h` 或 `src/transport/tcp_transport.h` 中添加：

```cpp
#include <random>
#include <chrono>
#include <thread>

class NetworkDelaySimulator {
private:
    bool enabled_;
    uint32_t delay_ms_;
    uint32_t jitter_ms_;
    double loss_rate_;
    std::mt19937 rng_;
    
public:
    NetworkDelaySimulator() {
        enabled_ = std::getenv("SHARDORA_NETWORK_ENABLED") ? 
                   std::atoi(std::getenv("SHARDORA_NETWORK_ENABLED")) : 0;
        delay_ms_ = std::getenv("SHARDORA_NETWORK_DELAY_MS") ? 
                    std::atoi(std::getenv("SHARDORA_NETWORK_DELAY_MS")) : 0;
        jitter_ms_ = std::getenv("SHARDORA_NETWORK_JITTER_MS") ? 
                     std::atoi(std::getenv("SHARDORA_NETWORK_JITTER_MS")) : 0;
        loss_rate_ = std::getenv("SHARDORA_NETWORK_LOSS_RATE") ? 
                     std::atof(std::getenv("SHARDORA_NETWORK_LOSS_RATE")) : 0;
        rng_.seed(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    bool ShouldDropPacket() {
        if (!enabled_ || loss_rate_ <= 0) return false;
        std::uniform_real_distribution<> dist(0.0, 1.0);
        return dist(rng_) < loss_rate_;
    }
    
    uint32_t GetDelayMs() {
        if (!enabled_ || delay_ms_ == 0) return 0;
        
        std::uniform_int_distribution<> jitter_dist(-jitter_ms_, jitter_ms_);
        int32_t actual_delay = delay_ms_ + jitter_dist(rng_);
        return std::max(0, actual_delay);
    }
    
    void ApplyDelay() {
        uint32_t delay = GetDelayMs();
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }
};
```

### 2. 在发送消息时应用延迟

在 `Send()` 方法中：

```cpp
void TcpTransport::Send(const MessagePtr& msg_ptr) {
    // 检查是否需要丢弃报文
    if (network_delay_simulator_.ShouldDropPacket()) {
        SHARDORA_DEBUG("Network simulation: dropping packet");
        return;
    }
    
    // 应用网络延迟
    network_delay_simulator_.ApplyDelay();
    
    // 继续正常的发送流程
    // ... 原有的发送代码 ...
}
```

### 3. 在接收消息时应用延迟

在消息处理线程中：

```cpp
void TcpTransport::HandleMessage(const MessagePtr& msg_ptr) {
    // 应用网络延迟
    network_delay_simulator_.ApplyDelay();
    
    // 继续正常的处理流程
    // ... 原有的处理代码 ...
}
```

## 使用方式

### 启用网络模拟

```bash
# 设置环境变量
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=25      # 单向 25ms
export SHARDORA_NETWORK_JITTER_MS=10     # 抖动 10ms
export SHARDORA_NETWORK_LOSS_RATE=0.0001 # 丢包率 0.01%

# 启动节点
./shardora
```

### 禁用网络模拟

```bash
export SHARDORA_NETWORK_ENABLED=0
./shardora
```

### 使用脚本启动

```bash
# 使用提供的脚本
source setup_app_layer_network_delay.sh
./shardora
```

## 优势

1. **避免 TCP 报文破坏**: 在应用层添加延迟，不会影响 TCP 层的报文完整性
2. **灵活控制**: 通过环境变量动态调整延迟参数，无需重新编译
3. **精确模拟**: 可以精确控制延迟、抖动和丢包率
4. **易于调试**: 可以轻松启用/禁用网络模拟进行对比测试

## 注意事项

1. **延迟精度**: 系统调度可能导致实际延迟与设置值有偏差
2. **CPU 占用**: 频繁的 sleep 操作可能增加 CPU 占用，建议在测试环境使用
3. **丢包处理**: 应用层丢包需要上层协议处理重传逻辑
4. **多线程**: 确保延迟注入在正确的线程上执行

## 配置示例

### 场景 1: 模拟公网 (50ms RTT)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=25      # 单向 25ms = 往返 50ms
export SHARDORA_NETWORK_JITTER_MS=10     # 抖动 10ms
export SHARDORA_NETWORK_LOSS_RATE=0.0001 # 丢包率 0.01%
```

### 场景 2: 模拟局域网 (低延迟)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=1       # 单向 1ms
export SHARDORA_NETWORK_JITTER_MS=0      # 无抖动
export SHARDORA_NETWORK_LOSS_RATE=0      # 无丢包
```

### 场景 3: 模拟高延迟网络 (100ms RTT)

```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=50      # 单向 50ms = 往返 100ms
export SHARDORA_NETWORK_JITTER_MS=20     # 抖动 20ms
export SHARDORA_NETWORK_LOSS_RATE=0.001  # 丢包率 0.1%
```

## 测试验证

启用网络模拟后，可以通过以下方式验证：

```bash
# 1. 检查环境变量是否设置
echo $SHARDORA_NETWORK_ENABLED
echo $SHARDORA_NETWORK_DELAY_MS

# 2. 查看日志中的延迟信息
grep "Network simulation" shardora.log

# 3. 使用 ping 测试网络延迟
ping <remote_node_ip>

# 4. 监控节点性能
# 观察 TPS、延迟等指标是否符合预期
```

## 后续优化

1. **自适应延迟**: 根据网络状况动态调整延迟
2. **带宽限制**: 在应用层实现带宽限制
3. **丢包恢复**: 实现智能重传机制
4. **性能监控**: 添加详细的网络模拟性能指标
