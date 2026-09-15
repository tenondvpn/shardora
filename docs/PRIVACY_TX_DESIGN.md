# 基于分片跨分片转账的隐私交易方案

> 基于 Shardora/Akaverse 分片架构，结合现有 BLS DKG、Feistel 影子地址、CrossShardBase 跨片转账机制设计

---

## 一、现有架构隐私缺陷分析

当前跨分片转账在以下层面完全暴露信息：

| 暴露点 | 具体内容 | 代码位置 |
|--------|---------|---------|
| `CrossTransferOut` 事件 | `from`（发送方）、`to`（接收方）、`amount`（金额）均明文索引 | `shardora_host.cc emit_log()` |
| `ToTxMessageItem` protobuf | `base_root_address`、目标地址、`amount256`、目标分片/池 | `pools.proto`，`block.proto` |
| Feistel 影子地址 | 给定 `base_addr + (shard, pool)` 任何人可推导 `shadow_addr` | `reversible_feistel_address.h` |
| 跨片 nonce | 序列计数器泄露转账时序和频率 | `contract_call.cc` |
| Shadow 合约余额 | `totalSupply` 和 `_balances` 链上可读 | 目标分片 EVM 状态 |

**根本问题**：`CrossTransferOut(base, from, to, amount, nonce, toShard, toPool)` 将完整的资金流图写入链上日志，任何观察者都能还原完整转账关系图。

---

## 二、隐私目标

| 目标 | 定义 |
|------|------|
| **发送方匿名性** | 观察者无法确定哪个地址发起了转账（匿名集 = 混合池所有存款者） |
| **接收方隐私** | 链上不出现真实接收地址 |
| **金额保密** | 转账金额对第三方不可见 |
| **跨分片关联性切断** | 无法将源分片的转出与目标分片的转入关联 |
| **防双花** | 在不暴露身份的情况下保证每笔隐私余额只能花一次 |

---

## 三、核心密码学原语

### 3.1 复用现有基础设施

系统已有 **alt_bn128（BN254）曲线** + **DKG 阈值密钥**，可直接复用：

| 现有能力 | 复用方式 |
|---------|---------|
| `libff::alt_bn128_G1/G2` 群运算 | Pedersen 承诺、ElGamal 加密 |
| `libff::alt_bn128_GT` 配对 | Groth16 proof 链上验证 |
| DKG 生成的 `local_sk_`（Fr 份额） | ElGamal 阈值解密份额（数学结构与 BLS 签名份额相同） |
| `ReconstructAndVerifyThresSign` Lagrange 插值框架 | 阈值解密重建（零修改复用） |
| `common_pk`（G2 公共公钥） | ElGamal 加密密钥 |

> **关键洞察**：ElGamal 阈值解密和 BLS 阈值签名的数学结构完全相同，都是对 Fr 域份额的 Lagrange 插值重建。DKG 已分发的密钥份额可同时用于两个用途，无需额外的密钥生成协议。

### 3.2 新增密码学原语

| 原语 | 用途 | 曲线 |
|------|------|------|
| **Pedersen 承诺** | 隐藏金额：`C = r·G + v·H` | alt_bn128 G1 |
| **Bulletproofs 范围证明** | 证明 `v ∈ [0, 2^64)` 而不揭露 `v` | alt_bn128 G1 |
| **一次性地址（Stealth Address）** | 隐藏接收方真实身份 | alt_bn128 G1 |
| **Nullifier（作废符）** | 防双花，类 Zcash 方案 | Hash（Poseidon/keccak256） |
| **Groth16 零知识证明** | 证明整体转账合法性 | alt_bn128（支持配对） |
| **ElGamal 阈值加密** | 跨分片消息加密，由目标分片委员会集体解密 | alt_bn128 G1 |

---

## 四、整体架构

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
│  路由目标 shard/pool 由 new_cm 中嵌入的目标坐标决定（加密前写入）          │
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
│                            接收方                                         │
│  用 view_sk 扫描链上 ephemeral_pk，匹配自己的 Note                        │
│  用 spend_sk 生成 Nullifier，花费 Note                                   │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 五、详细流程设计

### 5.1 隐私余额模型（替代 `_balances`）

当前 Shadow 合约使用 `mapping(address => uint256) _balances`，改为 **UTXO-like 承诺模型**：

