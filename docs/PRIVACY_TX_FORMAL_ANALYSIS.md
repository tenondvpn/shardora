# 隐私交易方案：形式化理论分析与业界方案完整对比

> 本文对 Shardora 分片隐私交易方案进行形式化安全分析，给出完整性/可靠性/零知识性证明框架，并与 17 个业界方案进行系统性对比

---

## 一、形式化安全定义

### 1.1 符号约定

```
λ           安全参数（典型取值 128 或 256 bit）
negl(λ)     可忽略函数：对任意多项式 p，∃ N, ∀λ>N: negl(λ) < 1/p(λ)
PPT         概率多项式时间
G₁, G₂     BN254 椭圆曲线群（分别对应 alt_bn128_G1/G2）
G_T         目标群（配对输出）
e: G₁×G₂→G_T  双线性配对
g, h        G₁ 的两个独立生成元（离散对数关系未知）
Fr          BN254 标量域（|Fr| ≈ 2^254）
H: {0,1}*→Fr  密码学哈希函数（Poseidon，zk-friendly）
```

### 1.2 密码学困难假设

**假设 1（离散对数，DL）**：对任意 PPT 算法 A，
```
Pr[A(G₁, g, a·g) = a : a ←$ Fr] ≤ negl(λ)
```

**假设 2（计算 Diffie-Hellman，CDH）**：对任意 PPT 算法 A，
```
Pr[A(G₁, g, a·g, b·g) = ab·g : a,b ←$ Fr] ≤ negl(λ)
```

**假设 3（判定 Diffie-Hellman，DDH）**：以下两个分布对 PPT 算法计算不可区分：
```
(g, a·g, b·g, ab·g) ≈_c (g, a·g, b·g, c·g)    a,b,c ←$ Fr
```

**假设 4（q-SDH，q-Strong Diffie-Hellman）**：给定
`(g, τ·g, τ²·g, ..., τ^q·g) ∈ G₁^{q+1}`，
对任意 PPT 算法 A：
```
Pr[A → (c, 1/(τ+c)·g) : c ∈ Fr] ≤ negl(λ)
```
Groth16 可靠性依赖此假设。

**假设 5（随机预言机模型，ROM）**：将 Poseidon 哈希建模为随机预言机，用于 Nullifier 和 Merkle 树的分析。

### 1.3 隐私属性形式化定义

#### 定义 1：承诺方案（Commitment Scheme）

一个承诺方案 `Com = (Setup, Commit, Verify)` 满足：

- **隐藏性（Hiding）**：对任意 PPT 敌手 A，
  ```
  |Pr[A(Com(m₀; r)) = 1] - Pr[A(Com(m₁; r)) = 1]| ≤ negl(λ)
  ```
  其中 `m₀, m₁` 是 A 选择的任意两个消息，`r ←$ Fr` 是随机数。

- **绑定性（Binding）**：对任意 PPT 敌手 A，
  ```
  Pr[A → (m, m', r, r') : m≠m' ∧ Com(m;r) = Com(m';r')] ≤ negl(λ)
  ```

**Pedersen 承诺的安全性**：
```
C = v·h + r·g    v ∈ [0, 2^64], r ←$ Fr
```
- **完美隐藏**：对任意 v，均匀随机 r 使得 C 在 G₁ 上均匀分布，因此是信息论意义上的完美隐藏。
- **计算绑定**：若存在 v≠v', r, r' 使 `v·h + r·g = v'·h + r'·g`，则 `(v-v')·h = (r'-r)·g`，即 `logg(h) = (r'-r)/(v-v')`，违反 DL 假设。

#### 定义 2：零知识证明系统（ZK Proof System）

一个证明系统 `(P, V)` 对关系 `R = {(x, w) : C(x,w)=1}` 满足：

- **完备性（Completeness）**：对任意 `(x, w) ∈ R`，
  ```
  Pr[V(x, P(x,w)) = 1] = 1
  ```

- **可靠性（Soundness）**：对任意 PPT 作弊证明者 P*，对所有 `x ∉ L_R`，
  ```
  Pr[V(x, P*(x)) = 1] ≤ negl(λ)
  ```
  Groth16 依赖 q-SDH 假设，在知识可靠性（Knowledge Soundness）下成立。

- **零知识性（Zero-Knowledge）**：存在 PPT 模拟器 Sim，对任意 `(x,w) ∈ R` 和任意 PPT 区分器 D，
  ```
  |Pr[D(x, P(x,w)) = 1] - Pr[D(x, Sim(x)) = 1]| ≤ negl(λ)
  ```
  即证明不泄露证人 w 的任何信息。

#### 定义 3：隐私交易方案

一个隐私交易方案 `Π = (Setup, KeyGen, Deposit, Spend, Verify)` 定义在分片集合 `S = {(s,p) : s ∈ Shards, p ∈ [0,31]}` 上。

- **发送方匿名性（Sender k-Anonymity）**：对任意 PPT 敌手 A，对任意匿名集 S 满足 |S| = k，
  ```
  Pr[A(Transcript) = "谁发送了转账"] ≤ 1/k + negl(λ)
  ```
  其中 Transcript 为链上所有可观察数据。

