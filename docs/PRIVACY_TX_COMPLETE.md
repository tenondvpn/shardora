# Shardora 隐私交易完整方案

> 基于 Shardora/Akaverse 分片架构，涵盖：跨分片对隐私的价值分析、业界方案对比、方案设计、形式化安全证明、性能分析

**文档结构**：

```
Part I   — 跨分片对隐私的战略价值
Part II  — 业界方案横向对比（17 个方案）
Part III — 隐私交易方案设计
Part IV  — 形式化安全理论与证明
Part V   — 性能定量分析
Part VI  — 实施路径与总结
```

*基于分支：xl0616 | 涉及模块：shardoravm、consensus/zbft、bls、protos | 版本：2026-09-15*

---

# Part I：跨分片对隐私交易的战略价值

## 1.1 核心命题

隐私交易在单链（Ethereum）上完全可以实现——Tornado Cash、Zcash 已经证明。

但**跨分片架构对隐私交易具有三个结构性价值**，这三个价值是密码学工具在单链上无法复制的，不是性能改进，而是解决了单链结构上不可克服的问题。

---

## 1.2 问题一：Gas 关联攻击——单链的结构性死穴

单链上所有隐私方案都面临同一个无解问题：

```
Tornado Cash 取款流程：
  1. 用户生成新地址（无余额）
  2. 提交 ZK 证明取款
  3. 但谁来付这笔交易的 Gas？

选项 A：从已知地址转 ETH 过来  → 直接破坏匿名性
选项 B：使用 Relayer 中继服务  → 中心化，Relayer 知道 IP + 取款地址关联
选项 C：Flashbots/隐私 mempool → 复杂，且 Relayer 仍能关联
```

这是**结构性问题**，不是工程问题：在单链上，接收方必须先有 Gas 才能接收，而获取 Gas 的行为本身就是一条关联线索。

**跨分片的解决方式**：目标分片的执行由 `SYSTEM_EXECUTOR`（协议内置地址）发起，Gas 成本由发送方在源分片的路由费中支付。接收方可以是**与整个系统从未有过任何交互的全新地址**，零成本收款。这在单链上从原理上不可能——单链没有"协议层系统执行者"这个角色。

---

## 1.3 问题二：观察域分离——密码学做不到的事

单链最根本的隐私局限：**所有节点看到所有交易**。

```
Ethereum 全节点的视角：
  所有 Transfer 事件、所有 ZK proof 提交、所有混币存取款
  ↑ 即使金额加密、地址一次性，节点仍能看到完整的"黑盒出入口时间表"

  Block 1000: 某地址存入混币（可见）
  Block 1050: 某新地址从混币取款（可见）
  → 时序 + 金额组合 = 统计关联
```

这个问题**无法用更好的 ZK 证明解决**——ZK 只能隐藏证明内容，无法隐藏"这个区块里有一笔混币存款"这个事实本身。单链共识要求所有节点看到所有交易，这是共识的基本假设，密码学无法绕过。

**跨分片的解决方式**：

```
分片 S_src 节点（n₁ 个节点）的视角：
  仅见：某地址在 T₁ 时刻创建了一个承诺 cm
  不知道：目标分片、接收方、金额

分片 S_dst 节点（n₂ 个节点，与 n₁ 完全不相交）的视角：
  仅见：某一次性地址在 T₂ 时刻收到了一个 Note
  不知道：源分片、发送方

路由层节点的视角：
  仅见：一个 ECIES 密文 + CSCC 签名从 S_src 流向 S_dst（Note 内容不可见）
```

这是**架构级的观察域分离**，密码学无法在单链上复制。

---

## 1.4 问题三：零增量信任的跨片授权——BFT 签名即授权

跨链隐私需要一个实体授权目标链铸造资产。在 Aztec 是 Sequencer（中心化），在 Keep Network 是独立 MPC 委员会（新的信任假设）。

**跨分片的独特价值**：目标分片的 BFT 委员会已经存在，已经是经济激励对齐的抗拜占庭委员会。本方案中，源分片 BFT 委员会对 ZK proof 验证结果签名，生成 **CSCC（跨分片信用证书）**，目标分片验证 BLS 签名后盲插承诺——授权的信任基础**直接等于现有共识的信任基础**，无需任何额外假设。

```
单链方案的信任结构：
  共识安全          需要 > 1/3 节点诚实
  跨链授权实体      需要独立的 Relayer / Sequencer（新的信任假设！）
  → 两个独立安全假设，攻击面叠加

跨分片方案的信任结构：
  共识安全          需要 > 1/3 节点诚实
  跨片授权（CSCC）  同一批 BFT 委员会 BLS 签名（无新假设！）
  → 单一安全假设，攻击面不增加
```

---

## 1.5 价值量化总结

| 价值维度 | 单链能否实现 | 跨分片的改进 |
|---------|------------|------------|
| Gas 关联攻击免疫 | ❌ 结构上不可能 | ✅ SYSTEM_EXECUTOR 原生解决 |
| 观察域架构级分离 | ❌ 密码学无法替代 | ✅ 不同分片节点物理隔离 |
| 零增量信任跨片授权（CSCC） | ❌ 必须引入独立 Relayer/MPC | ✅ BFT 委员会 BLS 签名直接授权 |
| 跨链关联泄露 | ❌ 桥接必然明文 | ✅ 路由层传 ECIES 密文，内容不可见 |
| 匿名集线性扩展 | ⚠️ 受 L1 TPS 限制 | ✅ 随分片数线性增长 |
| 吞吐量扩展 | ⚠️ 单链瓶颈 | ✅ 32×N 池并行 |

> 如果没有跨分片，可以实现约 80% 的隐私能力；这三个问题在单链上没有令人满意的答案，是**分片架构带来的隐私红利**，而不仅仅是性能红利。

---

# Part II：业界方案横向对比

## 2.1 单链现有方案及其根本局限

### Tornado Cash（混币器模型）

**原理**：向固定面额合约存款，通过 ZK 证明从任意新地址取款。

| 维度 | 能力 |
|------|------|
| 发送方匿名 | ✅ 隐藏（依赖匿名集大小） |
| 接收方隐私 | ✅ 隐藏（取款到新地址） |
| 金额隐藏 | ❌ **必须固定面额** |
| 跨链隐私 | ❌ 无，跨链必须走明文桥 |
| 吞吐量 | ❌ ~2.5 TPS（ETH L1 受限） |
| 协议层级 | 应用层合约 |

**根本局限**：固定面额导致金额信息泄露；时序关联攻击；2022 年被 OFAC 制裁，合约层可被黑名单。

### Tornado Cash Nova

变量面额的升级版，使用 ZK-UTXO 模型部署在 Gnosis Chain，通过 bridge 与以太坊交互。但 bridge 入/出是明文事件，桥接层金额完全暴露，根本问题未解决。

### Zcash Sapling / Orchard

**Sapling**（2018，Groth16）：全隐私 L1，协议原生支持 ZK 证明。  
**Orchard**（2021，Halo2/PLONK）：无 Trusted Setup 升级版。

| 维度 | 能力 |
|------|------|
| 三重隐私（发送/接收/金额） | ✅ 强 |
| 跨链隐私 | ❌ 独立链 |
| EVM 兼容 | ❌ |
| 实际使用率 | ⚠️ 约 20% 使用 shielded 地址 |

**根本局限**：独立链，无法与以太坊生态交互；单链吞吐量无法水平扩展。

### Monero（环签名 + RingCT）

环签名隐藏发送方，Stealth Address 隐藏接收方，Bulletproofs 隐藏金额（RingCT）。

**已知弱点**：环签名中的"0号成员"在某些时序下可被概率推断（Möser et al., 2018）；环大小固定（最大 16），匿名集有硬上限；独立链，无智能合约。

### Aztec v2（UltraPlonk L2）

以太坊 L2，UTXO + ZK Rollup，Noir 语言编写隐私应用。

**根本局限**：存款/取款时在 L1 是明文事件（桥接泄露）；Sequencer 中心化，可审查/MEV。

### Penumbra

Cosmos 生态，通过 IBC 实现跨链。但 IBC packet 内容在中继链上以明文传递，中继者可看到目标链和金额。

### Railgun

部署在 ETH/BSC/Polygon 的隐私合约，无新链。Groth16，UTXO 模型。同样受 L1 吞吐量限制，跨链走明文桥。

### MimbleWimble / Grin

Confidential Transaction + Cut-through（移除已花费 UTXO），无地址。高吞吐，但无智能合约，接收方隐私弱（无地址意味着需要在线协商）。

### Vitalik 的 Privacy Pools（2023）

专门解决合规问题：取款时附加 ZK 证明，证明"存款来自合规集合（Association Set），不来自已知恶意地址"，而不暴露具体是哪笔存款。可与本方案组合（见 Part III 第 3.3 节）。

### Secret Network / Oasis Network（TEE 方案）

使用 Intel SGX 实现隐私，非 ZK 路线。

| 对比点 | TEE（SGX）方案 | ZK 证明方案（本方案） |
|--------|--------------|-------------------|
| 隐私依赖 | Intel 硬件信任 | 密码学困难假设 |
| 历史漏洞 | SGAxe、Plundervolt、Foreshadow | 无硬件漏洞风险 |
| 吞吐量 | 高（接近原生执行） | 受证明生成速度限制 |
| 安全假设 | 工程信任 + 密码学 | 纯密码学 |

### Aleo

ZK 原生 L1，所有交易均为 ZK 证明，使用 Leo 语言，Marlin/AHP 证明系统（通用 SRS）。

### Namada

多资产屏蔽池（MASP），IBC 兼容，支持 Cosmos 生态跨链。PLONK/Sapling，通用 SRS。

### Firo / Lelantus Spark

One-sided payment，Sigma 协议 + Bulletproofs，无 Trusted Setup，灵活匿名集。

---

## 2.2 单链隐私的结构性困境三角

```
              吞吐量
             /      \
            /        \
    隐私强度 ─────── 去中心化

三者只能取其二：
- Tornado Cash：牺牲吞吐量（定额限制）换隐私 + 去中心化
- Aztec L2：   牺牲去中心化（Sequencer）换吞吐量 + 隐私
- ZK Rollup：  牺牲隐私（链上数据可用性）换吞吐量 + 去中心化
```

**根本原因**：单链是全局有序的状态机，任何人都能看到完整的资金流图。隐私层只能是"在透明账本上建造黑盒"，黑盒的出入口（存款/取款/桥接）永远是透明的。

---

## 2.3 17 个方案完整对比表

| # | 方案 | 发送方匿名 | 接收方隐私 | 金额隐藏 | 跨链隐私 | 智能合约 | 吞吐量 | 证明系统 | Trusted Setup | 量子安全 |
|---|------|-----------|-----------|---------|---------|---------|-------|---------|--------------|---------|
| 1 | Tornado Cash | ✅ | ✅ | ❌ 定额 | ❌ | ✅ EVM | 2.5 TPS | Groth16 | 电路专属 | ❌ |
| 2 | Tornado Cash Nova | ✅ | ✅ | ✅ | ⚠️ 桥泄露 | ✅ EVM | ~5 TPS | Groth16 | 电路专属 | ❌ |
| 3 | Zcash Sapling | ✅ | ✅ | ✅ | ❌ | ❌ | 5-10 TPS | Groth16 | MPC 仪式 | ❌ |
| 4 | Zcash Orchard | ✅ | ✅ | ✅ | ❌ | ❌ | 10-20 TPS | Halo2 | ✅ 无 | ❌ |
| 5 | Monero | ✅ 中 | ✅ | ✅ | ❌ | ❌ | 30-50 TPS | Bulletproofs | ✅ 无 | ❌ |
| 6 | Aztec v2 | ✅ | ✅ | ✅ | ⚠️ 桥泄露 | ⚠️ Noir | ~100 TPS | PLONK | ✅ 通用 | ❌ |
| 7 | Penumbra | ✅ | ✅ | ✅ | ✅ IBC | ⚠️ 受限 | ~1000 TPS | Groth16 | 仪式 | ❌ |
| 8 | Iron Fish | ✅ | ✅ | ✅ | ❌ | 计划中 | ~20 TPS | Groth16 | MPC 仪式 | ❌ |
| 9 | Aleo | ✅ | ✅ | ✅ | ❌ | ✅ Leo | ~100 TPS | Marlin | ✅ 通用 | ❌ |
| 10 | Railgun | ✅ | ✅ | ✅ | ⚠️ 桥泄露 | ✅ EVM | ~10 TPS | Groth16 | 仪式 | ❌ |
| 11 | Grin/MimbleWimble | ✅ | ❌ 无地址 | ✅ | ❌ | ❌ | ~1000 TPS | Bulletproofs | ✅ 无 | ❌ |
| 12 | Privacy Pools | ✅ | ✅ | ❌ 定额 | ❌ | ✅ EVM | ~2.5 TPS | Groth16 | 仪式 | ❌ |
| 13 | Secret Network | ✅ | ✅ | ✅ | ✅ IBC | ✅ CosmWasm | ~100 TPS | 无 ZK | 无 | ❌ |
| 14 | Oasis Network | ✅ | ✅ | ✅ | ⚠️ | ✅ EVM | ~1000 TPS | TEE | 无 | ❌ |
| 15 | Firo/Lelantus Spark | ✅ | ✅ | ✅ | ❌ | ❌ | ~10 TPS | Sigma/BP | ✅ 无 | ❌ |
| 16 | Namada | ✅ | ✅ | ✅ | ✅ IBC | ❌ | ~100 TPS | PLONK | ✅ 通用 | ❌ |
| 17 | **Shardora 分片隐私** | ✅ | ✅ | ✅ | ✅ **原生** | ✅ EVM | **38,400 TPS** | Groth16 | 电路专属 | ❌ |

