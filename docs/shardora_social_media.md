# Shardora 多期社媒推广文案

> 人设：硕士毕业后，业余时间独立开发，400,000+ 行 C++ 区块链代码，4 篇顶刊/顶会。
> 项目主页：https://github.com/tenondvpn/shardora

---

## 论文信息

| 论文 | 期刊/会议 | DOI |
|------|-----------|-----|
| Akaverse | IEEE TDSC | — |
| Shardora | IEEE TNSE | 10.1109/TNSE.2026.3684813 |
| SCoRE | SOSP 2026 | — |
| NMFT | IEEE TIFS | 10.1109/TIFS.2025.3639980 |

---

## 第 1 期｜四张图·完整策划

> 统一视觉规范：深色背景（#0D0D0D 或深海军蓝），主色调电光蓝（#00C2FF）+ 白色文字，
> 关键数字用橙色（#FF6B00）高亮，尺寸 3:4（1080×1440px），字体建议 HarmonyOS Sans / PingFang SC

---

### 图 1｜震撼钩子：以太坊 2.0 分片架构被攻破

**视觉设计**

```
背景：以太坊菱形 Logo 碎裂/裂纹特效，裂缝中透出蓝色光芒
     裂缝处流出「40万行代码」的字符流（类黑客帝国绿字雨）
左上角：小字 "2026 · Open Source"
中央大标题区（从上到下）：
```

**图面文字（从上到下排列）**

```
┌─────────────────────────────────┐
│                                 │
│   [以太坊菱形Logo · 碎裂状态]   │
│                                 │
│  以太坊 2.0                     │
│  分片架构技术                   │
│  ——被彻底攻破                   │  ← 主标题，极大字号，白色
│                                 │
│  并完全开源                     │  ← 副标题，电光蓝，稍小
│                                 │
│  ────────────────────────────   │
│                                 │
│  Web3 真正的颠覆                │  ← 说明文字
│  正在发生                       │
│                                 │
│  github.com/tenondvpn/shardora  │  ← 小字，底部
└─────────────────────────────────┘
```

**配套正文（贴文标题 + 首段）**

```
以太坊 2.0 喊了十年的分片，我一个人在业余时间做出来了，还开源了。

这不是噱头。
→ 4 篇顶会/顶刊论文（SOSP · IEEE TDSC · IEEE TNSE · IEEE TIFS）
→ 40 万行 C++ 代码，全部在 GitHub
→ 实测 40,000 TPS，确认 <2 秒，5000+ 节点

滑动看下一张 →
```

---

### 图 2｜问题引入：当前所有公链无法解决的场景

**视觉设计**

```
背景：深色，左半部分展示「需求场景数字墙」，右半部分展示「公链性能红叉对比」
整体感：数据驾驶舱 / 金融终端风格
```

**图面文字**

```
┌─────────────────────────────────────────┐
│                                         │
│  想象一个真实的链上金融系统：            │  ← 小标题
│                                         │
│   100                                   │
│   个 TOKEN                              │  ← 数字极大，橙色高亮
│                                         │
│   ~5,000 对 AMM 交易池                  │
│   100,000 个用户同时交易                │
│                                         │
│  ─────────────────────────────────────  │
│                                         │
│  当前公链能做到吗？                      │
│                                         │
│  Ethereum    15 TPS    ✗ 根本不够用     │  ← 红色 ✗
│  Solana      4K TPS    ✗ 多次宕机       │  ← 红色 ✗
│  BNB Chain   21个节点  ✗ 严重中心化     │  ← 红色 ✗
│  Near        跨分片延迟>6秒  ✗          │  ← 红色 ✗
│                                         │
│  没有一条链能同时满足：                  │
│  高TPS  ×  快确认  ×  真去中心化        │  ← 电光蓝，加粗
│                                         │
└─────────────────────────────────────────┘
```

**配套正文（第二段）**

```
10 万人同时在链上做 DeFi，这不是幻想——
Uniswap 高峰期单日交易量已经超过纽交所。

但现实很残酷：

以太坊：15 TPS，高峰期 gas 费能买一顿好饭
Solana：号称 65K TPS，实际高峰宕机超过 10 次
BNB Chain：21 个超级节点，去中心化只是幌子
Near 分片：跨分片调用要等 6 秒以上，AMM 直接废了

这个场景，当前所有公链都解决不了。

滑动看 Shardora 怎么做的 →
```

