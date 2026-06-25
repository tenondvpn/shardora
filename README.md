# Shardora: A Dynamic Blockchain Sharding System

**Shardora** is a high-performance blockchain sharding system featuring resilient and seamless shard reconfiguration. It optimizes consensus and transaction processing to maintain system stability even during complex shard transitions.

## 🔺 Blockchain Trilemma — Revised Triangle Area Comparison

| Project | Decentralization | Security | Scalability | Triangle Area | Rank |
|---------|:---:|:---:|:---:|:---:|:---:|
| **Shardora** | 9 | 9.5 | 10 | **42.9** | 🥇 1 |
| Polkadot | 6 | 8 | 8 | 27.7 | 🥈 2 |
| Ethereum 2.0 | 7 | 9 | 6 | 27.5 | 🥉 3 |
| Solana | 4 | 6 | 10 | 23.3 | 4 |
| Bitcoin | 8 | 10 | 2 | 19.6 | 5 |

>  [Full Analysis: SHARDORA Blockchain Trilemma Report](SHARDORA_BLOCKCHAIN_TRILEMMA_EN.md)

---

## Quick Start

### 1. Requirements

```
GCC/G++ : 13.0.0 or higher
CMake   : 3.25.1 or higher
```

### 2. Run customized network

```bash
bash build_third.sh
bash simple_remote.sh $each_machine_node_count $ip_list  
# each_machine_node_count like 4, mean each machine create 4 nodes. 
# ip_list like 192.168.0.1,192.168.0.2, mean 2 machine create 2 * 4 nodes shardora network
# machine user must root
# machine password must set, you can change it by edit simple_remote.sh
```

### 3. Run tests

```
cd clipy && python3 shardora3.py
```

```python
# Post-Quantum Attack Resistant
def oqs_sign_test():
    # Base configuration
    IP, PORT = "127.0.0.1", 23001
    # OQS keys (using sample ML-DSA-44 length Hex string here, should actually read from oqs_addrs file)
    # Note: Private key length must be > 128 bits to trigger auto-switch logic in code
    OQS_KEY = "4a6393c16df..."
    OQS_PK  = "4a6393c16df..."
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY_OQS = w3.client.get_oqs_address(OQS_PK)
    test_oqs_transfer(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_contract_deploy_and_call(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_library_with_contract(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_contract_prefund_flow(w3, MY_OQS, OQS_KEY, OQS_PK)
```

> More Resources & Stress Tests: [ShardoraTests](https://github.com/iPoW-Stack/ShardoraTests)

---

## Start Mining

```bash
git clone https://github.com/iPoW-Stack/ShardoraPub.git /root/shardora && cd /root/shardora
bash build_third.sh
bash start_miner.sh <RAW_HEX_PRIVATE_KEY>
```

---

## Related Papers

* **Shardora/Shardora (TNSE 2026)**: [Shardora: Scaling Blockchain Sharding via 2D Parallelism](https://github.com/user-attachments/files/26715054/Shardora_TNSE_revised2nd_pure.pdf)
* **Shardora NMFT (TIFS 2025)**: [NMFT: A Copyrighted Data Trading Protocol based on NFT and AI-powered Merkle Feature Tree](https://ieeexplore.ieee.org/document/11275867/)
* **Shardora SCoRE**: [SCoRE: A Runtime System for Service-Oriented Smart Contracts in Sharded Blockchains](https://sosp26.hotcrp.com/doc/sosp26-paper501.pdf)
* **Shardora BFT**: [Boosting Sharded Blockchain via Multi-Leader Parallel Pipelines](https://github.com/user-attachments/files/24961427/Akaverse.Boosting.Sharded.Blockchain.via.Multi-Leader.Parallel.Pipelines.pdf)
