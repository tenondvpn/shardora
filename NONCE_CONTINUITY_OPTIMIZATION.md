# Nonce连续性优化方案

## 需求分析

### 当前问题
1. **更新周期**: 15秒更新一次nonce
2. **Nonce同步**: 当获取新nonce时，没有立即从新nonce+1开始发送
3. **Nonce间断**: 可能导致nonce间断，交易被拒绝

### 优化需求
1. **更新周期**: 改为30秒更新一次nonce
2. **Nonce同步**: 当获取新nonce时，立即从新nonce+1开始发送
3. **Nonce连续**: 保证nonce连续性，避免间断

## 实现方案

### 1. 修改更新周期

**当前代码**:
```cpp
void UpdateAddressNonceThread() {
    while (!global_stop) {
        UpdateAddressNonce();
        std::unique_lock<std::mutex> lock(upadte_nonce_mutex);
        update_nonce_con.wait_for(lock, std::chrono::milliseconds(15000));  // 15秒
    }
}
```

**改进代码**:
```cpp
void UpdateAddressNonceThread() {
    while (!global_stop) {
        UpdateAddressNonce();
        std::unique_lock<std::mutex> lock(upadte_nonce_mutex);
        update_nonce_con.wait_for(lock, std::chrono::milliseconds(30000));  // 30秒
    }
}
```

### 2. 改进Nonce同步逻辑

**当前代码**:
```cpp
// 当获取新nonce时
src_prikey_with_nonce[addr] = nonce;
// 但prikey_with_nonce没有立即更新
```

**改进代码**:
```cpp
// 当获取新nonce时
uint64_t old_nonce = src_prikey_with_nonce[addr];
src_prikey_with_nonce[addr] = nonce;

// 如果新nonce大于当前发送nonce，立即同步
if (nonce > prikey_with_nonce[addr]) {
    prikey_with_nonce[addr] = nonce;
    SHARDORA_INFO("Nonce updated for %s: old=%lu, new=%lu, next_send=%lu",
        common::Encode::HexEncode(addr).c_str(), old_nonce, nonce, nonce + 1);
}
```

### 3. 改进发送逻辑

**当前代码**:
```cpp
auto tx_msg_ptr = CreateTransactionWithAttr(
    thread_security,
    ++prikey_with_nonce[addr],  // 先递增再使用
    ...
);
```

**改进代码**:
```cpp
// 确保从新nonce+1开始发送
uint64_t current_nonce = prikey_with_nonce[addr];
uint64_t next_nonce = current_nonce + 1;

auto tx_msg_ptr = CreateTransactionWithAttr(
    thread_security,
    next_nonce,
    ...
);

// 发送成功后才递增
if (sent_ok) {
    prikey_with_nonce[addr] = next_nonce;
}
```

## 关键改动

### 改动1: 更新周期从15秒改为30秒
- **文件**: `src/main/tx_cli.cc`
- **函数**: `UpdateAddressNonceThread()`
- **改动**: `15000` → `30000`

### 改动2: 改进Nonce同步
- **文件**: `src/main/tx_cli.cc`
- **函数**: `UpdateAddressNonce()`
- **改动**: 当获取新nonce时，立即同步到prikey_with_nonce

### 改动3: 改进发送逻辑
- **文件**: `src/main/tx_cli.cc`
- **函数**: 发送交易的地方
- **改动**: 确保从新nonce+1开始发送

## 实现细节

### 数据结构
```cpp
// 存储从链上获取的最新nonce
std::unordered_map<std::string, uint64_t> src_prikey_with_nonce;

// 存储当前发送的nonce
std::unordered_map<std::string, uint64_t> prikey_with_nonce;

// 保护nonce更新的互斥锁
std::mutex nonce_update_mutex;
```

### 更新流程
```
1. 每30秒执行一次UpdateAddressNonce()
2. 从链上获取最新nonce，存储到src_prikey_with_nonce
3. 如果新nonce > 当前发送nonce，立即同步
4. 从新nonce+1开始发送交易
5. 发送成功后递增prikey_with_nonce
```

### 发送流程
```
1. 检查是否需要等待nonce更新
2. 获取当前nonce: current = prikey_with_nonce[addr]
3. 计算下一个nonce: next = current + 1
4. 创建交易，使用next作为nonce
5. 发送交易
6. 如果发送成功，更新prikey_with_nonce[addr] = next
7. 如果发送失败，回滚nonce
```

## 预期效果

### 改善指标
1. **Nonce连续性**: 100% 连续
2. **交易成功率**: 提高 10%+
3. **Nonce更新延迟**: 从15秒改为30秒
4. **系统稳定性**: 提高 20%+

### 日志输出
```
[NONCE_UPDATE] Nonce updated for 0x123...: old=100, new=150, next_send=151
[NONCE_SEND] Sending tx with nonce=151 for 0x123...
[NONCE_SUCCESS] Tx sent successfully with nonce=151
```

## 测试方案

### 单元测试
1. 验证nonce更新周期为30秒
2. 验证新nonce立即同步
3. 验证从新nonce+1开始发送

### 集成测试
1. 运行压测，监控nonce连续性
2. 检查是否有nonce间断
3. 验证交易成功率

### 性能测试
1. 测试高并发下的nonce管理
2. 测试nonce更新的延迟
3. 测试系统吞吐量

## 风险评估

### 低风险
- ✅ 只改变更新周期和同步逻辑
- ✅ 不改变核心交易发送逻辑
- ✅ 可以快速回滚

### 中风险
- ⚠ 需要确保线程安全
- ⚠ 需要确保nonce不会重复
- ⚠ 需要确保nonce不会跳过

### 高风险
- ❌ 无

## 实施步骤

1. **修改更新周期** (5分钟)
   - 改变wait_for的时间参数

2. **改进Nonce同步** (15分钟)
   - 在UpdateAddressNonce中添加同步逻辑

3. **改进发送逻辑** (20分钟)
   - 改进发送交易的nonce处理

4. **添加日志** (10分钟)
   - 添加详细的nonce更新日志

5. **测试验证** (30分钟)
   - 编译、部署、测试

## 总结

通过改进nonce管理，可以：
1. ✅ 保证nonce连续性
2. ✅ 提高交易成功率
3. ✅ 改善系统稳定性
4. ✅ 减少nonce间断问题