---

### 图 2.5｜数据对比图：主流公链横向对比（可单独传播）

> 此图可独立作为一张「数据信息图」发布，也可插入第 1 期图 2 与图 3 之间作为第 3 张轮播图。

**视觉设计**

```
尺寸：1080×1350px（4:5 竖版，适合小红书信息图）
背景：深色（#0D1117）
字体：HarmonyOS Sans / PingFang SC
主色：电光蓝 #00C2FF（Shardora）
警示色：红色 #FF4444（其他链问题标注）
高亮色：橙色 #FF6B00（关键数字）
布局：上部坐标图 + 下部雷达图 二合一
```

**图面完整布局（从上到下）**

```
┌────────────────────────────────────────────┐
│                                            │
│  主流公链能力全景对比                       │  ← 顶部标题，白色
│  TPS × 去中心化 × 扩容能力                 │  ← 副标题，灰色小字
│                                            │
│  ══════════════ 第一部分 ══════════════    │
│  [ TPS vs 验证节点数 二维坐标图 ]           │
│                                            │
│  真实TPS                                   │
│  40K ┤                          ★          │
│      │                    Shardora         │  ← 蓝色大光晕气泡
│      │              （唯一右上角）          │  ← 橙色小标注
│  15K ┤          ◉Near(理论)                │  ← 虚线边框气泡
│      │                                    │
│   5K ┤     ◉Avalanche                     │
│      │          ◉Solana ⚠️宕机            │  ← 红色警告标
│   1K ┤      ◉Harmony ⚠️$1亿被黑          │  ← 红色警告标
│  500 ┤ ◉BNBChain(21节点)                  │
│   30 ┤◉Ethereum                           │
│      └──────────────────────────────▶     │
│        21  100  400  1200  5000+  节点数   │
│                                            │
│  ┌────────────────────────────────────┐   │
│  │  右上角空白区域                     │   │
│  │  「高性能 × 真去中心化」            │   │  ← 虚线框，橙色文字
│  │  当前所有公链的空白地带             │   │
│  └────────────────────────────────────┘   │
│                                            │
│  ══════════════ 第二部分 ══════════════    │
│  [ 五维雷达图：Ethereum vs Solana vs Shardora ] │
│                                            │
│           吞吐量(TPS)                      │
│              △                            │
│  扩容路径 ◀──┼──▶ 去中心化               │
│              ▽                            │
│  DeFi原子性──────确认速度                 │
│                                            │
│  ████ Shardora  （五边形最大，蓝色填充）   │
│  ···· Ethereum  （DeFi强，TPS极弱）       │
│  ---- Solana    （速度强，去中心化极弱）   │
│                                            │
│  ══════════════ 第三部分 ══════════════    │
│  [ 关键指标速查卡 3×2 网格 ]               │
│                                            │
│  ┌──────────┬──────────┬──────────┐       │
│  │ 真实TPS  │  确认    │  节点数  │       │
│  │  40,000  │  <2秒    │  5000+   │       │  ← 橙色数字
│  │ ETH:15   │ SOL:2-3s │ SOL:1500 │       │  ← 灰色对比
│  ├──────────┼──────────┼──────────┤       │
│  │  分片数  │ 扩容方式 │ DeFi原子 │       │
│  │  1024上限│  链上治理│  原生支持│       │  ← 橙色
│  │ ETH:规划中│ETH:硬分叉│ ETH:同链│       │  ← 灰色对比
│  └──────────┴──────────┴──────────┘       │
│                                            │
│  ─────────────────────────────────────    │
│  🔗 github.com/tenondvpn/shardora          │  ← 底部，电光蓝
│  论文：SOSP'26 · IEEE TDSC · TNSE · TIFS   │  ← 小字，灰色
│                                            │
└────────────────────────────────────────────┘
```

**配套正文（此图单独发布时使用）**

