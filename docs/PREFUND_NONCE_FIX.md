# Prefund Nonce Fix

## Problem
Type 5 (prefund) transactions were failing with invalid nonce errors:
- Error: `res: 3` (invalid nonce)
- Nonce value: `1` for all transactions from the same sender
- Only ~131 out of 30000 prefund operations succeeded

## Root Cause
In the TCP fast path for prefund (Phase 7), the code was fetching nonce from the chain for each sender:

```cpp
int64_t nonce = tsdk.fetchNonce(addr_hex);  // Blocking HTTP call
for (const auto& ca : grp.contract_addrs) {
    auto tx = CreateTransactionWithAttr(sec, ++nonce, ...);  // All get same nonce
}
```

**Problem**: Multiple threads fetch nonce concurrently for the same address:
1. Thread 1 fetches nonce=0, increments to 1, sends tx with nonce=1
2. Thread 2 fetches nonce=0 (same value!), increments to 1, sends tx with nonce=1
3. Thread 3 fetches nonce=0, increments to 1, sends tx with nonce=1
4. Only the first transaction succeeds; the rest fail with "invalid nonce"

## Solution
Use the **local nonce map** (`prikey_with_nonce`) that's already maintained by `UpdateAddressNonceThread()`:

```cpp
for (uint32_t gi = s; gi < e && !global_stop; ++gi) {
    auto& grp = pf_groups[gi];
    std::string prikey_raw = common::Encode::HexDecode(grp.prikey_hex);
    std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
    sec->SetPrivateKey(prikey_raw);
    std::string addr = sec->GetAddress();
    
    // Use local nonce map to ensure continuity across multiple prefund ops
    for (const auto& ca : grp.contract_addrs) {
        if (global_stop) break;
        uint64_t next_nonce = ++prikey_with_nonce[addr];  // Atomic increment
        auto tx = CreateTransactionWithAttr(sec, next_nonce, ...);
        if (tcp_enqueue_default(tx)) ++pf_ok;
        else ++pf_fail;
        usleep(200);
    }
}
```

## Benefits
1. **Nonce Continuity**: Each prefund transaction gets a unique, sequential nonce
2. **No Race Conditions**: Local map is protected by atomic operations
3. **No Blocking Calls**: Eliminates expensive HTTP `fetchNonce()` calls
4. **Consistency**: Uses same nonce management as regular tx sending (line 376)
5. **Expected Result**: 100% prefund success rate (30000/30000 instead of 131/30000)

## Files Modified
- `src/main/tx_cli.cc` (lines 2200-2221): Prefund TCP fast path nonce handling

## Testing
Run Phase 7 (Set User Prefund) and verify:
- All 30000 prefund operations succeed
- No "invalid nonce" errors
- Prefund verification shows 30000/30000 confirmed
