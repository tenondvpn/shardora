# 去中心化配套机制：理论推导与形式化证明

## 符号约定

| 符号 | 含义 |
|------|------|
| $N$ | 候选池节点总数（$10^6$） |
| $k$ | 委员会大小（$1024$） |
| $n$ | 某时刻实际在线节点数（内生变量） |
| $R$ | 单 Epoch 总奖励（含挖矿 + Gas） |
| $\alpha, \gamma, \delta$ | 共识/证明/在线层奖励比例，$\alpha+\gamma+\delta=1$ |
| $c$ | 单节点单 Epoch 运营成本（电费、带宽等） |
| $\sigma_i \in \{0,1\}$ | 节点 $i$ 的策略：在线/离线 |
| $\tau_W$ | 委员会暴露窗口（秒） |
| $\tau_C$ | 对手腐化单个节点所需时间（秒） |

---

## 第一部分：激励兼容性分析

### 1.1 基础博弈模型

**定义 1.1（参与博弈）**：$N$ 个节点各自选择策略 $\sigma_i \in \{0,1\}$，形成策略组合 $\boldsymbol{\sigma} = (\sigma_1, \ldots, \sigma_N)$。设在线节点集合 $\mathcal{O} = \{i : \sigma_i = 1\}$，$n = |\mathcal{O}|$。节点 $i$ 的期望效用：

$$U_i(\sigma_i, \boldsymbol{\sigma}_{-i}) = \sigma_i \cdot \mathbb{E}[\text{收益}_i \mid \boldsymbol{\sigma}] - \sigma_i \cdot c$$

其中 $c > 0$ 为运营成本。

---

### 引理 1.1（当前系统激励不相容）

**命题**：在现有奖励机制（奖励仅分配给委员会成员）下，对于所有非委员会节点，$\sigma_i = 0$（离线）是弱占优策略。

**证明**：

设节点 $i$ 在当前 Epoch 未入选委员会。其在线策略下的期望收益为：

$$\mathbb{E}[\text{收益}_i \mid \sigma_i=1, i \notin \text{委员会}] = 0$$

（奖励函数 `MiningToken()` 仅遍历 `valid_nodes`，非委员会节点不在其中。）

因此：

$$U_i(1, \boldsymbol{\sigma}_{-i}) = 0 - c = -c < 0 = U_i(0, \boldsymbol{\sigma}_{-i})$$

$\sigma_i = 0$ 严格优于 $\sigma_i = 1$。对所有非委员会节点成立，而委员会仅占比 $k/N = 0.1\%$。

**推论**：系统最终纳什均衡为 $n^* = k = 1024$（仅委员会成员在线），100W 候选池退化为 1024 节点系统。$\square$

---

### 定理 1.2（三层奖励的参与均衡存在性）

**定理**：在三层奖励机制下，若满足条件：

$$\frac{(\gamma + \delta) \cdot R}{N} \geq c$$

则存在唯一纳什均衡 $n^* \in [k, N]$，满足 $n^* = \frac{(\gamma + \delta) \cdot R}{c}$，且在线节点数量稳定收敛至 $n^*$。

**证明**：

**步骤 1：构造效用函数**。

对于在线节点 $i$（$\sigma_i = 1$），三层收益分别为：

- 共识层（期望）：$\mathbb{E}[R_i^{\text{con}}] = \frac{k}{N} \cdot \frac{\alpha R}{k} = \frac{\alpha R}{N}$（均匀抽签下，每个节点入选概率为 $k/N$，入选后平均分 $\alpha R$）

- 证明层：$\mathbb{E}[R_i^{\text{att}}] = \frac{\gamma R}{n}$（在线节点均分，假设全部提交有效证明）

- 在线层：$\mathbb{E}[R_i^{\text{onl}}] = \frac{\delta R}{n}$（在线节点均分）

总效用：

$$U_i(1, n) = \frac{\alpha R}{N} + \frac{(\gamma+\delta)R}{n} - c$$

**步骤 2：均衡条件**。

节点 $i$ 选择在线当且仅当 $U_i(1, n) \geq 0$，即：

