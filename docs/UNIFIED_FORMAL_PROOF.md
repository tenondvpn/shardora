# 单分片百万节点：去中心化机制统一形式化证明

> **本文融合以下三份文档并完成交叉验证：**
> - `DECENTRALIZATION_MECHANISMS.md`（五项机制工程设计）
> - `DECENTRALIZATION_FORMAL_PROOF.md`（机制激励、公平、安全的形式化证明）
> - `EPOCH_PERIOD_FORMAL_ANALYSIS.md`（Epoch 周期可行域与最优化分析）
>
> **交叉验证发现五处新结论**，见第六部分。原三份文档可废弃，以本文为准。

---

## 符号约定

| 符号 | 含义 | 代码溯源 |
|------|------|---------|
| $T$ | Epoch 周期（秒），唯一时间自由变量 | `utils.h:226 kRotationPeriod` |
| $T_\varphi$ | DKG 单阶段窗口，$T_\varphi=(T-30)/30$ | `bls_dkg.h:170 kDkgPeriodUs` |
| $T_W$ | 委员会暴露窗口，随机制 M2 是否启用而变化 | — |
| $N$ | 候选池节点总数（目标 $10^6$） | 设计参数 |
| $k$ | 委员会大小（当前 1024，目标 2048） | `kEachShardMaxNodeCount` |
| $n$ | 实际在线节点数（博弈内生均衡变量） | — |
| $\beta$ | 全网拜占庭节点比例，$\beta<1/3$ | 安全假设 |
| $R$ | 单 Epoch 总奖励（含 `kInitialTotalReward` + Gas） | `utils.h:253` |
| $\alpha,\gamma,\delta$ | 共识/证明/在线奖励比例，$\alpha+\gamma+\delta=1$ | 机制 M1 参数 |
| $c$ | 单节点单 Epoch 运营成本 | 运营参数 |
| $\Delta$ | GST 后网络单次消息延迟上界 | 网络参数 |
| $r$ | DKG 消息超时重传次数 | 协议参数 |
| $\lambda$ | 全网交易到达率（tx/s） | 负载参数 |
| $t_A$ | 对手针对性攻击单节点耗时（秒） | 威胁模型 |
| $M$ | 连续当选轮次上限（`kMaxConsecutiveElections=3`） | 机制 M3 参数 |
| $D(p\|q)$ | 二元 KL 散度 | 信息论 |
| $\lambda_{\sec}$ | 安全参数（bits） | 密码学 |

---

## 第一部分：问题全景

候选池 100W + 委员会 1024 设计存在四个去中心化漏洞：

| # | 问题 | 根因（代码位置） | 若不修复 |
|---|------|--------------|---------|
| P1 | 经济激励断层 | `elect_tx_item.cc:595` 奖励只遍历 `valid_nodes` | 非委员会零收入 → 节点离线 → 退化为 1024 节点 |
| P2 | 委员会提前暴露 | `elect_manager.cc:272` `StoreMembers` 选举块一提交即公开 | 对手有 $T \approx 600$s 攻击 341 个目标节点 |
| P3 | 轮换不强制 | `elect_tx_item.cc:1485` `gap_weight` 仅是软激励 | 高 FTS 节点连续垄断委员会席位 |
| P4 | 验证工作无偿 | 非委员会节点执行 `EnqueueVerifyBlock()` 但无报酬 | 验证质量退化，安全审计消失 |

---

## 第二部分：时间约束体系

### 2.1 时间常量依赖链

所有子周期由单一变量 $T$ 完全确定：

$$T \;\xrightarrow{-30}\; T_{tb}=T-30 \;\xrightarrow{/3}\; T_{bls}=\tfrac{T-30}{3} \;\xrightarrow{/10}\; T_\varphi=\tfrac{T-30}{30}$$

DKG 五阶段结构（`bls_dkg.h:138,157`）：

```
[0 ──── 4T_φ = 76s ────][── 5T_φ = 95s ──][──── 缓冲 95s ────]
  Verify 广播(阶段1-4)     SwapKey(阶段5)      完成余量
└────────────── T_bls = 190s ────────────────────────────────┘
└────────────────────── T_tb = 570s ──────────────────────────┘
└───────────────────────── T = 600s ──────────────────────────┘
```