**Note 定义**：
```
Note = (amount: uint64, randomness: Fr, spend_pk: G1)
Commitment = Pedersen(amount, randomness, spend_pk)
           = amount·H + randomness·G        # G, H 是 alt_bn128 G1 的独立生成元
```

**Shadow 合约新增状态**：

| 状态 | 类型 | 说明 |
|------|------|------|
| `cm_tree_root` | bytes32 | 承诺 Sparse Merkle 树根（代替 `totalSupply` 语义） |
| `notes[bytes32]` | mapping(bytes32 → bool) | 已存在的承诺集合 |
| `nullifiers[bytes32]` | mapping(bytes32 → bool) | 已花费的 Note 作废符集合 |
| `vk` | struct | Groth16 验证密钥（部署时写入） |

**Nullifier 定义**（防双花）：
```
nullifier = Hash(spending_key ∥ commitment)
```
花费时公开 `nullifier`，合约拒绝重复的 `nullifier`。观察者看到 `nullifier` 但无法反推是哪个 `commitment`（单向哈希），也无法关联到持有者。

### 5.2 一次性地址（Stealth Address）

接收方发布 `(spend_pk, view_pk)`，均为 alt_bn128 G1 点。

**发送方为每笔转账生成一次性地址**：
```
r       ← random Fr
epk     = r · G                        # 临时公钥，写入加密载荷
shared  = r · view_pk                  # ECDH 共享点
stealth_addr = Hash(shared) · G + spend_pk
```

**接收方扫链**：
```
对链上每笔交易的 epk：
  shared'    = view_sk · epk           # = view_sk · r · G = r · view_pk
  candidate  = Hash(shared') · G + spend_pk
  if candidate == stealth_addr → 此 Note 属于自己
```

**花费**：
```
# 需要 spend_sk
spend_key_for_note = Hash(shared) + spend_sk   # 对应 stealth_addr 的私钥
nullifier = Hash(spend_key_for_note ∥ commitment)
```

**效果**：每笔收款使用不同的链上地址，完全无法关联到接收方或同一接收方的多笔收款。

### 5.3 ZK 证明电路（Groth16 on alt_bn128）

每笔隐私跨分片转账需生成一个 Groth16 证明，电路约束如下：

**公开输入（写入链上）**：
```
old_nullifier     # 花费的 Note 的作废符
old_cm_root       # 花费时的承诺树根（保证 Note 存在）
new_cm_send       # 转往目标分片的新承诺
new_cm_change     # 找零承诺（留在源分片）
value_binding     # Pedersen 承诺 v·H（用于绑定范围证明）
```

**私有输入（不上链）**：
```
old_note          # (amount, randomness, spend_pk)
old_cm_path       # Merkle 路径（证明 old_cm 在树中）
spending_key      # 花费私钥
epk               # 临时公钥（用于 stealth address 生成）
new_note_send     # (amount_send, randomness_send, stealth_addr)
new_note_change   # (amount_change, randomness_change, spend_pk)
```

**电路约束**：

| 约束编号 | 内容 |
|---------|------|
| C1 | `old_cm = Pedersen(old_note.amount, old_note.randomness, old_note.spend_pk)` |
| C2 | `old_cm` 在以 `old_cm_root` 为根的 Merkle 树中（路径验证） |
| C3 | `old_nullifier = Hash(spending_key ∥ old_cm)` |
| C4 | `spending_key · G = old_note.spend_pk`（知道花费私钥） |
| C5 | `amount_send + amount_change = old_note.amount`（守恒） |
| C6 | `amount_send ≥ 0`，`amount_change ≥ 0`（范围约束，via Bulletproofs 绑定） |
| C7 | `new_cm_send = Pedersen(new_note_send)` |
| C8 | `new_cm_change = Pedersen(new_note_change)` |
| C9 | `value_binding = amount_send · H`（绑定范围证明到公开承诺） |

**链上验证**：合约调用 alt_bn128 配对预编译（`bn256Pairing`）验证 Groth16 proof，gas 成本固定（与 Tornado Cash 相近，约 1M gas 量级，可接受）。

### 5.4 跨分片加密载荷设计

