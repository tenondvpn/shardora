# Update: Pre-create User2 and User3 On-Chain

## 更新日期
2026-04-22

## 更新说明

修改了合约链部署逻辑，现在在部署任何合约之前，**先通过转币交易创建 User2 和 User3，并验证它们在链上合法**（余额已到账）。

## 修改原因

确保 User2 和 User3 在使用前已经在链上激活，避免后续部署合约时因用户不存在而失败。

## 修改内容

### 新增 Phase 0: 准备 User1
```python
# Phase 0: 准备 User1（资金提供者）
user1_key = KEY
user1_addr = MY
```

### 新增 Phase 1: 预先创建 User2 和 User3
```python
# Phase 1: 预先创建 User2 和 User3
# 1.1 创建 User2
user2_key, user2_addr = create_and_wait_for_address(
    w3, user1_key, user2_shard, user2_pool,
    initial_balance=10000000, max_wait=60
)

# 1.2 创建 User3
user3_key, user3_addr = create_and_wait_for_address(
    w3, user1_key, user3_shard, user3_pool,
    initial_balance=10000000, max_wait=60
)
```

### 修改 Phase 3 和 Phase 5
```python
# 修改前：生成新用户
user2_sk = SigningKey.generate(curve=SECP256k1)
user2_key = user2_sk.to_string().hex()
# ...

# 修改后：检查已存在的用户
user2_shard = calc_shard_id(user2_addr)
user2_pool = calc_pool_index(user2_addr)
print(f"👤 Current User2:")
```

## 完整流程

### 修改前
```
1. 准备 User1
2. 部署 ContractA
3. 生成 User2 → 检查 shard/pool → 可能重新生成
4. 部署 ContractB
5. 生成 User3 → 检查 shard/pool → 可能重新生成
6. 部署 ContractC
```

**问题**: User2 和 User3 在检查时才创建，可能导致后续操作失败。

### 修改后 ✅
```
0. 准备 User1（资金提供者）
1. 预先创建 User2 和 User3
   ├─ 生成 User2 → 在链上创建 → 等待生效 → 验证余额
   └─ 生成 User3 → 在链上创建 → 等待生效 → 验证余额
2. 部署 ContractA
3. 检查 User2 是否与目标匹配
   └─ 不匹配 → 重新创建新 User2
4. 部署 ContractB
5. 检查 User3 是否与目标匹配
   └─ 不匹配 → 重新创建新 User3
6. 部署 ContractC
```

**优势**:
- ✅ User2 和 User3 在使用前已验证合法
- ✅ 避免因用户不存在导致的失败
- ✅ 更清晰的流程和错误处理

## 输出示例

### Phase 0: 准备 User1
```
[Phase 0] Preparing User1 (Funder)

👤 User1 (Funder):
   Address: a1b2c3d4e5f6789012345678901234567890abcd
   Shard: 1, Pool: 2
```

### Phase 1: 预先创建用户
```
================================================================================
[Phase 1] Pre-creating User2 and User3 on-chain
================================================================================

[1.1] Creating User2...

👤 User2 (generated):
   Address: f6e5d4c3b2a1098765432109876543210fedcba9
   Shard: 2, Pool: 3

  💰 Creating User2 on-chain with initial balance...
  🔍 Searching for user address in shard 2, pool 3...
  ✅ Found matching address after 1247 attempts
     Address: 9876543210fedcba9876543210fedcba98765432
     Shard: 2, Pool: 3

  💰 Funding new address with 10000000 coins...
  📤 Transaction sent: a1b2c3d4e5f6...
  ⏳ Waiting for address to be active (max 60s)...
  ✅ Address is active! (took 3.2s)
     Balance: 10000000

  ✅ User2 created and verified on-chain

[1.2] Creating User3...

👤 User3 (generated):
   Address: 1234567890abcdef1234567890abcdef12345678
   Shard: 1, Pool: 4

  💰 Creating User3 on-chain with initial balance...
  🔍 Searching for user address in shard 1, pool 4...
  ✅ Found matching address after 2341 attempts
     Address: fedcba9876543210fedcba9876543210fedcba98
     Shard: 1, Pool: 4

  💰 Funding new address with 10000000 coins...
  📤 Transaction sent: b2c3d4e5f6a1...
  ⏳ Waiting for address to be active (max 60s)...
  ✅ Address is active! (took 2.8s)
     Balance: 10000000

  ✅ User3 created and verified on-chain

================================================================================
✅ User2 and User3 are now valid on-chain
================================================================================
```