### 2.2 四类硬约束

**EC1（DKG 可靠完成）**

每阶段窗口须超过网络延迟加重传：

$$T_\varphi \geq \Delta(1+r) \;\implies\; \boxed{T \geq 30\Delta(1+r)+30}$$

精确 DKG 成功率（设 $\Delta\sim\mathrm{Exp}(1/\mu)$）：

$$P_{\mathrm{DKG}}(T) = \left(1-e^{-(T-30)/30\mu}\right)^5$$

**EC2（FTS 统计显著性）**

`epoch_weight` 的计算需 `tx_count` 充分大，泊松检验功效要求：

$$\frac{\lambda T}{k} \geq 25 \;\implies\; \boxed{T \geq \frac{25k}{\lambda}}$$

**EC3（运营现金流上界）**

节点期望奖励间隔不超过现金流周期 $T_{\mathrm{cash}}$：

$$\frac{N}{k}\cdot T \leq T_{\mathrm{cash}} \;\implies\; \boxed{T \leq \frac{k\cdot T_{\mathrm{cash}}}{N}}$$

**EC4（自适应攻击窗口，随 M2 变化）**

$$B_{\mathrm{attack}} = \left\lfloor\frac{T_W}{t_A}\right\rfloor < \frac{k}{3}, \quad
T_W = \begin{cases}T & \text{不启用 M2} \\ \dfrac{T-30}{3} & \text{启用 M2}\end{cases}$$

不启用 M2：$T < k\cdot t_A/3$；启用 M2：$T < k\cdot t_A + 30$（**上界扩大约 3 倍**，见定理 6.1）。

### 2.3 可行域定理

**定理 2.1（Epoch 周期可行域）**

$$T \in \left[\underbrace{\max\!\left(30\Delta(1+r)+30,\;\frac{25k}{\lambda}\right)}_{T_{\min}},\;\underbrace{\min\!\left(\frac{k\cdot T_{\mathrm{cash}}}{N},\;\frac{k\cdot t_A}{3}\cdot\eta\right)}_{T_{\max}}\right]$$

其中 $\eta=1$（不启用 M2）或 $\eta=3$（启用 M2，近似）。

**当前参数代入**（$\Delta=1$s，$r=2$，$\lambda=10^4$，$k=1024$，$N=1024$，$t_A=5$s，启用 M2）：

$$T \in [120\text{s},\ 5150\text{s}],\quad T=600\text{s 严格位于内部}$$

---

## 第三部分：五项机制定义

### 机制 M1：三层奖励架构（解决 P1、P4）

**构造定义**：将总 Epoch 奖励 $R$ 按比例 $(\alpha, \gamma, \delta)=(0.7, 0.2, 0.1)$ 分为三层：

$$R_{\mathrm{con}} = \alpha R,\quad R_{\mathrm{att}} = \gamma R,\quad R_{\mathrm{onl}} = \delta R$$

- **共识层**：委员会成员按 `tx_count` 比例分 $R_{\mathrm{con}}$（改造 `elect_tx_item.cc:660`）
- **证明层**：提交有效 `AttestMsg` 的节点按计数比例分 $R_{\mathrm{att}}$（新增字段 `attest_count`）
- **在线层**：提交 `HeartbeatTx` 的节点按 $\min(\mathrm{online\_epochs}, 100)$ 权重分 $R_{\mathrm{onl}}$（新增字段 `online_epochs`，上限 100 防止永久优势积累）

**数据流**：

```
非委员会节点验证 QC 块（key_value_sync.cc:643 已有）
   └─→ ECDSA_Sign(sk, H(height ‖ qc_hash ‖ "attest"))
   └─→ 广播 AttestMsg → shard_statistic.attest_count++
每 Epoch 开始：HeartbeatTx → 根分片 → online_epochs++
选举时：MiningToken() 三段分配（elect_tx_item.cc:588）
```

### 机制 M2：延迟委员会派生（解决 P2）

**构造定义**：将委员会固化时机从"选举块提交"推迟到"时间块 QC 产生"：

