# Step Type Fix Summary

## 问题描述

在 `test_contract_chain_demo.py` 中，创建新用户地址时使用了错误的 step 类型。

## 错误代码

❌ **错误：**
```python
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    address,
    StepType.kRootCreateAddress,  # 错误！
    amount=initial_balance
)
```

## 修复后的代码

✅ **正确：**
```python
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    address,
    StepType.kNormalFrom,  # 正确！标准转账
    amount=initial_balance
)
```

## 原因说明

### kRootCreateAddress 的真实用途
- `kRootCreateAddress` (值为 9) 是 Root 网络内部使用的特殊地址创建操作
- 用于系统级别的地址创建，不是用户级别的操作
- 普通用户不应该直接使用这个 step 类型

### kNormalFrom 的正确用途
- `kNormalFrom` (值为 0) 是标准转账操作
- 当向一个不存在的地址发送资金时，会自动创建该地址
- 这是创建新用户地址的正确方式

## Step Type 使用规则

| 操作 | 正确的 Step Type | 值 | 说明 |
|------|-----------------|-----|------|
| 创建新用户地址（转账） | `kNormalFrom` | 0 | 标准转账到新地址 |
| 用户间转账 | `kNormalFrom` | 0 | 标准转账 |
| 部署合约 | `kCreateContract` | 6 | 合约部署 |
| 调用合约 | `kContractExcute` | 8 | 执行合约函数 |
| 合约预充值 | `kContractGasPrefund` | 7 | 设置合约 gas 预充值 |
| 合约退款 | `kContractRefund` | 18 | 从合约退还 gas |
| Root 地址创建 | `kRootCreateAddress` | 9 | ⚠️ 仅供内部使用 |

## 修改的文件

### 1. clipy/test_contract_chain_demo.py
**位置：** `create_and_wait_for_address()` 函数

**修改前：**
```python
# Use kRootCreateAddress to create the address
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    address,
    StepType.kRootCreateAddress,
    amount=initial_balance
)
```

**修改后：**
```python
# Use kNormalFrom for standard transfer to create the address
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    address,
    StepType.kNormalFrom,
    amount=initial_balance
)
```

## 影响范围

这个修复影响以下场景：

### Phase 1: 预创建 User2 和 User3
```python
# 创建 User2
user2_key, user2_addr = create_and_wait_for_address(
    w3, user1_key, user2_shard, user2_pool,
    initial_balance=10000000, max_wait=60
)

# 创建 User3
user3_key, user3_addr = create_and_wait_for_address(
    w3, user1_key, user3_shard, user3_pool,
    initial_balance=10000000, max_wait=60
)
```

### Phase 3: 重新生成 User2（如果需要）
```python
if user2_shard != target_shard or user2_pool != target_pool:
    new_key, new_addr = create_and_wait_for_address(
        w3, user1_key, target_shard, target_pool,
        initial_balance=10000000, max_wait=60
    )
```

### Phase 5: 重新生成 User3（如果需要）
```python
if user3_shard != target_shard or user3_pool != target_pool:
    new_key, new_addr = create_and_wait_for_address(
        w3, user1_key, target_shard, target_pool,
        initial_balance=10000000, max_wait=60
    )
```

## 测试验证

### 运行测试
```bash
# 启动区块链
./build/shardora --show_cmd -g 1 -n 1 -c 1 -m 1 -s 1 -d 1

# 运行 demo
cd clipy
python3 test_contract_chain_demo.py
```

### 预期输出
```
[1.1] Creating User2...
  🔍 Searching for user address in shard 2, pool 4...
  ✅ Found matching address after 123 attempts
     Address: abc123...
     Shard: 2, Pool: 4

  💰 Funding new address with 10000000 coins...
  📤 Transaction sent: def456...
  ⏳ Waiting for address to be active (max 60s)...
  ✅ Address is active! (took 3.2s)
     Balance: 10000000
  ✅ User2 created and verified on-chain
```

## SDK 自动处理

在大多数情况下，SDK 会自动选择正确的 step 类型：

### 标准转账
```python
# SDK 自动使用 kNormalFrom
receipt = w3.send_transaction({
    'to': recipient_address,
    'value': amount
}, sender_key)
```

### 合约部署
```python
# SDK 自动使用 kCreateContract
contract = w3.shardora.contract(abi=abi, bytecode=bytecode).deploy({
    'from': user_addr,
    'salt': salt,
}, user_key)
```

### 合约调用
```python
# SDK 自动使用 kContractExcute
receipt = contract.functions.myFunction().transact(user_key)
```

## 最佳实践

1. **优先使用高级 API**
   - 使用 `w3.send_transaction()` 而不是 `send_transaction_auto()`
   - 使用 `contract.deploy()` 而不是手动构造部署交易
   - 使用 `contract.functions.xxx().transact()` 而不是手动编码调用

2. **让 SDK 处理 step 类型**
   - SDK 会根据操作自动选择正确的 step 类型
   - 只在特殊情况下手动指定 step 类型

3. **避免使用内部 step 类型**
   - 不要使用 `kRootCreateAddress`
   - 不要使用 `kConsensusRootElectShard`
   - 不要使用其他系统级 step 类型

4. **创建新地址的正确方式**
   - 使用 `kNormalFrom` 进行标准转账
   - 向新地址发送资金会自动创建该地址
   - 等待交易确认后验证地址已创建

## 相关文档

- `STEP_TYPE_REFERENCE.md` - Step 类型完整参考
- `CONTRACT_CHAIN_SAME_SHARD_POOL_DEMO.md` - 合约链 demo 文档
- `CONTRACT_CHAIN_QUERY_LOGIC.md` - 查询逻辑文档
- `shardora_sdk.py` - SDK 实现

## 总结

这次修复确保了：
1. ✅ 使用正确的 step 类型创建新用户地址
2. ✅ 遵循 Shardora 区块链的标准操作流程
3. ✅ 与 SDK 的自动 step 类型选择保持一致
4. ✅ 避免使用内部系统级 step 类型

修复后，demo 将能够正确创建新用户地址并等待其在区块链上激活。