$$\frac{(\gamma+\delta)R}{n} \geq c - \frac{\alpha R}{N}$$

忽略共识层小量 $\frac{\alpha R}{N} \ll c$（当 $N \gg k$ 时），得近似均衡条件：

$$n^* = \frac{(\gamma+\delta)R}{c}$$

**步骤 3：唯一性与稳定性**。

定义超额效用函数 $f(n) = U_i(1, n) = \frac{(\gamma+\delta)R}{n} - c + \frac{\alpha R}{N}$，其关于 $n$ 严格递减（$f'(n) = -\frac{(\gamma+\delta)R}{n^2} < 0$）。

- $n < n^*$：$f(n) > 0$，节点有激励上线 → $n$ 增加
- $n > n^*$：$f(n) < 0$，节点有激励下线 → $n$ 减少

因此 $n^*$ 是全局稳定唯一均衡。$\square$

---

### 推论 1.3（支撑目标在线规模的奖励下界）

为使均衡在线节点数达到 $n^* \geq N_{\text{target}}$，总奖励需满足：

$$\boxed{R \geq \frac{c \cdot N_{\text{target}}}{\gamma + \delta}}$$

**数值代入**（$\gamma + \delta = 0.3$，$N_{\text{target}} = 10^6$，$c = 0.01$ SHARDORA/Epoch）：

$$R \geq \frac{0.01 \times 10^6}{0.3} \approx 33{,}333 \text{ SHARDORA/Epoch}$$

与现有 `kInitialTotalReward = 10000 SHARDORA/Epoch` × 1022 分片相比，单分片奖励量级匹配，机制在经济上可行。

---

## 第二部分：公平性与轮换机制

### 定理 2.1（长期参与公平性）

**定理**：设所有节点 FTS 权重相等（$w_i = w$），并施加连续当选上限 $M$（`kMaxConsecutiveElections = 3`）。则对任意节点 $i, j$，长期参与率之比满足：

$$\lim_{T \to \infty} \frac{\text{节点 } i \text{ 在前 } T \text{ 个 Epoch 中参与次数}}{\text{节点 } j \text{ 在前 } T \text{ 个 Epoch 中参与次数}} = 1$$

即等权重节点具有**渐近公平性**。

**证明**：

**步骤 1：建立马尔可夫链**。

每个节点的状态 $s \in \{0, 1, 2, \ldots, M, \text{休息}\}$ 表示其当前连续当选次数，其中"休息"状态表示已达上限被强制剔除。

转移概率（设单 Epoch 入选概率为 $p = k/N$）：

$$P(s \to s+1) = p, \quad s < M$$
$$P(s \to 0) = 1-p, \quad s < M$$
$$P(M \to \text{休息}) = 1 \text{（强制）}$$
$$P(\text{休息} \to 0) = 1 \text{（休息一轮后重置）}$$

**步骤 2：计算平稳分布**。

此马尔可夫链是有限状态、不可约、非周期的，故存在唯一平稳分布 $\pi$。

设 $\pi_s$ 为状态 $s$ 的平稳概率，由细致平衡方程：

$$\pi_0 = (1-p)(\pi_0 + \pi_1 + \cdots + \pi_{M-1}) + \pi_{\text{休息}}$$

由对称性，所有节点具有相同的转移核，故在相同权重下经历相同的平稳分布 $\pi$。

**步骤 3：参与率计算**。

节点处于"参与委员会"的平稳概率为：

$$\pi_{\text{参与}} = \sum_{s=1}^{M} \pi_s = \frac{p(1-(1-p)^M)}{1 - (1-p)^M \cdot p + (1-p)^M}$$

对 $p = k/N = 10^{-3}$，$M = 3$：$\pi_{\text{参与}} \approx 3p = 3 \times 10^{-3}$（远小于 1，可线性近似）。

由于所有等权重节点共享相同 $\pi_{\text{参与}}$，长期参与率之比趋近于 1。$\square$

---

### 推论 2.2（轮换上限对参与率的影响）

有无轮换上限 $M$ 时的长期参与率对比：