```
一张图看懂：为什么 Shardora 是当前唯一能承载真实链上金融的公链。

上图第一部分是核心：
X 轴是验证节点数（去中心化程度），Y 轴是真实 TPS。
所有主流公链都挤在左下角——要么快但中心化，要么去中心化但慢。
右上角「高性能 × 真去中心化」的区间，只有 Shardora 一个点。

数据来源：
- Ethereum：主网实测 15-30 TPS，Danksharding 尚未完成
- Solana：实测峰值约 4K TPS，2021-2024 年超过 10 次宕机
- Harmony：4 分片，2022 年 Horizon 桥被盗 $1 亿，已基本停滞
- Near：跨分片调用需等待 2-3 个额外区块（>4 秒），AMM 路由无法使用

Shardora：
→ 当前 4 分片，40,000 TPS，确认 <2 秒，5000+ 节点
→ 最多 1024 分片，链上治理自动扩容，无需硬分叉
→ 理论峰值 ~1000 万 TPS（Visa 全球峰值的 400 倍）

论文支撑：4 篇顶会/顶刊，每一项技术都有形式化证明。

#区块链 #DeFi #公链对比 #以太坊 #Solana #Shardora #Web3
```

---

### 图 3｜技术破局：Shardora 如何做到

**视觉设计**

```
背景：多车道高速公路俯视图（象征并行分片），或抽象六边形分片网络图
右侧：竖排核心数据卡片，每条数据一个色块
左侧：创新点列表
```

**图面文字**

```
┌─────────────────────────────────────────┐
│                                         │
│  Shardora                               │  ← Logo + 项目名
│                                         │
│  ┌──────────┐  ┌──────────┐            │
│  │ 40,000   │  │  <2秒    │            │  ← 数据卡片，橙色数字
│  │   TPS    │  │  确认    │            │
│  └──────────┘  └──────────┘            │
│  ┌──────────┐  ┌──────────┐            │
│  │  5000+   │  │ 1024分片 │            │
│  │  节点    │  │  上限    │            │
│  └──────────┘  └──────────┘            │
│                                         │
│  ─────────────────────────────────────  │
│                                         │
│  怎么做到的？                            │
│                                         │
│  ① BLS门限签名  → O(1)验证，1000节点不慢 │
│  ② 双委员会    → 换届全程不停机          │
│  ③ 声誉洗牌    → 防分片被腐化            │
│  ④ 跨分片AMM   → 数学级安全，零假币      │
│                                         │
│  链上治理自动扩容 · 无需硬分叉           │  ← 底部说明，电光蓝
│                                         │
└─────────────────────────────────────────┘
```

**配套正文（第三段）**

```
Shardora 的答案：

4 个分片 + 1 个信标链，当前跑 40,000 TPS
确认时间不到 2 秒
5000+ 节点，真正的去中心化

不够用？链上投票，自动加分片。
最多 1024 个分片，理论峰值 ~1000 万 TPS。
Visa 全球峰值是 24,000——Shardora 上限是 Visa 的 400 倍。

核心技术：

▸ BLS 门限签名：1000 个节点签名压缩成 1 个，验证速度与节点数无关
▸ 双委员会：新旧节点无缝交接，换届期间 TPS 不掉
▸ 声誉洗牌（FTS）：节点按信用随机分配，买不通一个分片
▸ Feistel 跨分片 AMM：数学证明零假币，历史上 $20 亿跨链桥漏洞的根治方案

4 篇顶会/顶刊论文，每一项都是独立的学术级突破。

滑动看最后一张 →
```

---

### 图 4｜个人人设：一个人做了这些

**视觉设计**

```
背景：程序员书桌场景（暗光，屏幕发光），或简洁白底学术风
中央：时间线 / 成就墙布局
右下角：GitHub 头像占位框（可替换真实头像）
整体感：克制、真实、有温度，与前三张技术感形成反差
```

**图面文字**

```
┌─────────────────────────────────────────┐
│                                         │
│  硕士毕业后                              │
│  我用业余时间做了一件事                  │  ← 主标题，白色大字
│                                         │
│  ─────────────────────────────────────  │
│                                         │
│  📝  论文 × 4                           │
│      SOSP 2026  ·  IEEE TDSC            │
│      IEEE TNSE  ·  IEEE TIFS            │
│                                         │
│  💻  代码 × 400,000 行                  │  ← 数字橙色高亮
│      C++  ·  全部开源                   │
│                                         │
│  👤  1 个人                             │
│      没有团队  ·  没有经费              │
│      只有白天上班后的晚上               │
│                                         │
│  ─────────────────────────────────────  │
│                                         │
│  github.com/tenondvpn/shardora          │  ← 底部，电光蓝
│  欢迎 Star ⭐                            │
│                                         │
└─────────────────────────────────────────┘
```