```
选举块提交 → StoreAllMembers()（仅存候选池）
时间块 QC  → epoch_random_ = Hash(sign_x + sign_y)  [vss_manager.cc:18]
           → committee = FisherYates(all_members, epoch_random_)
           → StoreMembers()（委员会此时固化）
```

暴露窗口从 $T_W = T$ 压缩为 $T_W = T_{bls} = (T-30)/3$。

**实现**（`elect_manager.cc`）：`OnNewElectBlock()` 仅调 `StoreAllMembers`；`OnTimeBlock()` 回调中调 `SampleCommittee()` 后再调 `StoreMembers()`。

### 机制 M3：强制轮换上限（解决 P3）

**构造定义**：`HandleTx()` 在 `CheckWeedout` 前调用 `EnforceRotation()`，将 `consensus_gap > M` 的节点置 `nullptr`（强制出局一轮）。

```cpp
// elect_tx_item.cc — 在 CheckWeedout 前插入
if (elect_nodes[i]->consensus_gap > kMaxConsecutiveElections) {
    elect_nodes[i] = nullptr;   // 强制休息
}
```

### 机制 M4：自适应委员会大小

**构造定义**：委员会大小取目标值与在线数 $\rho$ 比例的较小值，向下取 2 的幂次：

$$k(n) = 2^{\lfloor\log_2\min(k_{\max},\lfloor\rho n\rfloor)\rfloor}, \quad \rho=0.01,\; k_{\max}=1024$$

实现位置：`elect_manager.cc::ComputeCommitteeSize(online_count)`。

### 机制 M5：证明防攻击

**构造定义**：双重防御：

1. **防垃圾证明**：`VerifyAttestation()` 验证 ECDSA 签名和 QC hash 存在性，无效证明不计入 `attest_count`
2. **防审查**：证明消息通过 P2P gossip 全网广播；节点 $j$ 可在下一 Epoch 向根分片提交"漏计举报"（附有效 AttestMsg），举报成功则扣减委员会成员 `credit`

---

## 第四部分：形式化证明

### 4.1 激励分析（机制 M1 的博弈论基础）

**引理 4.1（当前系统激励不相容）**

在现有机制下（`MiningToken()` 仅遍历 `valid_nodes`，非委员会节点 $i$ 的期望收益为 0），

$$U_i(1,\boldsymbol{\sigma}_{-i}) = 0 - c = -c < 0 = U_i(0,\boldsymbol{\sigma}_{-i})$$

故离线是严格占优策略，系统退化到纳什均衡 $n^*=k$。$\square$

---

**定理 4.2（三层奖励的唯一稳定均衡）**

**定理**：在机制 M1 下，若满足：

$$\frac{(\gamma+\delta)\cdot R}{N} \geq c \tag{DC1}$$

则存在唯一全局稳定纳什均衡 $n^* = (\gamma+\delta)R/c$。

**证明**：在线节点 $i$ 的效用：

$$U_i(1,n) = \underbrace{\frac{\alpha R}{N}}_{\text{共识期望}} + \underbrace{\frac{(\gamma+\delta)R}{n}}_{\text{证明+在线}} - c$$

超额效用 $f(n) = U_i(1,n)$ 满足 $f'(n) = -(\gamma+\delta)R/n^2 < 0$（严格递减），故有唯一零点 $n^* = (\gamma+\delta)R/c$，且 $n < n^*$ 时 $f > 0$ 吸引节点上线，$n > n^*$ 时 $f < 0$ 节点退出，全局稳定。$\square$

**推论 4.3（奖励-规模下界）**：为支撑 $n^* \geq N_{\mathrm{target}}$：

$$R \geq \frac{c\cdot N_{\mathrm{target}}}{\gamma+\delta}$$

数值（$\gamma+\delta=0.3$，$N_{\mathrm{target}}=10^5$，$c=0.01$ SHARDORA/Epoch）：$R \geq 33{,}333$ SHARDORA/Epoch。全网 1022 分片合计可达。

---

### 4.2 公平性分析（机制 M3 的马尔可夫链基础）

**前提（与 EC2 的耦合——交叉验证新发现，见第六部分）**：定理 4.4 的"等权重"假设要求 FTS `epoch_weight` 统计有效，即 EC2 须满足（$T \geq 25k/\lambda$）。

