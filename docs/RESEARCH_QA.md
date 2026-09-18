# Shardora 十万节点去中心化研究：核心问题确认与合作分工

> 基于 `UNIFIED_FORMAL_PROOF.md` 当前版本（N=10^5，M1–M6，随机中继广播）整理。供论文写作与实验规划确认。

---

## 第一轮确认（读文档后）

### Q1：这篇文章最核心的问题是什么？

**一句话**：在**十万候选池**规模下，如何同时解决激励兼容（非委员会节点有动力持续在线）、块广播效率（委员会向全网高效传播块）、委员会暴露窗口压缩（延迟确定防定向攻击）三个相互制约的难题？

**补充说明**：
- 候选池规模是 $N=10^5$（十万），不是百万。论文标题是「单分片十万节点」。上个版本从百万改为十万的原因：百万节点对应的 JoinElectTx 注册吞吐、DKG 通信量和经济激励参数更难落地，十万规模在现有 root shard 吞吐（$\lambda_{\text{root}} \approx 10^4$ tx/s）下可在 10 分钟内完成全部候选节点注册，且 DKG 仍是 $O(k^2)$ 与 N 无关。
- 整体去中心化（有效 Nakamoto 系数 31,790，比原始委员会提升 93×）和委员会安全性是这三个问题解决后的**结果**，不是出发点。
- 与 Algorand/ETH2 的差异化在于：Algorand 没有非委员会激励，ETH2 没有候选/委员会分层，本方案首次在同一框架内同时形式化三者。

---

### Q2：M1–M6 里哪些是核心创新，哪些是辅助机制？

| 机制 | 定位 | 理由 |
|------|------|------|
| **M1 三层奖励** | **核心创新** | 将激励与「是否入选委员会」解耦；注册奖励 $r_{\text{reg}}$ + 证明奖励 `attest_count` 是候选池经济可持续的基础；现有代码中奖励仅分给委员会成员，M1 是对此的根本性修正 |
| **M2 延迟委员会派生** | **核心创新** | 用 `epoch_random_`（时间块 BLS 门限签名）在 Epoch 末才固化委员会，将暴露窗口从 $T=600$s 压缩至 $T_{\text{bls}}=190$s；**不依赖 VDF**，这是与 Algorand/ETH2 最主要的差异之一 |
| **M6 子分片广播** | **核心创新（使能性）** | 委员会为每子分片选 1 个中继节点（共 R=P=100 个），各向其所属唯一子分片注入块；复用现有 P2P 子分片拓扑，委员会发送量从 $O(N)$ 降至 $O(R)=O(100)$，零新增基础设施，无 BLS 聚合逻辑，确定性 100% 覆盖 |
| M3 强制轮换 | 辅助 | 常见设计；本文贡献是加权 Markov 链 Gini 上界（定理 4.4'），机制本身不新颖 |
| M4 自适应委员会大小 | 辅助 | 工程保护措施，无独立理论贡献 |
| M5 证明防攻击（含双签罚没） | 辅助 + 配套 | 为堵安全漏洞而加；防垃圾证明和防审查是 M1 的配套机制 |

**三句话核心贡献**：M1 解决「为什么在线」，M2 解决「在线后如何安全」，M6 解决「块如何高效传播到十万候选节点」。三者共用同一信标随机性（`epoch_random_`），在单一协议轮次内协同生效。

---

### Q3：代码已实现 vs. 仅文档设计

#### 已实现

| 内容 | 代码位置 |
|------|---------|
| HotStuff 核心共识（投票、QC、Leader rotation） | `hotstuff.cc`，零改动 |
| BLS DKG 五阶段 Pedersen 协议 | `bls_dkg.h/cc`，`kDkgPeriodUs` 时间参数 |
| Fisher-Yates 委员会抽签（`epoch_random_`） | `elect_tx_item.cc:59`，`mt19937_64` 驱动 |
| 非委员会节点验证 QC | `key_value_sync.cc:643`，`EnqueueVerifyBlock()` |
| 当前奖励仅分给委员会（P1 问题根因） | `elect_tx_item.cc:595`，`valid_nodes` 遍历 |
| **子分片 P2P 拓扑**（每节点属于 J=1 个子分片，pool_id=H(node_id) mod P） | 已有实现（确定性分配 + 子分片内 Gossip） |
| **JoinElectTx 容量控制**（每子分片 ≤ 1024 节点，P=100 个子分片） | root shard 共识强制，已有实现 |

