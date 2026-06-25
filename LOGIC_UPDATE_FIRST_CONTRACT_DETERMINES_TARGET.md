# Logic Update: First Contract Determines Target Shard/Pool

## 更新日期
2026-04-22

## 更新说明

修改了合约链部署逻辑，现在**第一个合约（ContractA）直接部署**，其 shard/pool 位置决定了后续所有合约的部署位置。

## 修改前的逻辑

```
1. 生成 User1, User2, User3
2. User1 部署 ContractA
3. User2 检查是否与 ContractA 在同一 shard/pool
   └─ 不匹配 → 重新生成 User2
4. User2 部署 ContractB
5. User3 检查是否与 ContractB 在同一 shard/pool  ❌ 错误
   └─ 不匹配 → 重新生成 User3
6. User3 部署 ContractC
```

**问题**: User3 与 ContractB 比较，而不是与 ContractA 比较，可能导致不一致。

## 修改后的逻辑 ✅

```
1. User1 直接部署 ContractA（不做任何检查）
2. 从 ContractA 确定目标 shard/pool
   ↓
   target_shard = ContractA.shard
   target_pool = ContractA.pool
   
3. User2 检查是否与 target 匹配
   └─ 不匹配 → 生成新 User2 → 创建并等待生效
4. User2 部署 ContractB
5. User3 检查是否与 target 匹配（注意：仍然是与 target 比较）
   └─ 不匹配 → 生成新 User3 → 创建并等待生效
6. User3 部署 ContractC
```

**优势**: 
- ✅ 所有合约都以 ContractA 为基准
- ✅ 逻辑清晰，易于理解
- ✅ 确保所有合约在同一 shard/pool

## 代码变化

### Phase 1: 准备 User1
```python
# 修改前
print("\n[Phase 1] Creating initial users...")
# 创建 User1, User2, User3

# 修改后
print("\n[Phase 1] Preparing User1 (Funder and ContractA deployer)")
# 只准备 User1
```

### Phase 2: 部署 ContractA
```python
# 修改前
print("[Phase 2] User1 deploys ContractA")

# 修改后
print("[Phase 2] User1 deploys ContractA (no shard/pool check)")
# 强调：直接部署，不检查
```

### 确定目标 shard/pool（新增）
```python
# 新增逻辑
target_shard = contract_a_shard
target_pool = contract_a_pool

print(f"🎯 Target shard/pool determined from ContractA:")
print(f"   Target Shard: {target_shard}")
print(f"   Target Pool: {target_pool}")
print(f"   All subsequent contracts will be deployed in this shard/pool")
```

### Phase 3: 准备 User2
```python
# 修改前
if user2_shard != contract_a_shard or user2_pool != contract_a_pool:
    # 与 ContractA 比较

# 修改后
if user2_shard != target_shard or user2_pool != target_pool:
    # 与 target 比较（更清晰）
```

### Phase 5: 准备 User3（关键修改）
```python
# 修改前
if user3_shard != contract_b_shard or user3_pool != contract_b_pool:
    # ❌ 错误：与 ContractB 比较

# 修改后
if user3_shard != target_shard or user3_pool != target_pool:
    # ✅ 正确：与 target（即 ContractA）比较
```

## 输出示例

### 新增的输出

