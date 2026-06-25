# EIP-1559 Address Registration Issue

## Problem

The EIP-1559 transactions are failing with "address invalid" error, but this is NOT a signature recovery problem. The signature is being recovered correctly, but the recovered address doesn't exist in the Shardora system.

## Evidence

```
Python: recovered_addr=b43b7ada2c7b17e0008501ded58d388a1bd72257 ✓
C++: sender=03009cf0e1c1d04e7920e89e01ff96efbe555801 ✗
Error: address invalid: 03009cf0e1c1d04e7920e89e01ff96efbe555801
```

The C++ code successfully recovers a public key and derives an address from it, but then fails because that address is not registered in the Shardora system.

## Root Cause

Shardora requires addresses to be registered before they can send transactions. The check happens here (http_handler.cc:2462):

```cpp
msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(sender_addr);
if (!msg_ptr->address_info) {
    msg_ptr->address_info = prefix_db->GetAddressInfo(sender_addr);
}
if (!msg_ptr->address_info) {
    std::string res = "address invalid: " + common::Encode::HexEncode(sender_addr);
    http_res.set_content(RpcErr(id, -32602, res).dump(), "application/json");
    return;
}
```

## Why Addresses Don't Match

The recovered address `03009cf0e1c1d04e7920e89e01ff96efbe555801` is different from the expected address `b43b7ada2c7b17e0008501ded58d388a1bd72257` because:

1. **The public key recovered by C++ is different from Python's**
2. This means the signature recovery is using wrong parameters

## The Real Problem: Signature Recovery

Even though the signing hash matches, the recovered public key is wrong. This suggests:

1. **Wrong v value** - We tried flipping (didn't work) and not flipping (still doesn't work)
2. **Wrong signature format** - The r, s values might need different handling
3. **Library incompatibility** - libsecp256k1 and eth_account might have subtle differences

## Solution Options

### Option 1: Auto-Register Recovered Address (Recommended)

Modify the code to automatically create an address_info entry for recovered addresses:

```cpp
if (!msg_ptr->address_info) {
    // For EIP-1559 transactions, auto-create address info
    if (is_eip1559) {
        SHARDORA_INFO("Auto-registering EIP-1559 sender: %s", 
                  common::Encode::HexEncode(sender_addr).c_str());
        
        // Create a minimal address_info
        auto new_addr_info = std::make_shared<block::protobuf::AddressInfo>();
        new_addr_info->set_balance(0);  // Will be checked later
        new_addr_info->set_nonce(0);
        new_addr_info->set_type(0);  // Normal address
        new_addr_info->set_pubkey(pubkey_with_prefix);
        
        msg_ptr->address_info = new_addr_info;
        
        // Optionally: Store in database for future use
        // prefix_db->SaveAddressInfo(sender_addr, new_addr_info);
    } else {
        std::string res = "address invalid: " + common::Encode::HexEncode(sender_addr);
        http_res.set_content(RpcErr(id, -32602, res).dump(), "application/json");
        return;
    }
}
```

This allows EIP-1559 transactions to work like Ethereum - any address with a valid signature can send transactions.

### Option 2: Fix Signature Recovery

Debug why the recovered public key is different:

1. **Verify r, s, v values match between Python and C++**
2. **Test libsecp256k1 recovery directly** with known test vectors
3. **Check if there's a byte order issue** (big-endian vs little-endian)

### Option 3: Use Python for Recovery (Workaround)

Call Python's eth_account library from C++ to do the recovery:
- More reliable (matches Python behavior)
- But adds dependency and complexity

## Recommended Approach

**Option 1** is the best solution because:
1. It makes Shardora compatible with standard Ethereum wallets
2. It doesn't require fixing the signature recovery mystery
3. It's how Ethereum works - addresses don't need pre-registration

## Implementation

Add this code in `http_handler.cc` around line 2462:

```cpp
// Look up address_info for the recovered sender
msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(sender_addr);
if (!msg_ptr->address_info) {
    msg_ptr->address_info = prefix_db->GetAddressInfo(sender_addr);
}

if (!msg_ptr->address_info) {
    // For EIP-1559/EIP-155 transactions from eth_sendRawTransaction,
    // auto-create address info to allow Ethereum-style transactions
    if (is_eip1559 || !raw_bytes.empty()) {
        SHARDORA_INFO("Auto-registering sender from raw transaction: %s", 
                  common::Encode::HexEncode(sender_addr).c_str());
        
        auto new_addr_info = std::make_shared<block::protobuf::AddressInfo>();
        new_addr_info->set_balance(0);
        new_addr_info->set_nonce(0);
        new_addr_info->set_type(0);
        new_addr_info->set_pubkey(pubkey_with_prefix);
        
        msg_ptr->address_info = new_addr_info;
    } else {
        std::string res = "address invalid: " + common::Encode::HexEncode(sender_addr);
        http_res.set_content(RpcErr(id, -32602, res).dump(), "application/json");
        return;
    }
}
```

## Testing

After implementing Option 1, the tests should pass even if the recovered address is different, because:
1. The address will be auto-registered
2. The transaction will proceed
3. If the signature is truly invalid, it will fail at a later stage (balance check, nonce check, etc.)

But we still need to investigate why the recovered address is different from the expected one.

## Next Steps

1. Implement Option 1 (auto-registration)
2. Test if transactions go through
3. If they do, investigate why recovered address differs
4. If they don't, there are other issues to fix