**配套正文（结尾段）**

```
我叫——，硕士毕业，现在是一名普通的打工人。

白天上班，晚上写代码。
不是为了跳槽，不是为了 KPI。
就是觉得这件事值得做到极致。

40 万行代码，4 篇顶会。
一个人，业余时间，五年。

项目叫 Shardora，完全开源。
如果你做 DeFi、研究区块链、或者只是好奇，
GitHub 在简介，欢迎来 Star。

如果你也在用业余时间做一件「不知道值不值得」的事——
评论区聊聊，我们都是同类。

#区块链 #DeFi #开源 #独立开发者 #程序员 #以太坊 #Web3
```

---

### Twitter/X（对应发布）

```
Ethereum 2.0's sharded architecture — solved, open-sourced, shipped.

Not by a team. Not with VC funding.
By one person, in spare time, after work.

Here's what it can do that no other chain can: 🧵
```

```
The scenario no current blockchain can handle:

→ 100 tokens
→ ~5,000 AMM trading pairs
→ 100,000 users trading simultaneously on-chain

Ethereum: 15 TPS ✗
Solana: 4K real TPS, multiple outages ✗
Near: cross-shard latency >6s, AMM broken ✗
BNB Chain: 21 validators, not decentralized ✗

No existing chain hits all three: high TPS × fast finality × true decentralization.
```

```
Shardora's numbers:

⚡ 40,000 TPS (4 shards, current)
⏱ <2s confirmation
🌐 5,000+ nodes
📈 Up to 1,024 shards via on-chain governance (no hard fork)
🔢 Theoretical peak: ~10,000,000 TPS (~400× Visa's global peak)

How?
→ BLS threshold sigs: O(1) verification regardless of committee size
→ Dual committee: zero TPS drop during node rotation
→ FTS reputation shuffle: shard corruption economically irrational
→ Feistel cross-shard AMM: math-level zero-counterfeit proof
```

```
The person behind it:

Master's graduate. Day job. Nights and weekends.

400,000+ lines of C++
4 top-tier publications:
  - SOSP 2026
  - IEEE TDSC
  - IEEE TNSE (DOI: 10.1109/TNSE.2026.3684813)
  - IEEE TIFS (DOI: 10.1109/TIFS.2025.3639980)

No team. No lab. No funding.

⭐ github.com/tenondvpn/shardora
```

---

## 第 2 期｜技术向·分片扩容（Shardora · IEEE TNSE）

### 小红书

**配图概念**

- 上：传统区块链"单车道"示意图（堵塞，红色）vs Shardora"多车道并行"示意图（绿色流畅）
- 下：数据对比卡片
  - 单分片：8326 TPS / 600 节点
  - 6 分片：50,000+ TPS
  - 同步开销：比现有方案低 **50~1000×**
- 风格：简洁信息图，蓝紫渐变，数字加粗

**文案**

区块链最大的敌人，不是黑客，是**堵车**。

所有节点验证同一批交易，天花板写死了。
以太坊主网现在还在 15~30 TPS 打转。

我的解法叫 **Shardora**——
把网络切成多个「分片委员会」，并行处理。

但分片有个老大难问题：
**节点换届的时候，TPS 会归零。**

Shardora 的方案：

✅ 「共识委员会 + 等待委员会」双委员会架构
✅ 新老节点无缝交接，**全程在线，TPS 不中断**
✅ 基于声誉分的节点洗牌（FTS 算法），防分片腐败
✅ 密钥预协商并行化，换届开销降低 **90%+**

实测结果：
单分片 **8326 TPS**（600 节点），
6 个分片跑出 **50,000+ TPS**。
账本同步开销比 RepShard / RepChain 低 **50~1000 倍**。

而且这不是天花板——
Shardora 最多支持 **1024 个分片**，
通过**链上治理投票自动扩容，无需硬分叉**。

理论峰值：1024 × ~10,000 TPS ≈ **千万级 TPS**。
Visa 全球峰值约 24,000 TPS。
Shardora 上限是 Visa 的 **400 倍**，且完全去中心化。

这篇论文已发表于 **IEEE TNSE**（Transactions on Network Science and Engineering），
DOI: 10.1109/TNSE.2026.3684813。

代码全部开源，链接在主页。

`#区块链` `#分布式系统` `#区块链性能` `#IEEE` `#开源项目`

---

