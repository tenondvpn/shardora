# Shardora 区块链不可能三角分析

## Shardora 如何同时实现去中心化、安全性和可扩展性

---

## 摘要

| 项目 | 去中心化 | 安全性 | 可扩展性 | 三角面积 |
|------|:---:|:---:|:---:|:---:|
| **Shardora** | **9** | **9.5** | **10** | **42.9** |
| Polkadot | 6 | 8 | 8 | 27.7 |
| Ethereum 2.0 | 7 | 9 | 6 | 27.5 |
| Solana | 4 | 6 | 10 | 23.3 |
| Bitcoin | 8 | 10 | 2 | 19.6 |

---

## 1. 可扩展性（10/10）

**2D 并行**：N 分片 × 32 池/分片 × ~170 TPS/池。实测：4,500–5,500 TPS（100% 跨分片负载）。

**三种 AMM 场景**（全部自动化，零开发者负担）：

| 场景 | 机制 | 原子性 | 吞吐量 | 测试 |
|------|------|--------|--------|------|
| 1. 共置 | 任意部署者自动定位到同一池 | ✅ 完全（单笔交易） | 单池 | `amm.py` |
| 2. 并行 | 独立池在不同分片 | ✅ 每池完全 | O(N) 线性 | `amm.py --test multi` |
| 3. 跨分片 | 销毁-中继-铸造桥接 | 每步原子 | 串行 | `amm.py --test cross` |

**GBP 消息压缩**：相比直接 QC 验证降低 6,000 倍（O(S²×P) → O(S²) 极小常数）。

**交易同步**：每地址上限（256 笔）+ 每消息上限（768 KB）防止网络瓶颈。

---

## 2. 安全性（9.5/10）

**Fast-HotStuff BFT**：每分片 f < n/3，约 1 秒最终性，两阶段提交。

**BLS 聚合**：O(n²) → O(1) 委员会通信。

**跟随者 Nonce 验证**：`block_acceptor.cc` 中每地址 nonce 连续性检查。任何间隔 → 整个提案被拒绝。

**跨分片安全**：两阶段 Fast-HotStuff 提交 + 高度连续性强制 + 三层重放保护。

**多算法签名**：ECDSA（以太坊）、SM2（中国标准）、OQS/ML-DSA-44（抗量子）。按密钥长度自动检测。

**跨分片合约调用**：GBP 中的 `contract_outputs` 携带 ABI 编码的 calldata，包含执行状态和调用者地址。目标分片通过 EVM 自动执行（`to_tx_local_item.cc`）。

**以太坊 CREATE 地址**：服务端 `GetContractAddress(sender, nonce)` = `keccak256(RLP([sender, nonce]))[-20:]`。ETH JSON-RPC 兼容。`eth_getTransactionReceipt` 返回 `contractAddress`。

**TCP 帧解析修复**：`msg_decoder.cc` 中部分 `PacketHeader` 解析 bug 已修复——防止消息静默丢失。

**扣分说明（−0.5）**：跨分片复合操作是最终一致的，非同步原子。通过销毁-中继-铸造模式和每跳滑点保护缓解。

---

## 3. 去中心化（9/10）

**低门槛准入**：最低质押 8 SHARDORA（8 × 10⁸ coins），无白名单。`start_miner.sh` 即可加入。

### 3.1 Gas 费用模型

| 参数 | 值 | 说明 |
|------|-----|------|
| Gas：普通转账 | 21,000 gas | 与以太坊一致（EIP-2028） |
| Gas：合约创建 | 53,000 gas + calldata | CREATE 操作码基础费用 |
| Gas：合约调用 | 21,000 gas + calldata | EIP-2028 兼容 |
| Gas：SSTORE（新槽） | 20,000 gas/槽 | EIP-2200 兼容 |
| Gas：SSTORE（脏槽） | 2,900 gas/槽 | EIP-2200 兼容 |
| Calldata：非零字节 | 16 gas/字节 | EIP-2028 |
| Calldata：零字节 | 4 gas/字节 | EIP-2028 |
| Gas 价格 | 可配置（默认 1） | 由交易发送者设定 |
| Prefund 模型 | 每合约 gas 预存 | 用户调用合约前存入 gas，未使用部分可退还 |

### 3.2 FTS 委员会选举（`elect_tx_item.cc`）

每个分片周期性运行委员会选举，使用公平代币选择（FTS）加权随机算法，综合平衡质押、信用、地理分散度和任期。

**选举周期**：
1. **统计收集**：每个 epoch 收集每节点指标——`tx_count`（交易数）、`stoke`（质押量）、`gas_sum`（gas 总量）、`credit`（信用分）、`area_point`（地理坐标）、`consensus_gap`（任期）。
2. **淘汰阶段**（`CheckWeedout`）：委员会底部 10%（`kFtsWeedoutDividRate = 10`）被移除。
   - **直接淘汰**：10% 配额的一半——`tx_count < max_tx_count / 2` 的节点直接移除。
   - **FTS 淘汰**：剩余配额通过反向 FTS 选择（低权重节点被淘汰概率更高）。