- **接收方不可追踪性（Receiver Unlinkability）**：对任意 PPT 敌手 A，不持有 view_sk 时，
  ```
  Pr[A(stealth_addr, spend_pk, view_pk) → "确认接收方为 spend_pk"] ≤ negl(λ)
  ```

- **金额保密性（Amount Confidentiality）**：对任意 PPT 敌手 A，
  ```
  |Pr[A(C) = v : C = v·h + r·g] - 1/|Fr|| ≤ negl(λ)
  ```

- **防双花（Double-Spend Resistance）**：对任意 PPT 敌手 A，
  ```
  Pr[A → (π₁, nul, π₂, nul) : V(π₁)=1 ∧ V(π₂)=1 ∧ π₁≠π₂] ≤ negl(λ)
  ```
  即同一 Nullifier 不能被两个不同的合法证明引用。

---

## 二、ZK 电路形式化规范

### 2.1 Note 结构与 Nullifier

```
Note := (v: u64, r: Fr, pk_spend: G₁)

Commitment:  cm  = Pedersen(v, r, pk_spend)
                 = v·h + r·g + H(pk_spend)·g₂    （三分量承诺）

Nullifier:   nul = H(sk_spend ∥ cm)              sk_spend·g = pk_spend
```

**引理 1（Nullifier 不可碰撞性）**：在 ROM 假设下，对任意两个不同的 Note `(v,r,pk)` 和 `(v',r',pk')`，
```
Pr[H(sk_spend ∥ cm) = H(sk_spend ∥ cm')] ≤ 1/2^λ
```

**引理 2（Nullifier 不可预测性）**：不知道 sk_spend 的敌手无法预计算 nul：
```
Pr[A(cm, pk_spend) = H(sk_spend ∥ cm)] ≤ negl(λ)   (DL 假设 + ROM)
```

### 2.2 Merkle 树规范

使用 Sparse Merkle Tree（SMT），深度 d = 20，叶节点容量 2^20 ≈ 100万。

```
leaf_hash(cm) = H(cm ∥ "leaf")
internal_hash(l, r) = H(l ∥ r ∥ "node")
root = SMT_root({cm_i})
```

**成员证明（Membership Proof）**：路径 π_merkle = {sibling_i}_{i=1}^d，满足：
```
H(H(H(...H(H(cm, sibling_1), sibling_2)...), sibling_d) = root
```

### 2.3 电路约束系统（R1CS）

令 `x = (nul, root, cm_out, cm_change, v_commit)` 为公开输入，
`w = (v, r, pk_spend, sk_spend, π_merkle, v_out, r_out, pk_stealth, v_change, r_change)` 为私有见证。

约束系统 `C(x, w) = 1` 当且仅当以下所有约束同时满足：

```
C1:  cm_in = v·h + r·g + H(pk_spend)·g₂           （输入承诺一致性）
C2:  SMT_Verify(root, cm_in, π_merkle) = 1          （成员性）
C3:  sk_spend·g = pk_spend                           （私钥知识）
C4:  nul = H(sk_spend ∥ cm_in)                      （Nullifier 正确性）
C5:  v_out + v_change = v                            （守恒律）
C6:  v_out ∈ [0, 2^64)                              （范围约束）
C7:  v_change ∈ [0, 2^64)                           （范围约束）
C8:  cm_out = v_out·h + r_out·g + H(pk_stealth)·g₂ （输出承诺）
C9:  cm_change = v_change·r_change·g + H(pk_spend)·g₂ （找零承诺）
C10: v_commit = v_out·h                              （绑定范围证明）
```

**约束规模**（以 Poseidon hash 实现，每个 hash ~250 约束）：

| 约束组 | R1CS 约束数 |
|--------|------------|
| C1：Pedersen 承诺（×3 份量） | 750 |
| C2：Merkle 路径（深度 20）× Poseidon | 20 × 500 = 10,000 |
| C3：EC 点乘（标量乘法，256 步） | 4,000 |
| C4：Nullifier hash | 500 |
| C5：算术守恒 | 10 |
| C6-C7：范围约束（64-bit 二进制分解） | 2 × 2,000 = 4,000 |
| C8-C9：输出承诺（×2） | 1,500 |
| C10：绑定承诺 | 250 |
| **合计** | **~21,010** |

### 2.4 Groth16 证明生成与验证

**证明生成（Prover 算法）**：

给定 CRS（Common Reference String）`σ = (α,β,γ,δ,τ 的 G₁/G₂ 编码)`，

```
π = (A, B, C) ∈ G₁ × G₂ × G₁

A = α·g₁ + Σᵢ aᵢ·(βuᵢ(τ)+αvᵢ(τ)+wᵢ(τ))/δ·g₁ + r·δ·g₁
B = β·g₂ + Σᵢ aᵢ·vᵢ(τ)·g₂ + s·δ·g₂
C = Σᵢ∈private aᵢ·(βuᵢ(τ)+αvᵢ(τ)+wᵢ(τ))/δ·g₁ + h(τ)·t(τ)/δ·g₁ + sA + rB - rsδ·g₁
```

复杂度：`O(|C| log|C|)` 次 G₁ 标量乘法（Multi-Scalar Multiplication，MSM）

**验证（Verifier 算法）**：

```
e(A, B) = e(α·g₁, β·g₂) · e(Σᵢ∈public xᵢ·(βuᵢ+αvᵢ+wᵢ)/γ·g₁, γ·g₂) · e(C, δ·g₂)
```