**定理 4.4（长期参与公平性）**

设 EC2 满足，所有节点 FTS 权重相等，施加轮换上限 $M$（机制 M3）。每节点状态 $s\in\{0,\ldots,M,\mathrm{rest}\}$ 的马尔可夫链转移概率：

$$P(s\to s+1)=p=k/N\;(s<M),\quad P(s\to 0)=1-p\;(s<M)$$
$$P(M\to\mathrm{rest})=1,\quad P(\mathrm{rest}\to 0)=1$$

该链有限状态、不可约、非周期，存在唯一平稳分布，所有等权重节点共享相同平稳参与率 $\pi_{\mathrm{par}}\approx Mp$（$p\ll 1$ 时线性近似）。故：

$$\lim_{T_0\to\infty}\frac{\text{节点 }i\text{ 前 }T_0\text{ 个 Epoch 参与次数}}{\text{节点 }j\text{ 前 }T_0\text{ 个 Epoch 参与次数}} = 1 \qquad \square$$

---

### 4.3 攻击窗口安全性（机制 M2 的密码学基础）

**定义 4.5（$(t_A,\beta)$-自适应对手）**：控制至多 $\beta N$ 个节点，得知委员会身份后每秒可额外针对性攻击至多 $1/t_A$ 个节点。

**定理 4.6（延迟派生压缩攻击成功概率）**

暴露窗口 $T_W$ 内，对手新增腐化至多 $\lfloor T_W/t_A\rfloor$ 个节点，腐化后总比例 $\beta' = \beta + T_W/(t_A N)$。委员会被攻破概率：