### Twitter/X Thread

```
How do you scale a blockchain without killing it during reconfiguration?

That's the core problem Shardora (IEEE TNSE) solves. 🧵
```

```
The problem:
Every sharded blockchain faces a "Zero-TPS window" — the moment a shard
rotates its node committee, transaction processing stalls.
Existing systems either halt or pay massive sync overhead.
Neither is acceptable.
```

```
Shardora's answer: Dual-Committee Architecture

→ Consensus Committee: processes transactions
→ Waiting Committee: syncs state in the background

When rotation happens, the waiting committee steps in instantly.
Zero downtime. Zero TPS drop.
```

```
Numbers:
- Single shard: 8,326 TPS (600 nodes)
- 6 shards: >50,000 TPS
- Sync overhead: 50–1000× lower than RepShard/RepChain
- Key negotiation overhead: >90% reduction via parallelized DKG + secret reuse
- Max shards: 1,024 (on-chain governance, no hard fork needed)
- Theoretical peak: 1,024 × ~10K = ~10,000,000 TPS (~400× Visa's global peak)
```

```
Anti-corruption built in:
FTS (reputation-based node shuffling) makes it economically irrational
to compromise a shard.
Reputation scores are public and continuously updated.

Published: IEEE Transactions on Network Science and Engineering (TNSE)
DOI: 10.1109/TNSE.2026.3684813
Code: github.com/tenondvpn/shardora
```

---

## 第 3 期｜技术向·单分片极速共识（Akaverse · IEEE TDSC）

### 小红书

**配图概念**

- 动感流水线图：多个「Leader 通道」并行推进，像高速公路多车道
- 对比图：传统单 Leader（瓶颈，沙漏形）vs Akaverse 多 Leader（宽带，流量充盈）
- 角落数据标签：19,000 TPS · 1024 节点 · <10s 延迟
- 风格：科技感，动态线条，深蓝+橙色高亮

**文案**

共识算法里有个「堵头」问题：
一个 Leader 死了，后面的交易全部排队等死。

叫做 **Head-of-Line Blocking**。
也叫：你的区块链为什么动不动就卡顿。

我的解法叫 **Akaverse**，发表在 **IEEE TDSC**（顶级安全期刊）。

核心思路：**多 Leader 并行流水线**。
不是一条车道，是高速公路。

每个分片内部开多个「交易池」，每个池有独立 Leader，
谁死了谁那条道重选，其他道照跑。

怎么保证多个池的交易顺序一致？
用了一个叫 **GBP（全局缓冲池）** 的设计——
专门用来跨池排序，原子性有保障，不会回滚。

安全门卫：**EVS（增强视图同步）**
在提交之前先过一道「门禁规则」，
Fork 攻击直接被拦在门外。

结果：
**1024 个节点，跑出 19,000 TPS，延迟 <10 秒**。
随机数信标生成速度：比 RandHound **快 7 倍**。

这不是模拟，是真实网络环境下的实测数据。

`#区块链共识` `#分布式系统` `#IEEE TDSC` `#高性能计算` `#技术分享`

---

### Twitter/X Thread

```
Head-of-line blocking kills blockchain throughput.
One slow leader = everything stalls.

Akaverse (IEEE TDSC) fixes this with multi-leader parallel pipelines. 🧵
```

```
The architecture:
Each shard runs multiple independent transaction pools in parallel.
Each pool has its own leader pipeline.
One pipeline stalls → others keep running. No global halt.
```

```
Cross-pool ordering problem:
Parallel pools create ordering ambiguity across shards.

Solution: GBP (Global Buffer Pool)
A dedicated SMR instance that serializes cross-pool ordering deterministically.
Atomicity guaranteed. No rollbacks.
```

```
Security gating:
EVS (Enhanced View Synchronization) acts as a front-door rule.
A replica can only commit a block if it has seen a quorum certificate
for the current view.
Fork attacks: blocked at the gate.
```

```
Results (real network, not simulation):
- 19,000 TPS at 1,024 replicas
- Latency < 10 seconds
- Randomness beacon: 7× faster than RandHound
- Resilient to adaptive adversaries and forking attacks

Published: IEEE Transactions on Dependable and Secure Computing (TDSC)
Code: github.com/tenondvpn/shardora
```

---

## 第 4 期｜技术向·跨分片智能合约（SCoRE · SOSP 2026）