复杂度：**O(1) 次配对**（3 次，与电路规模无关）

这是 Groth16 相比 PLONK 的核心优势：验证代价恒定，不随电路复杂度增长。

---

## 三、ElGamal 阈值加密的形式化分析

### 3.1 方案定义

设 DKG 输出主私钥份额 `{sk_i}_{i=1}^n ⊂ Fr`，满足 Shamir 结构：
```
sk_master = f(0)，其中 f 是 t-1 阶多项式，sk_i = f(i)
```

**ElGamal 加密**（G₁ 上）：
```
KeyGen:   pk_G1 = sk_master · g   （G₁ 主公钥，由 DKG 扩展生成）
Encrypt:  k ←$ Fr
          C₁ = k·g
          C₂ = M + k·pk_G1        （M ∈ G₁ 为消息点编码）
Decrypt:  D = sk_master·C₁ = sk_master·k·g = k·pk_G1
          M = C₂ - D
```

**阈值解密份额**：
```
D_i = sk_i · C₁    （节点 i 的部分解密）
```

**重建**（Lagrange 插值，直接复用现有 `ReconstructAndVerifyThresSign` 框架）：
```
λ_i = ∏_{j≠i} j/(j-i)    （Lagrange 系数）
D = Σᵢ∈S λᵢ·Dᵢ = (Σᵢ∈S λᵢ·skᵢ)·C₁ = f(0)·C₁ = sk_master·C₁
```

### 3.2 定理 1：阈值 ElGamal 的 IND-CPA 安全性

**定理 1**：若 DDH 假设在 G₁ 上成立，则对于任意腐化至多 t-1 个节点的 PPT 敌手 A，
```
Adv^{IND-CPA}_A ≤ 2 · Adv^{DDH}_{G₁}
```

**证明（归约）**：

设 A 能以优势 ε 破坏 t-of-n 阈值 ElGamal 的 IND-CPA 安全性，构造算法 B 攻击 DDH：

1. B 收到 DDH 挑战 `(g, ag, bg, c·g)`（其中 c = ab 或 c ←$ Fr）
2. B 模拟 DKG：生成 n 个 Shamir 份额使得主公钥为 `a·g`（即 `sk_master = a`，B 不知道 a）
   - 对 t-1 个被腐化节点 i，B 选择随机多项式 f 满足 `f(i)` 为对应份额，向 A 提供这些份额
   - 设置 `pk_G1 = a·g`（来自 DDH 挑战）
3. A 选择消息 `M₀, M₁`，B 生成挑战密文：
   - 设 `b = k`（来自 DDH 挑战的 b），`C₁ = b·g`，`C₂ = Mβ + c·g`（β ←$ {0,1}）
   - 若 `c = ab = sk_master·k`，则 `C₂ = Mβ + k·pk_G1` 是对 Mβ 的合法加密
   - 若 `c ←$ Fr`，则 `C₂` 对 A 是均匀随机（与 M₀, M₁ 无关）
4. 若 A 猜测正确，B 输出 "c = ab"；否则输出 "c 随机"

因此 `Pr[B 攻破 DDH] = |Pr[A 猜对] - 1/2| = ε/2`，即 `Adv^{DDH} ≥ ε/2`，故 `ε ≤ 2·Adv^{DDH}`。

### 3.3 定理 2：共识安全与隐私安全的假设对齐

**定理 2**：在 Shardora 模型中，破坏跨分片隐私与破坏 BFT 共识**需要完全相同的攻击能力**（均需腐化 ≥ t 个委员会节点）。

**证明**：

（方向1）若敌手可破坏隐私（即解密跨分片密文），则其腐化至少 t 个节点（由定理 1 的 IND-CPA 安全性逆否）。

（方向2）若敌手腐化至少 t 个节点，则其可重建 sk_master 并解密任意密文；同时，t ≥ ⌈2n/3⌉ 意味着其控制了 BFT 法定人数，BFT 安全性（safety）失效。

因此两者互为充要条件，安全假设完全对齐，隐私层不引入额外攻击面。□

---

## 四、端到端隐私的形式化证明

### 4.1 可观察视图的形式化定义

**定义 4（外部观察者视图）**：对于一次跨分片隐私转账，外部观察者 O（不持有任何委员会私钥份额）的视图为：
```
View_O = (nul, cm_out, C₁, C₂, π, route_hint)
```
其中：
- `nul`：Nullifier（32 字节哈希）
- `cm_out`：目标分片新承诺（32 字节）
- `(C₁, C₂)`：ElGamal 密文（两个 G₁ 点）
- `π`：Groth16 证明（~256 字节）
- `route_hint`：目标 (shard_id, pool_index)（明文，用于路由）

**引理 3（路由信息不构成泄露）**：目标 (shard_id, pool_index) 的暴露不破坏接收方隐私。

**证明**：shard/pool 是地址空间的粗粒度划分（32 个池 × 最多 1021 个分片 = ~32,000 个可能目标），在任意一个 pool 内有大量 Note 接收者，目标 shard/pool 的知晓仅将匿名集从全局缩小至单池匿名集，但不暴露具体接收方。□

### 4.2 引理 4：ElGamal 密文的语义安全