发送方用目标分片委员会 `common_pk`（DKG 生成的 G2 点，存在 `ElectItem` 中）加密载荷：

**ElGamal 加密（alt_bn128 G1）**：
```
k       ← random Fr
C1      = k · G            # G1 点，临时密钥
C2      = M + k · common_pk_G1    # G1 点，加密内容
```

其中 `common_pk_G1` 是将 `common_pk`（G2）对应的 G1 加密公钥（在 DKG 时同步生成，见下节）。

**加密的消息 M** 编码：
```
M = AES_KeyPoint(stealth_addr ∥ amount ∥ randomness_send)
  = Hash(stealth_addr ∥ amount ∥ randomness_send) · G
```
实际上用 `Hash(...) · G` 作为对称密钥种子，再用 AES-GCM 加密 Note 明文，`C2` 仅承载 AES 密钥的加密形式。

**ToTxMessageItem 新增字段**：

```protobuf
message ToTxMessageItem {
  // 原有字段保留（routing 用）
  optional uint32 sharding_id = ...;
  optional uint32 pool_index = ...;

  // 隐私转账新增字段
  optional bytes nullifier = 20;         // 32 bytes，作废符
  optional bytes new_commitment = 21;    // 32 bytes，新承诺
  optional bytes elgamal_c1 = 22;        // 64 bytes，G1 点
  optional bytes elgamal_c2 = 23;        // 64 bytes，G1 点
  optional bytes aes_ciphertext = 24;    // AES-GCM 密文
  optional bytes zk_proof = 25;          // Groth16 序列化
  optional bool  is_shielded = 26;       // true = 隐私转账
}
```

### 5.5 目标分片：委员会阈值解密

**数学过程**（与 BLS 阈值签名完全对称）：

```
# 每个委员节点 i 计算 ElGamal 解密份额
D_i = sk_i · C1           # sk_i 是 DKG 分发的 Fr 份额

# 收集 t 个份额后，用已有的 Lagrange 插值框架重建
D = Σ λ_i · D_i           # = (Σ λ_i · sk_i) · C1 = sk_master · C1

# 解密恢复 AES 密钥种子
M = C2 - D                # = M + k·common_pk - sk_master·k·G = M

# 用 M 解密 AES 密文，得到 (stealth_addr, amount, randomness_send)
```

**复用路径**：`Crypto::ReconstructAndVerifyThresSign()` 的核心是收集 G1 点份额 + Lagrange 插值，与上述过程完全一致。仅需在 `to_tx_local_item.cc` 中新增一个 `ShieldedDecryptAndCredit()` 函数，调用已有的插值逻辑。

**执行阶段**（解密成功后）：

1. 用合约内的 `vk` 验证 `zk_proof`（链上，每个节点独立验证，共识保证一致性）
2. 检查 `new_cm` 不在目标分片承诺树中（防重入）
3. 调用 `systemExecuteShieldedCredit(stealth_addr, new_cm)` → 将 `new_cm` 插入 Merkle 树
4. 发出 `ShieldedCrossTransferIn(new_cm, tree_root)` 事件（不暴露 stealth_addr，接收方离线扫描）

### 5.6 ElGamal G1 公钥的 DKG 扩展

DKG 已分发 `local_sk_`（Fr 份额）和 `common_pk_`（G2 公共公钥）。为支持 ElGamal 加密，需额外在 **G1** 上生成对应公钥：

```
common_pk_G1 = sk_master · G1   # G1 生成元上的主公钥
```

在 `BlsDkg::FinishBroadcast()` 中，Feldman VSS 验证向量已经包含 `a_{i,0}·G2`（即 `V_i[0]`）。只需同样计算 `a_{i,0}·G1` 并广播，聚合得到 `common_pk_G1 = Σ a_{i,0}·G1`。这不引入新的安全假设，与现有 DKG 一致。

---

## 六、匿名集增强：分片内固定面额混合池

单独的承诺模型仍可能通过金额关联发送方和接收方。增加**固定面额混合池**：

**面额集合**：`{1, 10, 100, 1000, 10000}` token（或更细粒度）

**存款流程**：
```
1. 用户将明文余额碎成固定面额
2. 对每个面额生成 Note commitment
3. 调用 PrivacyPool.deposit(commitment)
4. 合约将 commitment 插入 Merkle 树，扣除明文余额
```