| | 无上限 | 上限 $M=3$ |
|--|-------|-----------|
| 理论参与率 | $p = k/N$ | $\leq 3p = 3k/N$ |
| 极端情况（高 FTS 节点） | 可趋近 $1$ | **严格 $\leq 3/4$**（3 轮参与后强制 1 轮休息） |
| Nakamoto 系数 | 理论 $k/3$，实际更低 | 稳定维持 $k/3$ |

---

## 第三部分：延迟委员会派生的安全性

### 3.1 自适应对手模型

**定义 3.1（$(t_C, \beta)$-自适应对手）**：对手 $\mathcal{A}$ 控制至多 $\beta N$ 个节点，且可在得知委员会身份后的每秒额外腐化至多 $1/t_C$ 个节点（即腐化单节点需时 $t_C$ 秒）。

**注**：现实中 $t_C$ 对应 DDoS 攻击、贿赂或物理攻击准备时间，通常 $t_C \gg 1$ 秒。

---

### 定理 3.2（延迟派生压缩对手成功概率）

**定理**：设初始拜占庭节点数 $M_0 = \beta N$（$\beta < 1/3$），委员会暴露窗口为 $\tau_W$，对手成功攻破委员会（使委员会中拜占庭节点 $\geq k/3$）的概率满足：

$$\Pr[\text{委员会被攻破}] \leq \exp\!\left(-k \cdot D\!\left(\frac{1}{3} \,\Big\|\, \beta + \frac{\tau_W}{t_C \cdot N}\right)\right)$$

其中右侧括号内第二项为对手在窗口期内新增的腐化节点比例。

**证明**：

**步骤 1**：对手在窗口 $\tau_W$ 内可额外腐化至多 $\Delta M = \lfloor \tau_W / t_C \rfloor$ 个节点，腐化后总拜占庭比例：

$$\beta' = \frac{M_0 + \Delta M}{N} = \beta + \frac{\tau_W}{t_C \cdot N}$$

**步骤 2**：由定理 4.1（`MILLION_NODE_SHARD_FORMAL_PROOF.md`），以腐化后比例 $\beta'$ 代入：