3. **新节点加入**（`JoinNewNodes2ElectNodes`）：新节点填补空缺。
   - 委员会 < 256 节点（`kFtsMinDoubleNodeCount`）：委员会可翻倍。
   - 委员会 ≥ 256：增长 5%（`kFtsNewElectJoinRate = 5`）。
   - 最大委员会规模：1,024（`kEachShardMaxNodeCount`）。
   - 新节点通过 FTS 从 `join_elect_nodes` 候选池中选出。
4. **领导者选择**（`FtsGetNodes`）：`2^⌊log₂(n/3)⌋` 个领导者通过 FTS 选出（上限 32 = `kImmutablePoolSize`）。

**FTS 权重公式**（`SmoothFtsValue`）：每个节点的综合 FTS 值由四个归一化维度计算，每个维度映射到 [100, 10000]：

| 维度 | 来源 | 归一化方式 | 效果 |
|------|------|-----------|------|
| PoS 权重 | `stoke`（质押量） | 按质押排序，2/3 分位差平滑 + 随机化 | 质押越高 → 选中概率越大 |
| 信用权重 | `credit` 分数 | 线性 min-max 归一化 | 信用越高 → 选中概率越大 |
| 区域权重 | 地理分散度（avg + std_dev×0.5 + median×0.3） | 线性 min-max，除以 `kAreaPenaltyCoefficient` | 地理分散度越好 → 权重越高 |
| 任期权重 | `consensus_gap`（任期） | **反向** min-max（任期越长 → 分数越低） | 新节点优先，防止固化 |

最终 FTS 值 = `pos_weight × credit_weight × area_weight × gap_weight`（乘法复合）。

### 3.3 动态分片奖励系统

**基于 Epoch 的奖励 + 比特币式减半**：

| 参数 | 值 | 来源 |
|------|-----|------|
| Epoch 周期 | 600 秒 | `kTimeBlockCreatePeriodSeconds` |
| 每 epoch 初始奖励 | 10,000 SHARDORA | `kInitialTotalReward` |
| 减半周期 | 210,240 epochs（约 4 年） | `kHalvingPeriodEpochs` |
| 最低区块奖励 | 1 SHARDORA | `kMinBlockReward` |
| 最大减半次数 | 64 | `kMaxHalvingCount` |
| Gas 销毁比例 | 50%（EIP-1559 风格） | `kBurnRatio` |
| 早期奖励加成 | 分片数 < 1024 时 +10% | `kEarlyBonusMultiplier` |
| 交易量奖励 | 最高为分片奖励的 20% | `kTxBonusMultiplier` |

**奖励计算流程**（`CalculateTotalEpochReward`）：
```
epoch_number = (当前时间 - 创世时间戳) / 600
base_reward  = 10000 SHARDORA / 2^(epoch_number / 210240)
early_bonus  = base_reward × 1.1  （若 active_shards < 1024）
shard_reward = early_bonus × (分片权重 / 总权重)
tx_bonus     = shard_reward × min(log₂(max_tx_count+1)/20, 1.0) × 0.2
total_reward = shard_reward + tx_bonus
```

**分代分片权重**：早期分片获得更高比例奖励，激励早期参与：

| 代 | 分片数 | 权重 | 累计 |
|:---:|:---:|:---:|:---:|
| Gen 0 | 3（ID 3–5） | 1.000 | 3 |
| Gen 1 | 5（ID 6–10） | 0.900 | 8 |
| Gen 2 | 8（ID 11–18） | 0.810 | 16 |
| Gen 3 | 16（ID 19–34） | 0.729 | 32 |
| Gen 4 | 32（ID 35–66） | 0.656 | 64 |
| Gen 5 | 64（ID 67–130） | 0.590 | 128 |
| Gen 6 | 128（ID 131–258） | 0.531 | 256 |
| Gen 7 | 256（ID 259–514） | 0.478 | 512 |
| Gen 8 | 512（ID 515–1026） | 0.430 | 1024 |

**每节点奖励分配**（`MiningToken`）：
- epoch 内收集的 gas 费用分配：非根分片将 `gas / network_count` 分配给根分片，剩余分配给验证者。
- 每个验证者获得：`epoch_mining_count × (node_tx_count / max_tx_count) + node_gas_sum`。
- `tx_count = 0` 的节点按 `tx_count = 1` 计算（最低参与奖励）。

### 3.4 质押机制

| 参数 | 值 |
|------|-----|
| 最低质押单位 | 8 SHARDORA（8 × 10⁸ coins） |
| 质押操作 | `STAKE_OP_STAKE` / `STAKE_OP_REDEEM` / `STAKE_OP_NONE` |
| 质押持久化 | 存储在 `prefix_db`，重启后保留 |
| 重新加入行为 | 已有质押自动复用（`STAKE_OP_NONE`） |
| 余额检查 | 要求 `balance >= stake_amount` |

### 3.5 其他去中心化特性