**引理 4**：在 DDH 假设下，`(C₁, C₂) = (k·g, M + k·pk_G1)` 不泄露 M 的任何信息：
```
(k·g, M + k·pk_G1) ≈_c (k·g, R)    R ←$ G₁
```

**证明**：若区分器 D 能区分上述两个分布，以优势 ε，构造 DDH 区分器：
- 给定 `(g, a·g, b·g, c·g)`，令 `pk_G1 = a·g`，`C₁ = b·g`
- 若 `c = ab`：`C₂ = M + c·g = M + k·pk_G1` 是合法加密（k = b）
- 若 `c ←$ Fr`：`C₂ = M + c·g` 对 D 而言均匀随机
- D 的区分优势直接转化为 DDH 优势 ε。□

### 4.3 引理 5：Groth16 证明的零知识性

**引理 5**：Groth16 证明 π 在知识提取意义下零知识：存在 PPT 模拟器 Sim，使得：
```
{π = P(x, w)} ≈_c {Sim(x)}    对所有 (x,w) ∈ R
```

**证明框架**：Groth16 的零知识性由随机化参数 r, s ←$ Fr 保证（证明生成时注入随机性），模拟器在不知道 w 的情况下可生成分布相同的假证明（利用模拟的 CRS）。标准引用：Groth 2016, "On the Size of Pairing-Based Non-interactive Arguments"。□

### 4.4 主定理：端到端隐私

**定理 3（端到端跨分片隐私）**：在 DDH + DL + q-SDH + ROM 假设下，对任意 PPT 外部观察者 O 和任意腐化至多 t-1 个节点的联合敌手 A，

（a）**发送方不可追踪**：A 无法以超过 `1/k + negl(λ)` 的概率识别匿名集（大小 k）中的实际发送者。

（b）**接收方不可追踪**：A 无法以超过 `negl(λ)` 的概率将 stealth_addr 关联到 spend_pk。

（c）**金额保密**：A 无法以超过 `negl(λ)` 的概率从 `(C₁, C₂)` 推断转账金额 v。

（d）**跨分片不可关联**：A 无法以超过 `negl(λ)` 的概率将源分片上的 nul 与目标分片上的 cm_out 关联到同一笔转账。

**证明梗概**（混合论证 Hybrid Argument）：

构造混合实验序列 `H₀, H₁, ..., H₄`：

- **H₀**：真实方案执行
- **H₁**：将 `(C₁, C₂)` 替换为均匀随机 G₁ 点（由引理 4，H₀ ≈_c H₁，优势差 ≤ 2·Adv^{DDH}）
- **H₂**：将 π 替换为 Sim(x) 生成的模拟证明（由引理 5，H₁ ≈_c H₂，优势差 ≤ negl(λ)）
- **H₃**：将 cm_out 替换为随机 G₁ 点（由 Pedersen 完美隐藏性，H₂ ≡ H₃ 完美不可区分）
- **H₄**：将 nul 替换为随机哈希（由引理 1/2 的随机预言机，H₃ ≈_c H₄，优势差 ≤ negl(λ)）

在 H₄ 中，View_O 全部为独立均匀随机值，不含任何关于发送者/接收者/金额/关联关系的信息。
由混合论证，任意 A 在真实 H₀ 中的攻击优势 ≤ 2·Adv^{DDH} + negl(λ)。□

---

## 五、攻击向量形式化分析

### 5.1 时序关联攻击（Timing Analysis）

**攻击模型**：敌手观察第 t₁ 时刻源分片的 nul 和第 t₂ 时刻目标分片的 cm_out，根据 Δt = t₂ - t₁ 推断关联性。

**正式分析**：

设混合池中所有 Note 的存款时序为 `{t_i}_{i=1}^k`，取款时序为 `{t'_j}_{j=1}^k`。

若所有用户立即取款，时序差 `Δt = t'_i - t_i` 与跨分片路由时间高度相关（约 25-27s），敌手通过 `Δt ≈ 27s` 即可以 `O(1/k)` 的错误率关联存取款。

**缓解方案**：引入时延随机化

```
实际取款时延 = 基础跨分片路由时延 + Δ_random
Δ_random ← Exp(μ)   指数分布，期望 μ = 300 s（可配置）
```

在指数分布延迟下，关联概率降至 `Pr[正确关联] ≤ 1/k + ε(μ)`，其中 `ε(μ) → 0` 随 μ 增大。

**代价**：E2E 延迟从 27s 增至 ~327s（可配置），对敌手而言关联攻击的优势降至与随机猜测相当。

### 5.2 金额图分析攻击（Amount Graph Analysis）

**攻击模型**：即使金额加密，若每笔转账固定面额，敌手可通过多笔交易的金额组合推断关联性。

**分析**：对于任意面额 v，在混合池中存在 n_v 个存款面额为 v 的 Note。敌手猜测"是哪个 v 面额的 Note 被花费"的先验概率为 1/n_v。

当 n_v 足够大（n_v ≥ 100），猜测优势 ≤ 1%。推论：面额池的匿名集大小 n_v 是核心安全参数。

### 5.3 Nullifier 碰撞攻击

**攻击模型**：构造两个不同 Note 产生相同 Nullifier，使其中一个无法被花费（拒绝服务）。

**分析**：需要 `H(sk₁ ∥ cm₁) = H(sk₂ ∥ cm₂)`，即哈希碰撞，在 ROM 下概率 ≤ 1/2^256，不可行。