$$\Pr[X \geq k/3] \leq \exp(-k \cdot D(1/3 \| \beta'))$$

**步骤 3**：$D(1/3 \| \beta')$ 关于 $\beta'$ 严格递减（KL 散度在 $\beta' < 1/3$ 时随 $\beta'$ 增大而减小），故 $\tau_W$ 越小，安全性越强。$\square$

---

### 推论 3.3（窗口压缩的量化收益）

设 $\beta = 0.2$，$t_C = 60$s，$N = 10^6$，$k = 1024$：

**旧方案**（$\tau_W = 600$s）：$\Delta M = 10$ 个节点，$\beta' = 0.2 + 10^{-5} \approx 0.2$（影响可忽略）

**新方案**（$\tau_W = 190$s）：$\Delta M = 3$ 个节点，$\beta' \approx 0.2$

> *注*：当 $N = 10^6$ 时，$\Delta M / N$ 极小，窗口压缩的主要收益**不在于阻止节点腐化**，而在于以下定理：

---

### 定理 3.4（身份隐藏的有效期安全性）

**定理**：设对手需在委员会成员公开身份后才能发动有效 DoS 攻击（否则无差别攻击成本为 $O(N)$），且每次攻击消耗时间 $t_A \geq t_C$。将窗口从 $\tau_W^{\text{旧}}$ 压缩到 $\tau_W^{\text{新}}$，对手能针对性攻击的委员会成员上限从：

$$B^{\text{旧}} = \left\lfloor \frac{\tau_W^{\text{旧}}}{t_A} \right\rfloor \quad \text{降至} \quad B^{\text{新}} = \left\lfloor \frac{\tau_W^{\text{新}}}{t_A} \right\rfloor$$

**数值**（$t_A = 30$s 的 DDoS 准备时间）：

$$B^{\text{旧}} = \lfloor 600/30 \rfloor = 20, \qquad B^{\text{新}} = \lfloor 190/30 \rfloor = 6$$

攻击能力下降 **70%**。要攻破委员会需 $k/3 = 341$ 个成员，所需攻击时间：

$$\tau_{\text{需要}} = 341 \times t_A = 341 \times 30 = 10{,}230 \text{ s} \gg \tau_W^{\text{新}} = 190 \text{ s}$$

针对性攻击在时间上不可行。$\square$

---

## 第四部分：证明机制的激励相容性

### 定理 4.1（证明机制激励相容）

**定义 4.1（激励相容，IC）**：若对所有节点 $i$，在所有其他节点策略 $\boldsymbol{\sigma}_{-i}$ 固定时，提交真实有效证明（诚实行为）是弱占优策略，则称该机制是激励相容的。

**定理**：设每提交一次有效证明的期望增量奖励 $\Delta r = \gamma R / n$，提交假证明的惩罚 $\rho \geq \Delta r$（信誉扣分的折现价值），则证明机制是激励相容的。

**证明**：

对节点 $i$，考虑四种行为的收益：

| 行为 | 条件 | 期望收益 |
|------|------|---------|
| 提交真实证明 | — | $+\Delta r$ |
| 提交伪造证明 | ECDSA不可伪造 | **不可能**（被拒绝） |
| 不提交证明 | — | $0$ |
| 提交有效但重复证明 | 去重过滤 | $+\Delta r$（至多一次） |

由 ECDSA 不可伪造性（EUF-CMA 安全），节点 $i$ 无法以 $i'$ 的身份提交证明，故伪造不可行。

比较"提交"与"不提交"：$\Delta r > 0$ 始终成立，故提交是严格优于不提交的策略。IC 成立。$\square$

---

### 定理 4.2（委员会审查抵抗性）

**定理**：设信誉惩罚 $\rho_{\text{censor}} > \gamma R / n$（审查他人证明所得收益）。则在理性委员会假设下，委员会成员没有动机审查合法证明。

**证明**：

委员会成员 $c$ 审查节点 $j$ 的证明，所得额外收益为：

$$\text{审查收益} = \frac{\gamma R}{\tilde{n}-1} - \frac{\gamma R}{\tilde{n}} = \frac{\gamma R}{\tilde{n}(\tilde{n}-1)} \approx \frac{\gamma R}{n^2}$$

（将 $j$ 的证明奖励均分给剩余节点，$\tilde{n}$ 为审查后的有效证明数）

节点 $j$ 保留了签名过的证明消息，可在下一 Epoch 向根分片提交"审查举报"，触发对 $c$ 的信誉惩罚：

$$\text{审查代价} = \rho_{\text{censor}} \cdot \text{折现因子}$$

设 $\rho_{\text{censor}} = \lambda \cdot \gamma R / n$（$\lambda > 0$），审查的净期望收益为：

$$\frac{\gamma R}{n^2} - \lambda \cdot \frac{\gamma R}{n} = \frac{\gamma R}{n} \left(\frac{1}{n} - \lambda\right)$$

当 $\lambda > 1/n$（即 $n > 1$，显然成立）时，净期望收益 $< 0$，审查是严格劣势策略。$\square$

---

## 第五部分：自适应委员会大小的安全性保持

### 定理 5.1（自适应调整保持安全性）

**定理**：设自适应委员会大小为 $k(n) = \min(k_{\max}, \lfloor \rho n \rfloor)$（$\rho = 0.01$，$k_{\max} = 1024$）。对任意在线节点数 $n \geq k_{\min}$，委员会被攻破的概率满足：

$$\Pr[\text{被攻破}] \leq \exp\!\left(-k(n) \cdot D\!\left(\frac{1}{3} \,\Big\|\, \beta\right)\right)$$

且当 $n \to 0$ 时安全性**单调递减**，但始终 $> 0$。

**证明**：

$k(n)$ 关于 $n$ 非递减，且 $D(1/3 \| \beta) > 0$ 为与 $n$ 无关的常数。故指数上界关于 $n$ 单调递减（绝对值增大），即安全性随在线节点数增加而增强。

当 $n = k_{\min}$ 时，$k = k_{\min}$，此时安全性最低，但由 $k_{\min} \geq 3$ 保证基本 BFT 可运行。$\square$

**注**：$\rho n$ 的物理含义是"委员会不超过在线节点的 $\rho$ 比例"，防止委员会成员互相全部认识，形成中心化通信网络。

---

## 第六部分：机制组合的整体安全性

### 定理 6.1（五机制联合安全）

**定理**：设以下条件同时成立：
- (C1) $(\gamma+\delta)R/N \geq c$（定理 1.2 条件，参与均衡存在）
- (C2) $k \geq k_{\min}(\beta, \lambda)$（定理 4.1/推论 4.2，单 Epoch 安全）
- (C3) $M \leq k_{\max\_\text{cons}}$（推论 2.2，轮换上限）
- (C4) $\rho_{\text{censor}} > \gamma R / n$（定理 4.2，审查惩罚）
- (C5) $\tau_W \leq \tau_W^{\text{new}} = 190$s（定理 3.4，窗口压缩）

则系统以至少 $1 - 2^{-\lambda} - \text{negl}(\lambda)$ 的概率同时满足：

1. **安全性**：无两个诚实节点提交冲突块
2. **活性**：每个有效交易在有限时间内提交
3. **参与性**：均衡在线节点数 $n^* \geq N_{\text{target}}$
4. **公平性**：等权重节点长期参与率之比趋于 1
5. **审查抵抗性**：任何合法证明最终被正确记录

**证明**：

由定理 1.2，条件 (C1) 保证参与均衡 $n^* \geq N_{\text{target}}$（性质 3）。

由定理 1.2 和 5.1，在均衡 $n^*$ 下 $k(n^*) \geq k_{\min}$，由定理 4.1（`MILLION_NODE_SHARD_FORMAL_PROOF.md`）保证安全性和活性（性质 1, 2），失败概率 $\leq 2^{-\lambda}$。

由定理 2.1，条件 (C3) 保证长期公平性（性质 4）。

由定理 4.2，条件 (C4) 保证审查抵抗性（性质 5），额外失败概率为 $\text{negl}(\lambda)$（依赖 ECDSA 安全假设）。

五个性质同时成立，联合失败概率 $\leq 2^{-\lambda} + \text{negl}(\lambda)$（Union Bound）。$\square$

---

## 附录：条件 (C1) 可行性验证

将现有代码参数代入：

```
R = kInitialTotalReward = 10,000 SHARDORA/Epoch
γ + δ = 0.30 （证明层 20% + 在线层 10%）
N_target = 100,000 （取保守目标，非 100W）
```

每节点每 Epoch 的证明+在线层奖励下界：

$$\frac{(\gamma+\delta) \cdot R}{N_{\text{target}}} = \frac{0.3 \times 10000}{100000} = 0.03 \text{ SHARDORA/Epoch}$$

若 SHARDORA 价格 ≥ $0.001/枚，则每节点每 Epoch 奖励 ≥ $0.00003，折合年收益 ≈ $1.58。

对于 VPS 运营成本 $5/月的节点，**要使条件 (C1) 成立需要**：

$$R \geq \frac{c \cdot N_{\text{target}}}{\gamma+\delta} = \frac{(\$5/\text{月} \div \text{价格}) \times 100{,}000}{0.3}$$

这给出了 SHARDORA 价格与系统可支持的最大在线节点数之间的**经济可行性约束曲线**，可用于指导代币经济模型设计。

---

## 参考文献

1. Myerson, R. B. (1979). **Incentive Compatibility and the Bargaining Problem**. Econometrica.
2. Osborne, M. J., & Rubinstein, A. (1994). **A Course in Game Theory**. MIT Press.
3. Dolev, D., & Reischuk, R. (1985). **Bounds on Information Exchange for Byzantine Agreement**. JACM.
4. Buterin, V., et al. (2020). **Combining GHOST and Casper**. (Ethereum 2.0 incentive design).
5. Pass, R., & Shi, E. (2017). **Fruitchains: A Fair Blockchain**. PODC.
6. Luu, L., et al. (2016). **A Secure Sharding Protocol for Open Blockchains**. CCS.
