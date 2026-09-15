# Prefund Nonce Initialization Fix

## Problem
Phase 7 (Set User Prefund) was sending 30000 prefund transactions successfully, but verification showed 0/30000 confirmed. The transactions were being rejected on-chain because the nonce values were incorrect.

## Root Cause
The prefund users are created with **random private keys** in Phase 2, and their accounts are created on-chain in Phase 3. However, **their nonces were never fetched and initialized** in the `prikey_with_nonce` map before Phase 7 started.

When Phase 7 tried to use `++prikey_with_nonce[addr]`, the map entries didn't exist, so they defaulted to 0, and all transactions started with nonce=1. But the chain's actual nonce for these accounts was also 0 (newly created), so the first transaction would succeed, but subsequent ones would fail.

Additionally, the nonce map was using **raw address bytes** as keys (from `sec->GetAddress()`), but the initialization code was using **hex-encoded addresses** as keys, causing a mismatch.

## Solution
Added explicit nonce initialization for all confirmed users before Phase 7 starts:

```cpp
// Initialize nonces for all confirmed users before prefund
std::cout << "  Initializing nonces for " << confirmed_users.size() << " users..." << std::endl;
{
    ShardoraSDK nonce_sdk(global_chain_node_ip, global_chain_node_http_port);
    uint32_t nonce_init_threads = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)confirmed_users.size());
    if (nonce_init_threads == 0) nonce_init_threads = 1;
    uint32_t users_per_thread = confirmed_users.size() / nonce_init_threads;
    std::vector<std::thread> nonce_threads;
    std::atomic<uint32_t> nonce_init_ok{0};
    
    for (uint32_t t = 0; t < nonce_init_threads; ++t) {
        uint32_t s = t * users_per_thread;
        uint32_t e = (t == nonce_init_threads - 1) ? (uint32_t)confirmed_users.size() : (s + users_per_thread);
        nonce_threads.emplace_back([&, s, e]() {
            ShardoraSDK local_sdk(global_chain_node_ip, global_chain_node_http_port);
            for (uint32_t i = s; i < e && !global_stop; ++i) {
                uint32_t user_idx = confirmed_users[i];
                std::string addr_hex = users[user_idx].addr_hex;
                int64_t nonce = local_sdk.fetchNonce(addr_hex);
                if (nonce >= 0) {
                    std::string addr_raw = common::Encode::HexDecode(addr_hex);
                    prikey_with_nonce[addr_raw] = nonce;  // Use raw address as key
                    ++nonce_init_ok;
                }
            }
        });
    }
    for (auto& th : nonce_threads) th.join();
    std::cout << "  Nonce initialization: " << nonce_init_ok.load() << "/" << confirmed_users.size() << " users" << std::endl;
}
```

## Key Points
1. **Parallel Fetching**: Uses multiple threads to fetch nonces concurrently (up to `kMaxThreadCount`)
2. **Correct Key Format**: Converts hex address to raw bytes using `HexDecode()` to match the key format used in prefund sending
3. **Verification**: Prints how many users were successfully initialized
4. **Timing**: Runs immediately before prefund ops start, ensuring nonces are fresh

## Expected Result
- All 30000 prefund transactions now use correct, sequential nonces
- Verification should show 30000/30000 confirmed (100% success rate)
- No more "invalid nonce" errors

## Files Modified
- `src/main/tx_cli.cc` (lines 2162-2195): Added nonce initialization before Phase 7