### 5.4 跨分片前跑攻击（Cross-Shard Front-Running）

**攻击模型**：目标分片的恶意 Leader 看到 `(C₁, C₂, π)` 后，在解密前将自己插入取款队列。

**分析**：目标分片 Leader 无法单独解密密文（需 t 个节点协作）。在收集 t 个解密份额并完成 Lagrange 重建之前，Leader 不知道 `(stealth_addr, v)`，因此无法有针对性地前跑特定金额的取款。

**残余风险**：Leader 可延迟处理某笔隐私转账（审查，而非前跑）。缓解：HotStuff 的活性保证（liveness）确保合法 tx 最终被包含。

### 5.5 委员会内部人攻击

**攻击模型**：t 个委员会节点串通，重建 sk_master 并解密所有历史密文。

**分析**：这与攻击共识（forking）需要完全相同的资源（控制 ⌈2n/3⌉ 个节点）。这是系统的安全底线，任何去中心化共识系统都无法提供超过这个阈值的保证。

**与 Tornado Cash 对比**：Tornado Cash 无委员会，隐私完全依赖 ZK 数学，不存在此类攻击。代价是无法实现跨链隐私（定理 2 的另一面：零信任假设 ↔ 零跨链能力）。

---

## 六、复杂度形式化分析

### 6.1 计算复杂度

```
操作                              复杂度              具体估计（|C|=21,010）
────────────────────────────────────────────────────────────────────────────
Groth16 证明生成（MSM）          O(|C|log|C|)        ~300,000 G₁ 乘法
Groth16 验证                     O(1)                3 次配对
ElGamal 加密（用户侧）           O(1)                2 次 G₁ 乘法
ElGamal 部分解密（每节点）       O(1)                1 次 G₁ 乘法
ElGamal 重建（Leader）           O(t)                t 次 G₁ 乘法，t≈67
Merkle 路径更新（插入 Note）      O(d)                d=20 次 hash
Nullifier 查找                   O(1)                哈希表查找
────────────────────────────────────────────────────────────────────────────
```

### 6.2 通信复杂度

```
阶段                    消息大小          消息数
────────────────────────────────────────────────────────────────
用户 → 源分片           ~840 bytes        1
源分片广播 Propose      ~840 bytes        n（共识）
节点 → Leader (D_i)     64 bytes（G₁点）  t≈67（阈值解密）
跨分片路由              ~840 bytes        1
目标分片共识            ~840 bytes        n
────────────────────────────────────────────────────────────────
总通信量                                  O(n) 条消息，O(n·840) bytes
```

与普通跨分片转账相比，额外增加 t 条 64 字节的解密份额消息（共 ~4.3 KB），开销可忽略。

### 6.3 存储复杂度

```
Sparse Merkle Tree（深度 d=20）：
  最坏情况：O(2^d) 个节点 × 32 bytes = 32 MB/pool
  实际：Sparse 存储，O(N·d) × 32 bytes，N = 已插入 Note 数
  10万 Note：100,000 × 20 × 32 = 64 MB/pool

Nullifier Set：
  O(N) × 32 bytes，N = 已花费 Note 数
  10万已花费：3.2 MB/pool

相比普通余额存储（每地址 32 bytes）：
  放大因子 ≈ d = 20（Merkle 路径节点数）
```

---

## 七、业界 17 个方案完整对比

### 7.1 对比框架

**隐私保护方法分类**：
- **ZK-SNARK/STARK**：数学零知识证明
- **环签名（Ring Signature）**：隐藏真实签名者在环内
- **秘密共享（MPC/TEE）**：多方计算或可信硬件
- **混币（Mixer）**：打乱输入输出对应关系
- **隐身地址（Stealth Address）**：一次性接收地址
- **承诺（Commitment）**：隐藏金额

### 7.2 详细对比表