```
================================================================================
TEST: Contract Chain with Same Shard/Pool Enforcement
================================================================================

[Phase 1] Preparing User1 (Funder and ContractA deployer)

👤 User1:
   Address: a1b2c3d4e5f6789012345678901234567890abcd
   Shard: 1, Pool: 2

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

--------------------------------------------------------------------------------
[Phase 3] Preparing User2 for ContractB deployment
--------------------------------------------------------------------------------

👤 User2 (initial):
   Address: f6e5d4c3b2a1098765432109876543210fedcba9
   Shard: 1, Pool: 5

⚠️  User2 mismatch detected:
   User2: Shard 1, Pool 5
   Target: Shard 2, Pool 3

🔄 Creating new User2 to match target shard/pool...
  🔍 Searching for user address in shard 2, pool 3...
  ✅ Found matching address after 1247 attempts
     Address: 9876543210fedcba9876543210fedcba98765432
     Shard: 2, Pool: 3

  💰 Funding new address with 10000000 coins...
  📤 Transaction sent: a1b2c3d4e5f6...
  ⏳ Waiting for address to be active (max 60s)...
  ✅ Address is active! (took 3.2s)
     Balance: 10000000

✅ User2 created and activated successfully!

--------------------------------------------------------------------------------
[Phase 4] User2 deploys ContractB (depends on ContractA)
--------------------------------------------------------------------------------

📋 ContractB (predicted):
   Address: fedcba9876543210fedcba9876543210fedcba98
   Shard: 2, Pool: 3
   Depends on ContractA: abcdef1234567890abcdef1234567890abcdef12

✅ ContractB deployed at: fedcba9876543210fedcba9876543210fedcba98

--------------------------------------------------------------------------------
[Phase 5] Preparing User3 for ContractC deployment
--------------------------------------------------------------------------------

👤 User3 (initial):
   Address: 1234567890abcdef1234567890abcdef12345678
   Shard: 3, Pool: 2

⚠️  User3 mismatch detected:
   User3: Shard 3, Pool 2
   Target: Shard 2, Pool 3  ← 注意：与 target 比较，不是与 ContractB

🔄 Creating new User3 to match target shard/pool...
  [... 创建过程 ...]

✅ User3 created and activated successfully!

--------------------------------------------------------------------------------
[Phase 6] User3 deploys ContractC (depends on ContractB)
--------------------------------------------------------------------------------

📋 ContractC (predicted):
   Address: 0123456789abcdef0123456789abcdef01234567
   Shard: 2, Pool: 3
   Depends on ContractB: fedcba9876543210fedcba9876543210fedcba98

✅ ContractC deployed at: 0123456789abcdef0123456789abcdef01234567

================================================================================
[Phase 7] Verification Summary
================================================================================

📊 Deployment Summary:
   ContractA: abcdef123456... | Shard 2 | Pool 3
   ContractB: fedcba987654... | Shard 2 | Pool 3
   ContractC: 0123456789ab... | Shard 2 | Pool 3

✅ SUCCESS: All contracts are in the same shard (2) and pool (3)!
```

## 关键改进

### 1. 逻辑清晰
- **明确的基准**: ContractA 是所有合约的基准
- **统一的目标**: 所有用户都与同一个 target 比较

### 2. 易于理解
- **Phase 命名**: 更清晰地描述每个阶段的目的
- **输出信息**: 明确显示 target shard/pool

### 3. 避免错误
- **修复 Bug**: User3 现在正确地与 target 比较，而不是与 ContractB
- **一致性**: 确保所有合约在同一位置

## 测试验证

### 测试场景 1: 所有用户都需要重新生成
```
ContractA: Shard 2, Pool 3
User2 (initial): Shard 1, Pool 5 → 重新生成 → Shard 2, Pool 3 ✅
User3 (initial): Shard 3, Pool 2 → 重新生成 → Shard 2, Pool 3 ✅
结果: 所有合约在 Shard 2, Pool 3 ✅
```

### 测试场景 2: 部分用户匹配
```
ContractA: Shard 1, Pool 4
User2 (initial): Shard 1, Pool 4 → 无需重新生成 ✅
User3 (initial): Shard 2, Pool 3 → 重新生成 → Shard 1, Pool 4 ✅
结果: 所有合约在 Shard 1, Pool 4 ✅
```

### 测试场景 3: 所有用户都匹配（理想情况）
```
ContractA: Shard 3, Pool 6
User2 (initial): Shard 3, Pool 6 → 无需重新生成 ✅
User3 (initial): Shard 3, Pool 6 → 无需重新生成 ✅
结果: 所有合约在 Shard 3, Pool 6 ✅
```

## 相关文件

### 修改的文件
- `clipy/test_contract_chain_demo.py` - 主要逻辑修改

### 相关文档
- `CONTRACT_CHAIN_SAME_SHARD_POOL_DEMO.md` - 完整文档
- `ADDRESS_CREATION_WAIT_FEATURE.md` - 地址创建功能
- `UPDATE_SUMMARY_ADDRESS_WAIT.md` - 之前的更新

## 总结

本次更新修复了一个重要的逻辑错误，确保：

1. ✅ **ContractA 是基准**: 第一个合约直接部署，确定目标位置
2. ✅ **统一的比较标准**: 所有用户都与 target（ContractA 的位置）比较
3. ✅ **清晰的输出**: 明确显示目标 shard/pool
4. ✅ **正确的验证**: 确保所有合约在同一位置

这个修改使得合约链部署逻辑更加清晰、可靠和易于理解。

---

**更新者**: Kiro AI Assistant  
**状态**: 已完成 ✅
