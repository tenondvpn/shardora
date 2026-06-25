# EIP-1559 Double Prefix and Signing Hash Fix

## Issue Summary

The EIP-1559 transaction implementation had two critical bugs:

1. **Double `0x` Prefix Bug**: The RPC params showed `0x0x02f86984...` instead of `0x02f86984...`
2. **Incorrect Signing Hash**: The C++ code was using the same value for both `maxPriorityFeePerGas` and `maxFeePerGas`, causing signature verification to fail

## Changes Made

### 1. Python Client (`clipy/shardora3.py`)

#### Fix 1: Double Prefix Prevention
**Location**: Lines 2495-2500

**Before**:
```python
# Ensure we have 0x prefix (add if not present)
if not raw_tx_hex.startswith('0x'):
    raw_tx_with_prefix = "0x" + raw_tx_hex
else:
    raw_tx_with_prefix = raw_tx_hex
```

**After**:
```python
# Ensure we have exactly one 0x prefix (strip any existing prefix first)
raw_tx_clean = raw_tx_hex.removeprefix('0x') if hasattr(str, 'removeprefix') else (
    raw_tx_hex[2:] if raw_tx_hex.startswith('0x') else raw_tx_hex
)
raw_tx_with_prefix = "0x" + raw_tx_clean
```

**Why**: This ensures we ALWAYS have exactly one `0x` prefix, regardless of whether `raw_tx_hex` already has one or not.

#### Fix 2: Correct EIP-1559 Signing RLP Debug Output
**Location**: Lines 2465-2495

**Before**: Always calculated legacy (EIP-155) signing RLP for debug output

**After**: Calculates correct signing RLP based on transaction type:
- **EIP-1559**: `0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList])`
- **Legacy**: `RLP([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0])`

### 2. C++ Server (`src/init/http_handler.cc`)

#### Fix 1: Store maxPriorityFeePerGas Separately
**Location**: Lines 1792-1810 (function signature)

**Before**:
```cpp
static bool DecodeEthRawTx(
        const std::string& raw_bytes,
        uint64_t& nonce,
        std::string& to,
        uint64_t& value,
        uint64_t& gas_limit,
        uint64_t& gas_price,
        std::string& data,
        uint8_t& v_byte,
        std::string& r,
        std::string& s) {
```

**After**:
```cpp
static bool DecodeEthRawTx(
        const std::string& raw_bytes,
        uint64_t& nonce,
        std::string& to,
        uint64_t& value,
        uint64_t& gas_limit,
        uint64_t& gas_price,
        std::string& data,
        uint8_t& v_byte,
        std::string& r,
        std::string& s,
        uint64_t* max_priority_fee = nullptr) {
```

**Location**: Lines 1909-1916 (EIP-1559 decode section)

**Added**:
```cpp
// Store maxPriorityFeePerGas if pointer provided
if (max_priority_fee) {
    *max_priority_fee = be_to_u64(s_maxpriority);
}
```

#### Fix 2: Use Correct maxPriorityFeePerGas in Signing Hash
**Location**: Lines 2200-2205 (eth_sendRawTransaction handler)

**Before**:
```cpp
uint64_t nonce = 0, value = 0, gas_limit = 0, gas_price = 0;
std::string to, data, r, s;
uint8_t v_byte = 0;
if (!DecodeEthRawTx(raw_bytes, nonce, to, value, gas_limit, gas_price, data, v_byte, r, s)) {
```

**After**:
```cpp
uint64_t nonce = 0, value = 0, gas_limit = 0, gas_price = 0, max_priority_fee = 0;
std::string to, data, r, s;
uint8_t v_byte = 0;
if (!DecodeEthRawTx(raw_bytes, nonce, to, value, gas_limit, gas_price, data, v_byte, r, s, &max_priority_fee)) {
```

**Location**: Lines 2268-2275 (EIP-1559 signing hash calculation)

**Before**:
```cpp
payload += rlp_encode_uint(gas_price);  // maxPriorityFeePerGas (simplified)
payload += rlp_encode_uint(gas_price);  // maxFeePerGas
```

**After**:
```cpp
payload += rlp_encode_uint(max_priority_fee);  // maxPriorityFeePerGas
payload += rlp_encode_uint(gas_price);  // maxFeePerGas
```

## How to Test

### Step 1: Compile C++ Code
```bash
cd /root/shardora/build
make -j$(nproc)
```

### Step 2: Restart Shardora Node
```bash
# Stop the current node (if running)
pkill -f shardora

# Start the node
cd /root/shardora
./start_node.sh  # or however you start your node
```

### Step 3: Run EIP-1559 Tests
```bash
cd /root/shardora/clipy
/root/tools/python3.10/bin/python3 test_eip1559.py
```

### Step 4: Run Simple Example
```bash
cd /root/shardora/clipy
/root/tools/python3.10/bin/python3 eip1559_example.py
```

## Expected Results

### Debug Output Should Show:
1. **No double prefix**: `[DEBUG] RPC params: 0x02f86984...` (NOT `0x0x02...`)
2. **Correct signing RLP**: For EIP-1559, should show `0x02` prefix in the signing RLP
3. **Matching signing hashes**: Python and C++ should calculate the same signing hash

### Transaction Should:
1. Be accepted by the server (no "invalid raw transaction" error)
2. Return a transaction hash
3. Successfully transfer tokens or deploy contracts

## Technical Details

### EIP-1559 Transaction Format
```
0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, v, r, s])
```

### EIP-1559 Signing Hash
```
keccak256(0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]))
```

**Key Point**: The signing hash includes BOTH `maxPriorityFeePerGas` and `maxFeePerGas` as separate fields. Using the same value for both (as the old code did) produces a different hash and causes signature verification to fail.

## Troubleshooting

### If you still see "invalid raw transaction":
1. Check server logs for specific error messages:
   ```bash
   tail -f /root/shardora/logs/shardora.log | grep -i "eip-1559\|DecodeEthRawTx\|sendRawTransaction"
   ```

2. Verify the signing hash matches between Python and C++:
   - Python debug output: `[DEBUG] Python signing_hash=...`
   - C++ log output: `EIP-1559 signing: signing_hash=...`

3. Check that the transaction is properly formatted:
   - First byte should be `0x02`
   - Second byte should be `0xf8` or `0xf9` (RLP list start)

### If compilation fails:
1. Make sure you're in the correct build directory
2. Check for syntax errors in the modified files
3. Try a clean rebuild:
   ```bash
   cd /root/shardora/build
   make clean
   make -j$(nproc)
   ```

## Files Modified
- `clipy/shardora3.py` - Python client EIP-1559 signing and RPC call
- `src/init/http_handler.cc` - C++ server EIP-1559 decoding and signature verification

## Related Documentation
- `EIP1559_IMPLEMENTATION.md` - Complete EIP-1559 implementation guide
- `EIP1559_BUG_FIX.md` - Previous bug fix for accessList decoding
- `EIP1559_QUICK_REFERENCE.md` - Quick reference for using EIP-1559 transactions
