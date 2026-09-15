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
  仅见：一个 ElGamal 密文从 S_src 流向 S_dst（内容不可见）
```

这是**架构级的观察域分离**，密码学无法在单链上复制。

---

## 1.4 问题三：阈值解密权威——零增量信任假设

跨链隐私需要一个实体执行解密。在 Aztec 是 Sequencer（中心化），在 Keep Network 是独立 MPC 委员会（新的信任假设）。

**跨分片的独特价值**：目标分片的 BFT 委员会已经存在，已经是经济激励对齐的抗拜占庭委员会。破坏隐私（解密密文）与破坏共识（双花攻击）需要完全相同的能力：腐化 ≥ t = ⌈2n/3⌉ 个节点。

```
单链方案的信任结构：
  共识安全          需要 > 1/3 节点诚实
  隐私解密权威      需要独立的 MPC 委员会（新的信任假设！）
  → 两个独立安全假设，攻击面叠加

跨分片方案的信任结构：
  共识安全          需要 > 1/3 节点诚实
  隐私解密权威      同一批 BFT 委员会（无新假设！）
  → 单一安全假设，攻击面不增加
```

---

## 1.5 价值量化总结

| 价值维度 | 单链能否实现 | 跨分片的改进 |
|---------|------------|------------|
| Gas 关联攻击免疫 | ❌ 结构上不可能 | ✅ SYSTEM_EXECUTOR 原生解决 |
| 观察域架构级分离 | ❌ 密码学无法替代 | ✅ 不同分片节点物理隔离 |
| 零增量信任阈值解密 | ❌ 必须引入独立 MPC | ✅ 复用现有 BFT 委员会 |
| 跨链关联泄露 | ❌ 桥接必然明文 | ✅ 路由层全程密文 |
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

本方案路由层携带 ElGamal 密文，路由节点（GBP）不获得任何明文信息，密文仅在目标分片委员会内解密——**路由全程加密，无明文中继点**。

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
| `libff::alt_bn128_G1/G2` 群运算 | Pedersen 承诺、ElGamal 加密 |
| `libff::alt_bn128_GT` 配对 | Groth16 proof 链上验证 |
| DKG 生成的 `local_sk_`（Fr 份额） | ElGamal 阈值解密份额（数学结构与 BLS 签名份额相同） |
| `ReconstructAndVerifyThresSign` Lagrange 插值框架 | 阈值解密重建（零修改复用） |
| `common_pk`（G2 公共公钥） | ElGamal 加密密钥基础 |

> **关键洞察**：ElGamal 阈值解密和 BLS 阈值签名的数学结构完全相同，都是对 Fr 域份额的 Lagrange 插值重建，DKG 已分发的密钥份额可同时用于两个用途，无需额外协议。

### 新增密码学原语

| 原语 | 用途 | 曲线 |
|------|------|------|
| **Pedersen 承诺** | 隐藏金额：`C = r·G + v·H` | alt_bn128 G1 |
| **Bulletproofs 范围证明** | 证明 `v ∈ [0, 2^64)` 而不揭露 `v` | alt_bn128 G1 |
| **一次性地址（Stealth Address）** | 隐藏接收方真实身份 | alt_bn128 G1 |
| **Nullifier（作废符）** | 防双花，类 Zcash 方案 | Poseidon Hash |
| **Groth16 零知识证明** | 证明整体转账合法性 | alt_bn128（支持配对） |
| **ElGamal 阈值加密** | 跨分片消息加密，由目标分片委员会集体解密 | alt_bn128 G1 |

### Privacy Pools 合规扩展（可选）

基于 Vitalik 2023 年的 Privacy Pools 方案，在电路末尾追加约束 C11：

```
C11: Note 的来源不在监管黑名单 Merkle 树中
     （合规监管方发布黑名单 Merkle 树，用户证明自己不在其中即可合规取款）
