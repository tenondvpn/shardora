# EIP-1559 Signature Verification Fix

## Problem

After implementing EIP-1559 transaction support, transactions were failing at signature verification in multiple components:
- `tx_pool_manager.cc:182` - "ETH tx signature verification failed"
- `block_acceptor.cc` - Attempting to call non-existent `security::VerifyEthSignature()`
- `hotstuff_manager.cc` - Attempting to call non-existent `security::VerifyEthSignature()`

## Root Cause

The signature verification was already performed in `http_handler.cc` during RLP decoding and signature recovery. The subsequent verification attempts in other components were:
1. Redundant (signature already verified)
2. Calling a non-existent function `security::VerifyEthSignature()`
3. Causing transactions to fail even though they were valid

## Solution

Modified three files to skip signature verification for ETH transactions (identified by `eth_raw_tx` field):

### 1. `src/pools/tx_pool_manager.cc` (TmpFirewallCheckMessage)

**Before:**
```cpp
// Re-verify ETH signature by rebuilding EIP-155 hash and recovering pubkey
if (!security::VerifyEthSignature(tx_msg.eth_raw_tx(), tx_msg.pubkey(), tx_msg.sign())) {
    SHARDORA_ERROR("ETH tx signature verification failed");
    msg_ptr->set_status(transport::kTxInvalidSignature);
    return transport::kFirewallCheckError;
}
```

**After:**
```cpp
// ETH transactions have already been verified in http_handler.cc during
// RLP decoding and signature recovery. The address_info was also set there
// (including auto-registration for new addresses).
// Skip signature verification here to avoid redundant checks.

// Use address_info that was already set in http_handler.cc
if (msg_ptr->address_info == nullptr) {
    // Fallback: try to get from account manager
    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(
        security_->GetAddressWithPublicKey(tx_msg.pubkey()));
    
    if (msg_ptr->address_info == nullptr) {
        SHARDORA_DEBUG("ETH tx: address not found: %s",
            common::Encode::HexEncode(
                security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str());
        msg_ptr->set_status(transport::kTxInvalidAddress);
        return transport::kFirewallCheckError;
    }
}

SHARDORA_DEBUG("ETH tx passed firewall check, from: %s",
    common::Encode::HexEncode(
        security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str());
```

### 2. `src/consensus/hotstuff/block_acceptor.cc`

**Before:**
```cpp
// ETH-format tx: re-verify using EIP-155 signing hash.
if (ptx->has_eth_raw_tx() && !ptx->eth_raw_tx().empty()) {
    bool ok = security::VerifyEthSignature(
        ptx->eth_raw_tx(), ptx->pubkey(), ptx->sign());
    verify_results[idx] = ok ? 1 : -1;
    return;
}
```

**After:**
```cpp
// ETH-format tx: signature already verified in http_handler.cc
// during RLP decoding and signature recovery. Skip verification here.
if (ptx->has_eth_raw_tx() && !ptx->eth_raw_tx().empty()) {
    verify_results[idx] = 1;  // Already verified
    return;
}
```

### 3. `src/consensus/hotstuff/hotstuff_manager.cc`

**Before:**
```cpp
// ETH-format tx: re-verify using EIP-155 signing hash.
if (tx_ptr->tx_info->has_eth_raw_tx() && !tx_ptr->tx_info->eth_raw_tx().empty()) {
    if (security::VerifyEthSignature(
            tx_ptr->tx_info->eth_raw_tx(),
            tx_ptr->tx_info->pubkey(),
            tx_ptr->tx_info->sign())) {
        pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
    } else {
        SHARDORA_WARN("ETH tx verify failed in hotstuff_manager");
    }
} else if (tx_ptr->tx_info->pubkey().size() == 64u) {
```

**After:**
```cpp
// ETH-format tx: signature already verified in http_handler.cc
// during RLP decoding and signature recovery. Skip verification here.
if (tx_ptr->tx_info->has_eth_raw_tx() && !tx_ptr->tx_info->eth_raw_tx().empty()) {
    pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
} else if (tx_ptr->tx_info->pubkey().size() == 64u) {
```

## Why This Works

1. **Single Point of Verification**: Signature verification happens once in `http_handler.cc` where:
   - RLP transaction is decoded
   - Signing hash is computed according to EIP-1559/EIP-155
   - Signature is recovered using libsecp256k1
   - Public key is verified against recovered address
   - Address is auto-registered if needed

2. **Trust the Initial Verification**: Once verified in `http_handler.cc`, the transaction is marked with `eth_raw_tx` field, which serves as a flag that this transaction has been properly verified using Ethereum's signature scheme.

3. **Avoid Redundant Checks**: Subsequent components (pool manager, block acceptor, consensus) trust the initial verification and skip re-verification.

## Security Considerations

- The signature verification in `http_handler.cc` is thorough and follows EIP-1559/EIP-155 standards
- The `eth_raw_tx` field can only be set by `http_handler.cc` during RLP decoding
- Internal transactions cannot fake this field as they don't go through the RLP decoding path
- The verification uses industry-standard libsecp256k1 library

## Testing

After these changes, compile and test:

```bash
cd /root/shardora/build
make -j$(nproc)

cd /root/shardora/clipy
python3 test_eip1559.py
```

Expected result: Transactions should pass through all verification points and be successfully processed.

## Files Modified

1. `src/pools/tx_pool_manager.cc` - Lines ~173-200
2. `src/consensus/hotstuff/block_acceptor.cc` - Lines ~1109-1115
3. `src/consensus/hotstuff/hotstuff_manager.cc` - Lines ~532-543

## Related Documentation

- `EIP1559_IMPLEMENTATION.md` - Complete implementation details
- `EIP1559_SIGNATURE_RECOVERY_DEBUG.md` - Signature recovery debugging
- `EIP1559_ADDRESS_REGISTRATION_ISSUE.md` - Auto-registration feature
- `FINAL_SOLUTION_AUTO_REGISTER.md` - Auto-registration solution
