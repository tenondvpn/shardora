# tx_cli.cc Nonce 节流配置优化

## 修改内容

### 1. 定义独立的 Nonce 节流常量

在 `src/main/tx_cli.cc` 中添加两个独立的常量：

```cpp
// Nonce throttle limits (max unconfirmed transactions)
// Type 0: Standard mode - 512 unconfirmed txs
// Type 4: Stress test mode - 2048 unconfirmed txs (10k accounts)
static const uint32_t kMaxTxCountType0 = 512u;
static const uint32_t kMaxTxCountType4 = 2048u;
```

### 2. 修改原因

**原来的问题**:
- 所有模式都使用 `common::kMaxTxCount = 16384u`（全局常量）
- 这个值太大，导致 nonce 间断检查不够敏感
- 不同的测试模式需要不同的节流策略

**新的方案**:
- **Type 0 (标准模式)**：`kMaxTxCountType0 = 512`
  - 用于常规压测
  - 更严格的节流，确保 nonce 连续
  - 最多允许 1024 个未确认交易（2 * 512）

- **Type 4 (10k 账户压测)**：`kMaxTxCountType4 = 2048`
  - 用于大规模压测（10,000 个账户）
  - 更宽松的节流，适应高并发
  - 最多允许 4096 个未确认交易（2 * 2048）

### 3. 修改位置

#### Type 0 (tx_main 函数)
- 行 378: 间断检查条件
- 行 390: 再次检查条件

```cpp
if (src_prikey_with_nonce[addr] + 2 * kMaxTxCountType0 <= prikey_with_nonce[addr]) {
    // 触发节流
}
```

#### Type 4 (main 函数中的 argv[1][0] == '4' 分支)
- 行 1052: 交易发送线程中的间断检查
- 行 1233: 批量更新 nonce 时的节流检查
- 行 1560, 1564: 压力测试线程中的间断检查

```cpp
if (src_prikey_with_nonce[addr] + 2 * kMaxTxCountType4 <= prikey_with_nonce[addr]) {
    // 触发节流
}
```

## 使用方法

### Type 0 - 标准压测模式
```bash
./txcli 0 <shard> <pool> <ip> <port> [delay_us] [multi_pool] [tps]

# 示例：
./txcli 0 3 0 192.168.26.180 13001 0 1 10000
```

**特点**:
- 512 个未确认交易限制
- 适合常规性能测试
- nonce 更新频率：5 秒
- 更严格的间断检查

### Type 4 - 10k 账户压测模式
```bash
./txcli 4 <shard> <pool> <ip> <port> [threads] [tps]

# 示例：
./txcli 4 3 0 192.168.26.180 13001 16 50000
```

**特点**:
- 2048 个未确认交易限制
- 适合大规模账户压测
- 自动生成 10,000 个测试账户
- 更宽松的间断检查
- 支持自定义线程数和 TPS

## 性能对比

### Type 0 (512 限制)
- 更频繁的节流触发
- 更低的内存占用
- 更稳定的 nonce 序列
- 适合验证 nonce 连续性

### Type 4 (2048 限制)
- 更少的节流触发
- 更高的吞吐量
- 更好的并发性能
- 适合大规模压测

## 验证方法

### 检查 Type 0 的 nonce 连续性
```bash
# 运行压测
./txcli 0 3 0 192.168.26.180 13001

# 观察日志，应该看到：
# - 很少或没有 "nonce gap detected" 消息
# - 很少或没有 "reset nonce" 消息
# - 所有交易的 nonce 连续递增
```

### 检查 Type 4 的高吞吐量
```bash
# 运行 10k 账户压测
./txcli 4 3 0 192.168.26.180 13001 16 50000

# 观察日志，应该看到：
# - 创建 10,000 个账户
# - 高 TPS（50,000+ 交易/秒）
# - 偶尔的 "nonce gap detected" 是正常的（会自动恢复）
```

## 文件修改

- `src/main/tx_cli.cc`:
  - 行 39-41: 定义 `kMaxTxCountType0` 和 `kMaxTxCountType4`
  - 行 378, 390: Type 0 中使用 `kMaxTxCountType0`
  - 行 1052, 1233, 1560, 1564: Type 4 中使用 `kMaxTxCountType4`

## 总结

这个修改实现了：
1. ✅ Type 0 使用 512 的严格节流（确保 nonce 连续）
2. ✅ Type 4 使用 2048 的宽松节流（支持高并发）
3. ✅ 两种模式独立配置，互不影响
4. ✅ 更好的性能和稳定性
