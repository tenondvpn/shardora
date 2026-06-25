# Contract Chain Query - Quick Start Guide

## Summary of Changes

The contract chain demo now queries ContractA's actual shard and pool from the blockchain after deployment, ensuring consistency with the blockchain's assignment logic.

## Key Features

### 1. Blockchain Query After ContractA Deployment
After deploying ContractA, the demo queries its actual shard/pool from the blockchain:

```python
# Deploy ContractA
contract_a = w3.shardora.contract(...).deploy(...)

# Query actual shard/pool from blockchain
contract_a_info = query_address_info(w3, contract_a.address, max_wait=60)

if contract_a_info:
    target_shard = contract_a_info['shard_id']
    target_pool = contract_a_info['pool_index']
```

### 2. Verification Against Calculated Values
The demo compares queried values with locally calculated values:

```python
if target_shard != contract_a_shard or target_pool != contract_a_pool:
    print("⚠️  Actual values differ from predicted")
```

This helps verify that the Python calculation logic matches the C++ blockchain logic.

### 3. Graceful Fallback
If the query fails, the demo falls back to calculated values with a warning:

```python
else:
    print("⚠️  Could not query from blockchain, using calculated values")
    target_shard = calc_shard_id(contract_a.address)
    target_pool = calc_pool_index(contract_a.address)
```

## API Endpoint

The query uses the `/query_account` endpoint:

**Request:**
```
POST https://127.0.0.1:23001/query_account
Content-Type: application/x-www-form-urlencoded

address=abc123def456...
```

**Response (JSON with camelCase keys):**
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

**Note:** The API returns camelCase field names (`shardingId`, `poolIndex`), not snake_case (`sharding_id`, `pool_index`).

## Running the Demo

```bash
# Start blockchain
./build/shardora --show_cmd -g 1 -n 1 -c 1 -m 1 -s 1 -d 1

# Run demo
cd clipy
python3 test_contract_chain_demo.py
```

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
```

## Implementation Details

### Query Function
```python
def query_address_info(w3, address: str, max_wait: int = 60):
    """Query address info from blockchain"""
    clean_addr = address.replace('0x', '')
    
    while time.time() - start_time < max_wait:
        result = requests.post(
            w3.client.query_url, 
            data={"address": clean_addr}, 
            timeout=5, 
            verify=w3.client.verify_ssl
        )
        
        if result.status_code == 200:
            data = result.json()
            # API returns camelCase: 'shardingId' and 'poolIndex'
            shard_id = data.get('shardingId') or data.get('sharding_id') or data.get('shard_id')
            pool_index = data.get('poolIndex') or data.get('pool_index')
            
            if shard_id is not None and pool_index is not None:
                return {
                    'shard_id': int(shard_id),
                    'pool_index': int(pool_index),
                    'balance': int(data.get('balance', 0)),
                    'nonce': int(data.get('nonce', 0))
                }
        
        time.sleep(2)
    
    return None
```

### Deterministic Calculation (Fallback)
```python
def calc_shard_id(address: str) -> int:
    addr_bytes = bytes.fromhex(address.replace('0x', ''))[:20]
    hash_value = hash64(addr_bytes)
    shard_range = MAX_SHARD_ID - CONSENSUS_SHARD_BEGIN_NETWORK_ID + 1
    return (hash_value % shard_range) + CONSENSUS_SHARD_BEGIN_NETWORK_ID

def calc_pool_index(address: str) -> int:
    addr_bytes = bytes.fromhex(address.replace('0x', ''))[:20]
    return hash32(addr_bytes) % IMMUTABLE_POOL_SIZE
```

## Workflow

1. **Phase 0:** Prepare User1 (funder)
2. **Phase 1:** Pre-create User2 and User3 on-chain
3. **Phase 2:** Deploy ContractA → **Query actual shard/pool from blockchain**
4. **Phase 3:** Check User2 compatibility, regenerate if needed
5. **Phase 4:** Deploy ContractB
6. **Phase 5:** Check User3 compatibility, regenerate if needed
7. **Phase 6:** Deploy ContractC
8. **Phase 7:** Verify all contracts in same shard/pool
9. **Phase 8:** Execute contract call chain

## Troubleshooting

### Query Times Out
- Increase `max_wait` parameter
- Check blockchain is running
- Verify API endpoint is accessible

### Calculated Values Don't Match
- This indicates hash implementation mismatch
- Verify Python `hash32()` and `hash64()` match C++ implementations
- Check address format (20 bytes, no 0x prefix)

### Address Not Found
- Wait longer for transaction processing
- Check deployment transaction succeeded
- Verify address format is correct

## Files Modified

- `clipy/test_contract_chain_demo.py` - Main demo implementation
- `CONTRACT_CHAIN_QUERY_LOGIC.md` - Detailed documentation
- `CONTRACT_CHAIN_QUERY_QUICKSTART.md` - This quick start guide

## Related Documents

- `CONTRACT_CHAIN_SAME_SHARD_POOL_DEMO.md` - Overall demo documentation
- `CONTRACT_CHAIN_DEMO_QUICKSTART.md` - General quick start guide
- `ADDRESS_CREATION_WAIT_FEATURE.md` - Address creation details
- `LOGIC_UPDATE_FIRST_CONTRACT_DETERMINES_TARGET.md` - Logic update details
