# 单分片百万节点：理论推导与形式化证明

## 符号约定

| 符号 | 含义 |
|------|------|
| $N$ | 分片注册节点总数（目标：$10^6$） |
| $M$ | 其中拜占庭节点数，$M = \beta N$，$\beta < \tfrac{1}{3}$ |
| $k$ | 每轮随机委员会大小 |
| $t$ | BFT 门限（Quorum 阈值），$t = \lceil \tfrac{2k}{3} \rceil$ |
| $X$ | 随机变量：委员会中拜占庭节点数 |
| $\lambda$ | 安全参数（bits） |
| $D(p \| q)$ | 二元 KL 散度（相对熵） |
| $\mathcal{H}(N,M,k)$ | 超几何分布：从 $N$ 个中抽 $k$ 个，其中 $M$ 个"坏" |
| $\text{negl}(\lambda)$ | 关于安全参数的可忽略函数 |

---

## 第一部分：系统模型

### 1.1 网络模型

采用**偏同步网络模型**（partial synchrony，Dwork-Lynch-Stockmeyer 1988）：

- 存在未知的全局稳定时间（GST），GST 之后消息延迟有界 $\Delta$
- GST 之前网络可任意延迟、丢包
- HotStuff 在此模型下保证：安全性（任何时间）+ 活性（GST 之后）

### 1.2 对手模型

**静态拜占庭对手**：在协议执行前选定腐化节点集合，之后不可增加。

- 对手控制至多 $M = \beta N$ 个节点，$\beta < \tfrac{1}{3}$
- 腐化节点可任意偏离协议（发送伪造消息、沉默等）
- 对手是计算多项式时间受限的（PPT adversary）

### 1.3 随机信标模型

每轮委员会抽签的随机种子来自：

```
// src/vss/vss_manager.cc:18
epoch_random_ = Hash64(qc.sign_x + qc.sign_y)
```

其中 $(sign\_x, sign\_y)$ 是前一时间块的 **BLS 门限签名**。

**假设 A（信标不可预测性）**：在对手提交腐化策略之前，$epoch\_random\_$ 在计算上不可区分于均匀随机数。形式化见第二部分引理 2.1。

---

## 第二部分：随机信标安全性

### 引理 2.1（BLS 门限签名不可预测性）

**命题**：设门限 BLS 系统参数为 $(n, t)$，其中 $t = \lceil \tfrac{2n}{3} \rceil$。若对手控制的节点数 $< t$，则在离散对数假设（DLOG）下，门限签名输出 $\sigma^*$ 对任何 PPT 对手是计算不可区分于 $\mathbb{G}_1$ 上的均匀分布。

**证明思路**：

1. 门限 BLS 使用 Shamir 秘密共享将主私钥 $sk$ 分成 $n$ 份。任意 $t$ 份可通过 Lagrange 插值还原主私钥，进而还原签名。
2. 对手仅持有 $< t$ 份 partial share，由 $(t-1)$-次多项式的信息论性质，这些份额对 $sk$ 毫无信息泄露。
3. 门限 BLS 签名具有**唯一性**（uniqueness）：对同一消息 $m$，任意满足门限的 $t$ 个诚实 partial sign 子集均还原同一签名 $\sigma^* = H(m)^{sk}$（其中 $H: \{0,1\}^* \to \mathbb{G}_1$ 为哈希函数）。
4. 因此，对手无法通过选择性揭露 partial share 来影响输出（"最后揭露者攻击"无效）。
5. 在 DLOG 假设下，$\sigma^*$ 的哈希 $Hash64(\sigma^*)$ 在多项式时间内与均匀 64-bit 串不可区分。$\square$

**重要推论**：与以太坊 RANDAO 不同，BLS 门限签名方案天然抵抗偏差攻击（bias attack）。对手不能通过"拒绝参与"来重新骰子，因为一旦 $t$ 个诚实节点提交，签名便唯一确定，无任何自由度可供操纵。

---

## 第三部分：委员会安全性的概率界

### 3.1 精确分布

设 $X$ 为从 $N$ 个节点中不放回抽取 $k$ 个节点，其中拜占庭节点的数目：

$$X \sim \mathcal{H}(N, M, k), \quad M = \beta N$$

期望与方差：

$$\mathbb{E}[X] = k\beta, \quad \text{Var}[X] = k\beta(1-\beta)\frac{N-k}{N-1}$$

注意 $\text{Var}[X] < k\beta(1-\beta)$（不放回抽样方差小于放回抽样），这使超几何尾部界**严格优于**二项分布。

### 3.2 Serfling 不等式（Serfling 1974）

**引理 3.1**（Serfling 不等式）：设 $X \sim \mathcal{H}(N, M, k)$，$p = M/N$，对任意 $\varepsilon > 0$：