**取款流程**：
```
1. 用户生成 ZK 证明（知道树中某个 Note 的 spending_key）
2. 提交：PrivacyPool.withdraw(nullifier, zk_proof, recipient)
3. 合约验证 proof，检查 nullifier 未使用
4. 向 recipient 铸造同面额明文余额（recipient 可以是全新地址）
```

**跨分片使用**：存款 → 混合池跨分片发送（池到池，金额固定，无法区分用户） → 目标池取款。观察者只能看到池地址之间的固定面额流动，无法关联到个人。

---

## 七、完整隐私流程时序图

```
用户 A（发送）                    链上/路由层                    用户 B（接收）
    │                                │                                │
    │ 1. 生成 stealth_addr_B         │                                │
    │    (用 B 的 view_pk)           │                                │
    │                                │                                │
    │ 2. 构造 Note_send, Note_change │                                │
    │    生成 Groth16 proof          │                                │
    │                                │                                │
    │ 3. 用 dest_shard.common_pk_G1  │                                │
    │    ElGamal 加密 Note_send      │                                │
    │                                │                                │
    │ 4. 调用 PrivacyShadow.spend()  │                                │
    │    提交 nullifier, new_cm,     │                                │
    │    (C1,C2), zk_proof           │                                │
    │                                │                                │
    │             源分片共识打包     │                                │
    │             ──────────────►   │                                │
    │                            ShieldedCrossTransferOut             │
    │                            (nullifier, new_cm, C1, C2, proof)  │
    │                                │                                │
    │                            路由至目标分片                       │
    │                                │                                │
    │                         目标分片委员会阈值解密                   │
    │                         D_i = sk_i · C1                         │
    │                         重建 D = Σ λ_i·D_i                     │
    │                         解密 M = C2 - D                         │
    │                         验证 zk_proof                           │
    │                         ──────────────────────────────────►    │
    │                         systemExecuteShieldedCredit(cm)         │
    │                         插入目标分片 Merkle 树                  │
    │                                │                                │
    │                            ShieldedCrossTransferIn(new_cm)      │
    │                                │  ◄─────────────────────────── │
    │                                │  B 用 view_sk 扫链匹配 Note   │
    │                                │  B 用 spend_sk 可花费 Note    │
```

---

## 八、与现有代码的集成点

| 现有文件 | 改动性质 | 说明 |
|---------|---------|------|
| `src/shardoravm/shardora_host.cc` `emit_log()` | 新增 case | 拦截 `ShieldedCrossTransferOut` 主题 hash，解析 `(nullifier, new_cm, C1, C2, zk_proof)`，构造新的 `kShieldedTransfer` 类型 pending action |
| `src/shardoravm/host_journal_stack.h` `CrossShardPendingAction` | 扩展 | 新增 `kShieldedTransfer` 枚举值，字段从 `(to, amount)` 改为 `(nullifier, new_cm, encrypted_payload)` |
| `src/consensus/zbft/contract_call.cc` ~L431 | 修改 | shielded action 转 `ToTxMessageItem` 时写密文字段，不写 `amount/des` 明文 |
| `src/protos/pools.proto` `ToTxMessageItem` | 添加字段 | `nullifier`, `new_commitment`, `elgamal_c1/c2`, `aes_ciphertext`, `zk_proof`, `is_shielded` |
| `src/consensus/zbft/to_tx_local_item.cc` | 主要改动 | 新增 `ShieldedDecryptAndCredit()` 函数，调用阈值解密→验证 proof→执行 shielded credit |
| `src/bls/bls_dkg.cc` `FinishBroadcast()` | 小扩展 | 额外计算并广播 `common_pk_G1`（G1 点），存入 `ElectItem` |
| `src/consensus/hotstuff/elect_info.h` `ElectItem` | 添加字段 | `libff::alt_bn128_G1 common_pk_g1_` |
| Shadow 合约 Solidity | 新合约 | `PrivacyCrossShardBase`：承诺 Merkle 树 + Nullifier 集合 + Groth16 verifier + `systemExecuteShieldedCredit()` |

---

## 九、安全性分析