#### 仅文档设计，代码未实现

| 内容 | 备注 |
|------|------|
| **M1 三层奖励**（`attest_count`、`HeartbeatTx`） | proto 字段未加，`MiningToken()` 未改 |
| **M2 延迟委员会派生** | `OnTimeBlock` 中的 `SampleCommittee` 回调未实现 |
| M3 强制轮换（`EnforceRotation`） | 代码片段在文档附录，未合并 |
| **M6 随机中继广播**（`relay_set` 选举 + 单播） | 子分片拓扑已有；需新增：FisherYates 选中继、委员会单播 R 条消息 |
| 注册协议（`JoinElectTx` + CISSSM PoRA） | Part 13 全部为理论设计 |
| 质押上限 $W_{\max}$、双签罚没 | 无对应代码 |

**结论：HotStuff 核心共识 + 子分片 P2P 拓扑 + JoinElectTx 容量控制已有；M1/M2/M6 的新增逻辑（奖励分层、延迟委员会、中继广播）均未实现。**

---

### Q4：节点规模与角色分工

**确认：候选池 $N=10^5$，委员会 $k=1024$，两层明确分离。**

| 工作 | 状态 | 说明 |
|------|------|------|
| **验证工作** | 已实现 | 收到每个 QC 块后执行 `EnqueueVerifyBlock()`，验证 BLS 聚合签名和交易合法性 |
| **候选资格维护** | 待实现 | 发送 `JoinElectTx` 注册进入候选池 $\mathcal{C}_T$，持续在线等待 FisherYates 抽签 |
| **M6 中继（被选中时）** | 待实现 | 被选为 relay_set 成员时，将块广播到所属唯一子分片（走现有块同步路径） |
| **M1 证明奖励（实现后）** | 待实现 | 验证通过后提交在线证明，获取注册层 + 证明层奖励 |

非委员会节点**不参与**出块投票（不在 `HandleVoteMsg` 路径），对 TPS 和出块延迟**零影响**。十万候选节点中任意时刻只有 $k=1024$ 个进入委员会出块。

---

### Q5：延迟委员会确定与现有 DKG / key pre-generation 的冲突

**这是当前理论处理最粗、工程风险最高的地方，存在真实冲突。**

#### 现状（已实现）

选举块提交后委员会立即固化 → DKG 在 $T_{\text{bls}}=190$s 窗口内完成 → 下一 Epoch 用新密钥出块。DKG 有专用 190s 窗口，时间充足。

#### M2 引入后的冲突

M2 要求委员会在时间块 QC 产生（Epoch 末）才固化，距下个 Epoch 开始**只剩 $T_{\text{bls}}=190$s**：
- DKG 必须在 190s 内完成，无额外缓冲
- 若委员会提前知道候选集合做 key pre-generation，对手也能提前知道候选集合，部分削弱 M2 的暴露窗口压缩效果

#### 两个可选方向

| 方向 | 做法 | 代价 |
|------|------|------|
| **方向 A：接受 190s 紧窗口** | 不做 pre-generation；严格依赖 190s 完成 DKG；Epoch Extension 兜底（DKG 失败时延用上届密钥） | DKG 成功率 ≈ 99%（每 16.6 小时一次失败），需公网实测验证 |
| **方向 B：候选集合公开，随机化结果延迟** | 选举块时公开候选委员会集合（DKG 可提前），时间块 QC 只决定最终席位顺序/权重随机化 | 对手提前知道候选集合，暴露窗口压缩效果部分削弱；但 DKG 时间充足 |

**当前建议**：论文初稿按**方向 A** 写（暴露窗口 190s 的安全性声明更强，是差异化贡献的核心）；代码实现前先做 DKG 公网实测，若 99% 成功率可复现则坚持方向 A，否则切换方向 B。这个决策需要在动手实现 M2 之前明确。

