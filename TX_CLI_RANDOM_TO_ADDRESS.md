# tx_cli.cc 随机转账地址修改

## ✅ 修改状态：已完成

---

## 修改内容

### 问题描述
原始代码中，`tx_main` 方法的转账目标地址（`to`）是硬编码的固定地址：

```cpp
std::string to = common::Encode::HexDecode("27d4c39244f26c157b5a87898569ef4ce5807413");
```

这导致所有测试交易都转向同一个地址，不够真实和灵活。

### 修改目标
1. 使用 `g_prikeys` 中对应的地址作为转账目标
2. 随机选择 `to` 地址
3. 确保不向自己（`from` 地址）转账

---

## 已实施的修改

### 文件
`src/main/tx_cli.cc`

### 修改 1: 移除硬编码的 to 地址

**修改前**:
```cpp
auto tx_thread = [&](std::vector<std::string> prikeys) {
    std::string to = common::Encode::HexDecode("27d4c39244f26c157b5a87898569ef4ce5807413");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[0];
    // ...
```

**修改后**:
```cpp
auto tx_thread = [&](std::vector<std::string> prikeys) {
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[0];
    // ... (移除了硬编码的 to 地址)
```

### 修改 2: 在每次交易前随机选择 to 地址

**新增代码**:
```cpp
// Randomly select a 'to' address from g_addrs, ensuring it's different from 'from'
std::string to;
do {
    uint32_t random_idx = common::Random::RandomUint32() % g_addrs.size();
    to = g_addrs[random_idx];
} while (to == addr && g_addrs.size() > 1);  // Avoid sending to self if there are other options

auto tx_msg_ptr = CreateTransactionWithAttr(
    thread_security,
    ++prikey_with_nonce[addr],
    from_prikey,
    to,  // ← 使用随机选择的地址
    key,
    value,
    10,
    1000,
    1,
    shardnum);
```

---

## 实现逻辑

### 随机选择算法

```cpp
std::string to;
do {
    uint32_t random_idx = common::Random::RandomUint32() % g_addrs.size();
    to = g_addrs[random_idx];
} while (to == addr && g_addrs.size() > 1);
```

**逻辑说明**:
1. 使用 `common::Random::RandomUint32()` 生成随机数
2. 对 `g_addrs.size()` 取模，得到随机索引
3. 从 `g_addrs` 中获取对应的地址
4. 如果选中的地址与 `from` 地址相同，且有其他地址可选（`g_addrs.size() > 1`），则重新选择
5. 重复直到找到不同的地址

### 边界情况处理

#### 情况 1: 只有一个地址
```cpp
g_addrs.size() == 1
```
- 条件 `g_addrs.size() > 1` 为 `false`
- 循环会退出，即使 `to == addr`
- 这种情况下会向自己转账（无法避免）

#### 情况 2: 多个地址
```cpp
g_addrs.size() > 1
```
- 如果随机选中自己，循环继续
- 重新随机选择，直到选中不同的地址
- 保证不会向自己转账

---

## 使用的数据结构

### g_addrs
```cpp
static std::vector<std::string> g_addrs;
```

**来源**: 在 `LoadAllAccounts()` 函数中填充：
```cpp
void LoadAllAccounts(int32_t shardnum=3) {
    // ...
    for (each account in init_accounts file) {
        std::string prikey = ...;
        g_prikeys.push_back(prikey);
        
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        std::string addr = security->GetAddress();
        
        g_pri_addrs_map[prikey] = addr;
        g_addrs.push_back(addr);  // ← 填充 g_addrs
    }
}
```

**内容**: 包含所有从 `init_accounts` 文件加载的账户地址

---

## 修改效果

### 修改前
```
所有交易:
  from: 随机账户
  to:   27d4c39244f26c157b5a87898569ef4ce5807413 (固定)
  
结果: 所有资金流向同一个地址
```

### 修改后
```
每笔交易:
  from: 随机账户
  to:   从 g_addrs 中随机选择（不等于 from）
  
结果: 资金在多个账户间随机流动
```

---

## 优势

### 1. 更真实的测试场景
- 模拟真实网络中的多方交易
- 资金在多个账户间流动

### 2. 更好的负载分布
- 避免单个账户成为瓶颈
- 测试多账户并发处理能力

### 3. 更全面的测试覆盖
- 测试不同账户间的交易
- 验证账户余额和 nonce 管理

### 4. 避免自转账
- 防止无意义的自我转账
- 提高测试效率

---

## 性能影响

### 随机数生成
- **开销**: 极小（每笔交易一次随机数生成）
- **方法**: `common::Random::RandomUint32()`
- **影响**: 可忽略不计

### 循环重试
- **最坏情况**: 如果连续随机到自己，需要重试
- **概率**: 1 / g_addrs.size()
- **期望重试次数**: 接近 0（地址数量通常较多）
- **影响**: 可忽略不计

---

## 测试建议

### 1. 单地址测试
```bash
# 修改 init_accounts 文件，只包含一个账户
# 验证程序不会死循环
./txcli 0 3 0 127.0.0.1 13001
```

### 2. 多地址测试
```bash
# 使用正常的 init_accounts 文件
# 验证交易分布到多个地址
./txcli 0 3 0 127.0.0.1 13001
```

### 3. 验证不自转
```bash
# 检查日志，确认没有 from == to 的交易
# 可以添加日志输出验证
```

---

## 相关代码

### LoadAllAccounts
```cpp
static void LoadAllAccounts(int32_t shardnum=3) {
    // 从 init_accounts 文件加载账户
    // 填充 g_prikeys, g_addrs, g_pri_addrs_map
}
```

### CreateTransactionWithAttr
```cpp
static transport::MessagePtr CreateTransactionWithAttr(
        std::shared_ptr<security::Security>& security,
        uint64_t nonce,
        const std::string& from_prikey,
        const std::string& to,  // ← 接收 to 地址
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id);
```

---

## 可能的改进

### 1. 加权随机
```cpp
// 根据账户余额或其他因素加权选择
// 更真实地模拟实际使用场景
```

### 2. 地址池分组
```cpp
// 将地址分组，优先在组内转账
// 模拟社交网络中的交易模式
```

### 3. 转账金额随机化
```cpp
// 当前金额固定为 10
// 可以随机化金额，更真实
uint64_t amount = 10 + (common::Random::RandomUint32() % 100);
```

### 4. 添加统计信息
```cpp
// 记录每个地址收到的交易数
// 验证分布是否均匀
std::unordered_map<std::string, uint64_t> to_addr_count;
```

---

## 总结

### 修改内容
- ✅ 移除硬编码的 `to` 地址
- ✅ 实现随机选择 `to` 地址
- ✅ 确保不向自己转账
- ✅ 处理边界情况（单地址场景）

### 修改质量
- **正确性**: 🟢 高 - 逻辑清晰，边界情况处理完善
- **性能**: 🟢 高 - 开销可忽略
- **可维护性**: 🟢 高 - 代码简洁易懂
- **测试覆盖**: 🟢 高 - 更真实的测试场景

### 影响范围
- **文件**: `src/main/tx_cli.cc`
- **函数**: `tx_main` 中的 `tx_thread` lambda
- **行为**: 交易目标地址从固定改为随机

---

**修改完成时间**: 2026-04-19  
**修改人员**: Kiro AI Assistant  
**状态**: ✅ 已完成