| 威胁模型 | 防御机制 | 强度 |
|---------|---------|------|
| 发送方关联 | Nullifier 不暴露来源 Note；混合池匿名集 | 依赖匿名集大小 |
| 接收方识别 | 一次性 Stealth Address，每笔不同 | 强（信息论安全） |
| 金额推断 | Pedersen 承诺 + Bulletproofs | 强（计算安全） |
| 跨分片关联 | ElGamal 加密，只有委员会可解密；链上仅见承诺 hash | 强（依赖 DKG 阈值 t） |
| 双花 | Nullifier 集合 + ZK 约束 C3 | 强（计算安全） |
| 委员会串通解密 | 需 > t = ⌈2n/3⌉ 节点串通，与 BFT 阈值一致 | 同现有系统安全假设 |
| 无效 proof 攻击 | Groth16 链上验证，BN254 安全参数 128 位 | 强 |
| 重放攻击 | Nullifier 全局唯一；一次性 epk | 强 |
| 时序关联 | 混合池统一延迟出款；跨分片 nonce 改为随机 | 中（依赖混合池延迟策略） |

---

## 十、分阶段实施路径

```
Phase 1 — 接收方隐私
  目标：隐藏接收方地址
  工作量：低
  ├── 实现 Stealth Address 生成和扫描库
  ├── Shadow 合约新增 ephemeral_pk 字段
  └── 金额和发送方仍明文
  效果：链上不出现接收方真实地址

Phase 2 — 金额隐私（分片内）
  目标：隐藏分片内转账金额
  工作量：中
  ├── 实现 Pedersen 承诺 + Bulletproofs（可用现有 libff）
  ├── 替换 Shadow 合约 _balances 为承诺 UTXO 模型
  ├── ZK 电路覆盖分片内花费
  └── Groth16 trusted setup（需仪式或 Groth16-universal）
  效果：分片内金额隐藏，UTXO 模型生效

Phase 3 — 跨分片完整隐私
  目标：切断跨分片关联
  工作量：高
  ├── DKG 扩展输出 common_pk_G1（ElGamal 加密密钥）
  ├── ToTxMessageItem 改为加密载荷
  ├── to_tx_local_item.cc 新增阈值解密流程
  └── ZK 电路扩展支持跨分片转账证明
  效果：跨分片金额/接收方完全隐藏

Phase 4 — 匿名集扩大
  目标：隐藏发送方
  工作量：中
  ├── 固定面额混合池合约
  ├── 池间路由（池地址 ≠ 用户地址）
  └── 时序延迟策略（防时序关联）
  效果：发送方匿名性，匿名集 = 同面额存款人数
```

---

## 十一、核心设计决策总结

**最重要的复用点**

DKG 在每个分片委员会已分发 alt_bn128 Fr 域秘钥份额，ElGamal 阈值解密与 BLS 阈值签名**数学结构完全相同**（均为 Lagrange 插值重建线性组合）。`ReconstructAndVerifyThresSign()` 框架可零成本复用于阈值解密，仅需添加 G1 方向的 `common_pk_G1`。

**最大工程挑战**

1. **ZK 电路**：Groth16 电路需要 Rust/C++ 实现，Trusted Setup 需要多方参与仪式（或采用 PLONK 等通用 setup 方案绕开）。
2. **Shadow 合约状态迁移**：从 `mapping` 余额模型迁移到 Merkle 承诺树，需要精心设计存储布局和 SYSTEM_EXECUTOR 调用接口，确保与现有懒部署逻辑（`to_tx_local_item.cc`）兼容。
3. **Trusted Setup**：Groth16 需要特定电路的可信设置，建议采用 [Zcash Powers of Tau](https://github.com/zcash/powersoftau) 输出 + 电路专属二阶段仪式，或改用 PLONK（通用 setup，无需重复仪式）。

**与现有 `kConsensusLocalTos` 消息类型的关系**

隐私转账是 `kConsensusLocalTos` 的一个新子类型（`is_shielded = true`）。现有的路由分发逻辑（`block_manager.cc HandleCrossShardBaseTx`）不变，仅在 `to_tx_local_item.cc` 的处理阶段分叉：明文转账走原有路径，隐私转账走阈值解密→ZK 验证新路径。

---

*文档版本：2026-09-15*
*基于分支：xl0616*
*涉及核心模块：shardoravm、consensus/zbft、bls、protos*