### 小红书

**配图概念**

- 跨分片交互示意图：多个分片节点之间有「桥接通道」，合约在不同分片间流动
- 核心流程卡片：预存款 → 执行 → 结算 → 退款（四步漏斗图）
- 标签：SOSP 2026 · 无回滚 · 热合约稳定

**文案**

智能合约跨分片调用，是区块链的「老大难」。

A 分片的合约，调 B 分片的函数，
要么锁住资源等结果，TPS 崩；
要么乐观执行，一旦失败，**全链回滚**，灾难现场。

我的方案叫 **SCoRE**，发表在 **SOSP 2026**（操作系统与系统软件方向全球顶会）。

核心设计：**预存款执行模型**

1. 调用前先在 Root 分片预存款（锁资金）
2. 各分片独立执行，生成「执行证明」
3. 跨分片结算，证明携带结果
4. 多余的费用原路退回

不需要全局锁，不需要回滚，
「服务」和「交易」分离，互不干扰。

热合约并发场景下，**4 池并行（SCoRE-4P）** 吞吐量是单池的 **3 倍**。
负载再怎么倾斜，系统稳得住。

整个运行时用 C++ 实现在 Shardora（Shardora 的合约引擎）之上，
代码已开源。

`#智能合约` `#跨分片` `#SOSP` `#区块链开发` `#系统编程`

---

### Twitter/X Thread

```
Cross-shard smart contracts without rollbacks.
That's what SCoRE (SOSP '26) delivers. 🧵
```

```
The problem:
Cross-shard calls in existing systems either:
A) lock resources globally → throughput collapses, or
B) execute optimistically → catastrophic rollbacks on failure

Neither scales. Neither is safe.
```

```
SCoRE's model: Pre-Deposit Execution

1. Prefund  — lock funds in the Root Shard before execution
2. Execute  — each shard runs independently, generates execution proof
3. Settle   — proof-carrying cross-shard settlement (no global lock)
4. Refund   — unused gas returned atomically

Service calls and fund transfers are decoupled by design.
```

```
Root Shard role:
Global contract registry + governance rules + randomness beacon.
Every deployed contract is registered here.
Cross-shard calls route through it.
One source of truth, no ambiguity.
```

```
Under hot-contract skew (the hardest case):
SCoRE-4P (4 parallel pools) achieves ~3× throughput vs single-pool.
Stable under arbitrary load distribution.

Published: SOSP 2026
Implementation: C++ on Shardora (Shardora's contract engine)
Code: github.com/tenondvpn/shardora
```

---

## 第 5 期｜技术向·NFT 版权上链（NMFT · IEEE TIFS）

### 小红书

**配图概念**

- 左：一张 AI 生成图 + 被抄袭的「山寨版」，打上红色 ✗
- 右：NMFT 验证通过界面，打上绿色 ✓ + 哈希指纹
- 下方：Ethereum Sepolia 合约地址截图（模糊处理）
- 风格：艺术感 + 技术感融合，暖色调

**文案**

AI 时代，图片抄袭变得太容易了。
换个滤镜，裁个边，哈希值全变——传统方法完全失效。

问题：**怎么在链上证明一张图「是不是你的」？**

我的方案叫 **NMFT**（NFT Merkle Feature Tree），
发表在 **IEEE TIFS**（Transactions on Information Forensics and Security，信息取证与安全顶刊），
DOI: 10.1109/TIFS.2025.3639980。

两层设计：

🌲 **MFT（Merkle Feature Tree）**
不存像素，存 AI 提取的「特征向量」。
你的图像语义，变成一棵 Merkle 树上的叶子。

🔍 **LSH 压缩指纹（uint256）**
局部敏感哈希，把高维特征压到 256 位，
直接塞进以太坊合约，**Gas 费随规模亚线性增长**。

版权挑战流程：
- 上传图 → 生成特征哈希 → 上链存证
- 有人抄袭 → 提交挑战 → 链上智能合约自动比对
- 结果：相似度超阈值，版权归属链上锁定

已部署在 **Ethereum Sepolia** 测试网，合约代码开源：
github.com/tenondvpn/nmft

如果你是创作者，这件事和你有关。

`#NFT` `#版权保护` `#AI` `#以太坊` `#区块链应用` `#创作者经济`

---

### Twitter/X