---

## 2.4 证明系统技术对比

| 证明系统 | Proof 大小 | 证明时间 | 验证时间 | Trusted Setup | 递归 |
|---------|----------|---------|---------|--------------|------|
| Groth16 | **~256 B（最小）** | 1-3 s | **O(1)配对（最快）** | 电路专属 | ❌ |
| PLONK | ~800 B | 1-5 s | O(log n) | 通用 SRS | ✅ |
| Halo2 | ~1-2 KB | 2-5 s | O(log n) | ✅ 无 | ✅ |
| Bulletproofs | O(log n) KB | O(n) | O(n) | ✅ 无 | ❌ |
| STARKs | O(log² n) KB | O(n log n) | O(log² n) | ✅ 无 | ✅ |

**本方案选 Groth16 的理由**：Proof 体积最小（直接决定单块 Tx 容量），链上验证 O(1) 且成本恒定，BN254 曲线已是系统基础设施（BLS、DKG 均基于此）。**升级路径**：若 Trusted Setup 成为障碍，可替换为 PLONK（通用 SRS，Proof 增至 ~800B，TPS 从 1,265 降至 ~1,100/block，可接受）。

---

## 2.5 Penumbra vs 本方案的跨链隐私对比

Penumbra 通过 IBC 实现跨链隐私，但 IBC packet 内容在中继链上以**明文传递**（包括金额和目标地址），中继者可以看到。

本方案路由层携带 ECIES 密文（仅接收方可解）+ CSCC（BFT 签名授权），路由节点不获得任何 Note 明文——**路由全程不暴露金额和接收方，无明文中继点**。目标分片委员会也不解密 Note 内容，仅验证 CSCC 签名后盲插承诺。

代价：本方案仅在 Shardora 自有分片间工作，不与外部 IBC 链互操作。

---

## 2.6 方案定位图

```
                    单链
                      │
            弱跨链隐私 │ 强本链隐私
                      │
  Aztec(L2) ─────────┤─────────── Zcash / Zcash Orchard
  Tornado ────────────┤            Monero / Firo
  Railgun ────────────┤            Aleo
                      │
  ──────────────────────────────────────────────────
  多链/分片
                      │
  (此区域空白)         │
                      │
  ← Shardora 分片隐私填补此空白 →
  跨分片原生隐私 + EVM 兼容 + 高吞吐 + 无新信任假设
```

---

# Part III：隐私交易方案设计

## 3.1 现有架构隐私缺陷（精确定位）

当前跨分片转账在以下层面完全暴露信息：

| 暴露点 | 具体内容 | 代码位置 |
|--------|---------|---------|
| `CrossTransferOut` 事件 | `from`、`to`、`amount` 均明文索引 | `shardora_host.cc emit_log()` |
| `ToTxMessageItem` protobuf | `base_root_address`、目标地址、`amount256`、目标分片/池 | `pools.proto`，`block.proto` |
| Feistel 影子地址 | 给定 `base_addr + (shard, pool)` 任何人可推导 `shadow_addr` | `reversible_feistel_address.h` |
| 跨片 nonce | 序列计数器泄露转账时序和频率 | `contract_call.cc` |
| Shadow 合约余额 | `totalSupply` 和 `_balances` 链上可读 | 目标分片 EVM 状态 |

**根本问题**：`CrossTransferOut(base, from, to, amount, nonce, toShard, toPool)` 将完整资金流图写入链上日志，任何观察者都能还原完整转账关系图。

---

## 3.2 隐私目标

| 目标 | 定义 |
|------|------|
| **发送方匿名性** | 观察者无法确定哪个地址发起了转账（匿名集 = 混合池所有存款者） |
| **接收方隐私** | 链上不出现真实接收地址 |
| **金额保密** | 转账金额对第三方不可见 |
| **跨分片关联性切断** | 无法将源分片的转出与目标分片的转入关联 |
| **防双花** | 在不暴露身份的情况下保证每笔隐私余额只能花一次 |

---

## 3.3 核心密码学原语

### 复用现有基础设施

系统已有 **alt_bn128（BN254）曲线** + **DKG 阈值密钥**，可直接复用：

| 现有能力 | 复用方式 |
|---------|---------|
| `libff::alt_bn128_G1/G2` 群运算 | Pedersen 承诺、ECIES ECDH 共享点计算 |
| `libff::alt_bn128_GT` 配对 | Groth16 proof 在**源分片** EVM 内验证 |
| BLS 聚合签名（`BlsDkg::Sign/Aggregate`） | 源分片委员会生成 CSCC 签名（直接复用，零修改） |
| BLS 验签（`Crypto::VerifyBls`） | 目标分片验证 CSCC 签名（直接复用，零修改） |
| `common_pk`（G2 聚合公钥） | 目标分片验证 CSCC 的验证密钥（现有字段，无需新增） |

> **关键洞察**：CSCC 的签发与验证直接复用现有的 BLS 聚合签名/验签基础设施——源分片 BFT 委员会对每个出块已经签名，CSCC 只是在现有 BLS 签名中附加跨分片授权内容。目标分片验 CSCC 等价于验一条特殊的 BFT 消息，**零新增配对运算**，完全在现有安全假设范围内。

### 新增密码学原语

| 原语 | 用途 | 曲线 |
|------|------|------|
| **Pedersen 承诺** | 隐藏金额：`C = r·G + v·H` | alt_bn128 G1 |
| **Bulletproofs 范围证明** | 证明 `v ∈ [0, 2^64)` 而不揭露 `v` | alt_bn128 G1 |
| **一次性地址（Stealth Address）** | 隐藏接收方真实身份 | alt_bn128 G1 |
| **Nullifier（作废符）** | 防双花，类 Zcash 方案 | Poseidon Hash |
| **Groth16 零知识证明** | 证明整体转账合法性，在**源分片**验证 | alt_bn128（支持配对） |
| **ECIES（椭圆曲线集成加密）** | Note 明文加密给**接收方** view_pk，委员会完全不参与解密 | alt_bn128 G1 + AES-GCM |
| **跨分片信用证书（CSCC）** | 源分片 BFT 委员会签名，授权目标分片铸造对应承诺；替代委员会解密方案 | BLS on alt_bn128 G2 |

### Privacy Pools 合规扩展（可选）

基于 Vitalik 2023 年的 Privacy Pools 方案，在电路末尾追加约束 C11：

```
C11: Note 的来源不在监管黑名单 Merkle 树中
     （合规监管方发布黑名单 Merkle 树，用户证明自己不在其中即可合规取款）
```

实现与 Privacy Pools 等价的合规能力，同时支持跨分片。

---

## 3.4 整体架构

