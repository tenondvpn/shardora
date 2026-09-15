# 性能压测优化指南

## 快速开始

### 1. 禁用网络延迟（推荐用于压测）

编辑 `temp_cmd.sh`:
```bash
# 禁用网络延迟注入
export SHARDORA_NETWORK_ENABLED=0
export SHARDORA_NETWORK_DELAY_MS=0
export SHARDORA_NETWORK_JITTER_MS=0
export SHARDORA_NETWORK_LOSS_RATE=0
```

### 2. 优化系统参数

```bash
# 增加文件描述符限制
ulimit -n 65536

# 优化TCP参数
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.wmem_max=134217728
sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"
sysctl -w net.ipv4.tcp_tw_reuse=1
sysctl -w net.ipv4.tcp_fin_timeout=30
```

### 3. 编译和部署

```bash
# 重新编译
cd /root/shardora/cbuild_Release
make -j4

# 部署
./temp_cmd.sh
```

## 网络优化参数

### TCP缓冲区优化
- **接收缓冲区**: 20MB (从10MB增加)
- **发送缓冲区**: 20MB (从10MB增加)
- **原因**: 支持高吞吐量场景

### TCP Keepalive优化
- **间隔**: 120秒 (从60秒增加)
- **原因**: 减少网络压力下的误判

### 包验证优化
- **最大包大小**: 2MB (从1.5MB增加)
- **原因**: 支持大块数据传输

## 网络延迟注入

### 禁用网络延迟（推荐）
```bash
export SHARDORA_NETWORK_ENABLED=0
```

**优点**:
- ✅ 消除oversized packet错误
- ✅ 减少连接断开
- ✅ 提高TPS稳定性
- ✅ 简化诊断

**缺点**:
- ❌ 无法测试网络延迟场景

### 启用网络延迟（谨慎）
```bash
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=50
export SHARDORA_NETWORK_JITTER_MS=10
export SHARDORA_NETWORK_LOSS_RATE=0.01
```

**注意**: 在高并发下可能导致包头破坏

## 性能指标

### 目标指标
| 指标 | 目标值 | 说明 |
|------|--------|------|
| TPS | >1000 | 每秒交易数 |
| 延迟 | <100ms | 平均交易延迟 |
| 成功率 | >99% | 交易成功率 |
| 连接稳定性 | <1/分钟 | 连接断开频率 |

### 监控命令

#### 查看oversized packet错误
```bash
tail -f logfile.txt | grep "oversized or empty packet"
```

#### 查看连接断开
```bash
tail -f logfile.txt | grep "on_read error"
```

#### 查看网络延迟
```bash
tail -f logfile.txt | grep "NETWORK_SIM"
```

#### 统计错误数量
```bash
# oversized packet错误
grep "oversized or empty packet" logfile.txt | wc -l

# 连接断开
grep "on_read error" logfile.txt | wc -l

# 连接拒绝
grep "connection refused" logfile.txt | wc -l
```

## 故障排查

### 问题1: 仍然有oversized packet错误
**原因**: 网络延迟注入未完全禁用

**解决**:
```bash
# 检查环境变量
echo $SHARDORA_NETWORK_ENABLED

# 确保为0
export SHARDORA_NETWORK_ENABLED=0
```

### 问题2: 连接仍然频繁断开
**原因**: TCP参数未优化

**解决**:
```bash
# 检查TCP缓冲区
cat /proc/sys/net/core/rmem_max
cat /proc/sys/net/core/wmem_max

# 应该都是134217728 (128MB)
```

### 问题3: TPS仍然不稳定
**原因**: 可能是应用层问题

**解决**:
1. 检查日志中的错误
2. 检查CPU使用率
3. 检查内存使用率
4. 检查磁盘I/O

## 压测场景

### 场景1: 基准测试（推荐）
```bash
# 禁用网络延迟
export SHARDORA_NETWORK_ENABLED=0

# 运行压测
./temp_cmd.sh
```

**预期结果**:
- TPS: >1000
- 延迟: <100ms
- 成功率: >99%

### 场景2: 网络延迟测试
```bash
# 启用网络延迟
export SHARDORA_NETWORK_ENABLED=1
export SHARDORA_NETWORK_DELAY_MS=50
export SHARDORA_NETWORK_JITTER_MS=10
export SHARDORA_NETWORK_LOSS_RATE=0.01

# 运行压测
./temp_cmd.sh
```

**预期结果**:
- TPS: >500
- 延迟: <500ms
- 成功率: >95%

### 场景3: 高并发测试
```bash
# 禁用网络延迟
export SHARDORA_NETWORK_ENABLED=0

# 增加并发数
export SHARDORA_THREAD_COUNT=64

# 运行压测
./temp_cmd.sh
```

**预期结果**:
- TPS: >2000
- 延迟: <200ms
- 成功率: >99%

## 最佳实践

### 1. 逐步增加负载
- 从低负载开始
- 逐步增加并发数
- 监控系统指标

### 2. 监控关键指标
- TPS
- 延迟
- 成功率
- 连接稳定性

### 3. 记录测试结果
- 记录每次测试的参数
- 记录测试结果
- 对比不同配置的效果

### 4. 定期优化
- 根据测试结果优化参数
- 定期更新系统参数
- 定期检查日志

## 常见问题

### Q: 为什么禁用网络延迟后TPS还是不高？
A: 可能是其他瓶颈，如CPU、内存或磁盘I/O。检查系统资源使用情况。

### Q: 网络延迟注入为什么会导致oversized packet错误？
A: 延迟注入可能导致包头被破坏，长度字段读取错误。建议在高并发下禁用。

### Q: 如何在启用网络延迟的情况下避免oversized packet错误？
A: 这是一个已知的限制。建议使用TC层网络延迟注入而不是应用层。

### Q: 如何恢复到默认配置？
A: 重新启动系统或运行 `sysctl -p` 恢复默认值。

## 总结

**关键要点**:
1. 禁用网络延迟以获得最佳性能
2. 优化TCP参数以支持高吞吐量
3. 监控关键指标以发现问题
4. 逐步增加负载以找到系统极限

**预期效果**:
- ✅ 消除oversized packet错误
- ✅ 减少连接断开 90%+
- ✅ 提高TPS 50%+
- ✅ 改善系统稳定性