```
AI-generated art is everywhere.
So is AI-assisted plagiarism.

Traditional hash-based NFT verification breaks the moment anyone
applies a filter, crop, or recolor.

NMFT (IEEE TIFS) fixes this.

Instead of storing pixels, it stores AI-extracted feature vectors
in a Merkle tree structure.
LSH compression maps high-dimensional features to a uint256 fingerprint
— on-chain comparable, gas-efficient.

Gas cost grows sublinearly with dataset size.
Copyright challenges resolved entirely on-chain, no trusted third party.

Published: IEEE Transactions on Information Forensics and Security (TIFS)
DOI: 10.1109/TIFS.2025.3639980
Code: github.com/tenondvpn/nmft
```

---

## 第 7 期｜核弹级技术·跨分片 AMM 数学安全基础

### 小红书

**配图概念**

- 左侧：历史跨链桥攻击时间轴（Ronin $6.25亿 / Wormhole $3.2亿 / Harmony $1亿），红色警报风格
- 右侧：Shardora 防护层示意图，同样 7 条攻击线全部被"数学盾牌"拦截，绿色
- 底部：`IS_ROOT = false; totalSupply = 0;` 代码高亮截图
- 风格：安全感、密码学美学，深色背景 + 绿色矩阵字体

**文案**

区块链历史上损失最惨的不是黑客攻击智能合约，
而是攻击**跨链桥**。

Ronin Bridge：$6.25 亿
Wormhole：$3.2 亿
Poly Network：$6.1 亿
Harmony Horizon：$1 亿

**共同原因**：桥的安全性依赖「受信任的管理员私钥」。
私钥泄露 = 攻击者可以无限伪造跨链铸币。

我做 Shardora 的时候，把这个问题从根上砍掉了。

**核心思路：让数学本身成为安全边界，而不是私钥。**

分身合约（跨分片的代币镜像）在**构造函数**里运行一段 Feistel 密码计算，
用当前合约地址逆算出「根地址」。
只有根分片上的根合约能通过验证 → 才允许铸币。
其他所有分身合约：强制 `totalSupply = 0`，没有任何铸币入口。

攻击者就算拿到了合约 ABI，也找不到 `mint()` 函数。
因为它从来就不存在。

加上：
- 跨分片代币转账建模为**阿贝尔群加法**，网络乱序无法造成双花
- AMM 计算严格封闭在单池本地，跨分片不接触滑点状态
- Gas 编译期锁定，目标端零 Revert，不存在「资产扣了但没到账」的死账

**7 条历史攻击向量，全部数学级免疫。**

这不是安全加固，是从架构底层重新定义了「跨链安全」这件事。

代码开源，设计文档在仓库里：github.com/tenondvpn/shardora

`#区块链安全` `#跨链桥` `#DeFi` `#密码学` `#开源`

---

### Twitter/X Thread

```
Cross-chain bridges have lost over $2 BILLION to hacks.
Ronin: $625M. Wormhole: $320M. Poly Network: $611M. Harmony: $100M.

The root cause is always the same:
bridges rely on trusted admin keys.
Key gets compromised → attacker mints unlimited fake tokens.

Shardora eliminates this at the math level. 🧵
```

```
The innovation: Identity-Anchored Feistel Bijection

Every "avatar contract" (a token's shard mirror) runs a 4-round Feistel cipher
in its constructor to verify its own identity:

  recoveredBase = Feistel_Inverse(address(this), shardId, poolId)
  IS_ROOT = (shardId==0 && poolId==0 && recoveredBase == address(this))

If IS_ROOT is false → totalSupply = 0, immutable.
No mint() function exists. There is no attack surface.
```

```
The consensus layer enforces before ANY system call:

  RecoverBase(target_addr) == RecoverBase(src_addr)

A fake avatar contract fails this check cryptographically.
No whitelist. No admin key. No multisig.
The bijection IS the security proof.

O(1) stateless verification — no global lookup table needed.
```

```
Cross-shard token transfers modeled as Abelian group addition (ℤ, +):

  T_Δ1 ∘ T_Δ2(B) = T_Δ2 ∘ T_Δ1(B)

Network reordering, duplicate packets, delays:
→ final balance always converges correctly.
→ Zero distributed locks. Zero 2PC. Zero rollback cascades.

This is Strong Eventual Consistency (SEC) with a formal algebraic proof.
```