| # | 方案 | 发布年 | 隐私模型 | 发送方匿名 | 接收方隐私 | 金额隐藏 | 跨链隐私 | 智能合约 | 吞吐量 | 证明系统 | Trusted Setup | 量子安全 |
|---|------|--------|---------|-----------|-----------|---------|---------|---------|-------|---------|--------------|---------|
| 1 | **Tornado Cash** | 2019 | Mixer+ZK | ✅ 强 | ✅ 强 | ❌ 定额 | ❌ | ✅ EVM | 2.5 TPS | Groth16 | 电路专属 | ❌ |
| 2 | **Tornado Cash Nova** | 2022 | ZK-UTXO | ✅ 强 | ✅ 强 | ✅ 强 | ⚠️ 桥 | ✅ EVM | ~5 TPS | Groth16 | 电路专属 | ❌ |
| 3 | **Zcash Sapling** | 2018 | Groth16 JoinSplit | ✅ 强 | ✅ 强 | ✅ 强 | ❌ | ❌ | 5-10 TPS | Groth16 | MPC仪式 | ❌ |
| 4 | **Zcash Orchard** | 2021 | Halo2 | ✅ 强 | ✅ 强 | ✅ 强 | ❌ | ❌ | 10-20 TPS | PLONK/Halo2 | ✅ 无 | ❌ |
| 5 | **Monero** | 2014 | RingCT+Stealth | ✅ 中（环大小限制） | ✅ 强 | ✅ 强 | ❌ | ❌ | 30-50 TPS | Bulletproofs | ✅ 无 | ❌ |
| 6 | **Aztec v2** | 2023 | UltraPlonk | ✅ 强 | ✅ 强 | ✅ 强 | ⚠️ 桥 | ⚠️ Noir | ~100 TPS | PLONK | ✅ 通用 | ❌ |
| 7 | **Penumbra** | 2023 | ZK+DEX | ✅ 强 | ✅ 强 | ✅ 强 | ✅ IBC | ⚠️ 受限 | ~1000 TPS | Groth16 | 仪式 | ❌ |
| 8 | **Iron Fish** | 2023 | Groth16 | ✅ 强 | ✅ 强 | ✅ 强 | ❌ | 计划中 | ~20 TPS | Groth16 | MPC仪式 | ❌ |
| 9 | **Aleo** | 2024 | ZK原生L1 | ✅ 强 | ✅ 强 | ✅ 强 | ❌ | ✅ Leo | ~100 TPS | Marlin/AHP | ✅ 通用 | ❌ |
| 10 | **Railgun** | 2021 | 合约ZK | ✅ 强 | ✅ 强 | ✅ 强 | ⚠️ 桥 | ✅ EVM | ~10 TPS | Groth16 | 仪式 | ❌ |
| 11 | **Grin/MimbleWimble** | 2019 | CT+Cut-through | ✅ 强 | ❌（无地址） | ✅ 强 | ❌ | ❌ | ~1000 TPS | Bulletproofs | ✅ 无 | ❌ |
| 12 | **Privacy Pools**（Vitalik 2023） | 2023 | ZK+Compliance | ✅ 强 | ✅ 强 | ❌ 定额 | ❌ | ✅ EVM | ~2.5 TPS | Groth16 | 仪式 | ❌ |
| 13 | **Secret Network** | 2020 | TEE(SGX) | ✅ 强 | ✅ 强 | ✅ 强 | ✅ IBC | ✅ CosmWasm | ~100 TPS | 无ZK | 无 | ❌ |
| 14 | **Oasis Network** | 2020 | TEE+ZK | ✅ 强 | ✅ 强 | ✅ 强 | ⚠️ | ✅ EVM | ~1000 TPS | 无/TEE | 无 | ❌ |
| 15 | **Firo/Lelantus Spark** | 2023 | One-sided ZK | ✅ 强 | ✅ 强 | ✅ 强 | ❌ | ❌ | ~10 TPS | Sigma/Bulletproofs | ✅ 无 | ❌ |
| 16 | **Namada** | 2024 | MASP（多资产屏蔽池） | ✅ 强 | ✅ 强 | ✅ 强 | ✅ IBC | ❌ | ~100 TPS | PLONK/Sapling | ✅ 通用 | ❌ |
| 17 | **Shardora 分片隐私**（本方案） | - | ZK+阈值解密+Stealth | ✅ 强 | ✅ 强 | ✅ 强 | ✅ **原生** | ✅ EVM | **38,400 TPS** | Groth16 | 电路专属 | ❌ |

### 7.3 关键维度深度对比

#### 维度 A：跨链/跨分片隐私

```
方案类型    跨链隐私能力      根本原因
────────────────────────────────────────────────────────────────────────
单链 L1     ❌              所有 tx 在同一账本，桥接必须明文
L2 (Rollup) ⚠️ 桥接泄露     存款/取款在 L1 是明文事件
IBC 跨链    ✅ 有限          IBC packet 可携带屏蔽证明，但受制于目标链能力
TEE方案     ✅ 强（Secret）  SGX 在目标链执行，但信任 Intel
分片原生    ✅ 强（本方案）  路由层加密，委员会解密，无明文桥接点
────────────────────────────────────────────────────────────────────────
```

**Penumbra vs Shardora**：Penumbra 通过 IBC 实现跨链隐私，但 IBC packet 内容在中继链上是明文的，中继者可以看到目标链和金额。Shardora 的路由层密文从源分片到目标分片全程加密，路由节点只能看到目标 (shard_id, pool_id)，不能看到金额和接收方。

#### 维度 B：Trusted Setup 安全风险对比

| 类型 | 代表方案 | 风险说明 |
|------|---------|---------|
| 电路专属 MPC 仪式 | Tornado Cash, Zcash Sapling, Iron Fish | 若仪式中有一个参与者诚实则安全。历史上 Zcash Sapling 有 ~100 人参与，安全性高但不可验证 |
| 通用 SRS（Powers of Tau） | PLONK, Halo2, Aztec | 一次性通用仪式可被任意电路复用，降低攻击面 |
| 无 Trusted Setup | Bulletproofs, Halo2 Accumulation | 零信任假设，但证明更大或验证更慢 |
| 本方案（Groth16） | Shardora 隐私 | 同 Tornado Cash 模式，需专属仪式；**可升级至 PLONK 消除此风险** |

**注意**：TEE 方案（Secret/Oasis）无 ZK Trusted Setup，但信任 Intel/AMD 硬件，将密码学信任转化为工程信任，安全模型本质不同。

#### 维度 C：匿名集可扩展性

