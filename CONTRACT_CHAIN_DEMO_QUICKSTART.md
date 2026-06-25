# Contract Chain Demo - Quick Start Guide

## 快速开始

这个 demo 演示了如何在分片区块链中部署相互依赖的合约链（A → B → C），并确保所有合约都在同一个 shard 和 pool 中。

## 核心概念

### 地址映射规则

1. **Shard 计算**（基于 `root_to_tx_item.cc`）:
   ```cpp
   uint64_t hash_value = common::Hash::Hash64(to_addr);
   sharding_id = (hash_value % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
                 network::kConsensusShardBeginNetworkId;
   ```

2. **Pool 计算**（基于 `utils.cc`）:
   ```cpp
   return common::Hash::Hash32(addr.substr(0, kUnicastAddressLength)) % common::kImmutablePoolSize;
   ```

### 当前配置
- `MAX_SHARD_ID = 3`
- `CONSENSUS_SHARD_BEGIN_NETWORK_ID = 1`
- `IMMUTABLE_POOL_SIZE = 7`

## 运行 Demo

### 方法 1: 独立运行

```bash
cd clipy
python test_contract_chain_demo.py
```

### 方法 2: 集成到 shardora3.py

在 `shardora3.py` 的末尾添加:

```python
from test_contract_chain_demo import test_contract_chain_same_shard_pool

if __name__ == "__main__":
    w3 = ShardoraWeb3Mock("127.0.0.1", 8080)
    MY_KEY = "your_private_key_here"
    MY_ADDR = w3.client.get_address(MY_KEY)
    
    # 运行合约链 demo
    test_contract_chain_same_shard_pool(w3, MY_ADDR, MY_KEY)
```

## Demo 流程

### 1. 创建初始用户
```
👤 User1: 将部署 ContractA
👤 User2: 将部署 ContractB（依赖 A）
👤 User3: 将部署 ContractC（依赖 B）
```

### 2. 部署 ContractA
```
User1 部署 ContractA
→ ContractA 地址确定
→ 计算 ContractA 的 shard 和 pool
```

### 3. 确保 User2 在正确的 shard/pool
```
检查 User2 的地址是否与 ContractA 在同一 shard/pool
如果不是:
  → 重新生成 User2 的私钥
  → 重复直到找到匹配的地址
```

### 4. 部署 ContractB
```
User2 部署 ContractB(contractA_address)
→ ContractB 依赖 ContractA
→ 两者在同一 shard/pool 中
```

### 5. 确保 User3 在正确的 shard/pool
```
检查 User3 的地址是否与 ContractB 在同一 shard/pool
如果不是:
  → 重新生成 User3 的私钥
  → 重复直到找到匹配的地址
```

### 6. 部署 ContractC
```
User3 部署 ContractC(contractB_address)
→ ContractC 依赖 ContractB
→ 三者都在同一 shard/pool 中
```

### 7. 验证
```
✅ 检查所有合约是否在同一 shard 和 pool
✅ 执行合约调用链: C → B → A
✅ 验证状态变化正确传播
```

## 合约功能

### ContractA
```solidity
- value: uint256 (初始值 100)
- setValue(uint256): 设置 value
- getValue(): 查询 value
```

### ContractB
```solidity
- contractA: address (ContractA 地址)
- multiplier: uint256 (初始值 2)
- setMultiplier(uint256): 设置乘数
- updateValueInA(): 调用 A.setValue(A.getValue() * multiplier)
- getValueFromA(): 查询 A 的 value
```

### ContractC
```solidity
- contractB: address (ContractB 地址)
- addend: uint256 (初始值 50)
- setAddend(uint256): 设置加数
- triggerChainUpdate(): 
    1. 调用 B.setMultiplier(3)
    2. 调用 B.updateValueInA()
    3. 触发完整的调用链
- getValueFromChain(): 查询链上的最终值
```

## 示例输出