$$\Pr\!\left[\frac{X}{k} \geq p + \varepsilon\right] \leq \exp\!\left(\frac{-2k\varepsilon^2}{1 - \frac{k-1}{N}}\right)$$

当 $N \gg k$ 时分母趋近 1，退化为 Hoeffding 不等式。

### 3.3 KL 散度 Chernoff 界（最优界）

超几何分布可被二项分布随机占优（stochastic dominance），故以下更紧的 Chernoff 界成立：

**引理 3.2**（超几何 Chernoff）：

$$\Pr[X \geq k\tau] \leq \exp\!\left(-k \cdot D(\tau \| \beta)\right), \quad \tau > \beta$$

其中二元 KL 散度定义为：

$$D(\tau \| \beta) = \tau \ln\frac{\tau}{\beta} + (1-\tau)\ln\frac{1-\tau}{1-\beta}$$

---

## 第四部分：主定理

### 定理 4.1（委员会被攻破的概率上界）

**定理**：在假设 A（信标不可预测性）下，设拜占庭节点比例 $\beta < \tfrac{1}{3}$，每轮随机选取 $k$ 个节点组成委员会。委员会中拜占庭节点超过 $\tfrac{1}{3}$ 的概率满足：

$$\Pr\!\left[X \geq \frac{k}{3}\right] \leq \exp\!\left(-k \cdot D\!\left(\frac{1}{3} \,\Big\|\, \beta\right)\right)$$

其中：

$$D\!\left(\frac{1}{3} \,\Big\|\, \beta\right) = \frac{1}{3}\ln\frac{1}{3\beta} + \frac{2}{3}\ln\frac{2}{3(1-\beta)}$$

**证明**：

*步骤 1*：由假设 A，委员会成员等价于从 $N$ 个节点中均匀不放回抽取 $k$ 个，与对手的腐化策略独立（对手无法预知 $epoch\_random\_$，故无法针对性腐化被选中的节点）。

*步骤 2*：由于超几何分布被二项分布随机占优，即对任意 $t$：
$$\Pr_{\mathcal{H}(N,M,k)}[X \geq t] \leq \Pr_{\text{Binom}(k,\beta)}[Y \geq t]$$

*步骤 3*：对 $Y \sim \text{Binom}(k, \beta)$，令 $\mu = k\beta$，$\tau = 1/3 > \beta$，由乘法型 Chernoff 界：

$$\Pr\!\left[Y \geq k\tau\right] \leq \left(\frac{e^{\tau/\mu - 1}}{(\tau/\mu)^{\tau/\mu}}\right)^{\mu} = \exp\!\left(-k \cdot D(\tau \| \beta)\right)$$

*步骤 4*：令 $\tau = 1/3$，代入得到定理结论。$\square$

---

### 推论 4.2（委员会大小下界）

为使委员会安全失败概率 $\leq 2^{-\lambda}$（安全参数 $\lambda$ bits），所需最小委员会大小为：

$$\boxed{k_{\min}(\beta, \lambda) = \left\lceil \frac{\lambda \ln 2}{D\!\left(\tfrac{1}{3} \| \beta\right)} \right\rceil}$$

**数值表**（单位：节点数）：

| 对手比例 $\beta$ | $D(1/3 \| \beta)$ | $\lambda=64$ | $\lambda=80$ | $\lambda=128$ | $k=2048$ 等效安全位数 |
|:---:|:---:|:---:|:---:|:---:|:---:|
| 10% | 0.1451 | 306 | 382 | 611 | **422 bits** |
| 15% | 0.0828 | 536 | 669 | 1,071 | **245 bits** |
| 20% | 0.0488 | 908 | 1,135 | 1,817 | **144 bits** |
| 25% | 0.0174 | 2,553 | 3,192 | 5,107 | **51 bits** |
| 30% | 0.0028 | 15,857 | 19,822 | 31,715 | **8 bits** |

**关键结论**：
- 对手 $\leq 20\%$（$\beta \leq 0.2$）时，$k = 2048$ 提供 **144 位**安全，远超工程需求
- 对手 $\leq 25\%$ 时，$k = 2048$ 仅有 51 位安全；若需 80 位安全需将 $k$ 提升至 $4096$
- 对手达到理论上限 $\approx 33\%$ 时，$k$ 需数万，此时应优先考虑增加分片数而非扩大委员会

---

## 第五部分：HotStuff 安全性与活性

### 定理 5.1（随机委员会 HotStuff 安全性）

**定理**：设定理 4.1 的前提条件成立，且 $k \geq k_{\min}(\beta, \lambda)$。则系统以概率 $\geq 1 - 2^{-\lambda}$ 满足如下性质：

**安全性（Safety）**：不存在两个诚实节点提交相互冲突的块。

**证明**：