```
方案                匿名集大小             扩展方式
────────────────────────────────────────────────────────────────────────
Tornado Cash        全局存款用户数          不可扩展（固定合约）
Monero              环大小（固定 11）        不可扩展（环大小影响 tx 体积）
Zcash               全局 shielded 池        单链不可扩展
Railgun             合约部署链的存款用户    受限于单链
Aztec               L2 内存款用户           受 Sequencer 容量限制
Secret Network      每个合约内用户          不可扩展（容量受 SGX 内存）
Namada              MASP 内全部资产持有者   IBC 链数量线性扩展
Shardora 分片隐私   每 pool 存款用户        随分片数线性扩展（32×N pools）
────────────────────────────────────────────────────────────────────────
```

#### 维度 D：证明系统技术选型深度对比

| 证明系统 | 代表方案 | Proof大小 | 证明时间 | 验证时间 | Trusted Setup | 递归 | 量子安全 |
|---------|---------|----------|---------|---------|--------------|------|---------|
| Groth16 | Zcash Sapling, Tornado Cash, **本方案** | ~256 B（最小） | 1-3 s | O(1)配对（最快） | 电路专属 | ❌ | ❌ |
| PLONK | Aztec, ZKSync | ~800 B | 1-5 s | O(log n) | 通用 SRS | ✅ | ❌ |
| Halo2 | Zcash Orchard | ~1-2 KB | 2-5 s | O(log n) | ✅ 无 | ✅ | ❌ |
| Marlin/AHP | Aleo | ~1 KB | 1-3 s | O(√n) | 通用 SRS | ✅ | ❌ |
| Bulletproofs | Monero, Grin | O(log n) KB | O(n) | O(n) | ✅ 无 | ❌ | ❌ |
| STARKs | StarkNet | O(log² n) KB | O(n log n) | O(log² n) | ✅ 无 | ✅ | ✅ |

**本方案选 Groth16 的理由**：
1. Proof 大小最小（256B），直接决定 `kMaxProposeMsgBytes` 约束下的 Tx 容量
2. 链上验证 O(1) 配对，计算成本恒定（不随电路规模增长）
3. BN254 曲线已是本系统基础设施（BLS、DKG），复用现有 libff

**可升级路径**：若 Trusted Setup 成为障碍，可替换为 PLONK（同用 BN254，通用 SRS，Proof 体积增至 ~800B，TPS 从 1,200/block 降至 ~1,100/block，可接受）。

#### 维度 E：合规性与选择性披露对比

```
方案              合规接口                    监管友好度
────────────────────────────────────────────────────────────────────────
Tornado Cash      无（被制裁）                ❌❌
Zcash Sapling     Viewing Key（只读）         ⚠️ 可审计但 UI 差
Monero            View Key（接收方）          ⚠️ 发送方无法证明
Aztec             注册合规 Note              ⚠️ 需合规合约配合
Privacy Pools     Set Membership ZK Proof    ✅ 设计目标之一
Secret Network    合约定义访问控制            ✅ 灵活
Namada            Shielded Action + VK       ⚠️ 实现中
Shardora 分片隐私  view_sk + ZK 合规证明      ✅ 支持选择性披露
────────────────────────────────────────────────────────────────────────
```

**Vitalik 的 Privacy Pools（2023）方案专题**：

Privacy Pools 是 Buterin 等人提出的专门解决合规问题的隐私方案：用户在取款时同时提交一个 ZK 证明，证明"我的存款来自某个合规集合（Association Set），而不来自任何已知恶意地址"，而不暴露具体是哪笔存款。

本方案与 Privacy Pools 可组合：
- 用户的 Note 携带来源证明（Association Set Membership Proof）
- 在 ZK 电路 C10 后追加约束 C11：`Note 的来源不在黑名单 Merkle 树中`
- 合规监管方发布黑名单 Merkle 树，用户证明自己不在其中即可合规取款
- 实现与 Privacy Pools 等价的合规能力，但同时支持跨分片

#### 维度 F：TEE 方案 vs ZK 方案的本质对比

Secret Network 和 Oasis 使用 Intel SGX 实现隐私，这是一条完全不同的技术路线：

| 对比点 | TEE（SGX）方案 | ZK 证明方案（本方案） |
|--------|--------------|-------------------|
| 隐私依赖 | Intel 硬件信任 | 密码学困难假设 |
| 历史漏洞 | SGX 已有多个侧信道漏洞（Spectre, Foreshadow, SGAxe） | 无硬件漏洞风险 |
| 吞吐量 | 高（接近原生执行） | 受证明生成速度限制 |
| 可编程性 | 强（通用计算） | 受电路表达能力限制 |
| 抗量子 | ❌（密钥交换用椭圆曲线） | ❌（同样用椭圆曲线） |
| 后量子迁移 | 需更换密钥交换 | 需更换证明系统（STARK） |
| 安全假设 | 工程信任 + 密码学 | 纯密码学 |

**结论**：TEE 方案吞吐量更高，但引入硬件信任；ZK 方案安全假设更纯粹，更适合长期无信任架构。本方案选择 ZK，与 Shardora 的去中心化设计哲学一致。

---

## 八、本方案相对各类方案的差异化优势形式化总结

### 8.1 相对单链 ZK 方案（Tornado Cash, Railgun, Zcash）

**优势 1（跨分片原生隐私）**：见定理 3(d)。单链方案缺乏跨链隐私的密码学机制，而本方案将阈值解密嵌入跨分片路由，提供严格意义上的端到端隐私（定义 3）。