### Phase 2: 部署 ContractA
```
--------------------------------------------------------------------------------
[Phase 2] User1 deploys ContractA (no shard/pool check)
--------------------------------------------------------------------------------

📋 ContractA (predicted):
   Address: abcdef1234567890abcdef1234567890abcdef12
   Shard: 2, Pool: 3

✅ ContractA deployed at: abcdef1234567890abcdef1234567890abcdef12

🎯 Target shard/pool determined from ContractA:
   Target Shard: 2
   Target Pool: 3
   All subsequent contracts will be deployed in this shard/pool
```

### Phase 3: 检查 User2
```
--------------------------------------------------------------------------------
[Phase 3] Checking User2 compatibility with target shard/pool
--------------------------------------------------------------------------------

👤 Current User2:
   Address: 9876543210fedcba9876543210fedcba98765432
   Shard: 2, Pool: 3

✅ User2 already in correct shard/pool!
   No need to regenerate
```

**或者，如果不匹配**:
```
👤 Current User2:
   Address: 9876543210fedcba9876543210fedcba98765432
   Shard: 1, Pool: 5

⚠️  User2 mismatch detected:
   User2: Shard 1, Pool 5
   Target: Shard 2, Pool 3

🔄 Creating new User2 to match target shard/pool...
  [... 创建过程 ...]

✅ New User2 created and activated successfully!
```

## 关键改进

### 1. 提前验证
- **修改前**: 在需要时才创建用户
- **修改后**: 提前创建并验证用户合法性

### 2. 清晰的阶段划分
- **Phase 0**: 准备资金提供者
- **Phase 1**: 预先创建所有用户
- **Phase 2**: 部署第一个合约
- **Phase 3-6**: 检查并部署后续合约

### 3. 更好的错误处理
```python
if not user2_key:
    print(f"  ❌ Failed to create User2")
    return  # 提前退出，避免后续错误
```

### 4. 更清晰的输出
- 明确区分"生成"和"创建"
- 显示"当前用户"而不是"初始用户"
- 区分"新用户"和"原用户"

## 测试场景

### 场景 1: 所有用户都匹配（理想情况）
```
Phase 1: 创建 User2 (Shard 2, Pool 3) ✅
Phase 1: 创建 User3 (Shard 2, Pool 3) ✅
Phase 2: 部署 ContractA (Shard 2, Pool 3)
Phase 3: User2 匹配 → 无需重新创建 ✅
Phase 5: User3 匹配 → 无需重新创建 ✅
结果: 所有合约在 Shard 2, Pool 3 ✅
```

### 场景 2: 部分用户需要重新创建
```
Phase 1: 创建 User2 (Shard 1, Pool 5) ✅
Phase 1: 创建 User3 (Shard 3, Pool 2) ✅
Phase 2: 部署 ContractA (Shard 2, Pool 3)
Phase 3: User2 不匹配 → 重新创建 (Shard 2, Pool 3) ✅
Phase 5: User3 不匹配 → 重新创建 (Shard 2, Pool 3) ✅
结果: 所有合约在 Shard 2, Pool 3 ✅
```

### 场景 3: 用户创建失败
```
Phase 1: 创建 User2 失败 ❌
→ 提前退出，避免后续错误
```

## 性能影响

### 时间开销
- **Phase 1 新增时间**: 4-20 秒（创建 2 个用户）
  - User2 创建: 2-10 秒
  - User3 创建: 2-10 秒
- **总体影响**: 增加 4-20 秒，但提高了可靠性

### 优势
- ✅ 避免后续失败导致的重试时间
- ✅ 更清晰的错误定位
- ✅ 更好的用户体验

## 相关文件

### 修改的文件
- `clipy/test_contract_chain_demo.py` - 主要逻辑修改

### 相关文档
- `ADDRESS_CREATION_WAIT_FEATURE.md` - 地址创建功能
- `LOGIC_UPDATE_FIRST_CONTRACT_DETERMINES_TARGET.md` - 逻辑更新
- `UPDATE_SUMMARY_ADDRESS_WAIT.md` - 之前的更新

## 总结

本次更新实现了：

1. ✅ **提前创建用户**: 在部署合约前创建 User2 和 User3
2. ✅ **验证链上合法**: 等待余额到账，确保用户可用
3. ✅ **清晰的流程**: Phase 0-1 专门处理用户创建
4. ✅ **更好的错误处理**: 创建失败时提前退出
5. ✅ **更清晰的输出**: 区分不同阶段和状态

这个修改确保了合约链部署的可靠性，避免了因用户不存在导致的失败。

---

**更新者**: Kiro AI Assistant  
**状态**: 已完成 ✅