```

实现与 Privacy Pools 等价的合规能力，同时支持跨分片。

---

## 3.4 整体架构

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              用户层                                       │
│  发送方持有：spending_key (Fr)，viewing_key (Fr)                          │
│  接收方公布：spend_pk = spend_sk·G (G1)，view_pk = view_sk·G (G1)        │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ① 生成 ZK Proof + 一次性地址 + 加密载荷
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                   源分片：PrivacyShadow 合约                              │
│  承诺 Merkle 树：notes[cm_hash] — 替代 _balances[addr]                   │
│  Nullifier 集合：spent[nullifier] — 防双花                               │
│  事件：ShieldedCrossTransferOut(nullifier, new_cm, C1, C2, zk_proof)     │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ② cross_shard_to_array（密文载荷）
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                    全局缓冲池路由层（逻辑不变）                             │
│  ToTxMessageItem 携带：nullifier, new_cm, (C1, C2), zk_proof            │
│  路由目标 shard/pool 由 new_cm 中嵌入的目标坐标决定                        │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ③ 路由至目标分片
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│               目标分片：委员会阈值解密 + ZK 验证 + 执行                    │
│  各节点用 local_sk_i 计算 ElGamal 解密份额 D_i = sk_i · C1               │
│  收集 t 个份额 → Lagrange 插值重建 → 得到 (stealth_addr, amount, r)       │
│  验证 zk_proof → systemExecuteShieldedCredit(stealth_addr, new_cm)       │
│  将 new_cm 插入目标分片承诺 Merkle 树                                     │
└──────────────────────────────────────────────────────────────────────────┘
                                │ ④ 接收方扫链
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  接收方：用 view_sk 扫描链上 ephemeral_pk，匹配 Note；用 spend_sk 花费    │
└──────────────────────────────────────────────────────────────────────────┘
```

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

## 3.8 跨分片 ElGamal 加密载荷

**ElGamal 加密**（alt_bn128 G1，使用目标分片委员会 `common_pk_G1`）：

```
k       ← random Fr
C1      = k · G
C2      = M + k · common_pk_G1
```

消息 M 编码：`M = Hash(stealth_addr ∥ amount ∥ randomness_send) · G`，实际以 AES-GCM 加密 Note 明文，C2 仅承载 AES 密钥的加密形式。

**ToTxMessageItem 新增字段**：

```protobuf
message ToTxMessageItem {
  // 原有路由字段（保留）
  optional uint32 sharding_id = ...;
  optional uint32 pool_index  = ...;

  // 隐私转账新增
  optional bytes nullifier       = 20;   // 32 B
  optional bytes new_commitment  = 21;   // 32 B
  optional bytes elgamal_c1      = 22;   // 64 B（G1 点）
  optional bytes elgamal_c2      = 23;   // 64 B（G1 点）
  optional bytes aes_ciphertext  = 24;   // ~100 B
  optional bytes zk_proof        = 25;   // ~256 B（Groth16）
  optional bool  is_shielded     = 26;
}
```

**DKG 扩展（生成 G1 方向公钥）**：

在 `BlsDkg::FinishBroadcast()` 中，额外计算 `a_{i,0}·G1` 并广播，聚合得到：
```
common_pk_G1 = Σ a_{i,0}·G1 = sk_master · G1
```
不引入新的安全假设，与现有 DKG Feldman VSS 完全一致。

---

## 3.9 目标分片：委员会阈值解密

**数学过程**（与 BLS 阈值签名完全对称）：

```
每个节点 i：  D_i = sk_i · C1         （G1 标量乘法，~1 ms）
Leader 收集 t 个后 Lagrange 重建：
              D = Σ λ_i · D_i = sk_master · C1
解密：        M = C2 - D
```

**复用路径**：`Crypto::ReconstructAndVerifyThresSign()` 的 Lagrange 插值框架可直接复用。仅需在 `to_tx_local_item.cc` 新增 `ShieldedDecryptAndCredit()`，调用已有插值逻辑。

**执行阶段**：
1. 用合约内 `vk` 验证 `zk_proof`（链上，各节点独立验证）
2. 检查 `new_cm` 不在目标分片承诺树中（防重入）
3. 调用 `systemExecuteShieldedCredit(stealth_addr, new_cm)` → 插入 Merkle 树
4. 发出 `ShieldedCrossTransferIn(new_cm, tree_root)` 事件（不暴露 stealth_addr）

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
用户 A（发送）                    链上/路由层                    用户 B（接收）
    │                                │                                │
    │ 1. 生成 stealth_addr_B         │                                │
    │ 2. 构造 Note_send + Note_change│                                │
    │    生成 Groth16 proof          │                                │
    │ 3. ElGamal 加密 Note_send      │                                │
    │ 4. 调用 PrivacyShadow.spend()  │                                │
    │                                │                                │
    │             源分片共识打包     │                                │
    │──────────────────────────────►│                                │
    │                       ShieldedCrossTransferOut                  │
    │                       (nullifier, new_cm, C1, C2, proof)        │
    │                                │                                │
    │                       路由至目标分片（密文）                     │
    │                                │                                │
    │                       目标分片委员会阈值解密                     │
    │                       D_i = sk_i · C1                           │
    │                       D = Σ λ_i · D_i                          │
    │                       M = C2 - D                                │
    │                       验证 zk_proof                             │
    │                       ──────────────────────────────────────►  │
    │                       systemExecuteShieldedCredit(cm)           │
    │                                │                                │
    │                       ShieldedCrossTransferIn(new_cm)           │
    │                                │◄───────────────────────────── │
    │                                │  B 用 view_sk 扫链匹配 Note   │
    │                                │  B 用 spend_sk 花费 Note      │
