# EIP-1559 Signature Recovery Debug

## Current Status

✅ **Signing hash matches** between Python and C++
❌ **Public key recovery fails** - C++ recovers different public key than Python

## Evidence

### Python Side (CORRECT)
```
Signing hash: 3b614d97b99fd21ba97508d69d86f1ddd438904da8acf8c6721165b915bf39f6
Expected pubkey: 5e3ae491ca10790f96913451a70f3d3e701d885218b6820ca1db188e369d617568198be22209764d7be62eaf9343c3fc3e2962c90f316896e11d70e942b7633a
Expected address: b43b7ada2c7b17e0008501ded58d388a1bd72257
Recovered address: b43b7ada2c7b17e0008501ded58d388a1bd72257 ✓
```

### C++ Side (WRONG)
```
Signing hash: 3b614d97b99fd21ba97508d69d86f1ddd438904da8acf8c6721165b915bf39f6
Recovered pubkey: 69ac09a167383687722a3822468534933b9c81529a34f2d3666df2e872ef17101b97277c625d475c1b5d40602a4d0efaf27f803e330a7a240faf92486c4cecaa
Recovered address: 4e9764be543fb028f09ff1d2016e3b5675006be3 ✗
v_byte: 0
```

## Analysis

The signing hashes match perfectly, which means:
1. ✅ EIP-1559 transaction decoding is correct
2. ✅ Signing RLP construction is correct  
3. ✅ maxPriorityFeePerGas and maxFeePerGas are correctly used

However, the recovered public keys are completely different, which means:
- ❌ The signature recovery is using wrong parameters

## Possible Causes

### 1. Wrong Recovery ID (v value)

For EIP-1559, the `v` value in the transaction IS the recovery ID (0 or 1).

**C++ log shows**: `v_byte=0`

But the recovered public key is wrong. This could mean:
- The actual recovery ID should be 1, not 0
- OR there's an issue with how the signature (r, s) is being passed to the recovery function

### 2. Signature Format Issue

The C++ code builds the signature as: `r || s || v` (65 bytes total)

```cpp
std::string sign_for_recover;
sign_for_recover.append(r);  // 32 bytes
sign_for_recover.append(s);  // 32 bytes
sign_for_recover.push_back(static_cast<char>(v_byte));  // 1 byte
```

Then calls:
```cpp
std::string pubkey = security::Secp256k1::Instance()->Recover(
    sign_for_recover, signing_hash, false);
```

The `Secp256k1::Recover` function expects:
```cpp
int v = sign[64];  // Get v from position 64 (0-indexed)
```

This looks correct.

### 3. Signature Byte Order

Could there be an issue with big-endian vs little-endian?

The RLP encoding uses big-endian, and the signature r, s values are decoded as big-endian.

### 4. Signature Padding

The C++ code pads r and s to 32 bytes:
```cpp
r = std::string(32 - std::min<size_t>(s_r.size(), 32), '\0') + s_r.substr(s_r.size() > 32 ? s_r.size() - 32 : 0);
s = std::string(32 - std::min<size_t>(s_s.size(), 32), '\0') + s_s.substr(s_s.size() > 32 ? s_s.size() - 32 : 0);
```

This left-pads with zeros if the value is shorter than 32 bytes, which is correct.

## Hypothesis

The most likely issue is that the **recovery ID is wrong**. Even though the transaction contains `v=0`, the actual recovery ID needed might be different.

In Ethereum:
- For legacy transactions: `v = 27 + recovery_id` (or `v = chain_id * 2 + 35 + recovery_id` for EIP-155)
- For EIP-1559: `v = recovery_id` (0 or 1)

But `eth_account` might be doing something different internally.

## Next Steps

### Step 1: Add Debug Logging

Add logging to see the actual r, s, v values being used:

```cpp
SHARDORA_INFO("EIP-1559 signature: r=%s, s=%s, v=%u",
          common::Encode::HexEncode(r).c_str(),
          common::Encode::HexEncode(s).c_str(),
          v_byte);
```

### Step 2: Try Both Recovery IDs

Modify the C++ code to try both v=0 and v=1 and see which one recovers the correct address:

```cpp
// Try v=0
std::string sign_v0 = r + s + std::string(1, '\x00');
std::string pubkey_v0 = security::Secp256k1::Instance()->Recover(sign_v0, signing_hash, false);

// Try v=1  
std::string sign_v1 = r + s + std::string(1, '\x01');
std::string pubkey_v1 = security::Secp256k1::Instance()->Recover(sign_v1, signing_hash, false);

// Log both
SHARDORA_INFO("Recovery with v=0: pubkey=%s", common::Encode::HexEncode(pubkey_v0).c_str());
SHARDORA_INFO("Recovery with v=1: pubkey=%s", common::Encode::HexEncode(pubkey_v1).c_str());
```

### Step 3: Compare with Python

Run the Python script `clipy/check_v_value.py` to see which recovery ID Python uses:

```bash
cd /root/shardora/clipy
/root/tools/python3.10/bin/python3 check_v_value.py
```

This will show which v value (0 or 1) recovers to the expected address.

### Step 4: Fix the Code

Once we know the correct recovery ID, we can either:
- Use the v value from the transaction as-is (if it's correct)
- OR adjust the v value before recovery (if there's a systematic offset)

## Temporary Workaround

If the issue is that v=0 should be v=1 (or vice versa), we can add a simple fix:

```cpp
// EIP-1559: Try both recovery IDs and use the one that works
std::string pubkey;
for (int try_v = 0; try_v <= 1; try_v++) {
    std::string sign_try = r + s + std::string(1, static_cast<char>(try_v));
    std::string pubkey_try = security::Secp256k1::Instance()->Recover(sign_try, signing_hash, false);
    
    if (!pubkey_try.empty()) {
        // Check if this is a valid recovery (we can't verify the address here,
        // but at least we got a public key)
        if (pubkey.empty() || try_v == v_byte) {
            pubkey = pubkey_try;
            if (try_v == v_byte) break;  // Prefer the v from transaction
        }
    }
}
```

But this is a workaround, not a proper fix. We need to understand WHY the v value is wrong.

## Files to Modify

1. `src/init/http_handler.cc` - Add debug logging and potentially fix v value handling
2. Recompile: `cd /root/shardora/build && make -j$(nproc)`
3. Restart node
4. Test again

## Test Commands

```bash
# Recompile
cd /root/shardora/build && make -j$(nproc)

# Restart node
pkill -f shardora && sleep 2 && cd /root/shardora && ./start_node.sh

# Run Python check script
cd /root/shardora/clipy
/root/tools/python3.10/bin/python3 check_v_value.py

# Run test
/root/tools/python3.10/bin/python3 test_eip1559.py

# Check logs
tail -f /root/shardora/logs/shardora.log | grep -E "EIP-1559|signature|pubkey"
```