```
For AMM specifically:
AMM formula (x+Δx)(y-Δy) ≥ k is NON-commutative (order affects slippage).

Shardora's architectural solution:
→ AMM computation: 100% local within one shard pool (non-commutative, closed)
→ Cross-shard value: Abelian addition only (commutative, lock-free)

The two are orthogonally decoupled.
5000 AMM pairs. All parallel. No slippage interference between pools.
```

```
7 historic bridge attack vectors. All eliminated:

❌→✅ Fake minting: math-enforced zero supply on all avatars
❌→✅ Fake system calls: SYSTEM_EXECUTOR injected by consensus, unimpersonatable
❌→✅ OOG dead accounts: compile-time constant gas, pre-deducted at source
❌→✅ Address spoofing: Feistel inverse check rejects all impostors
❌→✅ Reorder double-spend: Abelian commutativity, order-invariant convergence
❌→✅ AMM slippage race: local closure, cross-shard never touches AMM state
❌→✅ Host revert desync: C++ snapshot stack, frame-level atomic rollback

Code: github.com/tenondvpn/shardora
```

---

## 第 6 期｜收尾·Call to Action

### 小红书

**配图概念**

- 全景技术栈图：Shardora 整体架构鸟瞰（分片层 / 共识层 / 合约层 / 版权层）
- 每一层标注对应论文名称和期刊
- 底部：GitHub Star 数量截图 + 「欢迎贡献」按钮
- 风格：科技蓝图感，工程图纸风格

**文案**

如果你一路看到这里，感谢你的耐心。

我用业余时间做了这些：

📄 **Akaverse**（IEEE TDSC）—— 多 Leader 并行共识，19K TPS
📄 **Shardora**（IEEE TNSE 2026）—— 双委员会分片，50K+ TPS
📄 **SCoRE**（SOSP 2026）—— 跨分片智能合约运行时
📄 **NMFT**（IEEE TIFS 2025）—— NFT 版权链上验证
🔐 **crossTransfer 协议**—— 跨分片任意 Token AMM，7 大桥攻击向量数学级免疫

代码：**40 万行 C++**，全部开源。
一个人写的，一行一行堆出来的。

我不是在炫耀，我只是想证明一件事：
**方向对了，一个人也能走很远。**

如果你是：
- 区块链研究者 → 论文在主页，欢迎引用/讨论
- 开发者 → GitHub 欢迎 PR 和 Issue
- 同样在业余时间做一件事的人 → 评论区见

⭐ github.com/tenondvpn/shardora

`#开源` `#区块链` `#独立开发者` `#技术人的业余时间` `#坚持`

---

### Twitter/X

```
Here's what one person can build in their spare time after graduation:

🔬 Akaverse (IEEE TDSC)
   multi-leader parallel BLS consensus, 19K TPS @ 1024 nodes

🔬 Shardora (IEEE TNSE '26, DOI: 10.1109/TNSE.2026.3684813)
   dual-committee sharding, 50K+ TPS, zero reconfiguration downtime

🔬 SCoRE (SOSP '26)
   rollback-free cross-shard smart contract runtime

🔬 NMFT (IEEE TIFS '25, DOI: 10.1109/TIFS.2025.3639980)
   on-chain NFT copyright via AI feature Merkle trees

400,000+ lines of C++. All open source. All spare time.

⭐ github.com/tenondvpn/shardora
```

---

## 发布节奏建议

| 期数 | 主题 | 建议间隔 |
|------|------|----------|
| 第 1 期 | 个人故事（开篇破圈） | 发布日 |
| 第 2 期 | Shardora 分片扩容 | +4 天 |
| 第 3 期 | Akaverse 并行共识 | +4 天 |
| 第 4 期 | SCoRE 跨分片合约 | +4 天 |
| 第 5 期 | NMFT NFT 版权 | +4 天 |
| 第 7 期 | 跨链桥安全·核弹级技术 | +4 天 |
| 第 6 期 | 汇总 Call to Action | +4 天 |

**运营建议：**

- 小红书第 1 期用个人故事测水温，反差感（毕业 + 业余 + 40万行）最容易破圈
- 技术期配合截图/架构图增强可信度，DOI 链接贴出来体现真实性
- Twitter 技术 thread 可 tag `@ethereum`, `@CryptoResearch` 等账号获取精准曝光
- GitHub Star 数量可在第 6 期截图展示，形成社交证明
