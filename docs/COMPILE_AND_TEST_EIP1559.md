# Compile and Test EIP-1559 Implementation

## Current Status

✅ **Python client fixed**: Double `0x` prefix issue resolved
✅ **C++ code updated**: maxPriorityFeePerGas now properly decoded and used in signing hash
❌ **Not compiled yet**: You need to recompile the C++ code for changes to take effect

## Error Analysis

The current error `address invalid: d29fc9cd40cae4c2d250a66af1d9f6e0c88f60a3` means:
- The signature is being decoded successfully
- The public key is being recovered from the signature
- But the recovered address doesn't match the expected address

This happens because the C++ server is still using the OLD code that doesn't properly handle `maxPriorityFeePerGas`, causing it to calculate a different signing hash than the Python client.

## Step-by-Step Instructions

### Step 1: Recompile C++ Code

```bash
cd /root/shardora/build
make -j$(nproc)
```

**Expected output**: Should compile successfully without errors. Look for:
```
[ 99%] Building CXX object ...
[100%] Linking CXX executable shardora
```

**If compilation fails**, check for:
- Syntax errors in `src/init/http_handler.cc`
- Missing dependencies
- Run `make clean` first, then `make -j$(nproc)` again

### Step 2: Restart Shardora Node

```bash
# Stop the current node
pkill -f shardora

# Wait a moment for it to fully stop
sleep 2

# Start the node (adjust command based on your setup)
cd /root/shardora
./start_node.sh
# OR
nohup ./build/shardora --config=./config.json > logs/shardora.log 2>&1 &
```

**Verify node is running**:
```bash
ps aux | grep shardora
# Should show the shardora process running
```

### Step 3: Monitor Server Logs (Optional but Recommended)

Open a second terminal and run:
```bash
tail -f /root/shardora/logs/shardora.log | grep -E "EIP-1559|eth_sendRawTransaction|signing"
```

This will show you the C++ side's signing hash calculation for comparison with Python.

### Step 4: Run EIP-1559 Tests

```bash
cd /root/shardora/clipy
/root/tools/python3.10/bin/python3 test_eip1559.py
```

### Step 5: Verify Success

**Look for these indicators**:

1. **No double prefix**: 
   ```
   [DEBUG] RPC params: 0x02f86984...  ✓ (NOT 0x0x02...)
   ```

2. **Correct signing RLP format**:
   ```
   [DEBUG] Python signing_rlp (with 0x02 prefix)=02e684c7facf95...
   ```

3. **Matching signing hashes** (check server logs):
   ```
   Python: [DEBUG] Python signing_hash=61177e41a3068a6569e22497174ae3195645e743d4dd7593b6123ff8d9aff99e
   C++:    EIP-1559 signing: signing_hash=61177e41a3068a6569e22497174ae3195645e743d4dd7593b6123ff8d9aff99e
   ```
   These should be IDENTICAL!

4. **Transaction accepted**:
   ```
   ✅ Transaction sent!
   TX Hash: 0x...
   ```

5. **No "address invalid" error**

## What Changed in the C++ Code

### Before (WRONG):
```cpp
// Used gas_price for BOTH fields
payload += rlp_encode_uint(gas_price);  // maxPriorityFeePerGas (simplified)
payload += rlp_encode_uint(gas_price);  // maxFeePerGas
```

This caused the signing hash to be:
```
keccak256(0x02 || RLP([chainId, nonce, 2, 2, gasLimit, to, value, data, accessList]))
                                        ↑  ↑
                                    both are 2!
```

### After (CORRECT):
```cpp
// Uses separate values
payload += rlp_encode_uint(max_priority_fee);  // maxPriorityFeePerGas = 1
payload += rlp_encode_uint(gas_price);         // maxFeePerGas = 2
```

This produces the correct signing hash:
```
keccak256(0x02 || RLP([chainId, nonce, 1, 2, gasLimit, to, value, data, accessList]))
                                        ↑  ↑
                                        1  2  (correct!)
```

## Troubleshooting

### If you still get "address invalid" after recompiling:

1. **Verify compilation actually happened**:
   ```bash
   ls -lh /root/shardora/build/shardora
   # Check the timestamp - should be recent (just now)
   ```

2. **Make sure you restarted the node**:
   ```bash
   ps aux | grep shardora
   # Kill any old processes
   pkill -f shardora
   # Start fresh
   ```

3. **Check server logs for signing hash**:
   The C++ log should show:
   ```
   EIP-1559 signing: type_and_rlp_hex=02e684c7facf95800102825208..., signing_hash=61177e41...
   ```
   Compare this hash with the Python debug output. They MUST match!

4. **If hashes still don't match**, there might be an issue with:
   - RLP encoding of integers (leading zeros?)
   - Byte order (big-endian vs little-endian)
   - Empty field encoding (should be 0x80)

### If compilation fails:

**Common error**: `'max_priority_fee' was not declared in this scope`

**Solution**: Make sure you updated ALL three locations in `http_handler.cc`:
1. Function signature (line ~1807)
2. Decode section (line ~1915)
3. eth_sendRawTransaction handler (line ~2200 and ~2272)

## Expected Test Results

After successful compilation and restart:

```
======================================================================
EIP-1559 Transaction Test Suite
======================================================================
...
TEST CASE 1: EIP-1559 Native Token Transfer
✅ Transaction sent!
TX Hash: 0x...
✅ Balance updated correctly

TEST CASE 2: EIP-1559 Contract Deployment
✅ Contract deployed!
Contract Address: 0x...

======================================================================
TEST SUMMARY
======================================================================
EIP-1559 Transfer................................. ✅ PASSED
EIP-1559 Contract Deploy.......................... ✅ PASSED

Total: 2/2 tests passed
```

## Next Steps After Success

Once tests pass:
1. Try the simple example: `/root/tools/python3.10/bin/python3 eip1559_example.py`
2. Test with different gas values
3. Test contract interactions with EIP-1559
4. Update documentation with any findings

## Need Help?

If you're still having issues after following these steps:
1. Share the compilation output
2. Share the server log output (especially the signing_hash lines)
3. Share the Python debug output
4. We can compare the signing hashes to identify the mismatch