> **架构核心原则**：目标分片委员会**永远不解密** Note 内容（amount, randomness, spend_pk）。委员会仅验证源分片 BFT 签名授权，然后盲目插入承诺。这与 Zcash 全节点的工作方式完全一致——全节点验证 ZK 证明、更新 Merkle 树，但从不知道 Note 内容。

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              用户层                                       │
│  发送方持有：spending_key (Fr)，view_sk (Fr)                              │
│  接收方公布：spend_pk = spend_sk·G (G1)，view_pk = view_sk·G (G1)        │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ① 生成：ZK Proof（源分片验证）
                                │         ECIES 密文（接收方 view_pk 加密）
                                │         stealth_addr（路由用，一次性）
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                   源分片：PrivacyShadow 合约                              │
│  ▸ 验证 Groth16 ZK proof（EVM 内，3 次 BN254 配对，~100ms）               │
│  ▸ 检查并记录 old_nullifier（防双花）                                     │
│  ▸ 将 new_cm_change（找零承诺）插入源分片 Merkle 树                       │
│  ▸ 源分片 BFT 委员会对 (new_cm_send, target_shard, pool, block_hash)     │
│    签名，生成 CSCC（跨分片信用证书，BLS 签名 ~96 B）                       │
│  事件：ShieldedCrossTransferOut(old_nullifier, new_cm_send, ecies_ct)    │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ② ToTxMessageItem 携带：
                                │   new_cm_send（32B）+ ecies_ct（~112B）
                                │   + cscc_signature（96B）—— 无明文金额/地址
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                    全局缓冲池路由层（逻辑不变）                             │
│  路由目标 shard/pool 由 CSCC 中嵌入的 target_shard/pool 字段决定          │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ③ 路由至目标分片
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│               目标分片：CSCC 验证 + 盲插承诺 + 存储密文                   │
│  ▸ 验证 CSCC 中的 BLS 签名（源分片 BFT 公钥，~1-2ms，无配对）             │
│  ▸ 验证 CSCC 中 target_shard/pool 与本分片匹配                           │
│  ▸ 将 new_cm_send 插入目标分片承诺 Merkle 树（盲插，不知道 amount）        │
│  ▸ 将 ecies_ct 存储链上（供接收方离线扫描）                               │
│  ▸ 委员会节点【不知道】amount、randomness、spend_pk 中的任何一项           │
│  事件：ShieldedCrossTransferIn(new_cm_send, new_cm_tree_root)            │
└──────────────────────────────────────────────────────────────────────────┘
                                │ ④ 接收方离线扫链
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  接收方：用 view_sk 逐条尝试解密链上 ecies_ct，匹配 Note；用 spend_sk 花费 │
└──────────────────────────────────────────────────────────────────────────┘
```

**与原设计的关键区别**：

| 维度 | 原错误设计 | 修正后设计 |
|------|-----------|-----------|
| Note 内容加密 | ElGamal（委员会集体解密） | ECIES（接收方 view_pk，仅接收方能解密） |
| 目标分片执行授权 | 委员会解密后知道 amount/stealth_addr | CSCC（BLS 签名），委员会不知道 Note 内容 |
| 目标分片配对数 | 3600 次（1200 tx × 3 pairing）≈ 3.6-7.2s | 0 次配对（只验 BLS 签名） |
| 与 Zcash 模型一致性 | ❌ 全节点知道金额（违反 Zcash 设计） | ✅ 全节点不知道金额（与 Zcash 一致） |

---

## 3.5 隐私余额模型（替代 `_balances`）

将 Shadow 合约的 `mapping(address => uint256) _balances` 替换为 **UTXO-like 承诺模型**：

```
Note = (amount: uint64, randomness: Fr, spend_pk: G1)
Commitment = amount·H + randomness·G    # G, H 是 alt_bn128 G1 的独立生成元
```

**Shadow 合约新增状态**：

| 状态 | 类型 | 说明 |
|------|------|------|
| `cm_tree_root` | bytes32 | 承诺 Sparse Merkle 树根 |
| `notes[bytes32]` | mapping(bytes32→bool) | 已存在的承诺集合 |
| `nullifiers[bytes32]` | mapping(bytes32→bool) | 已花费的 Note 作废符集合 |
| `vk` | struct | Groth16 验证密钥（部署时写入） |

**Nullifier 定义**（防双花）：
```
nullifier = Hash(spending_key ∥ commitment)
```
花费时公开 `nullifier`，合约拒绝重复 nullifier。观察者看到 nullifier 但无法反推是哪个 commitment（单向哈希）。

---

## 3.6 一次性地址（Stealth Address）

接收方发布 `(spend_pk, view_pk)`，均为 alt_bn128 G1 点。

**发送方为每笔转账生成一次性地址**：
```
r           ← random Fr
epk         = r · G                      # 临时公钥，写入加密载荷
shared      = r · view_pk               # ECDH 共享点
stealth_addr = Hash(shared) · G + spend_pk
```

**接收方扫链**：
```
对链上每笔交易的 epk：
  shared'  = view_sk · epk              # = r · view_pk
  candidate = Hash(shared') · G + spend_pk
  if candidate == stealth_addr → 此 Note 属于自己
```

**花费**（需要 spend_sk）：
```
spend_key_for_note = Hash(shared) + spend_sk
nullifier = Hash(spend_key_for_note ∥ commitment)
```

> **设计自洽性说明——Pure Commitment Ledger**
>
> Section 3.6 描述的 `stealth_addr` 是 **Note 创建时的数学推导过程**，而非链上存储字段。Shardora 采用与 Zcash Sapling / Orchard 完全一致的"零地址沉淀设计"：
>
> - **链上仅存**：`new_cm`（Pedersen 承诺，~32B）和 `ecies_ct`（ECIES 密文，~144B）——**链上零地址痕迹**
> - **stealth_addr 的归宿**：`stealth_addr` 即为该次转账的一次性 `spend_pk`（见公式：`stealth_addr = Hash(shared)·G + spend_pk_recipient`），作为 Note 内部字段封装于 `ecies_ct` 密文（`note_pt.spend_pk = stealth_addr`），外部不可见
> - **接收方识别**：对每条链上 `(new_cm, ecies_ct)` 记录，用 `view_sk · epk` 恢复 ECDH 共享密钥，解密 `ecies_ct`，检验 `note_pt.spend_pk == my_spend_pk`（即验证 `stealth_addr` 是否由自己的密钥派生）——等价于 Section 3.6 的 `candidate == stealth_addr` 比对，无需在链上存储地址
> - **安全含义**：链上可见数据仅为（承诺哈希, ECIES 密文），对外部观察者呈现为均匀随机串，无法从承诺集合推断接收方身份（定理 B 信息论不可链接性的直接来源）

---

## 3.7 ZK 证明电路（Groth16 on alt_bn128）

**公开输入（写入链上）**：

```
old_nullifier     # 花费的 Note 的作废符
old_cm_root       # 花费时的承诺树根
new_cm_send       # 转往目标分片的新承诺
new_cm_change     # 找零承诺（留在源分片）
value_binding     # Pedersen 承诺 v·H（绑定范围证明）
```

**私有输入（不上链）**：

```
old_note, old_cm_path, spending_key, epk,
new_note_send, new_note_change
```

**电路约束**：

| 约束 | 内容 |
|------|------|
| C1 | `old_cm = Pedersen(old_note)` |
| C2 | `old_cm` 在 Merkle 树 `old_cm_root` 路径中 |
| C3 | `old_nullifier = Hash(spending_key ∥ old_cm)` |
| C4 | `spending_key · G = old_note.spend_pk`（知道花费私钥） |
| C5 | `amount_send + amount_change = old_note.amount`（守恒） |
| C6-C7 | `amount_send ≥ 0`，`amount_change ≥ 0`（范围约束） |
| C8 | `new_cm_send = Pedersen(new_note_send)` |
| C9 | `new_cm_change = Pedersen(new_note_change)` |
| C10 | `value_binding = amount_send · H`（绑定范围证明） |

**约束规模**（Poseidon hash，~250 约束/次）：

| 模块 | R1CS 约束数 |
|------|------------|
| Pedersen 承诺（×3） | 750 |
| Merkle 路径（深度 20）× Poseidon | 10,000 |
| EC 标量乘法（256 步） | 4,000 |
| Nullifier hash | 500 |
| 守恒约束 | 10 |
| 范围约束（64-bit 二进制分解，×2） | 4,000 |
| 输出承诺（×2）+ 绑定 | 1,750 |
| **合计** | **~21,010** |

---

## 3.8 跨分片传输载荷：ECIES + CSCC

### ECIES：Note 内容加密（仅接收方可解）

```
发送方（客户端）：
  r         ← random Fr
  epk       = r · G                            # 临时公钥（64B，链上可见）
  shared    = r · view_pk                      # ECDH 共享点（不上链）
  key       = HKDF-SHA256(shared ∥ "shardora_note_v1")  # 256-bit AES key
  note_pt   = encode(amount, randomness, spend_pk)       # ~64 B 明文
  ct        = AES-256-GCM(key, note_pt, aad=new_cm)     # ~80 B 密文
  ecies_ct  = epk ∥ ct                         # ~144 B，链上存储

接收方（离线扫链）：
  for each (new_cm, ecies_ct) on target shard:
    epk, ct = split(ecies_ct)
    shared' = view_sk · epk
    key'    = HKDF-SHA256(shared' ∥ "shardora_note_v1")
    try: note_pt = AES-256-GCM-decrypt(key', ct, aad=new_cm)
    if ok and note_pt.spend_pk == my_spend_pk:
      此 Note 属于我，记录 (amount, randomness) 备用
```

**关键**：委员会节点在任何步骤中都**不持有**和**不需要** `view_sk`，永远无法解密 `ecies_ct`。Note 内容对委员会是完全不透明的黑盒。

### CSCC：跨分片信用证书（授权目标分片铸造）

源分片 BFT 委员会将对应出块中的隐私跨分片请求打包成 CSCC：

```
CSCC = {
  new_cm_send    : bytes32,   // 将在目标分片铸造的承诺（不含金额）
  target_shard   : uint32,
  pool_index     : uint32,
  src_block_hash : bytes32,   // 源分片确认该 ZK proof 的区块哈希
  bls_signature  : bytes96,   // 源分片 BFT 委员会 BLS 聚合签名
}
```

CSCC 中**不含** amount、stealth_addr 或任何 Note 明文。目标分片只凭 BLS 签名验证源分片 BFT 已确认了合法的 ZK proof。这与现有 CrossTransfer 消息的授权模式完全一致（源分片 BFT 共识 → 路由 → 目标分片执行），无需引入任何新机制。

### ToTxMessageItem 新增字段（精简后）

```protobuf
message ToTxMessageItem {
  // 原有路由字段（保留）
  optional uint32 sharding_id   = 1;
  optional uint32 pool_index    = 2;
  // ...现有字段...

  // 隐私转账新增（无明文 amount/address）
  optional bytes  new_commitment  = 21;   // 32 B
  optional bytes  ecies_ct        = 22;   // ~144 B（epk + AES-GCM 密文）
  optional bytes  cscc_signature  = 23;   // 96 B（BLS 聚合签名）
  optional bytes  src_block_hash  = 24;   // 32 B
  optional bool   is_shielded     = 26;
}
// ZK proof 不过路由层——仅在源分片 EVM 内验证，不放入 cross-shard 消息
```

消息总大小：**~320 B**（vs 原设计 ~840 B，减少 62%）

---

## 3.9 目标分片：CSCC 验证与盲插承诺

目标分片的执行路径极度简化——**零配对运算**：

```
to_tx_local_item.cc：ShieldedCreditFromCSCC(item)

  // Step 1: 验证 CSCC BLS 签名（~1-2 ms，无配对）
  src_committee_pk = elect_info.GetCommitteePk(item.src_shard_id)
  assert BLS.Verify(src_committee_pk, item.cscc_signature,
                    Hash(item.new_commitment ∥ item.target_shard ∥ ...))

  // Step 2: 检查 new_cm 未被重复铸造
  assert !target_cm_tree.Contains(item.new_commitment)

  // Step 3: 插入承诺 Merkle 树（盲插，不知道 Note 内容）
  target_cm_tree.Insert(item.new_commitment)

  // Step 4: 将 ecies_ct 持久化（供接收方扫链）
  ecies_store[item.new_commitment] = item.ecies_ct

  // Step 5: 发出事件
  emit ShieldedCrossTransferIn(item.new_commitment, target_cm_tree.Root())
  // 事件中无 amount、无 stealth_addr，委员会对这两个值一无所知
```

**与原设计对比**：

| 项目 | 原错误设计 | 修正后设计 |
|------|-----------|-----------|
| 目标分片知道 amount？ | ✅ 是（重建 M 后知道） | ❌ 否（永远不知道） |
| 目标分片知道 stealth_addr？ | ✅ 是 | ❌ 否 |
| 目标分片配对运算数 | 3600 次/块 ≈ 7s | **0 次**（仅 BLS 验签） |
| 信任假设新增 | DKG 共用密钥（新假设） | 复用现有 BFT 签名（零新假设） |
| 与 Zcash 模型一致 | ❌ | ✅ |

### 3.9.1 跨分片原子性、故障恢复与隐私无损保证

> **审稿人常见质疑**："若源分片已出块消耗旧 Note（nullifier 上链、找零已生成），但路由层断连或目标分片持久分区，资金会丢失吗？若引入回滚，回滚会不会暴露发送方？"

本方案通过以下设计**彻底规避 2PC（两阶段提交）的复杂性与隐私风险**：

#### （1）源分片终局性（Source-Side Finality）

隐私跨分片交易在源分片 BFT 最终确认块（Finalized Block）中完成以下操作，且这三步在**同一个原子块**内执行：

```
原子块 B_src（一次 BFT 最终确认，不可回滚）：
  ① 旧 Note 作废：old_nullifier → nullifier_set（不可逆）
  ② 找零 Note 铸造：new_cm_change → source_cm_tree（不可逆）
  ③ CSCC 签发：BFT 委员会 BLS 聚合签名 → CSCC（不可逆授权凭据）
```

**一旦 B_src 被 BFT 最终确认，用户资产状态已在密码学和协议层面不可逆变更**。旧 Note 已死，CSCC 已发。此时不存在"源分片需要等待目标分片回应才能提交"的依赖关系，因此**根本无需引入 2PC**。

#### （2）CSCC 幂等执行（Idempotent Execution at Target）

CSCC 是**自包含的合规铸造凭证**——目标分片处理 CSCC 的 `ShieldedCreditFromCSCC()` 天然幂等：

```
// Step 2（防重入校验）：
assert !target_cm_tree.Contains(item.new_commitment)
```

若 CSCC 因网络原因被重复投递，第二次执行在 Step 2 处返回 no-op。目标分片从任何分区中恢复后，收到合法 CSCC 即可**确定性、无条件**地完成盲插——与目标分片的历史宕机时长无关，无需任何来自源分片的二次交互。

#### （3）CSCC 持久化重传（At-Least-Once Delivery）

CSCC 消息遵循 Shardora 现有跨分片消息的**至少一次可靠投递**机制（等同于当前 `kConsensusLocalTos` CrossTransfer 消息的路由策略）：源分片将待投递 CSCC 持久化于本地消息队列，直到收到目标分片的入块确认（ACK）。路由层临时断连或目标分片暂时分区不会丢失 CSCC，仅引入额外延迟。

```
故障场景分析：

情形 1（路由层临时断连）：
  CSCC 在源分片消息队列中等待 → 网络恢复后自动重传
  → 用户资金安全，延迟增加，隐私不受影响

情形 2（目标分片持久分区）：
  CSCC 持久化于路由层 → 目标分片恢复后幂等执行
  → 用户资金安全，延迟增加，隐私不受影响

情形 3（极端情况：目标分片永久下线）：
  等价于现有普通跨分片转账的对应故障场景
  → 超出本协议范围，属 Shardora 基础层容灾策略
```

#### （4）不回滚即不泄露（No Rollback = Zero Privacy Leak）

这是本设计**最关键的隐私优势**：

| 机制 | 时序关联风险 | 地址暴露风险 |
|------|------------|------------|
| **2PC 有回滚** | 高——超时回滚的时刻可与存款关联 | 高——回滚退款目标地址即发送方地址 |
| **CSCC 无回滚** | **零**——不存在回滚事件 | **零**——不存在退款地址 |

在 2PC 方案中，若目标分片超时，源分片需向**原始账户**退款——此退款事件在链上可见，直接暴露"nullifier 对应的发送方地址"，彻底破坏发送方匿名性。

**CSCC 方案的源分片终局性设计从根本上消除了此攻击面**：无超时、无回滚、无退款地址、无时序关联事件。

---

## 3.10 匿名集增强：固定面额混合池

面额集合：`{1, 10, 100, 1000, 10000}` token

```
存款：明文余额 → 碎成固定面额 → 生成 Note commitment → deposit(cm)
      合约插入 Merkle 树，扣除明文余额

取款：生成 ZK 证明（知道树中 Note 的 spending_key）
      → withdraw(nullifier, zk_proof, recipient)
      → 合约向 recipient 铸造同面额明文余额
```

跨分片使用：混合池跨分片发送（池到池，固定面额，无法区分用户）→ 目标池取款。

---

## 3.11 完整隐私流程时序图

```
用户 A（发送方）                  链上/路由层                    用户 B（接收方）
    │                                │                                │
    │ 1. 生成 B 的一次性 stealth_addr │                                │
    │    （ECDH: r←rand, epk=r·G,    │                                │
    │     shared=r·view_pk_B）        │                                │
    │                                │                                │
    │ 2. 构造 Note_send（B 收）       │                                │
    │    + Note_change（A 找零）      │                                │
    │                                │                                │
    │ 3. 生成 Groth16 proof          │                                │
    │    公开输入：nullifier,         │                                │
    │    old_cm_root, new_cm_send,   │                                │
    │    new_cm_change               │                                │
    │                                │                                │
    │ 4. ECIES 加密 Note_send        │                                │
    │    ecies_ct = epk ∥ AES(key,  │                                │
    │              Note_plaintext)   │                                │
    │    （仅 B 可解密）              │                                │
    │                                │                                │
    │ 5. 调用 PrivacyShadow.spend(   │                                │
    │    nullifier, new_cm_send,     │                                │
    │    new_cm_change, proof,       │                                │
    │    ecies_ct, target_shard)     │                                │
    │                                │                                │
    │         ◄── 源分片 EVM 验证 ──►│                                │
    │         Groth16 verify(proof)  │                                │
    │         （3 次 BN254 配对）    │                                │
    │         check nullifier ∉ spent│                                │
    │         insert new_cm_change   │                                │
    │         into src Merkle tree   │                                │
    │                                │                                │
    │──────────────────────────────►│                                │
    │             源分片 BFT 共识    │                                │
    │             生成 CSCC 签名：   │                                │
    │             BLS.Sign(src_bft_sk│                                │
    │             , new_cm_send ∥   │                                │
    │             target_shard ∥    │                                │
    │             block_hash)        │                                │
    │                                │                                │
    │   ShieldedCrossTransferOut(    │                                │
    │   nullifier, new_cm_send,      │                                │
    │   ecies_ct)  [不含金额/地址]   │                                │
    │                                │                                │
    │          路由层转发             │                                │
    │    ToTxMessageItem 携带：      │                                │
    │    new_cm_send + ecies_ct      │                                │
    │    + cscc_signature            │                                │
    │    [无明文，无配对运算]         │                                │
    │                                │                                │
    │          目标分片处理           │                                │
    │    BLS.Verify(src_committee_pk │                                │
    │              , cscc_signature) │                                │
    │    （~1-2 ms，零配对）         │                                │
    │    insert new_cm_send into     │                                │
    │    dst Merkle tree（盲插）      │                                │
    │    store ecies_ct on-chain     │                                │
    │    [委员会不知道 amount/addr]   │                                │
    │                                │                                │
    │                   ShieldedCrossTransferIn(new_cm_send)          │
    │                                │◄────────────── B 离线扫链 ─── │
    │                                │  ecies_ct ──ECIES decrypt──►  │
    │                                │  key = HKDF(view_sk · epk)    │
    │                                │  Note_plaintext = AES-dec(key) │
    │                                │  → 获知 amount, randomness     │
    │                                │  → 用 spend_sk 花费 Note       │
```

---

## 3.12 与现有代码的集成点

| 现有文件 | 改动性质 | 说明 |
|---------|---------|------|
| `src/shardoravm/shardora_host.cc` `emit_log()` | 新增 case | 拦截 `ShieldedCrossTransferOut`，构造 `kShieldedTransfer` pending action |
| `src/shardoravm/host_journal_stack.h` `CrossShardPendingAction` | 扩展 | 新增 `kShieldedTransfer` 枚举值，字段替换为 `(nullifier, new_cm, encrypted_payload)` |
| `src/consensus/zbft/contract_call.cc` ~L431 | 修改 | shielded action 转 `ToTxMessageItem` 时写密文字段 |
| `src/protos/pools.proto` `ToTxMessageItem` | 添加字段 | `new_commitment`, `ecies_ct`, `cscc_signature`, `src_block_hash`, `is_shielded`（无 ElGamal 字段） |
| `src/consensus/zbft/to_tx_local_item.cc` | 主要改动 | 新增 `ShieldedCreditFromCSCC()`：验证 CSCC BLS 签名 → 盲插 new_cm → 存 ecies_ct |
| `src/consensus/zbft/contract_call.cc` | 新增 | 源分片出块后，对 shielded pending action 生成 CSCC 并附入 ToTxMessageItem |
| `src/consensus/hotstuff/elect_info.h` `ElectItem` | **无需修改** | `common_pk`（G2）已存在，直接用于 CSCC 验签，无需新增 G1 字段 |
| Shadow 合约 Solidity | 新合约 | `PrivacyCrossShardBase`：承诺 Merkle 树 + Nullifier 集合 + Groth16 verifier |

---

## 3.13 与现有交易模式的兼容性

### 3.13.1 用户发起的 EVM 交易层：完全兼容

隐私交易在用户侧依然是一笔普通 EVM 交易，调用 `PrivacyShadow` 合约：

```
普通转账：   from=Alice, to=Bob,           amount=100, data=""
隐私转账：   from=Alice, to=PrivacyShadow, amount=0,   data=spend(nullifier, cm, proof, ...)
```

网络视角看两者都是合法合约调用，交易格式完全相同，区别仅在合约逻辑，不在交易结构。

### 3.13.2 `ToTxMessageItem` protobuf 层：向后兼容

Protobuf optional 字段天然向后兼容。新增的隐私字段默认不存在，对旧节点透明：

```protobuf
message ToTxMessageItem {
  // 现有字段（保留不动）
  optional uint32 sharding_id  = 1;
  optional uint32 pool_index   = 2;
  optional bytes  from         = 3;
  optional bytes  to           = 4;
  optional bytes  amount       = 5;
  // ...（其余现有字段）

  // 新增字段（默认缺失 = 普通交易，旧节点安全忽略）
  // ZK proof 留在源分片 EVM，不放入跨分片消息
  optional bool   is_shielded     = 26;
  optional bytes  new_commitment  = 21;   // 32 B
  optional bytes  ecies_ct        = 22;   // ~144 B（epk + AES-GCM 密文）
  optional bytes  cscc_signature  = 23;   // 96 B（BLS 聚合签名）
  optional bytes  src_block_hash  = 24;   // 32 B
}
```

- **旧节点**收到含隐私字段的消息：忽略未知字段，正常处理
- **新节点**收到不含隐私字段的旧消息：`is_shielded` 缺省为 false，走原有路径

### 3.13.3 各层兼容性汇总

| 层级 | 兼容性 | 说明 |
|------|--------|------|
| 用户发起交易（EVM tx 格式） | ✅ 完全兼容 | 都是合约调用，格式相同 |
| Proto 消息格式 | ✅ 向后兼容 | Optional 字段，旧节点安全忽略 |
| 跨分片路由 | ✅ 完全兼容 | 路由只依赖 `sharding_id` + `pool_index`，不感知 `is_shielded` |
| 源分片共识/打包 | ✅ 兼容 | `CrossShardPendingAction` 新增枚举值，不影响旧类型 |
| 目标分片执行（`to_tx_local_item.cc`） | ✅ 兼容（新增 if 分支） | 普通交易走 else，代码路径一行不改 |
| 合约状态模型 | ✅ 并存 | `_balances`（普通）与承诺 Merkle 树（隐私）在不同合约内独立存在 |

目标分片执行层仅新增一个 `if` 分支，现有普通交易路径**零修改**：

```cpp
// to_tx_local_item.cc
if (item.is_shielded()) {
    // 新路径：验证 CSCC BLS 签名 → 盲插承诺 → 存 ECIES 密文
    ShieldedCreditFromCSCC(item);
} else {
    // 原有路径：完全不变
    NormalCrossTransferCredit(item);
}
```

### 3.13.4 混合节点升级期的唯一注意事项

网络升级期间若同时存在旧节点和新节点，旧节点收到 `is_shielded=true` 的消息时会因 `to`/`amount` 字段缺失而执行失败。

**解决方案**：隐私交易作为特性开关，在分片委员会**全部升级后**统一启用——在 `ElectItem` 中新增 `privacy_enabled` 标志位，节点未升级则拒绝包含隐私 tx 的 Proposal，与 EIP 硬分叉激活机制一致。

---

# Part IV：形式化安全理论与证明

## 4.1 符号约定与困难假设

```
λ           安全参数（典型 128 bit）
negl(λ)     可忽略函数
G₁, G₂     BN254 椭圆曲线群
e: G₁×G₂→G_T  双线性配对
g, h        G₁ 的两个独立生成元（离散对数关系未知）
Fr          BN254 标量域（|Fr| ≈ 2^254）
H           Poseidon 哈希（zk-friendly）
```

**假设 1（DL）**：`Pr[A(G₁, g, a·g) = a] ≤ negl(λ)`

**假设 2（CDH）**：`Pr[A(g, a·g, b·g) = ab·g] ≤ negl(λ)`

**假设 3（DDH）**：`(g, a·g, b·g, ab·g) ≈_c (g, a·g, b·g, c·g)`

**假设 4（q-SDH）**：给定 `(g, τ·g, ..., τ^q·g)`，`Pr[A → (c, 1/(τ+c)·g)] ≤ negl(λ)`（Groth16 可靠性依赖此假设）

**假设 5（ROM）**：Poseidon 哈希建模为随机预言机

---

## 4.2 隐私属性形式化定义

**定义 1（Pedersen 承诺安全性）**：

- **完美隐藏**：`C = v·h + r·g`，均匀随机 r 使得 C 在 G₁ 上均匀分布（信息论安全）
- **计算绑定**：若 `v≠v'` 且 `v·h+r·g = v'·h+r'·g`，则 `logg(h) = (r'-r)/(v-v')`，违反 DL 假设

**定义 2（ZK 证明系统三性质）**：

- **完备性**：`∀(x,w)∈R: Pr[V(x,P(x,w))=1] = 1`
- **可靠性**：`∀x∉L_R: Pr[V(x,P*(x))=1] ≤ negl(λ)`（依赖 q-SDH）
- **零知识性**：`∃` PPT 模拟器 Sim，`{P(x,w)} ≈_c {Sim(x)}`

**定义 3（隐私交易方案四大属性）**：

| 属性 | 形式化表达 |
|------|-----------|
| 发送方 k-匿名 | `Pr[A(Transcript) → "实际发送者"] ≤ 1/k + negl(λ)` |
| 接收方不可追踪 | `Pr[A(stealth_addr, spend_pk, view_pk) → "确认接收方"] ≤ negl(λ)` |
| 金额保密 | `|Pr[A(C)=v] - 1/|Fr|| ≤ negl(λ)` |
| 防双花 | `Pr[A → (π₁,nul,π₂,nul) : π₁≠π₂ ∧ V(π₁)=V(π₂)=1] ≤ negl(λ)` |

---

## 4.3 定理 1：ECIES IND-CCA2 安全性 与 CSCC EUF-CMA 抗伪造性

本方案的跨分片载荷由两部分构成：Note 内容的 ECIES 加密，以及授权铸造的 CSCC 签名。对应两个独立的安全性质。

### 定理 1a：ECIES IND-CCA2 安全性

**定理 1a**：若 CDH 在 G₁ 上成立，AES-256-GCM 是 IND-CPA 对称加密方案，HKDF 建模为随机预言机，则 ECIES 方案满足 IND-CCA2（选择密文攻击不可区分性）：

```
Adv^{IND-CCA2}_{ECIES, A} ≤ Adv^{CDH}_{G₁} + Adv^{IND-CPA}_{AES-GCM} + negl(λ)
```

**证明梗概**：标准 ECIES 安全性归约（Bellare-Rogaway 1993 范式）。CDH 假设保证 ECDH 共享密钥 `k = r·view_pk` 不可区分于随机；HKDF（ROM）将 ECDH 输出扩展为均匀 AES 密钥；AES-GCM 的 IND-CPA 安全性保证密文不区分。CCA2 安全性额外依赖 GCM 的认证标签（防止密文篡改后解密）。 □

**与 Note 隐私的关联**：对任意不知道 `view_sk` 的敌手，`ecies_ct` 与随机串计算不可区分，即敌手无法从链上存储的 `ecies_ct` 中获取任何关于 amount、randomness 或 spend_pk 的信息。

### 定理 1b：CSCC BLS 签名 EUF-CMA 抗伪造性

**定理 1b**：若 BLS 签名方案满足 EUF-CMA（存在不可伪造性），则任意 PPT 敌手在不控制源分片 BFT 委员会私钥的情况下，伪造合法 CSCC 的概率 ≤ negl(λ)。

```
Adv^{EUF-CMA}_{BLS, A} ≤ negl(λ)
```

（BLS 在 co-CDH 假设下满足 EUF-CMA，Boneh-Lynn-Shacham 2001）

**与 CSCC 安全的关联**：敌手无法伪造一个声称由合法源分片 BFT 委员会签署的 CSCC，从而无法向目标分片注入未经合法 ZK proof 验证的虚假承诺。

---

## 4.4 定理 2：共识安全与隐私安全的假设完全对齐

**定理 2**：破坏跨分片隐私（读取 Note 内容或伪造 CSCC）与破坏 BFT 共识需要**完全相同的攻击能力**。

**证明**：

- （Note 内容隐私→共识）若敌手破坏 Note 内容隐私（即解密 ecies_ct），则其知道 `view_sk`（ECIES 密钥，定理 1a 逆否），而 `view_sk` 仅为接收方持有，不与共识相关——注意这与共识无关，是**接收方私钥的保密性**，不依赖委员会。

- （CSCC 完整性→共识）若敌手伪造合法 CSCC（定理 1b 的逆否），则其获得了源分片 BFT 委员会私钥的知识；私钥由 DKG 生成，腐化至少 t = ⌈2n/3⌉ 个节点方可获得；t ≥ ⌈2n/3⌉ 等价于破坏 BFT safety。

两个方向总结：Note 内容隐私由接收方私钥保证（与共识无关），CSCC 完整性由 BFT 共识安全保证（等价于共识）。**隐私系统的攻击面不超过（接收方私钥安全 ∪ BFT 共识安全）**，后者即系统现有安全基础。 □

---

## 4.5 定理 3：端到端跨分片隐私（主定理）

**定理 3**：在 DDH + DL + q-SDH + ROM 假设下，对任意 PPT 外部观察者 O 和腐化至多 t-1 个节点的联合敌手 A，本方案同时满足：发送方 k-匿名、接收方不可追踪、金额保密、跨分片不可关联。

**证明梗概（混合论证）**：

构造混合实验序列 `H₀ → H₁ → H₂ → H₃ → H₄`：

| 混合步骤 | 操作 | 不可区分性依据 | 优势差 |
|---------|------|-------------|--------|
| H₀ → H₁ | `ecies_ct` 替换为均匀随机串 | 定理 1a（ECIES IND-CCA2） | `≤ Adv^{CDH} + negl(λ)` |
| H₁ → H₂ | π 替换为 Sim(x) 模拟证明 | Groth16 零知识性（定义 2） | `≤ negl(λ)` |
| H₂ → H₃ | `cm_out` 替换为随机 G₁ 点 | Pedersen 完美隐藏性（定义 1） | 0（完美不可区分） |
| H₃ → H₄ | `nul` 替换为随机哈希 | Nullifier 不可预测性（ROM + DL） | `≤ negl(λ)` |

在 H₄ 中，`View_O = (nul, cm_out, ecies_ct, π)` 全部为独立均匀随机值，不含任何隐私信息。

总优势 ≤ `Adv^{CDH} + negl(λ)`（ECIES IND-CCA2 规约）。 □

---

## 4.6 攻击向量分析

### 时序关联攻击

若用户立即取款，时序差 `Δt ≈ 27s` 与跨分片路由时间高度相关。

**缓解**：引入指数分布延迟 `Δ_random ← Exp(μ)`，期望 μ = 300s（可配置）。关联概率降至 `≤ 1/k + ε(μ)`，ε(μ) → 0 随 μ 增大。代价：E2E 延迟从 27s 增至 ~327s。

### Nullifier 碰撞攻击

需要 `H(sk₁ ∥ cm₁) = H(sk₂ ∥ cm₂)`，即哈希碰撞，在 ROM 下概率 ≤ 1/2^256，不可行。

### 跨分片前跑攻击

恶意 Leader 看到跨分片消息（`new_cm, ecies_ct, cscc_signature`）后，尝试在盲插前抢先构造竞争交易。分析：Leader 无法解密 `ecies_ct`（需接收方 view_sk），不知道金额和接收方地址；`cscc_signature` 绑定 `new_cm_send` 和 `target_pool_index`，不可重放到其他池。HotStuff liveness 保证合法 tx 最终被包含。

### 委员会内部人攻击

CSCC 架构下，目标分片委员会**永远无法**获得 Note 内容（amount, stealth_addr, randomness），因为这些信息仅由接收方 view_sk 加密保护（ECIES），与委员会完全隔离。攻击者即使控制整个目标分片委员会，也只能拒绝服务（破坏 liveness），不能窃取隐私。源分片委员会伪造 CSCC 需破坏 BFT safety（≥ 2/3 腐化），与攻击共识等价（定理 2），是系统安全底线。

---

## 4.7 复杂度分析

```
操作                              复杂度              具体估计（|C|=21,010）
────────────────────────────────────────────────────────────────────────────
Groth16 证明生成（MSM）          O(|C|log|C|)        ~300,000 G₁ 乘法
Groth16 验证（批量）             O(1)                (2+k) 次配对，k 笔共享
ECIES 加密（发送方，客户端）     O(1)                1 次 ECDH + AES-256-GCM
ECIES 解密（接收方，客户端）     O(1)                1 次 ECDH + AES-256-GCM
CSCC 生成（源分片 BLS 聚合）     O(n)                n≈100 次 G₁ 乘法（~2 ms/block）
CSCC 验签（目标分片）            O(1)                1 次配对（~1-2 ms）
Merkle 路径更新（插入 Note）      O(d)                d=20 次 hash
Nullifier 查找                   O(1)                哈希表查找
────────────────────────────────────────────────────────────────────────────
```

---

## 4.8 局限性

**量子计算威胁**：BN254 的离散对数问题可被 Shor 算法解决，这是全行业共同问题。后量子迁移路径：Groth16 → 格基 SNARK（Latticefold），ECIES/ECDH → CRYSTALS-Kyber（ML-KEM，NIST PQC 标准），BLS 签名 → Dilithium/Falcon，Pedersen → 哈希基承诺。当前尚无成熟方案在合理 proof 大小内运行，属行业整体挑战。

**Trusted Setup**：Groth16 CRS 若被毒化，攻击者可伪造证明（破坏防双花）或无声铸币。缓解：采用大规模 MPC 仪式（100+ 参与者），或替换 PLONK（无专属仪式，代价 proof 增大 3×）。

**Merkle 树深度限制**：深度 d=20 支持最多 2^20 ≈ 100 万个并发 Note/pool。超过后需树深度扩展（增加约束数）或引入可更新 Accumulator。

---

## 4.9 分片架构专有强化定理

本节提取分片架构独有的数学结构，得出比 4.1-4.8 节更强或在密码学类型上完全不同的定理。这些结果是跨分片隐私方案的核心理论贡献，在单链方案中均不成立。

---

### 定理 A：标准账户状态机下无 Gas 隐私不可能性（Impossibility Result）

#### 形式化计算模型（Ideal/Real 框架）

**定义（Gas-First 账户状态机，GASM）**：GASM 是一个状态转换系统 `(S, TX, →)`，其中：
- 状态 S 包含账户余额映射 `bal: Addr → ℕ`
- 交易 tx ∈ TX 有效当且仅当 `bal[tx.sender] ≥ tx.gas_cost`（Gas 先决条件）
- 状态转换 `s →^{tx} s'` 首先扣除 Gas，再执行 tx 逻辑

**注**：以太坊 EVM（含 EIP-1559）、当前 ERC-4337 UserOperation（不含 Paymaster）均满足 GASM 定义。

**三个属性的形式化定义**：

设 Π 是在 GASM 上运行的协议，D 是敌手，B 是接收新地址。

```
SenderPrivacy(Π, D):
  Pr[D(Transcript_chain) → "tx_i 的发送者是 A"] ≤ 1/k + negl(λ)
  （k = 匿名集大小）

GasAutonomy(Π, B):
  B 在首次接收隐私 tx 前，∀t < T_receive: bal[B][t] = 0
  且无任何 approve/allowance 记录

Decentralized(Π):
  ∄ 单一实体 R 使得 R 知晓 (sender_i, receiver_i) 的完整映射关系
  （形式化：R 的视图 View_R 与 (sender, receiver) 对在统计距离上 ≥ ε）
```

**定理 A（不可能性，GASM 下）**：在 GASM 模型中，不存在协议 Π 同时满足 SenderPrivacy(Π, D) ∧ GasAutonomy(Π, B) ∧ Decentralized(Π)。

**证明**：

设 Π 满足 GasAutonomy(Π, B)，则 B 在首次接收前余额为 0。

由 GASM 的 Gas 先决条件：B 不能主动发起任何交易触发自己的隐私余额。必须存在外部实体 R 发起令 B 可接收的交易。

令 `TX_credit` 为向 B 发送隐私信用的交易。在 GASM 中：
- `TX_credit.sender` ≠ B（B 余额为 0，无法发起）
- `TX_credit` 必须包含足以令目标状态机识别接收方的信息（否则无法更新 B 的状态）

**情形 1**：R 是单一实体（中心化 Paymaster/Relayer）。

R 构造 `TX_credit`，知晓 B 的接收地址（否则无法构造有效交易）。任何触发此交易的链下请求（来自发送方 A）均经过 R。R 视图 `View_R ⊇ {(A 的标识, B 的地址)}`，违反 Decentralized(Π)。

**情形 2**：R 是去中心化网络（P2P Relay 网络，含 ERC-4337 Bundler 网络）。

P2P Relay 网络中，至少有 1 个节点处理 `TX_credit` 的构造或广播。该节点视图包含：UserOperation 中的 `callData`（含接收方信息），以及发起 UserOperation 的 IP/身份。即使采用零知识证明隐藏 callData，构造 UserOperation 的发送方必须向某个 Bundler 节点暴露明文意图（否则 Bundler 无法构造合法 UserOperation）。设 Bundler 网络有 m 个节点，只需其中 1 个被动监听，即可以 ≥ 1/m 的概率关联（m 在实践中很小），违反 Decentralized(Π)。

**情形 3**：无 R，接收方 B 自行触发。

B 余额为 0 → 违反 GASM Gas 先决条件 → B 不能发起任何交易 → 违反 GasAutonomy(Π, B)。

三情形穷举，矛盾。□

#### ERC-4337 Paymaster 反例的处理

**反驳**：ERC-4337 引入 Paymaster 合约，第三方可代付 Gas，且 Paymaster 可通过 ZK 证明接收来自隐私池的 Gas 偿还而不暴露用户信息。

**回应**：

定理 A 在标准 GASM 模型下严格成立（不含 Paymaster 扩展）。

ERC-4337 Paymaster 的情形属于情形 1/2 的变体：Paymaster 本身是已知链上实体（必须预先在 `EntryPoint` 注册存入 ETH），其代付行为在链上是公开的（`postOp` 调用可见）。形成以下可观测链：

```
链上可见：UserOperation 被某个 Paymaster 代付 Gas
  → 该 Paymaster 的历史行为（从哪些地址收取 Gas 偿还）可追溯
  → 即使 ZK 证明隐藏了单次偿还来源，Paymaster 作为聚合中介形成关联
```

更精确地，ERC-4337 Paymaster 方案在以下扩展假设下可部分缓解 Gas 关联：
- Paymaster 本身是完全不可追踪的（即匿名集 = 所有使用该 Paymaster 的用户）
- Paymaster 的 Gas 偿还通过隐私池进行（形成循环依赖）

但此方案将问题转移而非消除：Paymaster 自身变成了新的隐私瓶颈，且 Paymaster 必须预存 ETH 在链上（产生新的关联源）。

**结论**：定理 A 在 GASM 模型下是严格定理；ERC-4337 是工程上的部分缓解，不是对定理的反驳，因为 Paymaster 本身不满足 Decentralized(Π)（它是链上可识别的聚合者）。

**跨分片的规避方式**：目标分片委员会通过 SYSTEM_EXECUTOR 执行信用，不属于 GASM——因为协议层执行者不是"余额为 0 的新地址发起交易"，而是"协议内置系统账户代为执行"，根本不触发 Gas 先决条件。

---

### 定理 B：目标分片的信息论源不可链接性

#### 适用模型与假设（必须明确）

**定理 B 在以下理想化模型下成立**：

```
假设 B1（理想同步信道）：
  所有跨分片消息以固定大小、固定延迟传输
  （无包大小泄露、无排队延迟差异、无流量元数据）

假设 B2（无全局被动观察者，GPA）：
  敌手控制 Shard_dst 全部节点，但无法同时监控
  Shard_src 与 Shard_dst 之间的网络流量

假设 B3（匿名集非空）：
  目标分片匿名集 K ≥ 2
```

**在不满足 B1/B2 的真实网络中**（存在 GPA 或流量特征），定理 B 退化为计算安全界，需通过以下工程措施恢复：批处理（batching）、指数延迟调度（exponential delay）、固定包大小填充（padding）。见 4.6 节攻击分析。

**定理 B（理想模型下）**：在假设 B1-B3 下，对控制目标分片 Shard_dst 全部节点、计算能力**无界**的敌手 A，将其观测到的 Note（new_cm）与 Shard_src 中任意具体 nullifier 正确关联的概率，精确等于 1/K。

即：`Pr[A(View_dst) → "new_cm 来自 nullifier_i"] = 1/K`（信息论安全，与密码学假设无关）

**证明**：

考察 Shard_dst 节点的完整视图：

```
View_dst = { new_cm, ShieldedCrossTransferIn 事件, ZK proof π }
```

逐项分析：

（new_cm 的独立性）`new_cm = v_new·H + r_new·G + f(spend_pk_new)`，其中 `r_new ←$ Fr` 独立均匀随机。Pedersen 承诺完美隐藏（见定义 1），故 new_cm 的分布与 old_cm 独立，不含任何关于 old_cm 的信息。

（nullifier 的不可见性）`nullifier = H(spending_key ∥ old_cm)` 仅在 Shard_src 出现，从未传递至 Shard_dst。View_dst 中完全不含 nullifier。

（ZK proof 的零知识性）π 由 Sim(x) 与真实 P(x, w) 计算不可区分（定义 2 零知识性），其中 w 包含 old_cm 和 Merkle 路径。View_dst 中的 π 不含关于 w（即 old_cm 来源）的任何信息。

综合：A 的视图 View_dst 与 {nullifier_i} 的任何关联在信息论上为零。A 的最优策略等价于在 K 个 nullifier 中均匀猜测，概率精确为 1/K。□

**与计算安全的对比**：

| 对手类型 | Shard_dst 上的关联概率 | 依赖假设 |
|---------|-------------------|---------|
| 计算有界（当前定理 3） | ≤ 1/K + negl(λ) | DDH + ROM |
| 计算无界（定理 B） | = 1/K（精确） | **无**（信息论） |

**意义**：单链方案（Tornado Cash、Zcash）中，所有事件在同一链上均可见，时序关联使实际隐私远低于 1/K。本定理证明跨分片架构在目标侧实现了**最优不可关联性**（信息论下界）。

---

### 定理 C：匿名集乘法复合性

#### 适用模型

**定理 C 在假设 B1-B2（理想信道 + 无 GPA）下成立。**

在真实网络中，GPA 可通过流量分析将乘法界退化至加法界。缓解措施：固定路由跳数、批处理、延迟随机化。

**定理 C**：在 DDH 假设 + 假设 B1-B2 下，通过 M 个分片跳转（各分片池规模分别为 K₁, K₂, ..., K_M）的隐私路由，敌手正确关联来源与最终目的 Note 的概率满足：

```
Pr[A 正确关联] ≤ ∏ᵢ₌₁ᴹ (1/Kᵢ) + M · negl(λ)
              = 1/(K₁·K₂·...·K_M) + M · negl(λ)
```

**对比单链串联混币**（设信任中间节点）：

```
单链串联混币（加法）：Pr ≥ 1/(K₁ + K₂ + ... + K_M)
跨分片路由（乘法）：  Pr ≤ 1/(K₁ · K₂ · ... · K_M)
```

乘法界比加法界强 Θ(K^{M-1}) 倍（K 为平均池大小）。

**证明梗概**：

对 M 跳路由，构造混合序列 G₀, G₁, ..., G_M：

在 Gⱼ 中，前 j 跳的 ECIES 密文替换为均匀随机串（ECIES IND-CCA2 不可区分性，每步优势 ≤ Adv^{CDH} + negl(λ)）。

在 G_M 中，所有密文均随机，A 的视图与路由路径完全独立。对 M 个分片独立猜测，各跳猜中概率 ≤ 1/Kᵢ，联合概率 ≤ ∏(1/Kᵢ)（独立事件）。

混合序列总损失 ≤ M·negl(λ)，得结论。□

**实践含义**：

```
K = 1,000（每分片池大小），M 跳：

M=1：Pr ≤ 1/1,000
M=2：Pr ≤ 1/1,000,000      （单链串联：1/2,000，差 500 倍）
M=3：Pr ≤ 1/10⁹             （单链串联：1/3,000，差 333,333 倍）
```

**与混币网络（Mix-net）理论的联系**：Chaum 1981 Mix-net 具有相同的乘法匿名集性质，但要求信任混合节点。本定理证明跨分片系统在 BFT 安全假设下实现了等价的乘法匿名集，且信任基础等价于共识安全（定理 D），无需引入独立可信节点。

---

### 定理 D：双轨安全定理（Dual-Track Security Guarantee）

**动机**：ECIES + CSCC 架构实现了密码学层面的**完全解耦**——"Note 机密性"仅依赖接收方私钥安全（CDH 困难性），与 BFT 共识状态**完全无关**；"账本完整性"依赖 BFT 共识安全与 BLS 签名不可伪造性。这比旧版混合界（`Adv^{privacy} ≤ Adv^{BFT} + 2·Adv^{DDH}`）更精确，揭示了在 ElGamal 阈值方案中不存在的强保证。

**定理 D**（双轨安全，ECIES+CSCC 架构）：对任意 PPT 敌手 A，设 λ 为安全参数、n 为委员会规模、t = ⌈2n/3⌉ 为诚实门限：

**轨道 1 — Note 机密性（与共识完全独立）**：

```
Adv^{confidentiality}_A(λ) ≤ Adv^{CDH}_{G₁}(λ) + negl(λ)
```

归约链：破坏 Note 机密性 ⟹ 区分 ECIES 密文 ⟹ 求解 G₁ 上的 CDH 问题。**此链条与委员会人数 n、t 完全无关**。

**轨道 2 — 账本完整性（CSCC 防伪，价值守恒）**：

```
Adv^{integrity}_A(λ, n, t) ≤ Adv^{BFT-safety}_A(λ, n, t) + Adv^{EUF-CMA}_{BLS}(λ)
```

归约链：伪造合法 CSCC ⟹ 伪造源分片 BFT 委员会 BLS 签名（破坏 EUF-CMA），或控制 ≥ ⌈2n/3⌉ 节点直接签名（破坏 BFT safety）。

**强推论（BFT 全面崩溃下的隐私保护）**：

即使 BFT 共识层完全被攻陷（`Adv^{BFT-safety} = 1`，即所有 n 个节点均为拜占庭），**轨道 1 依然成立**：任意接收方的 Note 内容（amount, randomness, spend_pk）对外部观察者仍以 CDH 困难性为保护界。攻击者掌握全部共识权力，可拒绝包含隐私交易（破坏 liveness）或强行插入无效 CSCC；但**无法解密任何 ecies_ct**（等价于求解 CDH，与共识无关）。

> 对比旧架构（ElGamal 阈值解密）：t 个节点串通 → 重建 sk_master → 直接解密所有历史 Note。新架构彻底消除此攻击面。

**证明梗概**：

*（轨道 1 规约）* 设 B 是 CDH 挑战求解者，输入随机点对 `(aG, bG) ∈ G₁²`，嵌入 `aG` 作为接收方 `view_pk`，运行 A。若 A 以 ε 优势区分 ecies_ct，B 以 ECIES IND-CCA2 规约（Abdalla-Bellare-Rogaway ROM 框架）构造 CDH 解 `abG`，矛盾。故 ε ≤ Adv^{CDH} + negl(λ)。

*（轨道 2 规约）* 若 A 以 ε' 伪造合法 CSCC（即通过 BLS 验签的签名），则在 BFT safety 成立时（A 无法控制 ≥ ⌈2n/3⌉ 节点），B' 以 ε' 优势破坏 BLS EUF-CMA；否则 A 已破坏 BFT safety，代价为 Adv^{BFT-safety}。两路合并得轨道 2 界。□

**与旧定理 D（ElGamal 架构）的对比**：

| 维度 | 旧定理 D（ElGamal 阈值） | 新定理 D（ECIES + CSCC） |
|------|---------------------|------------------------|
| 机密性归约目标 | DDH，且需 BFT safety（阈值解密依赖委员会） | CDH only，**与共识彻底解耦** |
| t 节点串通后果 | 重建 sk_master → 解密全部历史 Note | 仅可拒绝服务，**无法解密任何 Note** |
| 完整性归约目标 | BFT safety（隐含） | BFT safety + BLS EUF-CMA（显式精确） |
| 公式形式 | 单一混合界 | 两条独立轨道，各司其职 |
| BFT 崩溃后隐私 | **完全丧失** | **CDH 级别保护持续成立** |

---

### 定理 E：隐私-活性相容性

**定理 E**：在 BFT Liveness（超过 2/3 的委员会节点诚实）成立的条件下，任意合法隐私交易在 O(κ·Δ) 时间内以压倒性概率被确认，其中 κ = n/(n-t) 为轮次加速因子，Δ 为网络延迟上界。

**证明**：

合法隐私交易 tx（ZK proof 有效，nullifier 未使用）在源分片提交后，由 BFT Liveness 保证在有限轮次内被打包进块（HotStuff Liveness 标准结果）。

源分片 BFT Liveness 保证 ZK proof 验证 + CSCC 签名生成在 O(κ·Δ) 内完成；跨分片消息路由时间有界（BFT Liveness of routing layer）；目标分片收到 CSCC 后，`ShieldedCreditFromCSCC()` 执行纯本地 BLS 验签 + 盲插操作，零额外跨分片交互，确定性完成。故 tx 端到端在 O(κ·Δ) 内确认。□

**与单链方案对比**：

| 方案 | Liveness 保证 | 审查抵抗 |
|------|-------------|---------|
| Tornado Cash | ❌ Relayer 可拒绝服务（永久卡死） | 单点 |
| Aztec | ⚠️ L1 强制包含（高延迟、高成本） | 弱 |
| **跨分片隐私** | ✅ BFT Liveness 直接保证 | 等价于共识审查抵抗 |

---

### 新定理体系总览

| 定理 | 安全类型 | 强度 | 单链是否成立 |
|------|---------|------|------------|
| 定理 1-3（原有） | 计算安全 | negl(λ) 优势界 | 部分成立 |
| 定理 4-6（Shield） | 计算安全 | negl(λ) + 1/K 界 | 部分成立 |
| **定理 A（不可能性）** | 不可能性定理 | 绝对（无假设） | ✅ 即为单链的负结果 |
| **定理 B（信息论不可链接）** | **信息论安全** | 精确 1/K | ❌ 单链无法达到 |
| **定理 C（乘法匿名集）** | 计算安全 | 1/∏Kᵢ 乘法界 | ❌ 单链只有加法界 |
| **定理 D（紧归约）** | 计算安全，精确参数 | 量化安全损失 | ❌ 无 BFT 委员会 |
| **定理 E（活性相容）** | 系统性质 | BFT Liveness 直接推出 | ❌ 单链有 Relayer 单点 |

---

# Part V：性能定量分析

## 5.1 系统基础参数

```
kRotationPeriod                = 600 s
kTimeBlockCreatePeriodSeconds  = 570 s
kLeaderRotationPeriodSeconds   = 10 s    （HotStuff 出块周期）
kMaxTxCount                    = 20,480  （每块最大交易数）
kMaxProposeMsgBytes            = 1 MB    （Propose 消息上限）
kBlockMaxGasLimit              = 6×10^16 （实际无 gas 上限约束）
kImmutablePoolSize             = 32      （每分片并行池数）
t = ⌈2n/3⌉                             （BFT 阈值，n=100 → t=67）
```

---

## 5.2 E2E 延迟拆解

**普通跨分片转账基准**：

```
源分片 HotStuff 共识出块     ~10 s
跨分片消息路由（GBP 投递）   ~5  s
目标分片 HotStuff 共识出块   ~10 s
──────────────────────────────────
总计                         ~25 s
```

**隐私转账额外开销**：

| 阶段 | 时间 | 是否在关键路径 |
|------|------|-------------|
| 客户端 Groth16 证明生成（PC/Rust） | 1-2 s | **否**，与源分片等待（10s）并行 |
| 客户端证明生成（GPU/代理） | ~200 ms | **否** |
| 源分片 CSCC 生成（BLS 聚合，随 HotStuff 投票）| ~2 ms | **否**，藏入现有 HotStuff 投票流程 |
| 目标分片 CSCC 验签 + 盲插 | ~1-2 ms | 目标分片出块内完成 |

**E2E 延迟对比**：

```
普通跨分片    ~25 s
隐私跨分片    ~26-27 s    （增量 < 10%）
```

关键原因：CSCC 生成复用现有 BLS 聚合签名流程，**不增加共识轮次**；目标分片 `ShieldedCreditFromCSCC()` 在单个出块周期内完成。

---

## 5.3 吞吐量分析

### 5.3.1 配对运算瓶颈精确分析（关键修正）

**原设计的致命问题（已修正）**：若沿用"委员会阈值解密 + 目标分片验证 ZK proof"的错误架构，目标分片每块需执行：

```
1,200 txs/block × 3 次 BN254 配对/tx = 3,600 次配对
BN254 配对耗时：1-2 ms（软件，现代服务器）
合计：3.6 - 7.2 s  ← 几乎耗尽 10 s 出块周期，尚未含 BFT 投票与网络通信
```

这是**顶会必杀的数字错误**，在 CCS/USENIX 会场直接被拒。

**修正后架构（CSCC 方案）的配对分布**：

| 阶段 | 配对运算 | 耗时 | 说明 |
|------|---------|------|------|
| 源分片（用户提交 tx） | 3 次/tx | ~3-6 ms/tx | 用户 tx 进入 EVM 时验证，分摊到出块窗口内 |
| 路由层 | 0 次 | 0 | 仅转发 CSCC + ECIES 密文 |
| 目标分片（铸造承诺） | 0 次/tx | ~1-2 ms/tx | 仅验证 BLS 聚合签名（1 次配对验全批） |

**目标分片实际计算负载**：

```
BLS 聚合签名验证（1 次/batch，含 1,000 txs）：
  1 次 G2 配对 + n 次 G1 标量乘法 ≈ 2-5 ms（整批）
  均摊：< 0.005 ms/tx
```

目标分片的瓶颈从 3.6-7.2 s 降至 **< 5 ms（整块）**，出块周期 10 s 绰绰有余。

**源分片的配对负载**（真实瓶颈）：

```
源分片每块验证所有用户提交的隐私 tx ZK proof：
  每块用户提交量（估算）：设 P = 待证明 tx 数
  单次验证：3 次配对 ≈ 3-6 ms（不批量）
  批量验证（Groth16 batch）：(2+P) 次配对 vs 3P 次，节省 ~33%
  
  实际瓶颈：P × 3 ms（无批量）或 P × 2 ms（有批量）
  若 P = 100 txs/block（合理估计，用户主动提交量）：
    无批量：300 ms   ← 10 s 窗口内可接受
    批量：  200 ms   ← 更优
```

**TPS 重新计算（修正后架构）**：

**隐私 Tx 消息大小（ECIES + CSCC 方案）**：

```
new_commitment：    32 B
ecies_ct：         ~144 B（64B epk + 80B AES-GCM 密文）
cscc_signature：    96 B（BLS 聚合签名）
src_block_hash：    32 B
routing 字段：      ~50 B
──────────────────────────────────────────
合计：             ~354 B   （vs 原 ~840 B，减少 58%）
（vs 普通 tx ~142 B，放大 ~2.5×）
```

**注**：ZK proof（256 B）留在源分片 EVM 处理，不进入跨分片 ToTxMessageItem。

**单池实际 TPS 受源分片配对验证约束**：

```
源分片每块可验证 ZK proof 数（批量验证，200 ms 内）：
  200 ms / (2 ms/tx × 批量系数 0.67) ≈ 150 txs/block（保守估计）
  → 单池 TPS：150 / 10 s = 15 TPS（源分片验证瓶颈）

  启用 SnarkPack/批量聚合（Leader GPU ~5s 聚合，链上验证 1 次）：
  → 单池 TPS：1,000+ / 10 s = 100+ TPS

  启用证明代理池（GPU 集群，多 proof 并行生成 + 批量聚合）：
  → 单池 TPS：向消息大小上限逼近：354B × 容量 → ~2,800 txs/block → 280 TPS/pool
```

**系统总 TPS（修正后，保守与乐观估计）**：

```
配置                          单池 TPS   单分片 TPS   10分片 TPS
──────────────────────────────────────────────────────────────
无批量聚合（基准）              15         480         4,800
批量验证（bellman batch）       50         1,600       16,000
GPU 聚合 + SnarkPack            100+       3,200+      32,000+
```

**注意**：上述数字为分析估算，以实际 Shardora 节点硬件（实测配对耗时）为准。论文 Evaluation 部分需提供：① 单节点 BN254 配对基准测试；② 不同 batch size 下批量验证耗时曲线；③ 真实分布式环境下的 E2E TPS 实测值。

**与业界方案 TPS 对比（修正后）**：

| 方案 | 隐私 TPS（实测/估算） | 配对运算/块 | 扩展性 |
|------|-------------------|-----------|-------|
| Tornado Cash（ETH L1） | 2.5（实测） | 受 gas limit | ❌ |
| Zcash Sapling（单链） | 5-10（实测） | 受出块速度 | ❌ |
| Monero | 30-50（实测） | 无配对 | ❌ |
| Aztec（L2，Sequencer） | ~100（估算） | Sequencer 私有 | ⚠️ |
| **Shardora（批量验证，10分片）** | **16,000-32,000**（估算，待实测） | 源分片批量，目标零配对 | ✅ |

---

## 5.4 证明生成性能

| 环境 | 估算时间 | 说明 |
|------|---------|------|
| 桌面 CPU（bellman/arkworks Rust） | 1-2 s | 主流开发机 |
| GPU（CUDA，bellman-cuda） | ~200 ms | RTX 4090 级别 |
| 移动端（ARM，软件实现） | 5-15 s | 需证明代理 |
| 证明代理服务（GPU 集群） | <500 ms（含网络往返） | 生产推荐方案 |

对比：Tornado Cash 电路 ~28,000 约束 → ~1s；本方案 ~21,010 约束，性能相近。

**证明代理服务的隐私安全性**（重要）：证明代理接收的 witness 不含 `spending_key`——因为 `spending_key` 仅用于计算 `nullifier`（客户端本地完成），计算完毕后立即销毁。代理收到的 witness 包含公开输入和随机数，不足以暴露发送方身份。

---

## 5.5 链上存储开销（修正后）

| 数据结构 | 每笔 Tx 开销 | 位置 |
|---------|------------|------|
| 承诺 Merkle 叶节点 | 32 B | 目标分片 |
| ECIES 密文（接收方扫链） | ~144 B | 目标分片 |
| Nullifier（防双花） | 32 B | 源分片 |
| Groth16 Verifying Key（一次性） | ~1-2 KB | 源分片合约 |

ECIES 密文上链是新增开销，但换来了委员会无需知道 Note 内容的关键安全性质。

---

## 5.6 性能瓶颈与优化路径

| 瓶颈 | 量化影响 | 优化方案 |
|------|---------|---------|
| 源分片配对验证（主要瓶颈） | ~3ms/tx，无批量时 ~15 TPS/pool | Groth16 批量验证 / SnarkPack 聚合 |
| 移动端证明生成 | 5-15 s 用户体验差 | GPU 证明代理服务 |
| ECIES 密文链上存储 | 144 B/tx 额外存储 | 可降至链外 DA 层（IPFS/Celestia）存密文，链上存 hash |
| Nullifier 集合增长 | 线性增长 | 快照归档（Merkle 根上链，原始数据链外） |

**批量 Groth16 验证**：验证 k 个 proof 代价 `(2+k)` 次配对 vs 单独 `3k` 次，k=50 时节省 97 次配对（64%）。批量验证是**必须实现**的工程特性，不是可选优化。

**SnarkPack 递归聚合**（最终方案）：将 N 个 Groth16 proof 聚合为 1 个（Inner Product Argument），链上仅验证 1 次 ≈ O(log N) 配对。Leader GPU 聚合耗时 ~5-10s（可在前一轮共识期间预聚合）。单块隐私 Tx 容量仅受消息大小约束。

---

## 5.7 性能总结（修正后）

```
架构：ECIES + CSCC，ZK proof 在源分片验证，目标分片零配对

源分片 ZK 验证（基准，无聚合）：
  ~15 TPS/pool（受配对运算约束）
  → 实测得出，以实际硬件为准

系统总吞吐（批量聚合，10 分片）：
  16,000 - 32,000 TPS（估算，待实测验证）

E2E 延迟：
  ~26 s（vs 普通跨分片 ~25 s，目标分片改为 BLS 验证后延迟更低）

Tx 跨分片消息大小：
  ~354 B（vs 原设计 ~840 B，减少 58%）

关键警示：
  论文 Evaluation 节必须提供实测数据：
  ① BN254 配对基准（单节点）
  ② 批量验证 TPS 曲线（batch size 10/50/100/500）
  ③ 真实广域网环境 E2E 延迟分布
  以上缺一不可，否则 USENIX/CCS 必拒
```

---

# Part VI：实施路径与总结

## 6.1 分阶段实施路径

```
Phase 1 — 接收方隐私（工作量：低）
  ├── 实现 Stealth Address 生成和扫描库
  ├── Shadow 合约新增 ephemeral_pk 字段
  └── 金额和发送方仍明文
  效果：链上不出现接收方真实地址

Phase 2 — 金额隐私（工作量：中）
  ├── 实现 Pedersen 承诺 + Bulletproofs（复用 libff）
  ├── 替换 Shadow 合约 _balances 为 UTXO 承诺模型
  ├── ZK 电路覆盖分片内花费
  └── Groth16 Trusted Setup 仪式
  效果：分片内金额隐藏，UTXO 模型生效

Phase 3 — 跨分片完整隐私（工作量：高）
  ├── pools.proto 添加 ecies_ct / cscc_signature / src_block_hash 字段
  ├── 源分片：ZK 验证通过后，BLS 聚合生成 CSCC，嵌入 ToTxMessageItem
  ├── 目标分片：to_tx_local_item.cc 新增 ShieldedCreditFromCSCC()（BLS 验签 + 盲插）
  └── ZK 电路扩展支持跨分片转账证明（new_cm_send 约束）
  效果：跨分片金额/接收方完全隐藏，目标分片零配对

Phase 4 — 匿名集扩大（工作量：中）
  ├── 固定面额混合池合约
  ├── 池间路由（池地址 ≠ 用户地址）
  └── 时序延迟策略（防时序关联）
  效果：发送方匿名性，匿名集 = 同面额存款人数
```

---

## 6.2 核心设计决策

**最重要的复用点**

系统已有完整 BLS 聚合签名基础设施（`bls_dkg.cc`, `ReconstructAndVerifyThresSign()`）。CSCC 即源分片 BFT 委员会的一次标准 BLS 聚合签名，**完全复用**现有签名逻辑，无需新增任何密钥或协议：
- **源分片**：复用 BLS 聚合算法，对 `H(new_cm_send ∥ target_shard ∥ pool_index ∥ src_block_hash)` 签名生成 CSCC
- **目标分片**：复用 BLS 验签逻辑（`bls::PublicKey::Verify()`），单次配对验证 CSCC 合法性

**最大工程挑战**

1. **ZK 电路**：Groth16 电路需 Rust/C++ 实现，Trusted Setup 需多方仪式（或改 PLONK 通用 setup）
2. **Shadow 合约状态迁移**：从 `mapping` 余额模型迁移到 Merkle 承诺树，需精心设计与 `to_tx_local_item.cc` 懒部署逻辑的兼容
3. **移动端证明体验**：5-15s 证明时间需通过证明代理服务解决

**消息类型关系**

隐私转账是 `kConsensusLocalTos` 的新子类型（`is_shielded = true`）。现有路由分发逻辑（`block_manager.cc HandleCrossShardBaseTx`）不变，仅在 `to_tx_local_item.cc` 处理阶段分叉：明文转账走原有路径，隐私转账走 **CSCC 验签→盲插新路径**（`ShieldedCreditFromCSCC()`）。

---

## 6.3 安全性汇总

| 威胁模型 | 防御机制 | 强度 |
|---------|---------|------|
| 发送方关联 | Nullifier 不暴露来源 Note；混合池匿名集 | 依赖匿名集大小 |
| 接收方识别 | 一次性 Stealth Address，每笔不同 | 强（信息论安全） |
| 金额推断 | Pedersen 承诺 + Bulletproofs | 强（计算安全） |
| 跨分片关联 | ECIES 加密（接收方 view_pk），CSCC BLS 授权（仅承诺哈希） | 强（CDH + BFT safety） |
| 双花 | Nullifier 集合 + ZK 约束 C3 | 强（计算安全） |
| 委员会串通 | 需 > t = ⌈2n/3⌉ 节点串通（= 攻破共识） | 同现有系统安全假设 |
| 无效 proof | Groth16 链上验证，BN254 安全参数 128 位 | 强 |
| 时序关联 | 混合池延迟策略，指数分布出款间隔 | 中（依赖混合池延迟参数） |
| Gas 关联 | SYSTEM_EXECUTOR 支付 Gas，接收方无需预持代币 | 强 |

---

## 6.4 定理汇总

### 基础引理

| 引理 | 内容 | 安全类型 | 依赖假设 |
|-----|------|---------|---------|
| Pedersen 隐藏性 | 完美隐藏 | **信息论** | 无 |
| Pedersen 绑定性 | 计算绑定 | 计算安全 | DL |
| Nullifier 不可碰撞 | 碰撞概率 ≤ 1/2^256 | 统计安全 | ROM |
| Nullifier 不可预测 | 不知 sk 无法预计算 | 计算安全 | DL + ROM |

### 核心定理（原有，4.1-4.8 节）

| 定理 | 内容 | 安全类型 | 依赖假设 |
|------|------|---------|---------|
| **定理 1a** | ECIES IND-CCA2 安全（Note 内容加密） | 计算安全 | CDH + ROM |
| **定理 1b** | CSCC BLS EUF-CMA 安全（跨分片授权） | 计算安全 | DL（BLS），BFT safety |
| **定理 2** | Note 隐私（接收方密钥）与 CSCC 完整性（BFT 共识）独立安全 | 计算安全 | CDH + BFT 安全模型 |
| **定理 3** | 端到端跨分片隐私（主定理） | 计算安全 | CDH + DL + q-SDH + ROM |
| **定理 4** | Shield 后不可链接性 | 计算安全 | DDH + ROM |
| **定理 5** | Unshield 最小暴露界 | 计算安全 | DL + q-SDH + ROM |
| **定理 6** | 混合余额模型全生命周期隐私 | 计算安全 | DDH + DL + q-SDH + ROM |

### 分片架构专有强化定理（4.9 节，核心理论贡献）

| 定理 | 内容 | 安全类型 | 单链是否成立 |
|------|------|---------|------------|
| **定理 A** | 单链无 Gas 隐私不可能性 | **不可能性定理**（无假设） | 即为单链负结果 |
| **定理 B** | 目标分片信息论源不可链接（精确 1/K） | **信息论安全**（无界敌手） | ❌ |
| **定理 C** | 匿名集乘法复合性（1/∏Kᵢ 乘法界） | 计算安全 | ❌（单链仅加法界） |
| **定理 D** | 双轨安全：机密性独立于共识（CDH only）∧ 完整性=BFT+BLS EUF-CMA | 计算安全，两轨解耦 | ❌（单链无法分离两轨） |
| **定理 E** | 隐私-活性相容性（BFT Liveness 直接推出） | 系统性质 | ❌（Relayer 单点） |

> **最强新结果排序**：定理 A（不可能性定理，论文黄金贡献）→ 定理 B（目标侧信息论不可链接性，无界敌手下精确 1/K 界）→ 定理 D（双轨解耦，BFT 崩溃下 Note 隐私仍成立，颠覆旧架构的混合界假设）。三者在单链方案中均不成立。

---

## 6.5 最终结论

> **核心结论 1**：没有分片架构，隐私交易完全可以实现（Tornado Cash、Zcash 已证明）。但存在三个结构性问题在单链上无法克服：Gas 关联攻击、观察域无法分离、额外加密原语导致独立 MPC 或受信任中介。
>
> **核心结论 2**：跨分片架构对隐私交易的价值是结构性的，不是性能性的。它解决了单链上没有令人满意答案的三个问题：SYSTEM_EXECUTOR 消除 Gas 关联、物理节点集合分离消除观察域重叠、BFT 委员会复用消除额外信任假设。
>
> **核心结论 3**：在 DDH + DL + q-SDH + ROM 假设下，本方案对任意 PPT 外部观察者（包括最多 t-1 个腐化委员会节点）提供完备的发送方/接收方/金额隐私，及跨分片端到端不可关联性。破坏隐私与破坏 BFT 共识需要完全相同的攻击能力——这是分片架构隐私的最深层保证。

---

# Part VII：用户主动隐私选择——Shield/Unshield 机制与安全性

## 7.1 问题：为何协议层"隐私 flag"不可行

用户自然希望在发起交易时简单指定"我要隐私转账"。但这在**协议层**上结构不兼容：

| 维度 | 普通转账 | 隐私转账 |
|------|---------|---------|
| 必要链上字段 | `from, to, amount`（明文） | `nullifier, commitment`（密文替代 to/amount） |
| 余额状态 | `_balances[addr]`（账户模型） | Merkle 承诺树（UTXO Note 模型） |
| 客户端准备 | 签名（<1ms） | 选 Note + 生成 ZK proof + ECIES 加密（1-2s） |
| 节点执行路径 | 直接更新余额 | 验证 ZK proof → CSCC 签名 → 目标分片盲插承诺树 |

普通交易的 `(from, to, amount)` 三元组在隐私交易中**根本不存在**——没有 `to`（替换为 stealth address 承诺），没有 `amount`（替换为 Pedersen commitment）。协议层无法在接收到"普通交易+flag"后凭空生成这些字段，因为它们依赖用户侧的私密输入（spending_key、随机数、接收方公钥）。

---

## 7.2 解决方案：SDK/钱包层统一接口

在协议层之上提供**统一发送接口**，隐私细节全部在客户端处理：

```
┌─────────────────────────────────────────────────────────────┐
│                  用户/应用层调用                              │
│   wallet.send(recipient_id, amount, privacy=True/False)     │
└──────────────────────┬──────────────────────────────────────┘
                       │
          ┌────────────▼────────────┐
          │   Shardora Wallet SDK   │
          │                         │
          │  privacy=False:         │
          │    → 构造普通 CrossTransfer tx                    │
          │    → 签名 → 广播                                  │
          │                         │
          │  privacy=True:          │
          │    Step 1: 检查隐私余额（UTXO Note pool）         │
          │      若不足：先 Shield 明文余额入池（见 7.3）       │
          │    Step 2: 选 Note，计算 nullifier                │
          │    Step 3: 生成 stealth_addr（接收方 view_pk）    │
          │    Step 4: Groth16 proof 生成（1-2s）             │
          │    Step 5: ECIES 加密 Note 载荷（接收方 view_pk）  │
          │    Step 6: 提交 ShieldedCrossTransferOut tx       │
          └─────────────────────────────────────────────────┘
```

**SDK 的隐私保证**：SDK 在本地完成所有私密计算。网络上只有最终的隐私交易（nullifier + commitment + 密文 + ZK proof），不含 spending_key 或明文 amount。

---

## 7.3 Shield / Unshield：明文与隐私余额的入口/出口

用户的资产在两个平行的余额域之间流动：

```
明文余额域（_balances）
        │                         ↑
        │  Shield（存入）          │  Unshield（取出）
        ▼                         │
隐私余额域（UTXO Note Merkle 树）
        │
        │  隐私跨分片转账（Part III 完整流程）
        ▼
（目标分片隐私余额域）
```

### Shield 操作（明文 → 隐私）

```
用户输入：amount_shield（明文金额），spend_pk（接收方），r ← rand(Fr)

链上步骤（PrivacyShadow.shield(amount, commitment)）：
  1. 从 msg.sender 的 _balances 扣除 amount_shield
  2. cm = Pedersen(amount_shield, r, spend_pk)
  3. 将 cm 插入承诺 Merkle 树 cm_tree
  4. 发出事件 ShieldDeposit(cm, cm_tree_root)
     ← 事件仅暴露承诺 cm 和新树根，不暴露 spend_pk 或 r

公开暴露：amount_shield（链上可见）
隐藏：spend_pk（Pedersen 承诺绑定但不揭露）、r（完全隐藏）
```

### Unshield 操作（隐私 → 明文）

```
用户输入：Note 的 spending_key、commitment 的 Merkle 路径、recipient_addr（明文接收地址）

链上步骤（PrivacyShadow.unshield(nullifier, recipient, amount, proof)）：
  1. 验证 Groth16 proof
     公开输入：nullifier, cm_root, value_binding, recipient_addr_hash
     私密输入：spending_key, Merkle 路径, Note 内容
  2. 检查 nullifier 未被使用
  3. 向 recipient_addr 的 _balances 增加 amount
  4. 发出事件 ShieldWithdraw(nullifier, recipient_addr, amount)
     ← 事件暴露：recipient_addr、amount（不可避免，明文接收地址）
     ← 隐藏：哪个承诺被花费（ZK proof 隐藏 Merkle 路径）
```

---

## 7.4 安全性分析

### 7.4.1 Shield 操作的隐私界

**已知暴露（不可避免）**：
- `amount_shield`：链上明文，任何观察者可见
- 时序：Shield 发生的区块号和时间戳

**已隐藏**：
- `spend_pk`：Pedersen 承诺的完美隐藏性（信息论安全，见定义 1）
- 后续流向：Shield 之后，资产在 UTXO 域流转，链上只见承诺

**关联风险**：若用户 Shield 金额 v，再从同一分片 Unshield 金额 v，观察者可凭金额推断关联。缓解手段（见 7.4.4）：拆分金额、跨分片转移后再 Unshield、混合池中间跳转。

### 7.4.2 Unshield 操作的隐私界

**已知暴露（不可避免）**：
- `recipient_addr`：明文地址，链上可见
- `amount`：明文金额，链上可见
- `nullifier`：链上可见，但无法反推来源 Note（单向哈希，见引理 4）

**已隐藏**：
- 哪个 Note（commitment）被花费：ZK 约束 C2（Merkle 路径验证）确保任何树中的 Note 都能合法花费，而不揭露具体是哪个
- `spending_key`：ZK 约束 C4 证明知识，但不暴露值

### 7.4.3 SDK 层的信息隔离

SDK 在客户端运行，需保证：

| 信息 | 存储位置 | 是否上网 |
|------|---------|---------|
| `spending_key`（Fr 标量） | 本地钱包，加密存储 | ❌ 永不出设备 |
| `randomness r`（Fr 标量） | 本地 Note 数据库 | ❌ 永不出设备 |
| `view_sk`（扫链私钥） | 本地钱包 | ❌ 永不出设备 |
| Groth16 proof | 本地生成后 → 广播 | ✅ 仅最终 proof |
| ECIES 密文（ecies_ct） | 本地加密后 → 广播 | ✅ 密文形式，仅接收方可解 |
| Witness（电路输入） | 内存临时 | ❌ 不持久化 |

若使用**证明代理服务**（见 5.6 优化路径），witness 中不含 spending_key：
```
Nullifier = H(spending_key ∥ cm)
  → spending_key 用于计算 nullifier 后立即销毁
  → 代理接收的 witness 包含 nullifier（公开输入），不含 spending_key
```

### 7.4.4 特有攻击向量

#### 金额图分析（Amount Graph Analysis）

攻击：观察者记录所有 Shield 金额和 Unshield 金额，按金额匹配找关联。

```
攻击者的信息：
  ShieldDeposit events:  {cm₁→v₁, cm₂→v₂, cm₃→v₃, ...}  （金额可见）
  ShieldWithdraw events: {nul_a→w₁, nul_b→w₂, ...}        （金额可见）
  
  若 vᵢ = wⱼ → 怀疑关联
```

**缓解层级**：

| 缓解手段 | 效果 | 代价 |
|---------|------|------|
| 固定面额混合池（见 3.10） | 消除精确金额匹配 | 需碎券/合券操作 |
| Note 拆分（1个 Note → N 次 Shield） | 增加匹配难度 | 多笔 Shield 事务 |
| 跨分片中转（Shield 在 Shard_A，Unshield 在 Shard_B） | 不同观察域 | 跨分片延迟 |
| 时序随机延迟（Exp 分布） | 切断时序关联 | 平均 +300s 延迟 |

#### Shield 时序窗口关联

攻击：观察 Shield 操作与后续隐私转账的时间间隔，推断资金流向。

单链上这是主要攻击面（Tornado Cash 受此影响），但在跨分片场景下：
- Shield 在 Shard_src 的节点集 N_src 可见
- 后续隐私转账路由到 Shard_dst，由节点集 N_dst（与 N_src 不相交）处理
- 没有任何单一观察者能同时看到 Shield 事件和后续隐私转账目的地

形式化：设 E_shield = ShieldDeposit 事件，E_transfer = ShieldedCrossTransferOut 事件。外部观察者可见 E_shield（源分片公开）和 E_transfer（源分片公开），但 E_transfer 中的 Note 载荷（金额、接收方 stealth_addr）通过 ECIES 加密，仅接收方 view_sk 可解，观察者无法知道最终去向。

---

## 7.5 形式化定义与定理

### 定义 4（Shield 操作安全性）

Shield 操作 `Sh(amount, spend_pk, r) → cm` 满足：

- **金额绑定**（依赖 DL）：`Pr[∃(amount'≠amount, r') : Pedersen(amount',r')=cm] ≤ negl(λ)`
- **spend_pk 隐藏**（完美隐藏）：`∀(pk₀, pk₁) : Pr[A(cm)=b | cm←Ped(v,r,pk_b)] = 1/2`
- **随机性隐藏**（完美隐藏）：Pedersen 承诺对 r 均匀随机时完美隐藏 spend_pk 和 amount

### 定义 5（Unshield 操作安全性）

Unshield 操作 `Unsh(sk, cm_path, recipient) → (nul, π)` 满足：

- **Note 不可链接性**：对外部观察者，`nul = H(sk ∥ cm)` 无法反推 `cm`（ROM 下单向性）
- **消费合法性**（ZK 可靠性）：`Pr[伪造合法 proof 花费未知 Note] ≤ negl(λ)`（依赖 q-SDH）
- **防双花**（Nullifier 唯一性）：合约拒绝重复 `nul`，攻击者无法对同一 Note 生成两个不同 `nul`（ROM 下 hash 单射）

### 定理 4：Shield 后的不可链接性

**定理 4**：在 DDH + ROM 假设下，对于在 Merkle 承诺树中有 k 个叶节点的系统，任意 PPT 外部观察者 A 区分两个 Shield 操作 `Sh(v, pk₀)` 和 `Sh(v, pk₁)` 的优势不超过 negl(λ)，且将后续任意隐私转账与具体 Shield 操作关联的概率不超过 1/k + negl(λ)。

**证明**：

（spend_pk 不可区分）由定义 4，Pedersen 承诺对 spend_pk 完美隐藏，即使 amount 相同，任何观察者也无法从 cm 区分 pk₀ 和 pk₁。

（后续转账不可关联）花费 Note 时公开的 nullifier = H(spending_key ∥ cm)。由 ROM，H 的输出对不知道 spending_key 的观察者是伪随机的；cm 是 k 个叶节点之一，观察者猜中对应关系的概率为 1/k。

两者联合：关联优势 ≤ 1/k + negl(λ)。 □

### 定理 5：Unshield 的隐私界（不可避免的暴露）

**定理 5**：在任意密码学假设下，Unshield 操作**不可避免地**向链上观察者暴露 `recipient_addr` 和 `amount`，但以下内容在 DL + q-SDH + ROM 假设下对外部观察者计算保密：
1. 被花费的具体 Note（哪个 cm）
2. Note 的来源（哪笔 Shield 或哪次隐私转账）
3. `spending_key`（私钥）

**证明**：

（不可避免性）明文余额的接收方必须能接收，即其地址必须公开，否则无法转账至其账户——这是账户模型的基本约束。

（被花费 Note 的保密性）Unshield ZK 约束 C2 证明存在一条 Merkle 路径，但 Merkle 树有 k 个叶节点，观察者无法从 nullifier 和 proof 推断路径，由 Groth16 零知识性（定义 2），proof 不泄露 witness，而 Merkle 路径正是 witness 的一部分。

（来源保密性）nullifier = H(spending_key ∥ cm)，由 ROM 单向性，不能从 nullifier 反推 cm，从而不能追溯来源。 □

> **推论**：Unshield 是隐私系统的最大信息泄露点。最佳实践是：在隐私域内完成所有中间转账，只在最终资金出口时 Unshield，且 Unshield 金额应与 Shield 金额不同（拆分/合并），降低金额匹配关联风险。

### 定理 6：混合余额模型整体隐私保证

**定理 6**（主定理扩展）：在 DDH + DL + q-SDH + ROM 假设下，用户资金从 Shield 到若干次跨分片隐私转账再到 Unshield 的完整生命周期，外部观察者 A 能获得的信息不超过：
```
I_A = {amount_shield, timestamp_shield, amount_unshield, recipient_unshield}
```

具体地，A 对以下内容的优势不超过 negl(λ)：
- 确定 Shield 和 Unshield 操作是否属于同一用户
- 确定中间经过多少次跨分片转账
- 确定中间经过哪些分片/地址
- 确定接收方在 Shield 之前是否知道该笔资金

**证明梗概（混合论证）**：

在定理 3 的 H₀→H₄ 混合序列基础上，在两端接续新的混合步骤：

- H_{-1} → H₀：将 Shield 的 spend_pk 替换为均匀随机 G₁ 点（定义 4 完美隐藏性，优势差 = 0）
- H₄ → H₅：将 Unshield 中的 Merkle 路径 witness 替换为 Simulator 模拟（Groth16 ZK 性，优势差 ≤ negl(λ)）

整个序列 H_{-1} → H₅ 中，中间阶段（跨分片隐私转账）已由定理 3 保证，首尾的 Shield/Unshield 边界由本定理的新混合步骤覆盖。

外部观察者视图 `View_A = (cm_shield, cm_unshield_nullifier, I_A)`，其中 cm_shield 完美隐藏内容，nullifier 是伪随机的，只有 `I_A` 是真实暴露的信息。 □

---

## 7.6 混合余额模型的状态机图

```
                     Shield(v)
                     ──────────►  UTXO Note 余额域
                     链上暴露：v
明文余额域
_balances[addr]        ┌──────────────────────────────────────────────────┐
                       │  Privacy Domain                                   │
                       │                                                   │
                       │  隐私分片内转账（Part III）                        │
                       │  • nullifier + commitment，全加密                  │
                       │  • 外部不可见                                      │
                       │                                                   │
                       │  跨分片隐私转账（Part III）                        │
                       │  • ECIES 加密 Note，CSCC BLS 授权跨片              │
                       │  • 源/目标分片节点集不相交                         │
                       └──────────────────────────────────────────────────┘
                     ◄────────── Unshield(v', recipient)
                     链上暴露：v', recipient
```

**信息泄露时机**：

| 操作 | 链上暴露 | 不暴露 |
|------|---------|-------|
| Shield | `amount_shield`，时间戳 | spend_pk，随机数，后续去向 |
| 隐私域内跨分片转账 | nullifier（不可反推），new_cm | 金额，接收方，源/目标 |
| Unshield | `amount_unshield`，`recipient` | 来源 Note，中间路径 |

---

## 7.7 对完整定理体系的影响

新增定理 4、5、6 与原有定理 1-3 的关系：

```
定理 1a（ECIES IND-CCA2）+ 定理 1b（CSCC BLS EUF-CMA）
    ↓ 被定理 3 混合论证使用
定理 2（Note 隐私 ≡ 接收方密钥安全；CSCC 完整性 ≡ BFT 共识安全）
    ↓ 共享安全假设
定理 3（端到端跨分片隐私，主定理）
    ↓ 定理 6 将其扩展至 Shield/Unshield 边界
定理 4（Shield 后不可链接性）──────┐
定理 5（Unshield 的隐私界）        ├─→ 定理 6（混合余额模型整体隐私）
定理 4 + 5 + 3 ───────────────────┘
```

**完整安全定理体系**：

| 定理 | 内容 | 依赖假设 |
|------|------|---------|
| 定理 1a | ECIES IND-CCA2（Note 内容加密） | CDH + ROM |
| 定理 1b | CSCC BLS EUF-CMA（跨分片授权） | DL + BFT safety |
| 定理 2 | Note 隐私（接收方密钥安全）∧ CSCC 完整性（BFT 共识安全） | CDH + BFT 安全模型 |
| 定理 3 | 端到端跨分片隐私 | CDH + DL + q-SDH + ROM |
| **定理 4** | Shield 后不可链接 | DDH + ROM |
| **定理 5** | Unshield 的不可避免暴露界 | DL + q-SDH + ROM |
| **定理 6** | 混合余额模型全生命周期隐私 | DDH + DL + q-SDH + ROM |

**最大攻击面排序**（从高到低）：

```
1. Unshield 操作（recipient + amount 公开）        ← 不可避免，应尽量推迟
2. Shield 操作（amount 公开）                      ← 用固定面额混合池缓解
3. 委员会内部人串通（≥t 个节点）                   ← 等价于攻破共识（定理 2）
4. 金额图分析（Shield-Unshield 金额匹配）          ← 用拆分/合并缓解
5. 时序关联攻击                                    ← 用指数延迟缓解
```

---

*整合自：PRIVACY_TX_DESIGN.md、PRIVACY_TX_COMPARISON.md、PRIVACY_TX_PERFORMANCE.md、PRIVACY_TX_FORMAL_ANALYSIS.md*
*版本：2026-09-15 | 分支：xl0616 | 核心模块：shardoravm、consensus/zbft、bls、protos*
*参考：Groth 2016、Pedersen 1991、Buterin et al. 2023、Möser et al. 2018、Bünz et al. 2018*
