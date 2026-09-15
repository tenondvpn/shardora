# Contract Chain Same Shard/Pool Demo

## Overview

This demo demonstrates how to deploy a chain of dependent contracts (A → B → C) while ensuring all contracts are deployed in the same shard and pool. This is critical for efficient contract-to-contract calls in a sharded blockchain architecture.

## Problem Statement

In a sharded blockchain:
- Each address is mapped to a specific **shard** and **pool** based on deterministic hash functions
- Cross-shard contract calls are expensive and complex
- For optimal performance, dependent contracts should be in the same shard and pool

## Solution

The demo implements an intelligent address generation strategy:
1. User1 deploys ContractA (shard/pool determined by ContractA's address)
2. Before User2 deploys ContractB:
   - Calculate ContractB's predicted address
   - Check if User2's address maps to the same shard/pool as ContractA
   - If not, regenerate User2's private key until finding one that maps correctly
3. Repeat for User3 and ContractC

## Address Mapping Logic

### Shard Calculation
```python
def calc_shard_id(address: str) -> int:
    hash_value = Hash64(address)
    shard_range = MAX_SHARD_ID - CONSENSUS_SHARD_BEGIN_NETWORK_ID + 1
    return (hash_value % shard_range) + CONSENSUS_SHARD_BEGIN_NETWORK_ID
```

**C++ equivalent:**
```cpp
uint64_t hash_value = common::Hash::Hash64(to_addr);
sharding_id = (hash_value % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
              network::kConsensusShardBeginNetworkId;
```

### Pool Calculation
```python
def calc_pool_index(address: str) -> int:
    return Hash32(address) % IMMUTABLE_POOL_SIZE
```

**C++ equivalent:**
```cpp
return common::Hash::Hash32(addr.substr(0, kUnicastAddressLength)) % common::kImmutablePoolSize;
```

## Constants

```python
CONSENSUS_SHARD_BEGIN_NETWORK_ID = 1
MAX_SHARD_ID = 3
IMMUTABLE_POOL_SIZE = 7
UNICAST_ADDRESS_LENGTH = 20
```

## Contract Architecture

### ContractA (Base Contract)
```solidity
contract ContractA {
    uint256 public value;
    
    function setValue(uint256 _value) external;
    function getValue() external view returns (uint256);
}
```

### ContractB (Depends on A)
```solidity
contract ContractB {
    address public contractA;
    uint256 public multiplier;
    
    constructor(address _contractA);
    
    function updateValueInA() external {
        IContractA(contractA).setValue(currentValue * multiplier);
    }
}
```

### ContractC (Depends on B)
```solidity
contract ContractC {
    address public contractB;
    
    constructor(address _contractB);
    
    function triggerChainUpdate() external {
        IContractB(contractB).setMultiplier(3);
        IContractB(contractB).updateValueInA();
    }
}
```

## Usage

### Standalone Script

```bash
cd clipy
python test_contract_chain_demo.py
```

**Configuration:**
```python
# Edit the script to set your private key
MY_KEY = "your_private_key_here"

# Or use environment variable
export SHARDORA_PRIVATE_KEY="your_private_key_here"
```

### Integration with shardora3.py

Add to `shardora3.py`:
```python
from test_contract_chain_demo import test_contract_chain_same_shard_pool

if __name__ == "__main__":
    w3 = ShardoraWeb3Mock("127.0.0.1", 8080)
    MY_KEY = "your_private_key"
    MY_ADDR = w3.client.get_address(MY_KEY)
    
    # Run the contract chain demo
    test_contract_chain_same_shard_pool(w3, MY_ADDR, MY_KEY)
```

## Demo Flow

### Phase 1: Create Initial Users
```
👤 User1:
   Address: a1b2c3d4e5f6...
   Shard: 2, Pool: 3

👤 User2 (initial):
   Address: f6e5d4c3b2a1...
   Shard: 1, Pool: 5

👤 User3 (initial):
   Address: 1234567890ab...
   Shard: 3, Pool: 2
```

### Phase 2: Deploy ContractA
```
📋 ContractA (predicted):
   Address: abcdef123456...
   Shard: 2, Pool: 3

✅ ContractA deployed at: abcdef123456...
```

### Phase 3: Ensure User2 Matches ContractA's Shard/Pool
```
⚠️  User2 mismatch detected:
   User2: Shard 1, Pool 5
   ContractA: Shard 2, Pool 3

🔄 Regenerating User2 to match ContractA's shard/pool...
🔍 Searching for user address in shard 2, pool 3...
✅ Found matching address after 1247 attempts
   Address: 9876543210fe...
   Shard: 2, Pool: 3
```

### Phase 4: Deploy ContractB
```
📋 ContractB (predicted):
   Address: fedcba987654...
   Shard: 2, Pool: 3
   Depends on ContractA: abcdef123456...

✅ ContractB deployed at: fedcba987654...
```

### Phase 5-6: Repeat for User3 and ContractC

### Phase 7: Verification
```
📊 Deployment Summary:
   ContractA: abcdef123456... | Shard 2 | Pool 3
   ContractB: fedcba987654... | Shard 2 | Pool 3
   ContractC: 0123456789ab... | Shard 2 | Pool 3

✅ SUCCESS: All contracts are in the same shard (2) and pool (3)!
```

### Phase 8: Execute Contract Calls
```
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
```

## Key Features

### 1. Deterministic Address Generation
- Uses the same hash functions as the C++ backend
- Ensures Python calculations match on-chain behavior

### 2. Intelligent User Regeneration
- Automatically finds users that map to target shard/pool
- Configurable max attempts (default: 10,000)
- Provides progress feedback

### 3. Complete Contract Chain
- Demonstrates A → B → C dependency
- Shows cross-contract calls within same shard/pool
- Verifies state changes propagate correctly

### 4. Comprehensive Verification
- Checks shard/pool mapping at each step
- Verifies contract deployment addresses
- Confirms contract calls execute successfully

## Performance Considerations

### Address Generation Time
- Average attempts to find matching address: ~1,000-5,000
- Time per attempt: ~0.1ms
- Total time: 0.1-0.5 seconds per user

### Why This Matters
- **Intra-shard calls**: Fast, single consensus round
- **Cross-shard calls**: Slow, requires cross-shard coordination
- **Same pool**: Optimal for transaction batching

## Troubleshooting

### Issue: Cannot find matching address
```
❌ Failed to find matching address after 10000 attempts
```

**Solution:** Increase `max_attempts`:
```python
new_key, new_addr = generate_user_for_target_shard_pool(
    target_shard, 
    target_pool, 
    max_attempts=50000  # Increase this
)
```

### Issue: Contract deployment fails
```
❌ Contract deployment failed: insufficient gas
```

**Solution:** Ensure deployer has sufficient balance:
```python
# Check balance before deployment
balance = w3.client.get_balance(user_addr)
print(f"User balance: {balance}")

# Fund if needed
w3.send_transaction({'to': user_addr, 'value': 1000000}, FUNDER_KEY)
```

### Issue: Shard/pool mismatch after deployment
```
❌ FAILURE: Contracts are NOT in the same shard/pool!
```

**Solution:** This indicates a bug in address calculation. Verify:
1. Hash functions match C++ implementation
2. Constants are correct (MAX_SHARD_ID, IMMUTABLE_POOL_SIZE)
3. CREATE2 address calculation is correct

## Related Files

- `clipy/test_contract_chain_demo.py` - Standalone demo script
- `clipy/shardora_sdk.py` - SDK with hash functions
- `src/consensus/zbft/root_to_tx_item.cc` - C++ shard assignment logic
- `src/common/utils.cc` - C++ pool calculation (GetAddressPoolIndex)
- `src/common/hash.h` - C++ hash function definitions

## References

- [User Specified Shard Feature](USER_SPECIFIED_SHARD_FEATURE.md)
- [AMM Solution Demo](AMM_SOLUTION_DEMO.md)
- [Shardora Reviewer Response](SHARDORA_REVIEWER_RESPONSE.md)

## Future Enhancements

1. **Parallel User Generation**: Generate multiple users concurrently
2. **Address Pool**: Pre-generate addresses for each shard/pool combination
3. **Smart Contract Factory**: Deploy contracts with automatic shard/pool matching
4. **Monitoring Dashboard**: Visualize contract deployment across shards/pools