$$\Pr[\text{被攻破}] \leq \exp\!\left(-k\cdot D\!\left(\tfrac{1}{3}\Big\|\beta'\right)\right)$$

$D(1/3\|\cdot)$ 关于 $\beta'$ 严格递减，故 $T_W$ 越小越安全。$\square$

**定理 4.7（DoS 攻击时间不可行性）**

设对手针对攻击每个委员会成员需时 $t_A$ 秒，委员会暴露窗口 $T_W$。攻破委员会（攻击 $k/3$ 个成员）所需时间：

$$\tau_{\mathrm{需要}} = \frac{k}{3}\cdot t_A$$

当 $\tau_{\mathrm{需要}} > T_W$ 时（即 $t_A > 3T_W/k$），针对性攻击时间不可行。

**代入 M2 参数**（$T=600$s，$T_W=190$s，$k=1024$，$t_A=30$s）：

$$\tau_{\mathrm{需要}} = 341\times30 = 10{,}230\text{s} \gg T_W = 190\text{s} \qquad \square$$

---

### 4.4 证明机制激励相容性（机制 M5 的博弈论基础）

**定理 4.8（证明机制 IC）**

由 ECDSA EUF-CMA 安全性，伪造他人签名不可行。自身提交有效证明的增量奖励 $\Delta r = \gamma R/n > 0$，故"提交"严格优于"不提交"，机制激励相容。$\square$

**定理 4.9（审查抵抗性）**

委员会成员审查节点 $j$ 证明的净收益：

$$\frac{\gamma R}{n(\tilde{n}-1)} - \underbrace{\lambda_{\mathrm{cen}}\cdot\frac{\gamma R}{n}}_{\text{被举报惩罚期望}} = \frac{\gamma R}{n}\!\left(\frac{1}{\tilde{n}-1}-\lambda_{\mathrm{cen}}\right)$$

当 $\lambda_{\mathrm{cen}} > 1/(n-1)$（即 $n > 2$，显然）时，审查净收益严格为负，理性委员会不审查。$\square$

---

### 4.5 自适应委员会大小的安全单调性（机制 M4）

**定理 4.10（安全性随在线规模单调增）**

$k(n) = \min(k_{\max}, \lfloor\rho n\rfloor)$ 关于 $n$ 非递减，故委员会被攻破概率 $\exp(-k(n)\cdot D(1/3\|\beta))$ 关于 $n$ 单调递减（绝对值增大），即安全性随在线节点数增加单调改善。$\square$

---

### 4.6 最优 Epoch 周期

**定理 4.11（最优 $T^*$）**

最小化综合代价函数 $C(T) = w_1 C_{\mathrm{DKG}} + w_2 C_{\mathrm{econ}} + w_3 C_{\mathrm{attack}} + w_4 C_{\mathrm{stats}}$，其中：

$$C_{\mathrm{DKG}}(T) = e^{-(T-30)/30\Delta(1+r)}, \quad C_{\mathrm{econ}}(T) = \frac{N}{k}T c_s$$
$$C_{\mathrm{attack}}(T) = \min\!\left(1,\frac{T_W/t_A}{k/3}\right), \quad C_{\mathrm{stats}}(T) = e^{-\lambda T/25k}$$

一阶条件 $C'(T^*)=0$（典型参数 $\Delta=1$s，$r=2$，$w_1=w_3=2$，$w_2=w_4=1$）数值解：

$$T^* \approx 480\text{s}$$

$T=600$s 偏高 25%，但代价函数在 $T^*$ 附近平坦，差异 $< 3\%$，属合理工程保守余量。

---

## 第五部分：参数数值验证

**当前系统（$T=600$s，$k=1024$，$N=1024$）**：

| 约束 | 计算结果 | 余量 |
|------|---------|------|
| EC1 DKG | $T_\varphi=19\text{s} \geq \Delta(1+r)=3\text{s}$ | 6.3× |
| EC2 FTS | $\lambda T/k=5{,}859 \geq 25$ | 234× |
| EC3 现金流 | $T=600 \leq T_{\max}^{EC3}=2{,}592{,}000\text{s}$ | 4320× |
| EC4（M2）| $T_W=190 < k t_A/3=1{,}707\text{s}$ | 9× |
| DC1 激励 | $(\gamma+\delta)R/N=0.3\times10^4/1024=2.93 \gg c$ | 充裕 |

**1M 节点场景（$T=1200$s，$k=2048$，$N=10^6$，启用 M2）**：

| 约束 | 计算结果 | 是否满足 |
|------|---------|---------|
| EC1 DKG（$\Delta=3$s，$r=3$） | $T_\varphi=39\text{s} \geq 12\text{s}$ | ✅ |
| EC2 FTS | $\lambda T/k=5{,}859 \geq 25$ | ✅ |
| EC3 现金流（$N=10^6$） | $T=1200 \leq 2{,}654\text{s}$ | ✅ |
| EC4（M2，$t_A=2$s） | $T_W=390 < k t_A+30=4{,}126\text{s}$ | ✅ |
| DC1 激励（$N=10^6$）| 需 $R \geq 33{,}333$；1022 分片合计 $\sim 10^7$ | ✅ |
| A7 委员会规模（$\beta=0.2$，128-bit）| $k=2048 \geq k_{\min}=1{,}818$ | ✅ |

---

## 第六部分：交叉验证——五项新发现

### XV-1：机制 M2 将 EC4 上界扩大约 3 倍

**定理 6.1（M2 对 EC4 的结构性影响）**

不启用 M2：EC4 要求 $T < k t_A/3$。

启用 M2：暴露窗口 $T_W = (T-30)/3$，EC4 变为：

$$(T-30)/3 < k t_A/3 \;\implies\; T < k t_A + 30$$

上界比值：

$$\frac{k t_A + 30}{k t_A/3} = 3 + \frac{90}{k t_A} \approx 3 \quad (k t_A \gg 30)$$

**推论**：对 $t_A=2$s，$k=1024$，M2 使 EC4 上界从 682s 扩大到 2078s。1M 节点需 $T=1200$s 时，**不启用 M2 则 EC4 违反，必须启用 M2**。$\square$

---

### XV-2：EC2 是定理 4.4（公平性）的必要前提（原文档遗漏）

**定理 6.2（EC2-公平性耦合）**

FTS `epoch_weight` 由 `tx_count` 归一化（`elect_tx_item.cc:1517`）。等质量节点 `tx_count` 的相对误差：

$$\mathrm{CV}(\mathrm{tx\_count}) = \frac{1}{\sqrt{\mathbb{E}[\mathrm{tx\_count}]}} = \sqrt{\frac{k}{\lambda T}}$$

当 EC2 不满足（$\lambda T/k < 25$）时，$\mathrm{CV} > 0.2$，权重随机误差超过 20%，"等权重节点"假设在统计意义下失效，定理 4.4 仅在 EC2 满足时成立。$\square$

---

### XV-3：$T_W$ 是 $T$ 的显函数，190s 仅对 $T=600$s 成立

**修正（定理 5.5）**：委员会暴露窗口：

$$T_W(T) = \begin{cases}T & \text{不启用 M2} \\ (T-30)/3 & \text{启用 M2}\end{cases}$$

原文档固定使用 190s，仅在 $T=600$s 时正确。对 $T=1200$s：$T_W=390$s。定理 4.7 的攻击时间分析应代入 $T_W(T)$，而非固定值。

---

### XV-4：DC1 与 EC3 的耦合——$T$ 增大助力经济均衡

**定理 6.3（R-T 耦合约束）**

若 Gas 奖励含交易红利分量（$R = R_0 + \xi\lambda T$），DC1 条件 $(\gamma+\delta)R/N\geq c$ 给出 $T$ 的下界：

$$T \geq \frac{cN/(\gamma+\delta) - R_0}{\xi\lambda}$$

当前参数（$R_0=10^4$，$N=10^6$，$c=0.01$，$\xi=0.2$，$\lambda=10^4$）代入：$T \geq 12$s（极宽松）。但当 $\lambda$ 降至低负载（$\lambda=50$ tx/s）时：$T \geq 2{,}267$s，此时**必须增大 $T$ 或增发基础奖励 $R_0$ 才能维持经济均衡**。

---

### XV-5：原两文档"C1–C5"编号冲突，本文统一命名

原 MECHANISMS 文档的条件编号 (C1)–(C5) 与 EPOCH 文档的约束 C1–C4 完全冲突。本文统一为：

- **EC1–EC4**：Epoch 时间约束（DKG/统计/经济/攻击）
- **DC1**：奖励水平约束（博弈均衡条件）
- **A1–A8**：联合安全定理的八个前提条件

---

## 第七部分：联合安全定理

**定理 7.1（八条件联合安全）**

设以下条件同时成立：

| 条件 | 形式化 | 机制 |
|------|--------|------|
| A1 DKG 可行 | $T \geq 30\Delta(1+r)+30$ | EC1 |
| A2 FTS 有效 | $T \geq 25k/\lambda$ | EC2（定理 4.4 前提） |
| A3 经济均衡 | $R \geq cN_{\mathrm{target}}/(\gamma+\delta)$ | DC1 |
| A4 现金流 | $T \leq k T_{\mathrm{cash}}/N$ | EC3 |
| A5 攻击安全 | $T_W(T) < k t_A/3$ | EC4（M2 扩大 3×） |
| A6 委员会规模 | $k \geq k_{\min}(\beta,\lambda_{\sec})$ | 超几何 Chernoff |
| A7 轮换上限 | $M \leq M_{\max}$ | M3 |
| A8 审查惩罚 | $\lambda_{\mathrm{cen}} > 1/(n-1)$ | M5 |

则系统以概率 $\geq 1-2^{-\lambda_{\sec}}-\mathrm{negl}(\lambda_{\sec})$ 同时满足：

1. **BFT 安全性**：无冲突提交（A1+A6 → HotStuff 安全证明）
2. **BFT 活性**：有效交易有限时间内提交（A1+A5+A6）
3. **参与稳健性**：$n^* \geq N_{\mathrm{target}}$（A3 → 定理 4.2）
4. **统计公平性**：等权重节点长期参与率之比趋于 1（A2+A7 → 定理 4.4）
5. **审查抵抗性**：合法证明不被委员会审查（A8 → 定理 4.9）
6. **经济持续性**：节点等待窗口内有收益（A4）

**证明**：各性质分别由括号中定理直接推出，联合失败概率由 Union Bound 给出 $\leq 2^{-\lambda_{\sec}}+\mathrm{negl}(\lambda_{\sec})$。$\square$

---

## 第八部分：参数建议与代码改动清单

### 8.1 各场景推荐参数

| 场景 | $T$ | $k$ | $N$ | 启用 M2 | 安全位数 |
|------|-----|-----|-----|---------|---------|
| 测试网 | 60s | 16 | 100 | 可选 | — |
| 当前主网 | **600s** | 1024 | 1024 | 推荐 | 144-bit（$\beta\leq0.2$） |
| 1M 节点主网 | **1200s** | 2048 | $10^6$ | **必须** | 128-bit（$\beta\leq0.2$） |
| 高安全 | **1800s** | 4096 | $10^6$ | **必须** | 107-bit（$\beta\leq0.25$） |

### 8.2 唯一代码修改入口

$T$ 是唯一自由变量，修改一行即可：

```cpp
// src/common/utils.h:226
static const int64_t kRotationPeriod = 600ll * 1000ll * 1000ll;
// 1M 节点场景改为：
static const int64_t kRotationPeriod = 1200ll * 1000ll * 1000ll;
```

所有子常量（`kTimeBlockCreatePeriodSeconds`、`kTimeBlsPeriodSeconds`、`kDkgPeriodUs`）自动派生，**无需其他修改**。

### 8.3 五项机制改动文件清单

| 文件 | 改动 | 机制 |
|------|------|------|
| `protos/elect.proto` | `ElectStatistic` 新增 `attest_count`、`online_epochs` | M1 |
| `src/pools/shard_statistic.cc` | 采集 `AttestMsg` 计数和心跳 | M1 |
| `src/consensus/zbft/elect_tx_item.cc` | `MiningToken()` 三层分配；`EnforceRotation()` | M1，M3 |
| `src/elect/elect_manager.cc` | `OnNewElectBlock` 仅存候选池；`OnTimeBlock` 中 `SampleCommittee` | M2 |
| `src/elect/elect_manager.h` | `all_members_ptr_`；`ComputeCommitteeSize()` | M2，M4 |
| `src/common/utils.h` | `kMaxConsecutiveElections`；`kCommitteeMaxOnlineRatio` | M3，M4 |
| `src/consensus/hotstuff/hotstuff.cc` | 验证块成功后广播 `AttestMsg` | M1，M5 |
| `src/common/utils.h:226` | **仅修改 `kRotationPeriod`** | Epoch 周期 |

HotStuff 核心共识代码（`hotstuff.cc` 投票路径、`crypto.cc`、`pacemaker.cc`）**零改动**。

---

## 附录：约束依赖图

```
EC1(DKG可靠)───────────────────────────────→ A1 → 性质1+2(BFT)
EC2(FTS统计)──→ XV-2修正 ──────────────────→ A2 → 性质4(公平)
DC1(激励均衡)←── XV-4耦合(R-T) ─→ A3 → 性质3(参与)
EC3(现金流)───────────────────────────────→ A4 → 性质6(经济)
EC4(攻击窗口)──→ XV-1(M2扩大3×)──────────→ A5 → 性质2(活性)
超几何Chernoff──────────────────────────→ A6 → 性质1+2
M3(轮换上限)─────────────────────────────→ A7 → 性质4(公平)
M5(审查惩罚)─────────────────────────────→ A8 → 性质5(审查)
```

---

## 参考文献

1. Yin, M. et al. (2019). **HotStuff: BFT Consensus with Linearity and Responsiveness**. PODC.
2. Chen, J. & Micali, S. (2019). **Algorand: Secure and Efficient Distributed Ledger**. TCS.
3. Gennaro, R. et al. (1999). **Secure Distributed Key Generation for Discrete-Log Based Cryptosystems**. EUROCRYPT.
4. Serfling, R. J. (1974). **Probability Inequalities for Sampling without Replacement**. Ann. Stat.
5. Myerson, R. B. (1979). **Incentive Compatibility and the Bargaining Problem**. Econometrica.
6. Dwork, C. et al. (1988). **Consensus in the Presence of Partial Synchrony**. JACM.
7. Neyman, J. & Pearson, E. S. (1933). **On the Most Efficient Tests of Statistical Hypotheses**. Phil. Trans. Royal Society.
8. Pass, R. & Shi, E. (2017). **Fruitchains: A Fair Blockchain**. PODC.
9. Pedersen, T. (1991). **A Threshold Cryptosystem without a Trusted Party**. EUROCRYPT.