```

---

## 3.12 与现有代码的集成点

| 现有文件 | 改动性质 | 说明 |
|---------|---------|------|
| `src/shardoravm/shardora_host.cc` `emit_log()` | 新增 case | 拦截 `ShieldedCrossTransferOut`，构造 `kShieldedTransfer` pending action |
| `src/shardoravm/host_journal_stack.h` `CrossShardPendingAction` | 扩展 | 新增 `kShieldedTransfer` 枚举值，字段替换为 `(nullifier, new_cm, encrypted_payload)` |
| `src/consensus/zbft/contract_call.cc` ~L431 | 修改 | shielded action 转 `ToTxMessageItem` 时写密文字段 |
| `src/protos/pools.proto` `ToTxMessageItem` | 添加字段 | `nullifier`, `new_commitment`, `elgamal_c1/c2`, `aes_ciphertext`, `zk_proof`, `is_shielded` |
| `src/consensus/zbft/to_tx_local_item.cc` | 主要改动 | 新增 `ShieldedDecryptAndCredit()`，调用阈值解密→验证 proof→执行 shielded credit |
| `src/bls/bls_dkg.cc` `FinishBroadcast()` | 小扩展 | 额外计算并广播 `common_pk_G1`（G1 点） |
| `src/consensus/hotstuff/elect_info.h` `ElectItem` | 添加字段 | `libff::alt_bn128_G1 common_pk_g1_` |
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
  optional bool   is_shielded    = 26;
  optional bytes  nullifier      = 20;
  optional bytes  new_commitment = 21;
  optional bytes  elgamal_c1    = 22;
  optional bytes  elgamal_c2    = 23;
  optional bytes  zk_proof      = 25;
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
    // 新路径：阈值解密 → ZK 验证 → 更新承诺 Merkle 树
    ShieldedDecryptAndCredit(item);
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

## 4.3 定理 1：阈值 ElGamal 的 IND-CPA 安全性

**定理 1**：若 DDH 在 G₁ 上成立，则对腐化至多 t-1 个节点的 PPT 敌手 A，
```
Adv^{IND-CPA}_A ≤ 2 · Adv^{DDH}_{G₁}
```

**证明（归约）**：B 收到 DDH 挑战 `(g, ag, bg, c·g)`，设 `pk_G1 = a·g`。

- 若 `c = ab`：`C₂ = Mβ + c·g = Mβ + k·pk_G1` 是对 Mβ 的合法加密（k = b）
- 若 `c ←$ Fr`：`C₂` 对 A 是均匀随机，与 M₀,M₁ 无关

A 的区分优势直接转化为 B 破坏 DDH 的优势，故 `Pr[B 攻破 DDH] = ε/2`，即 `ε ≤ 2·Adv^{DDH}`。 □

---

## 4.4 定理 2：共识安全与隐私安全的假设完全对齐

**定理 2**：破坏跨分片隐私与破坏 BFT 共识需要**完全相同的攻击能力**（均需腐化 ≥ t 个委员会节点）。

**证明**：

- （→）若敌手可破坏隐私（解密跨分片密文），则其腐化至少 t 个节点（定理 1 的 IND-CPA 逆否）
- （←）若腐化至少 t 个节点，可重建 sk_master 并解密任意密文；同时 t ≥ ⌈2n/3⌉ 意味着控制了 BFT 法定人数，共识 safety 失效

两者互为充要条件，安全假设完全对齐，隐私层不引入额外攻击面。 □

---

## 4.5 定理 3：端到端跨分片隐私（主定理）

**定理 3**：在 DDH + DL + q-SDH + ROM 假设下，对任意 PPT 外部观察者 O 和腐化至多 t-1 个节点的联合敌手 A，本方案同时满足：发送方 k-匿名、接收方不可追踪、金额保密、跨分片不可关联。

**证明梗概（混合论证）**：

构造混合实验序列 `H₀ → H₁ → H₂ → H₃ → H₄`：

| 混合步骤 | 操作 | 不可区分性依据 | 优势差 |
|---------|------|-------------|--------|
| H₀ → H₁ | `(C₁, C₂)` 替换为均匀随机 G₁ 点 | 定理 1（ElGamal IND-CPA） | `≤ 2·Adv^{DDH}` |
| H₁ → H₂ | π 替换为 Sim(x) 模拟证明 | Groth16 零知识性（定义 2） | `≤ negl(λ)` |
| H₂ → H₃ | `cm_out` 替换为随机 G₁ 点 | Pedersen 完美隐藏性（定义 1） | 0（完美不可区分） |
| H₃ → H₄ | `nul` 替换为随机哈希 | Nullifier 不可预测性（ROM + DL） | `≤ negl(λ)` |

在 H₄ 中，`View_O = (nul, cm_out, C₁, C₂, π)` 全部为独立均匀随机值，不含任何隐私信息。

总优势 ≤ `2·Adv^{DDH} + negl(λ)`。 □

---

## 4.6 攻击向量分析

### 时序关联攻击

若用户立即取款，时序差 `Δt ≈ 27s` 与跨分片路由时间高度相关。

**缓解**：引入指数分布延迟 `Δ_random ← Exp(μ)`，期望 μ = 300s（可配置）。关联概率降至 `≤ 1/k + ε(μ)`，ε(μ) → 0 随 μ 增大。代价：E2E 延迟从 27s 增至 ~327s。

### Nullifier 碰撞攻击

需要 `H(sk₁ ∥ cm₁) = H(sk₂ ∥ cm₂)`，即哈希碰撞，在 ROM 下概率 ≤ 1/2^256，不可行。

### 跨分片前跑攻击

恶意 Leader 看到密文后，在解密前将自己插入取款队列。分析：Leader 无法单独解密（需 t 个节点协作），在重建 sk_master 之前不知道金额和接收方，无法有针对性前跑。HotStuff liveness 保证合法 tx 最终被包含。

### 委员会内部人攻击

t 个节点串通重建 sk_master。这与攻击共识需要相同资源（定理 2），是系统安全底线。对比：Tornado Cash 无委员会（零信任→零跨链能力），这是明确的安全-功能权衡。

---

## 4.7 复杂度分析

```
操作                              复杂度              具体估计（|C|=21,010）
────────────────────────────────────────────────────────────────────────────
Groth16 证明生成（MSM）          O(|C|log|C|)        ~300,000 G₁ 乘法
Groth16 验证                     O(1)                3 次配对（恒定）
ElGamal 加密（用户侧）           O(1)                2 次 G₁ 乘法
ElGamal 部分解密（每节点）       O(1)                1 次 G₁ 乘法（~1 ms）
ElGamal 重建（Leader）           O(t)                t≈67 次 G₁ 乘法（~70 ms）
Merkle 路径更新（插入 Note）      O(d)                d=20 次 hash
Nullifier 查找                   O(1)                哈希表查找
────────────────────────────────────────────────────────────────────────────
```

---

## 4.8 局限性

**量子计算威胁**：BN254 的离散对数问题可被 Shor 算法解决，这是全行业共同问题。后量子迁移路径：Groth16 → 格基 SNARK（Latticefold），ElGamal → Kyber/ML-KEM，Pedersen → 哈希基承诺。当前尚无成熟方案在合理 proof 大小内运行。

**Trusted Setup**：Groth16 CRS 若被毒化，攻击者可伪造证明（破坏防双花）或无声铸币。缓解：采用大规模 MPC 仪式（100+ 参与者），或替换 PLONK（无专属仪式，代价 proof 增大 3×）。

**Merkle 树深度限制**：深度 d=20 支持最多 2^20 ≈ 100 万个并发 Note/pool。超过后需树深度扩展（增加约束数）或引入可更新 Accumulator。

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
| ElGamal 部分解密广播 + 收集 | ~300-600 ms | **否**，藏入 HotStuff 投票收集窗口 |
| Groth16 链上验证（EVM 执行） | ~100 ms | 目标分片出块内完成 |

**E2E 延迟对比**：

```
普通跨分片    ~25 s
隐私跨分片    ~26-27 s    （增量 < 10%）
```

关键原因：阈值解密与 BLS 投票数学结构相同，`D_i` 消息直接搭载在现有 VoteMsg 广播上，**不增加共识轮次**。

---

## 5.3 吞吐量分析

**隐私 Tx 消息大小**：

```
Groth16 proof：        256 B
公开输入（6×32B）：    192 B
ElGamal C1, C2：       128 B
AES 密文：             ~100 B
Nullifier + new_cm：    64 B
其他 protobuf 字段：   ~100 B
──────────────────────────────
合计：                ~840 B   （vs 普通 tx ~142 B，放大 ~5.6×）
```

**单池隐私 TPS**：
```
每块容量 = kMaxProposeMsgBytes / 840B ≈ 1,200 txs/block
单池 TPS = 1,200 / 10 s = 120 TPS
```

**系统总隐私 TPS（随分片线性扩展）**：

```
单分片（32 池）：  120 × 32 = 3,840 TPS
5  分片：         19,200 TPS
10 分片：         38,400 TPS
20 分片：         76,800 TPS
```

**与业界方案 TPS 对比**：

| 方案 | 隐私 TPS | 扩展性 |
|------|---------|-------|
| Tornado Cash（ETH L1） | 2.5 | ❌ 固定 |
| Zcash Sapling（单链） | 5-10 | ❌ 固定 |
| Monero | 30-50 | ❌ 固定 |
| Aztec（L2，Sequencer） | ~100 | ⚠️ 中心化 |
| **Shardora 隐私（10 分片）** | **38,400** | ✅ 线性扩展 |

---

## 5.4 证明生成性能

| 环境 | 估算时间 |
|------|---------|
| 桌面 CPU（bellman/arkworks Rust） | 1-2 s |
| GPU（CUDA，bellman-cuda） | ~200 ms |
| 移动端（ARM 软件实现） | 5-15 s ⚠️ |
| 证明代理服务 | <500 ms（网络往返） |

对比：Tornado Cash 电路 ~28,000 约束 → ~1s；本方案 ~21,010 约束，性能相近。

---

## 5.5 链上存储开销

| 数据结构 | 每笔 Tx 开销 |
|---------|------------|
| Merkle 树叶节点（Sparse 存储） | 32 B × d = 640 B（d=20 路径节点） |
| Nullifier 集合（已花费） | 32 B |
| Groth16 Verifying Key（合约内，一次性） | ~1-2 KB 固定 |

放大因子约 20×（Merkle 路径）vs 普通 `_balances` 每账户 32B。

---

## 5.6 性能瓶颈与优化路径

| 瓶颈 | 影响 | 优化方案 |
|------|------|---------|
| 移动端证明生成（5-15 s） | 用户体验 | 证明代理服务（witness 不含私钥，零知识） |
| 单块隐私 Tx 上限（1,200/block） | 峰值吞吐 | Groth16 批量聚合验证（节省 60% 配对成本） |
| Nullifier 集合无界增长 | 长期存储 | 快照归档（Merkle 根上链，原始数据链外） |

**Groth16 批量验证**：验证 k 个 proof 代价 ≈ `(2+k)` 次配对 vs 单独验证 `3k` 次。k=10 时节省 60%，单池 TPS 可进一步提升。

**递归聚合**：将 N 个 proof 聚合为 1 个（Leader GPU，~5s），区块只需验证 1 个聚合 proof。单块隐私 Tx 容量上限从 1,200 提升至消息大小约束（~7,000 txs/block）。

---

## 5.7 性能总结

```
E2E 延迟：         ~27 s  （vs 普通跨分片 ~25 s，增量 < 10%）
客户端延迟（PC）：  1-2 s  （不在关键路径，与链上确认并行）
系统吞吐量：       38,400 TPS（10 分片，随分片数线性扩展）
Tx 消息大小：      ~840 B （vs 普通 ~142 B，5.6× 放大）
链上存储/Tx：      ~64 B  （Nullifier + 承诺，与普通账户相当）
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
  ├── DKG 扩展输出 common_pk_G1
  ├── ToTxMessageItem 改为加密载荷
  ├── to_tx_local_item.cc 新增阈值解密流程
  └── ZK 电路扩展支持跨分片转账证明
  效果：跨分片金额/接收方完全隐藏