```
================================================================================
TEST: Contract Chain with Same Shard/Pool Enforcement
================================================================================

[Phase 1] Creating initial users...

👤 User1:
   Address: a1b2c3d4e5f6789012345678901234567890abcd
   Shard: 2, Pool: 3

👤 User2 (initial):
   Address: f6e5d4c3b2a1098765432109876543210fedcba9
   Shard: 1, Pool: 5

👤 User3 (initial):
   Address: 1234567890abcdef1234567890abcdef12345678
   Shard: 3, Pool: 2

--------------------------------------------------------------------------------
[Phase 2] User1 deploys ContractA
--------------------------------------------------------------------------------

📋 ContractA (predicted):
   Address: abcdef1234567890abcdef1234567890abcdef12
   Shard: 2, Pool: 3

✅ ContractA deployed at: abcdef1234567890abcdef1234567890abcdef12

--------------------------------------------------------------------------------
[Phase 3] Ensuring User2 is in same shard/pool as ContractA
--------------------------------------------------------------------------------

⚠️  User2 mismatch detected:
   User2: Shard 1, Pool 5
   ContractA: Shard 2, Pool 3

🔄 Regenerating User2 to match ContractA's shard/pool...
  🔍 Searching for user address in shard 2, pool 3...
  ✅ Found matching address after 1247 attempts
     Address: 9876543210fedcba9876543210fedcba98765432
     Shard: 2, Pool: 3

✅ User2 regenerated successfully!

--------------------------------------------------------------------------------
[Phase 4] User2 deploys ContractB (depends on ContractA)
--------------------------------------------------------------------------------

📋 ContractB (predicted):
   Address: fedcba9876543210fedcba9876543210fedcba98
   Shard: 2, Pool: 3
   Depends on ContractA: abcdef1234567890abcdef1234567890abcdef12

✅ ContractB deployed at: fedcba9876543210fedcba9876543210fedcba98

[... Phase 5-6 similar for User3 and ContractC ...]

================================================================================
[Phase 7] Verification Summary
================================================================================

📊 Deployment Summary:
   ContractA: abcdef123456... | Shard 2 | Pool 3
   ContractB: fedcba987654... | Shard 2 | Pool 3
   ContractC: 0123456789ab... | Shard 2 | Pool 3

✅ SUCCESS: All contracts are in the same shard (2) and pool (3)!

================================================================================
[Phase 8] Executing Contract Calls
================================================================================

[Call 1] User1 calls ContractA.getValue()
   Result: 100

[Call 2] User2 calls ContractB.getValueFromA()
   Result: 100

[Call 3] User3 calls ContractC.triggerChainUpdate()
   ✅ Chain update successful
   📍 Event: MultiplierSet → {'newMultiplier': 3}
   📍 Event: ValueUpdated → {'originalValue': 100, 'newValue': 300}
   📍 Event: ChainUpdated → {'finalValue': 300}

[Verification] Checking final value in ContractA
   Final value in ContractA: 300

================================================================================
✅ Contract Chain Demo Complete!
================================================================================
```

## 为什么这很重要？

### 性能优势
- **同 shard/pool 内的合约调用**: 单次共识，延迟 ~1 秒
- **跨 shard 的合约调用**: 需要跨分片协调，延迟 ~2-3 秒

### 实际应用场景
1. **DeFi 协议**: Pool、Treasury、Bridge 等合约需要频繁交互
2. **NFT 市场**: Marketplace、Auction、Royalty 合约的协作
3. **DAO 治理**: Governor、Timelock、Treasury 合约的调用链

## 技术细节

### 地址生成算法
```python
def generate_user_for_target_shard_pool(target_shard, target_pool, max_attempts=10000):
    for attempt in range(max_attempts):
        # 1. 生成随机私钥
        sk = SigningKey.generate(curve=SECP256k1)
        private_key = sk.to_string().hex()
        
        # 2. 计算地址
        pub = sk.verifying_key.to_string("uncompressed")[1:]
        k = keccak.new(digest_bits=256)
        k.update(pub)
        address = k.digest()[-20:].hex()
        
        # 3. 检查 shard 和 pool
        shard = calc_shard_id(address)
        pool = calc_pool_index(address)
        
        # 4. 如果匹配，返回
        if shard == target_shard and pool == target_pool:
            return private_key, address
    
    return None, None
```

### CREATE2 地址计算
```python
def calc_create2_address(sender, salt, bytecode):
    # keccak256(0xff ++ sender ++ salt ++ keccak256(bytecode))
    code_hash = keccak256(bytecode)
    input_data = bytes.fromhex("ff") + sender_bytes + salt_bytes + code_hash
    return keccak256(input_data)[-20:]
```

## 常见问题

### Q: 为什么需要重新生成用户地址？
A: 因为用户地址决定了其部署的合约地址，而合约地址又决定了 shard/pool。通过选择合适的用户地址，可以控制合约的部署位置。

### Q: 生成匹配地址需要多长时间？
A: 平均 1000-5000 次尝试，约 0.1-0.5 秒。

### Q: 如果找不到匹配的地址怎么办？
A: 增加 `max_attempts` 参数，或者使用用户指定 shard 功能（见 `USER_SPECIFIED_SHARD_FEATURE.md`）。

### Q: 这个方法适用于所有合约吗？
A: 是的，只要合约之间有调用关系，就应该考虑将它们部署在同一 shard/pool 中。

## 相关文档

- [完整文档](CONTRACT_CHAIN_SAME_SHARD_POOL_DEMO.md)
- [用户指定 Shard 功能](USER_SPECIFIED_SHARD_FEATURE.md)
- [AMM 解决方案](AMM_SOLUTION_DEMO.md)

## 下一步

1. 运行 demo 并观察输出
2. 修改合约逻辑，测试不同的调用模式
3. 尝试部署更长的合约链（A → B → C → D → E）
4. 集成到你的实际应用中