**动态分片**：分片增减无需停止共识。BLS DKG 委员会轮换。

**自动目标部署**：任何用户可通过 `test_contract_chain_demo.py` 模式将合约部署到任何目标池——SDK 自动生成映射到目标分片/池的部署者地址。

**完全以太坊兼容**：Solidity、EVM（evmone）、EIP-155、CREATE/CREATE2、REVERT、ERC20。

**每轮选举审计日志**：每轮选举写入 JSON 日志（`elect_logs/elect_{shard}_{ts}_{height}.json`），包含所有 FTS 参数、节点权重、领导者分配和挖矿奖励，实现完全透明。

**扣分说明（−1.0）**：有界委员会大小（1,024）和确定性分片分配。

---

## 4. Shardora 为何打破不可能三角

| 权衡 | 传统约束 | Shardora 的解决方案 |
|------|---------|---------------|
| D↔Sc | 更多节点 = 更多开销 | BLS 聚合：O(n²) → O(1) |
| Se↔Sc | 全局共识 = 串行 | 合约共置：池内原子，DeFi 无需跨分片 |
| D↔Sc | 更多分片 = 更多流量 | GBP：6,000 倍消息压缩 |

**形式化模型**：吞吐量 = N × 32 × 170（线性）。安全性 = f < n/3（常数）。去中心化 = N × 委员会（增长）。三者均随 N 改善。

---

## 5. 对比分析

| 维度 | Ethereum 2.0 | Polkadot | Solana | **Shardora** |
|------|-------------|----------|--------|----------|
| 分片 | 64 静态 | 中继链 | 单链 | **动态 + 32 池/分片** |
| 最终性 | 约 12 分钟 | 约 60 秒 | 约 0.4 秒 | **约 1 秒** |
| 跨分片 | 仅异步 | XCMP | 不适用 | **GBP 两阶段 + 销毁-中继-铸造** |
| AMM 原子性 | ✅ 完全 | 异步 | ✅ 完全 | **✅ 完全（共置）+ 跨分片桥接** |
| 抗量子 | 否 | 否 | 否 | **是（OQS/ML-DSA-44）** |
| EVM | 完整 | Substrate | 部分 | **完整** |

---

## 6. 量化证据

| 测试 | 结果 |
|------|------|
| `tx_cli.cc` 压力测试（100% 跨分片） | **4,500–5,500 TPS** |
| AMM 单池兑换 | 约 1 秒 |
| AMM 并行池 | 并发已确认 |
| 跨分片 AMM（A→B→B2→C） | 约 3-5 秒 |
| 跨分片合约调用 | output 中继 + EVM 执行 |
| OQS 合约生命周期 | 部署 + 调用 + 自毁 |
| ETH JSON-RPC 部署 | CREATE 地址与以太坊一致 |

---

## 7. 结论

Shardora 通过以下创新打破不可能三角：

1. **2D 并行**（分片 × 池）：O(N) 吞吐量，三种 AMM 场景全部自动化
2. **Fast-HotStuff + BLS**：O(1) 通信，约 1 秒最终性，f < n/3
3. **GBP**：6,000 倍消息压缩，两阶段提交，通过 `contract_outputs` 实现跨分片合约执行
4. **销毁-中继-铸造**：跨分片代币兑换，每跳独立滑点保护，零补偿逻辑
5. **FTS 选举 + 动态奖励**：四维加权选择（质押、信用、地理、任期），比特币式减半 + 分代分片权重，50% gas 销毁（EIP-1559）

结果：D=9，Se=9.5，Sc=10——三角面积 42.9。

---

## 相关文件

| 文件 | 描述 |
|------|------|
| `clipy/amm.py` | 三种 AMM 场景：单池、多池、跨分片 |
| `clipy/test_cross_shard_call.py` | 跨分片合约调用演示 |
| `clipy/test_contract_chain_demo.py` | 自动目标跨用户合约共置 |
| `clipy/shardora3.py` | 20+ 测试用例：ETH 签名、OQS、GMSSL、自毁 |
| `src/consensus/zbft/elect_tx_item.cc` | FTS 委员会选举、淘汰机制、动态分片奖励 |
| `src/consensus/zbft/to_tx_local_item.cc` | 通过 `contract_outputs` 跨分片合约执行 |
| `src/consensus/hotstuff/block_acceptor.cc` | 跟随者 nonce 验证 |
| `src/consensus/consensus_utils.h` | Gas 常量（EIP-2028/2200 兼容） |
| `src/common/utils.h` | 经济模型常量、分片分代表 |
| `src/init/network_init.cc` | 质押逻辑（最低 8 SHARDORA） |
| `src/pools/to_txs_pools.cc` | GBP 实现 |
| `src/security/security_utils.h` | 以太坊 CREATE 地址公式 |
| `SHARDORA_REVIEWER_RESPONSE.md` | 审稿人回应详细文档（英文） |
| `SHARDORA_REVIEWER_RESPONSE_CN.md` | 审稿人回应详细文档（中文） |
