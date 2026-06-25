# Contract Chain Demo: Query Logic Implementation

## Overview

This document explains how the contract chain demo queries ContractA's shard and pool information from the blockchain after deployment, ensuring all subsequent contracts are deployed in the same shard/pool.

## Key Changes

### 1. Query Address Info Function

The `query_address_info()` function queries address information from the blockchain:

```python
def query_address_info(w3, address: str, max_wait: int = 60):
    """
    Query address information from the blockchain, including shard and pool.
    
    Returns:
        dict: Address info with 'shard_id' and 'pool_index', or None if failed
    """
```

**Implementation Details:**
- Uses the `/query_account` API endpoint
- Polls every 2 seconds for up to 60 seconds
- Returns a dict with `shard_id`, `pool_index`, `balance`, and `nonce`
- Returns `None` if the query times out or fails

**API Response Format:**
```json
{
  "balance": "0",
  "shardingId": 3,
  "poolIndex": 21,
  "addr": "shTzhb3S85Rsk5iM0vOCBjLS5M0=",
  "type": "kNormal",
  "bytesCode": "...",
  "latestHeight": "17",
  "txIndex": 0,
  "nonce": "0"
}
```

**Important:** The API returns camelCase field names:
- `shardingId` (not `sharding_id` or `shard_id`)
- `poolIndex` (not `pool_index`)
- `latestHeight` (not `latest_height`)
- `txIndex` (not `tx_index`)

The query function handles multiple naming conventions for compatibility:
```python
shard_id = data.get('shardingId') or data.get('sharding_id') or data.get('shard_id')
pool_index = data.get('poolIndex') or data.get('pool_index')
```

### 2. Query After ContractA Deployment

After deploying ContractA, the demo queries its actual shard/pool from the blockchain:

```python
# Deploy ContractA
contract_a = w3.shardora.contract(abi=a_abi, bytecode=a_bin, sender_address=user1_addr).deploy({
    'from': user1_addr,
    'salt': salt_a,
}, user1_key)

# Query actual shard/pool from blockchain
contract_a_info = query_address_info(w3, contract_a.address, max_wait=60)

if contract_a_info:
    target_shard = contract_a_info['shard_id']
    target_pool = contract_a_info['pool_index']
else:
    # Fallback to calculated values
    target_shard = calc_shard_id(contract_a.address)
    target_pool = calc_pool_index(contract_a.address)
```

### 3. Comparison with Calculated Values

The demo compares the queried values with the locally calculated values:

```python
if target_shard != contract_a_shard or target_pool != contract_a_pool:
    print(f"⚠️  Note: Actual values differ from predicted:")
    print(f"   Predicted: Shard {contract_a_shard}, Pool {contract_a_pool}")
    print(f"   Actual: Shard {target_shard}, Pool {target_pool}")
```

This helps verify that the local calculation logic matches the blockchain's assignment logic.

## Workflow

### Phase 0: Prepare User1 (Funder)
- User1 is the account that will fund all operations

### Phase 1: Pre-create User2 and User3
- Generate User2 and User3 addresses
- Create them on-chain via `kRootCreateAddress` transactions
- Wait for balance confirmation (up to 60 seconds)

### Phase 2: Deploy ContractA and Query
1. Deploy ContractA directly (no shard/pool checks)
2. **Query ContractA's actual shard/pool from blockchain**
3. Set `target_shard` and `target_pool` based on query result
4. If query fails, fall back to calculated values (with warning)

### Phase 3: Check User2 Compatibility
- Calculate User2's shard/pool
- If it doesn't match target, regenerate User2 in correct shard/pool
- Create new User2 on-chain and wait for confirmation

### Phase 4: Deploy ContractB
- User2 deploys ContractB (depends on ContractA)
- ContractB should be in same shard/pool as ContractA

### Phase 5: Check User3 Compatibility
- Calculate User3's shard/pool
- If it doesn't match target, regenerate User3 in correct shard/pool
- Create new User3 on-chain and wait for confirmation

### Phase 6: Deploy ContractC
- User3 deploys ContractC (depends on ContractB)
- ContractC should be in same shard/pool as ContractA and ContractB

### Phase 7: Verification
- Verify all contracts are in the same shard/pool
- Display summary

### Phase 8: Execute Contract Calls
- User1 calls ContractA.getValue()
- User2 calls ContractB.getValueFromA()
- User3 calls ContractC.triggerChainUpdate()

## Deterministic Shard/Pool Calculation

The local calculation matches the C++ implementation:

### Shard Calculation
```python
def calc_shard_id(address: str) -> int:
    addr_bytes = bytes.fromhex(address.replace('0x', ''))[:20]
    hash_value = hash64(addr_bytes)
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
    addr_bytes = bytes.fromhex(address.replace('0x', ''))[:20]
    return hash32(addr_bytes) % IMMUTABLE_POOL_SIZE
```

**C++ equivalent:**
```cpp
return common::Hash::Hash32(addr.substr(0, kUnicastAddressLength)) % common::kImmutablePoolSize;
```

## Error Handling

### Query Timeout
If the blockchain query times out:
- The demo falls back to calculated values
- A warning is displayed
- The demo continues but may encounter issues if calculated values differ from actual

### Address Creation Timeout
If address creation times out:
- The demo returns the address anyway (it may still be valid)
- A warning is displayed
- The user may need to wait longer or check manually

### Query Failure
If the query fails due to network or API errors:
- The demo retries every 2 seconds
- After max_wait seconds, it falls back to calculated values
- Error messages are displayed for debugging

## Testing

To test the query logic:

1. **Start the blockchain:**
   ```bash
   ./build/shardora --show_cmd -g 1 -n 1 -c 1 -m 1 -s 1 -d 1
   ```

2. **Run the demo:**
   ```bash
   cd clipy
   python3 test_contract_chain_demo.py
   ```

3. **Verify the output:**
   - Check that ContractA's shard/pool is queried from blockchain
   - Verify that calculated values match queried values
   - Confirm all contracts are in the same shard/pool

## Expected Output

```
[Phase 2] User1 deploys ContractA (no shard/pool check)
📋 ContractA (predicted):
   Address: abc123...
   Shard: 2, Pool: 4

✅ ContractA deployed at: abc123...

🔍 Querying ContractA's actual shard/pool from blockchain...
  🔍 Querying address info from blockchain...
  ✅ Address info retrieved! (took 3.2s)
     Shard: 2, Pool: 4
     Balance: 0

✅ ContractA info retrieved from blockchain:
   Actual Shard: 2
   Actual Pool: 4
   ✅ Matches predicted values

🎯 Target shard/pool determined:
   Target Shard: 2
   Target Pool: 4
   All subsequent contracts will be deployed in this shard/pool
```

## Troubleshooting

### Query Always Times Out
- Check that the blockchain is running
- Verify the API endpoint is accessible
- Check network connectivity
- Increase `max_wait` parameter

### Calculated Values Don't Match Queried Values
- This indicates a mismatch between Python and C++ hash implementations
- Verify that `hash32()` and `hash64()` match C++ `Hash::Hash32()` and `Hash::Hash64()`
- Check that address format is consistent (20 bytes, no 0x prefix)

### Address Not Found
- The address may not have been created yet
- Wait longer for the transaction to be processed
- Check that the deployment transaction succeeded

## Conclusion

The query logic ensures that:
1. ContractA's actual shard/pool is retrieved from the blockchain
2. The local calculation is verified against the blockchain
3. All subsequent contracts are deployed in the correct shard/pool
4. The demo handles errors gracefully with fallbacks and warnings