---

### Q6：实验方案规划

#### 对比方案

| 对比方案 | 差异化维度 |
|---------|-----------|
| **原始 Shardora**（$k=N=1024$ 静态委员会） | Baseline；Nakamoto 系数仅 342 |
| **Algorand** | VRF 自我选举，无非委员会激励，无候选池概念 |
| **ETH2 Beacon Chain** | 暴露窗口=整个 Epoch（6.4 min），依赖 VDF，无候选/委员会分层 |

#### 节点规模与实现方式

| 实验 | 推荐方式 | 理由 |
|------|---------|------|
| DKG 压力测试（$k=1024$） | **实机跨多云**（AWS/GCP/Azure 各约 340 节点） | 必须是真实 WAN 延迟；容器模拟无法反映网络毛刺和 Complaint 连锁 |
| M6 广播延迟测试 | **实机**（$R=P=100$ 中继，$P=100$ 子分片，$J=1$） | 验证 2 跳传播延迟 ≤ $t_b/2=5$s；测量 pull 同步触发频率 |
| $N=10^5$ 候选池验证 | **仿真 + 实跑**（实跑 $N=10^3$–$10^5$） | 十万节点实机成本可接受；Nakamoto 系数公式可解析验证 |

#### 关键实验指标（Part 15.3 的最低要求）

1. **DKG 成功率 CDF**：注入 10%/20% 拜占庭故障，对比 $T_{\text{bls}}=190$s 与 300s 两个窗口
2. **块广播效率**：直接 Gossip vs. M6 随机中继（$R=P=100$，$J=1$）的委员会发送量对比（目标：$O(N)$ → $O(R)=100$ 条/块，确定性覆盖）
3. **Nakamoto 系数 vs. N**：验证 $\mathcal{N}_{\text{eff}}/\mathcal{N}^* \to 95.4\%$ 的拟合曲线

---

### Q7：论文分工建议

#### 你来准备（理论 + LaTeX 排版）

| 内容 | 文档现状 |
|------|---------|
| Introduction + Related Work | 对比 Algorand/ETH2 的差异化在文档 C3 已写好，转 LaTeX 即可 |
| 系统模型 + 机制定义（M1/M2/M6） | 形式化定义已完备，精炼措辞 |
| 安全性证明（定理 4.1–7.1、13.x） | 完整证明链已有，需检查符号统一 |
| 去中心化极限分析（Part 10） | $N_{\text{eff}}=31,790$，93× 提升推导已完备 |
| 定理 4.4' 加权 Markov 链分析 | 封闭解和 Gini 上界已给出 |
| M6 广播复杂度定理（定理 9.4） | 随机中继方案定理已完备 |

#### 我来准备（实验 + 实现）

| 内容 | 优先级 |
|------|--------|
| **Q5 的工程决策**：方向 A vs. B，明确 M2 实现路线 | 🔴 最优先，阻塞实验 |
| 跨云 DKG 测试环境搭建（$k=1024$，3 云区域） | 🔴 高，需提前申请资源 |
| M1 + M6 最小可用实现 | 🟡 中，用于广播延迟测试 |
| DKG 成功率 CDF 图、Nakamoto 系数 vs. N 曲线 | 🟡 中，论文图表 |
| 块广播直接 Gossip vs. M6 中继延迟测试 | 🟢 可并行 |

---

## 第二轮确认（开始整理初稿前）

### Q8：论文核心贡献按 M1/M2/M6 三个来写，M3–M5 作为辅助机制？

**确认，按此写。**

具体结构建议：
- **Section 3（System Model）**：定义候选池/委员会两层模型，说明 HotStuff 核心共识不变
- **Section 4（Mechanism Design）**：
  - 4.1 M1：三层奖励（经济基础）
  - 4.2 M2：延迟委员会派生（安全基础）
  - 4.3 M6：随机中继广播（通信基础）
  - 4.4 Auxiliary：M3 轮换 + M4 自适应 + M5 防攻击（一节简述，不单独设节）
- **Section 5（Formal Analysis）**：定理 4.2、4.6、9.4、10.3 为核心，其余定理可移至 Appendix