1. 由定理 4.1，以概率 $\geq 1 - 2^{-\lambda}$，委员会中诚实节点数 $\geq \lceil 2k/3 \rceil$（恰好是 `GetSignerCount(k)` 的返回值）。

2. 在诚实多数委员会内，HotStuff 的标准安全证明（Yin et al. 2018）给出：任意两个 QC $QC_1, QC_2$ 对应的签名集合大小均 $\geq t = \lceil 2k/3 \rceil$，故两个签名集合**必有交集**。

3. 由 Quorum 相交引理：$|Q_1| + |Q_2| > k$，且两个 Quorum 的交集中至少有一个诚实节点（因为拜占庭节点数 $< k/3$，而 $2\cdot\lceil 2k/3\rceil - k \geq k/3 + 1 >$ 拜占庭节点数）。

4. 该诚实节点在两个 QC 均签名，由 HotStuff 的"锁"机制（Lock Rule），两个 QC 必须在同一条链上（否则节点不会违反 lock）。因此不可能出现分叉。$\square$

### 定理 5.2（随机委员会 HotStuff 活性）

**定理**：在 GST 之后，以概率 $\geq 1 - 2^{-\lambda}$，每个有效交易最终被提交。

**证明**：

1. 以概率 $\geq 1 - 2^{-\lambda}$，委员会内诚实节点占多数。

2. HotStuff Pacemaker 保证在 GST 后，经过有限轮 View Change，诚实 Leader 当选（`GetLeader` 的旋转机制，`hotstuff.h:407`）。

3. 诚实 Leader 会提案包含待处理交易，诚实多数节点响应投票，QC 形成。

4. 委员会每 epoch 轮换，非委员会的 99.8% 节点通过验证 QC（`common_pk_` 聚合公钥）接受结果，无需参与投票。$\square$

---

## 第六部分：委员会选取的均匀性

### 定理 6.1（Fisher-Yates 产生均匀子集）

**命题**：Fisher-Yates 算法以一个 $\ell$-bit 均匀随机种子驱动，输出 $N$ 个节点的一个 $k$-子集，且每个 $k$-子集被选中的概率相等（均为 $1/\binom{N}{k}$），当且仅当 PRNG 的输出与均匀随机字节流不可区分。

**当前实现分析**：

```cpp
// elect_tx_item.cc:59
g2_ = std::make_shared<std::mt19937_64>(vss_mgr_->EpochRandom());
// EpochRandom() 返回 uint64_t → 64-bit 种子
```

`mt19937_64` 是**伪随机数生成器（PRNG）**，内部状态为 19937 位，但种子仅 64 位，存在以下安全局限：

- **熵上限**：有效随机性仅为 64 bits（种子熵），而非 mt19937 的周期 $2^{19937}$
- **非密码学安全**：mt19937 的输出序列在已知部分输出后可预测（Berlekamp-Massey 攻击）

**推荐修复**：用 ChaCha20-CTR 或 AES-256-CTR 替代 mt19937_64，种子为 `epoch_random_` 的完整 256-bit 输出（当前 `Hash64` 截断为 64 位，也应扩展到 `Hash256`）：

```cpp
// 推荐替换
epoch_random_256_ = common::Hash::Hash256(qc.sign_x() + qc.sign_y());
// 用 ChaCha20(epoch_random_256_) 驱动 Fisher-Yates
```

在上述修复下，定理 6.1 的前提满足，委员会选取在计算意义下与均匀无放回抽样不可区分。

---

## 第七部分：与两级聚合（Leader 分身）的严格对比

### 7.1 Leader 分身的两级 Quorum 安全性分析

设子组数量 $G = 1024$，每组大小 $m = 1024$，每组门限 $t_1 = \lceil 2m/3 \rceil$，组间门限 $t_2 = \lceil 2G/3 \rceil$。

**定义**：委员会"被攻破"事件：主 Leader 凑到 $t_2$ 个子组 QC，每个子组 QC 中拜占庭节点参与了投票。

**攻击场景（集中攻击）**：设总拜占庭节点 $M = N/3 - 1$。对手将所有拜占庭节点集中在 $G/3$ 个子组内（每组全满）：

- 这 $G/3$ 个子组：拜占庭比例 = $1$，组内 BFT 失效，子组可被对手完全控制
- 剩余 $2G/3$ 个子组：全诚实，子组 QC 正常

结果：对手控制 $G/3$ 个子组的 QC，无法达到主 Leader 需要的 $\lceil 2G/3 \rceil$ 个组 QC，**活性被破坏**。

更危险的场景：对手将 $M = N/3$ 个节点以 $M/G = N/3G$ 的密度均匀分布，同时在每组内拜占庭比例 $= 1/3$，则每组恰好可以阻止子组达成 Quorum（需要诚实节点 $= 2m/3$，但诚实节点恰好是 $2m/3$，在边界），安全性退化至概率 1/2。

**结论**：两级 Quorum 的等效拜占庭容错率为：