Phase 4 — 匿名集扩大（工作量：中）
  ├── 固定面额混合池合约
  ├── 池间路由（池地址 ≠ 用户地址）
  └── 时序延迟策略（防时序关联）
  效果：发送方匿名性，匿名集 = 同面额存款人数
```

---

## 6.2 核心设计决策

**最重要的复用点**

DKG 已在每个分片委员会分发 alt_bn128 Fr 域秘钥份额。ElGamal 阈值解密与 BLS 阈值签名**数学结构完全相同**（均为 Lagrange 插值重建线性组合）。`ReconstructAndVerifyThresSign()` 框架可**零成本复用**于阈值解密，仅需添加 G1 方向的 `common_pk_G1`。

**最大工程挑战**

1. **ZK 电路**：Groth16 电路需 Rust/C++ 实现，Trusted Setup 需多方仪式（或改 PLONK 通用 setup）
2. **Shadow 合约状态迁移**：从 `mapping` 余额模型迁移到 Merkle 承诺树，需精心设计与 `to_tx_local_item.cc` 懒部署逻辑的兼容
3. **移动端证明体验**：5-15s 证明时间需通过证明代理服务解决

**消息类型关系**

隐私转账是 `kConsensusLocalTos` 的新子类型（`is_shielded = true`）。现有路由分发逻辑（`block_manager.cc HandleCrossShardBaseTx`）不变，仅在 `to_tx_local_item.cc` 处理阶段分叉：明文转账走原有路径，隐私转账走阈值解密→ZK 验证新路径。

---

## 6.3 安全性汇总

| 威胁模型 | 防御机制 | 强度 |
|---------|---------|------|
| 发送方关联 | Nullifier 不暴露来源 Note；混合池匿名集 | 依赖匿名集大小 |
| 接收方识别 | 一次性 Stealth Address，每笔不同 | 强（信息论安全） |
| 金额推断 | Pedersen 承诺 + Bulletproofs | 强（计算安全） |
| 跨分片关联 | ElGamal 加密，路由层全程密文 | 强（依赖 DKG 阈值 t） |
| 双花 | Nullifier 集合 + ZK 约束 C3 | 强（计算安全） |
| 委员会串通 | 需 > t = ⌈2n/3⌉ 节点串通（= 攻破共识） | 同现有系统安全假设 |
| 无效 proof | Groth16 链上验证，BN254 安全参数 128 位 | 强 |
| 时序关联 | 混合池延迟策略，指数分布出款间隔 | 中（依赖混合池延迟参数） |
| Gas 关联 | SYSTEM_EXECUTOR 支付 Gas，接收方无需预持代币 | 强 |

---

## 6.4 定理汇总

| 定理/引理 | 内容 | 依赖假设 |
|---------|------|---------|
| Pedersen 隐藏性 | 完美隐藏（信息论） | 无 |
| Pedersen 绑定性 | 计算绑定 | DL |
| Nullifier 不可碰撞 | 1/2^256 碰撞概率 | ROM |
| Nullifier 不可预测 | 不知 sk 无法预计算 | DL + ROM |
| **定理 1** | 阈值 ElGamal IND-CPA 安全 | DDH，t-1 腐化 |
| **定理 2** | 隐私安全与共识安全假设等价 | BFT 安全模型 |
| **定理 3** | 端到端跨分片隐私（主定理） | DDH + DL + q-SDH + ROM |

---

## 6.5 最终结论

> **核心结论 1**：没有分片架构，隐私交易完全可以实现（Tornado Cash、Zcash 已证明）。但存在三个结构性问题在单链上无法克服：Gas 关联攻击、观察域无法分离、阈值解密需独立 MPC 基础设施。
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
| 客户端准备 | 签名（<1ms） | 选 Note + 生成 ZK proof + ElGamal 加密（1-2s） |
| 节点执行路径 | 直接更新余额 | 阈值解密 → 验证 ZK proof → 更新承诺树 |

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
          │    Step 5: ElGamal 加密 Note 载荷                 │
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
| ElGamal C1, C2 | 本地加密后 → 广播 | ✅ 密文形式 |
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

形式化：设 E_shield = ShieldDeposit 事件，E_transfer = ShieldedCrossTransferOut 事件。外部观察者可见 E_shield（源分片公开）和 E_transfer（源分片公开），但 E_transfer 中的目标分片 ID 和接收方通过 ElGamal 加密，观察者无法知道最终去向。

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
                       │  • ElGamal 加密，路由全程密文                      │
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
定理 1（ElGamal IND-CPA）
    ↓ 被定理 3 混合论证使用
定理 2（隐私安全 ≡ 共识安全）
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
| 定理 1 | ElGamal 阈值 IND-CPA | DDH，t-1 腐化 |
| 定理 2 | 隐私安全 ≡ 共识安全 | BFT 安全模型 |
| 定理 3 | 端到端跨分片隐私 | DDH + DL + q-SDH + ROM |
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