**优势 2（吞吐量线性扩展）**：单链 TPS 受 L1 限制，本方案 TPS = 120 × 32 × N_shards（N_shards 线性增长）。

**优势 3（Gas 关联攻击免疫）**：`systemExecuteShieldedCredit` 由 SYSTEM_EXECUTOR 支付 Gas，接收方无需预持原生代币（解决"Gas Station Problem"，单链方案普遍存在此问题）。

### 8.2 相对跨链隐私方案（Penumbra, Namada）

**优势（路由层加密）**：Penumbra/Namada 通过 IBC 实现跨链，IBC packet 内容在中继链上以明文传递（包括金额和目标地址）。本方案路由层携带 ElGamal 密文，路由节点（GBP）不获得任何明文信息，密文仅在目标分片委员会内解密。

**代价**：本方案依赖 Shardora 自有分片基础设施，不与外部链 IBC 互操作；Penumbra/Namada 可与任意 IBC 链交互。

### 8.3 相对 TEE 方案（Secret, Oasis）

**优势（无硬件信任）**：本方案安全性完全基于密码学困难假设，不依赖 Intel/AMD 硬件。历史上 SGX 已有 SGAxe（2020）、Plundervolt（2019）等漏洞，TEE 方案的安全假设存在工程上的不确定性。

**代价**：吞吐量低于 TEE 方案（证明生成开销 vs 原生执行），智能合约表达能力受电路约束。

### 8.4 相对 Monero（环签名方案）

**优势 1（金额隐藏方案更优）**：Monero 用 Bulletproofs 做范围证明（O(log n) proof 大小），本方案将范围约束内嵌 Groth16 电路，验证 O(1) 且 proof 大小固定（256B vs Monero 的 ~1.5KB）。

**优势 2（跨链能力）**：Monero 是独立链，无法与 EVM 智能合约交互。

**优势 3（可扩展匿名集）**：Monero 的环大小固定（目前最大 16），匿名集 ≤ 16。本方案匿名集 = 池内存款用户总数，可达数千至数万。

---

## 九、局限性与未解决问题

### 9.1 量子计算威胁

**所有现有方案（包括本方案）均不抵抗量子计算机**。BN254 曲线的离散对数问题可被 Shor 算法在多项式时间内解决。

**后量子迁移路径**：
- 将 Groth16（基于配对）替换为基于格（Lattice）的 SNARK（如 Latticefold, Greyhound）
- 将 ElGamal（基于 ECDH）替换为基于格的 KEM（如 Kyber/ML-KEM）
- 将 Pedersen 承诺替换为基于哈希的承诺（无离散对数假设）

当前行业内尚无成熟的后量子 zkSNARK 可在合理 proof 大小和验证时间内运行，这是全行业共同的未解决问题，不是本方案特有的弱点。

### 9.2 Trusted Setup 的长期风险

若 Groth16 的 CRS 生成过程存在后门（毒化参数），攻击者可伪造任意 proof（破坏防双花）或无声地进行未授权铸币。

**缓解**：采用大规模 MPC 仪式（参考 Zcash 的 "Powers of Tau" 有 100+ 参与者），或替换为无 Trusted Setup 的方案（PLONK/Halo2，代价：proof 体积增大 3×）。

### 9.3 Merkle 树深度限制

深度 d=20 支持最多 2^20 ≈ 100 万个并发 Note/pool。若单个 pool 的 Note 数超过此限制，需要树深度扩展（增加 d 会增大电路约束数，影响证明时间）。

**缓解**：使用可更新的 Merkle 根（Accumulator），允许旧树归档并以新树继续，同时提供跨树 Note 消费证明。

---

## 十、结论与定理汇总

| 定理/引理 | 内容 | 依赖假设 |
|---------|------|---------|
| 引理 1 | Nullifier 不可碰撞 | ROM |
| 引理 2 | Nullifier 不可预测 | DL + ROM |
| 引理 3 | 路由信息不构成泄露 | 匿名集大小 |
| 引理 4 | ElGamal 密文语义安全 | DDH |
| 引理 5 | Groth16 零知识性 | Sim 存在（标准结论） |
| 定理 1 | 阈值 ElGamal IND-CPA 安全 | DDH，t-1 腐化 |
| 定理 2 | 隐私安全与共识安全假设对齐 | BFT 安全模型 |
| 定理 3 | 端到端跨分片隐私 | DDH + DL + q-SDH + ROM |

在上述假设成立且混合池匿名集足够大的条件下，本方案对任意 PPT 外部观察者（包括最多 t-1 个腐化委员会节点）提供：
- **完备的发送方/接收方/金额隐私**（满足定义 3 的全部属性）
- **跨分片端到端隐私**（不依赖任何中间节点的诚实性，仅依赖密码学困难假设）
- **与现有 BFT 共识完全对齐的安全假设**（不引入新的信任假设）

---

*文档版本：2026-09-15*  
*参考文献：Groth 2016（Groth16），Boneh et al. 2018（BLS），Pedersen 1991（承诺），Buterin et al. 2023（Privacy Pools），Möser et al. 2018（Monero 匿名集分析），Ben-Sasson et al. 2014（SNARK），Bünz et al. 2018（Bulletproofs）*