---

### Q9：M2 和 DKG 的冲突，初稿先按哪个方案写？

**初稿按方向 A 写，保留方向 B 为脚注备选。**

具体建议：
- 正文安全性声明按「暴露窗口 $T_W = 190$s，DKG 在 190s 窗口内完成（成功率 99%，定理 13.12）」
- 在 Section 6（Discussion）或 Limitations 中用一段话说明：「方向 A 依赖 190s 内完成 DKG 的工程可行性，将通过 §7.1 的跨云压力测试验证；若实测成功率低于 95%，可切换到方向 B（候选集合提前公开），代价是将暴露窗口加长至选举块确认后的剩余 Epoch 时间」
- 公式和数值结论先按方向 A 填入，之后实测数据出来后可直接替换

---

### Q10：理论公式和数值结论，发现问题可以直接修改吗？

**可以直接改，但请标注修改原因。**

目前存在以下已知可能需要核查的地方：

| 位置 | 潜在问题 | 建议 |
|------|---------|------|
| 定理 13.12（DKG 成功率 99%） | 基于 $\Delta<5$s + 无 Complaint 攻击，公网对抗环境下需实测修正 | 标注「待实测替换」 |
| $R=100$ vs $R=200$ 的覆盖率计算 | 期望覆盖率基于均匀哈希假设，实际分布可能偏斜 | 可调整 R 推荐值，保留计算方法 |
| $\beta^*=0.3179$（Nakamoto 系数中的 $\beta^*$） | 数值解，依赖 $k=1024$ 的 KL 散度数值积分 | 可复现验证，若有误直接改 |
| 注册吞吐 $\lambda_{\text{root}} = 10^4$ tx/s | 来自文档假设，需对照 `root shard` 实测吞吐 | 对照代码实测后替换 |

---

### Q11：实验部分初稿先搭结构、具体数据后填，可以吗？

**完全可以，按以下框架搭：**

```
Section 7: Evaluation
  7.1 DKG Reliability Under Byzantine Faults
      RQ: Does DKG complete within 190s under 20% Byzantine nodes?
      Baseline: Epoch Extension (fallback to previous epoch keys)
      Metric: Success rate CDF over 100 runs, 10%/20% Byzantine ratio
      [FIGURE 1: CDF curves — placeholder]

  7.2 Block Broadcast Efficiency (M6)
      RQ: How much does random-relay broadcast reduce committee send cost?
      Baseline: Direct Gossip (committee floods all N nodes)
      Metric: Committee outbound messages/block, end-to-end propagation latency
      [FIGURE 2: message count vs. N, latency CDF — placeholder]

  7.3 Nakamoto Coefficient vs. Candidate Pool Size
      RQ: Does N_eff/N converge to 95.4% as predicted?
      Baseline: Static committee k=N=1024 (N_eff = 342)
      Metric: N_eff measured via 10^4 Fisher-Yates draws at each N
      [FIGURE 3: N_eff/N vs. log(N), fitted curve — placeholder]

  7.4 Incentive Equilibrium (optional, if M1 implemented)
      RQ: Does online node count converge to n* = (γ+δ)R/c?
      [FIGURE 4: equilibrium convergence — placeholder]
```

---

## 当前最紧迫的三个决策

| 优先级 | 决策 | 影响 |
|--------|------|------|
| 🔴 **P1** | Q9 的 DKG 冲突：方向 A（190s 紧窗口）还是方向 B（候选集合提前公开） | 影响 M2 的安全性声明和 DKG 实验设计 |
| 🔴 **P2** | M6 中继节点数 $R$ 的最终值（100 vs 200 vs 500）以及覆盖率目标 | 影响实验 7.2 的 baseline 和结论 |
| 🟡 **P3** | 论文目标会议（IEEE S&P / ACM CCS / USENIX ATC）确定 | 影响证明详细程度、实验规模要求和页数限制 |

**当前阶段的边界**：上述三个决策确认前，论文初稿结构、LaTeX 排版和占位图可以正常推进；安全性声明的精确数值（190s vs 其他）和实验 Figure 需在 P1 决策后锁定。