$$\beta_{\text{eff}}^{2\text{-level}} = \min\!\left(\frac{1}{3}, \frac{1}{G}\right) \text{ （集中攻击下）}$$

当 $G = 1024$ 时，单个子组被攻击只需 $1/1024$ 的节点。

### 7.2 随机委员会方案的等效安全性

随机委员会方案的有效拜占庭容错率：

$$\beta_{\text{eff}}^{\text{random}} = \frac{1}{3} - \varepsilon(\lambda, k)$$

其中 $\varepsilon(\lambda, k)$ 是安全参数决定的极小量（$\lambda = 80$ 时可取 $\beta \leq 0.2$）。

**两方案安全性对比（形式化）**：

$$\Pr[\text{两级方案被攻破} \mid \beta = 0.25] = \Omega(1)$$
$$\Pr[\text{随机委员会被攻破} \mid \beta = 0.25, k = 4096] \leq 2^{-80}$$

随机委员会方案在相同对手能力下的安全概率**指数级优于**两级聚合方案。

---

## 第八部分：复杂度分析

### 8.1 通信复杂度

| 阶段 | Leader 分身 | 随机委员会 |
|------|------------|-----------|
| 子 Leader 收票 | $O(m) = O(\sqrt{N})$ | — |
| 主 Leader 收票 | $O(G) = O(\sqrt{N})$ | $O(k)$（常数） |
| 全网广播 QC | $O(N)$ | $O(N)$ |
| 单节点总消息 | $O(\sqrt{N})$ | $O(1)$ |

### 8.2 计算复杂度

| 操作 | Leader 分身 | 随机委员会 |
|------|------------|-----------|
| BLS Lagrange 插值 | $O(t_1^2) + O(t_2^2)$ | $O(t^2)$，$t \leq 1366$ |
| DKG 轮次 | $2 \times O(m^2)$ | $O(k^2)$ |
| 非委员会节点 QC 验证 | $O(1)$（聚合公钥） | $O(1)$（聚合公钥） |

### 8.3 延迟对比

| 方案 | 投票轮次 | 等效延迟 |
|------|---------|---------|
| 两级聚合 | 2 轮 | $2\Delta$ |
| 随机委员会 | **1 轮** | $\Delta$（不变） |

---

## 第九部分：参数推荐

综合安全性、性能与实现复杂度，推荐参数如下：

| 场景 | $\beta$（保守估计） | 推荐 $k$ | 安全位数 | 总节点上限 |
|------|-------------|---------|---------|---------|
| 测试网 | 20% | 2048 | 144 bits | $10^6$ |
| 主网（标准） | 25% | 4096 | 71 bits | $10^6$ |
| 主网（高安全） | 25% | 6144 | 107 bits | $10^6$ |
| 超高安全 | 30% | 32768 | 91 bits | $10^6$ |

**默认推荐**：`kEachShardCommitteeSize = 4096`，覆盖 25% 拜占庭对手的 71-bit 安全，同时将 BFT 参与人数控制在可接受范围（DKG 复杂度 $O(4096^2)$）。

---

## 附录：KL 散度数值速查

$$D\!\left(\frac{1}{3} \Big\| \beta\right) = \frac{1}{3}\ln\frac{1}{3\beta} + \frac{2}{3}\ln\frac{2}{3(1-\beta)}$$

| $\beta$ | $D(1/3 \| \beta)$ | $k_{\min}$ ($\lambda=80$) | $k_{\min}$ ($\lambda=128$) |
|:-------:|:-----------------:|:------------------------:|:-------------------------:|
| 0.10 | 0.14506 | 382 | 611 |
| 0.15 | 0.08282 | 669 | 1,071 |
| 0.20 | 0.04876 | 1,136 | 1,817 |
| 0.25 | 0.01737 | 3,192 | 5,107 |
| 0.28 | 0.00651 | 8,516 | 13,626 |
| 0.30 | 0.00283 | 19,601 | 31,362 |
| 0.32 | 0.00060 | 92,289 | 147,663 |

---

## 参考文献

1. Yin, M., Hoang, D., Abraham, I., Malkhi, D., & Reiter, M. K. (2019). **HotStuff: BFT Consensus with Linearity and Responsiveness**. PODC 2019.
2. Chen, J., & Micali, S. (2019). **Algorand: Secure and Efficient Distributed Ledger**. Theoretical Computer Science.
3. Serfling, R. J. (1974). **Probability Inequalities for the Sum in Sampling without Replacement**. Annals of Statistics.
4. Dwork, C., Lynch, N., & Stockmeyer, L. (1988). **Consensus in the Presence of Partial Synchrony**. JACM.
5. Boneh, D., Drijvers, M., & Neven, G. (2018). **Compact Multi-Signatures for Smaller Blockchains**. ASIACRYPT.
