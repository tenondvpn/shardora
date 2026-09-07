# 问题一：论文修改方案

> 针对 Reviewer 3 第二轮审稿意见 Problem 1：Gap 同步机制的阻塞行为分析，以及 Lemma A4 跨窗口排序论证不足。

---

## 一、需要修改的论文位置

### 修改点 1：Section IV.A Correctness（第8页，原文第三段）

**现有文字：**

> Correct cross-pool execution is ensured through height-ordered processing, gap-aware synchronization, and replay protection. Cross-pool transfers are ordered by verified source-side heights rather than network arrival order. A transfer becomes eligible for proposal only after its corresponding source-side height has been locally verified, so out of order delivery does not affect the execution sequence. If prerequisite source-side blocks are missing, the block manager detects the gap, synchronizes the missing blocks, and buffers the transfer until all prerequisite blocks are available and verified. To prevent duplicate delivery, each transfer carries a unique digest that is checked against persistent storage before processing, while propagation is constrained by monotonic source-side height progression. These mechanisms preserve execution order, prevent premature processing, and ensure at-most-once application of each cross-pool effect.

**问题：** 这段文字仅定性描述了 gap-aware synchronization 的功能，没有说明：（1）Gap 同步的性能代价范围；（2）Gap 阻塞期间对其他 pool 的影响是否存在。

**修改方案——在该段末尾追加以下内容：**

> Gap synchronization delays are bounded by the time to retrieve missing source-side blocks; once the missing blocks arrive, the GBP resumes normal proposal progress without global interruption. Critically, gap synchronization blocks only the GBP batching of cross-pool records from the affected source pool; it does not stall regular pool consensus. All 32 regular pools continue committing local transactions in parallel during the gap resolution window. Thus, the throughput impact of gap synchronization is confined to cross-pool confirmation latency for the affected source, and does not induce system-wide stalls.

---

### 修改点 2：Section III.D.3 Global Buffer Pool（第7页，GBP scheduling window 描述之后）

**现有文字（在 GBP block validity 条件描述附近）：**

> The GBP operates in fixed scheduling windows of length δ. During each window, it collects certified cross-pool output records derived from committed source-pool blocks.

**问题：** 没有说明 GBP leader 在选取高度范围时的单调约束（`prev_to_heights[i] <= leader_to_heights[i]`），这是跨窗口顺序保证的核心协议不变量，审稿人正是基于这一缺失提出了跨窗口排序的质疑。

**修改方案——在该段之后新增一个 Protocol Invariant 说明框（可作为编号 Remark 或 Invariant）：**

> **Invariant (Monotonic Height Selection).** Let $H_i^t$ denote the maximum committed source-pool height from pool $P_i$ included in GBP window $W_t$. The GBP protocol enforces a strict monotonic constraint: for any source pool $P_i$ and consecutive windows $W_t$ and $W_{t+1}$,
> $$H_i^{t+1} > H_i^t.$$
> The GBP leader selects the height range for each source pool from the interval $(H_i^{t-1}, H_i^t]$ in window $W_t$, where $H_i^{t-1}$ is the upper bound committed in the preceding window. This constraint is enforced in `LeaderCreateToHeights()` and ensures that no source-pool block is admitted twice and that the per-source ordering of admitted records is consistent across window boundaries.

---

### 修改点 3：Lemma A4 的精化（第10页，在 Lemma A4 之后新增 Lemma A4'）

**现有 Lemma A4：**

> **Lemma A4 (GBP-Log Order Consistency).** Suppose two cross-pool records $r_i$ and $r_j$ are committed in the GBP log. All honest replicas observe the same committed order of $r_i$ and $r_j$. Moreover, if $r_i$ and $r_j$ originate from the same source pool and target the same destination pool, and $\sigma(r_i) <_{\mathrm{lex}} \sigma(r_j)$, then $r_i$ precedes $r_j$ in the GBP log and is applied earlier at the destination pool.

**问题：** 现有证明只处理了 $r_i, r_j$ 在**同一 GBP block** 内的情况（依靠 canonical key $\kappa(\cdot)$ 排序），没有处理 $r_i \in W_t$、$r_j \in W_{t+1}$ 的**跨窗口**情形。审稿人质疑：当 $r_1, r_2$ 来自同一源 pool，$r_1$ 对应高度 $h_1 < h_2$（$r_2$ 对应高度 $h_2$），但 $r_2$ 先于 $r_1$ 到达 GBP 时，协议如何保证跨窗口的源序。

**修改方案——在 Lemma A4 的 Proof 末尾（$\square$ 之前）追加一段，或新增 Lemma A4' 作为精化版本，建议后者以保持结构清晰：**

---

**新增 Lemma A4'（插入在 Lemma A4 的 Proof 结束之后，Lemma A5 之前）：**

> **Lemma A4' (Cross-Window Source-Order Preservation).** Let $r_1$ and $r_2$ be two cross-pool records originating from the same source pool $P_i$, where $r_1$ corresponds to committed source-pool height $h_1$ and $r_2$ to height $h_2 > h_1$. Under the monotonic height constraint of the GBP protocol, $r_1$ is admitted to the GBP log no later than $r_2$. That is, there exist GBP windows $W_s$ and $W_t$ with $s \leq t$ such that $r_1 \in W_s$ and $r_2 \in W_t$.
>
> **Proof.** We consider two cases based on the relative arrival order of $r_1$ and $r_2$ at the GBP.
>
> **Case 1 (Normal delivery: $r_1$ arrives before $r_2$).** The GBP leader selects source heights up to $H_i^t \geq h_2 > h_1$ in window $W_t$. Since $h_1 < h_2 \leq H_i^t$, the record $r_1$ at height $h_1$ is eligible for inclusion in $W_t$ or an earlier window $W_s$ ($s \leq t$). By the deterministic ordering rule within a GBP block (canonical key $\kappa(\cdot)$ sorts by source height), $r_1$ precedes $r_2$ within the same block if they fall in the same window, or $r_1$ is committed in an earlier window. In both cases, the source order is preserved.
>
> **Case 2 (Out-of-order delivery: $r_2$ arrives before $r_1$ at the GBP).** In this case, $r_2$ at height $h_2$ is available to the GBP, but the source-side block at height $h_1$ (which is a predecessor of $h_2$ in $P_i$'s chain) has not yet been verified by the GBP. By the monotonic height constraint, the GBP leader cannot advance the upper bound $H_i^t$ beyond any height for which all predecessor blocks have not been continuously verified. Formally, the protocol requires that the height range selected for $P_i$ in window $W_t$ covers a contiguous range $(H_i^{t-1}, H_i^t]$ of verified source heights. Since $h_1 < h_2$ and $h_1$ has not yet been verified, the GBP leader cannot set $H_i^t \geq h_2$. Consequently, $r_2$ cannot be included in window $W_t$; it remains buffered in the GBP's pending set. Gap-aware synchronization detects the missing block at height $h_1$, retrieves it, and verifies it. Only after $h_1$ is verified does the GBP advance its height bound to include $h_2$, ensuring that $r_1$ (at height $h_1$) is admitted in window $W_s \leq W_t$ and $r_2$ (at height $h_2$) is admitted in a subsequent window $W_t$ with $t > s$ or in the same window if both become available simultaneously. In either case, $r_1$ precedes $r_2$ in the committed GBP log.
>
> In both cases, the source-side ordering $h_1 < h_2$ is reflected in the GBP-log order. Since the destination pool applies cross-pool records in GBP-log order (by Lemma A4), $r_1$'s target-side effect is applied before $r_2$'s. $\square$

---

## 二、Section IV.A Correctness 补充性能代价分析

在 Section IV.A 的结尾，建议新增如下段落（可独立成 Remark）：

> **Remark (Performance Impact of Gap Synchronization).** Under the partial synchrony model, let $\Delta$ denote the message delay bound after GST. A gap of $k$ consecutive missing source-side blocks introduces an additional delay of at most $k \cdot \Delta$ before the GBP can resume processing records from the affected source pool. During this interval, (i) the GBP continues to process records from other source pools whose blocks have been verified; (ii) all 32 regular pools continue normal Fast-HotStuff consensus without interruption; and (iii) only the cross-pool confirmation of transactions from the affected source pool is delayed. Once the gap is resolved, the GBP immediately resumes full-rate processing. In practice, under the experimental configuration reported in Section V (50 ms network latency, 10 ms jitter), gaps caused by network jitter resolve within a bounded number of additional rounds and do not produce persistent latency accumulation, as confirmed by the stable latency curves in Fig. 10.

---

## 三、修改位置汇总

| 修改点 | 论文位置 | 性质 | 内容摘要 |
|--------|---------|------|---------|
| 1 | Section IV.A Correctness，末段追加 | 补充说明 | Gap 同步仅影响跨池确认延迟，不造成全局阻塞；常规 pool 不受影响 |
| 2 | Section III.D.3 Global Buffer Pool，scheduling window 段后新增 | 新增 Invariant | 形式化陈述单调高度约束 $H_i^{t+1} > H_i^t$，明确协议不变量 |
| 3 | Lemma A4 Proof 后，Lemma A5 前，新增 Lemma A4' | 新增引理 | 精化跨窗口源序保证：Case 1（正常交付）和 Case 2（乱序交付）两种情形的完整证明 |
| 4 | Section IV.A 末尾，新增 Remark | 补充 Remark | 量化 Gap 延迟代价（上界 $k \cdot \Delta$），说明实验数据印证了理论分析 |

---

## 四、与审稿人回复文件的对应关系

本文档中的修改对应 `TDSC_R2_REVIEWER3_RESPONSE.md` 问题一回应（Section 1.1 和 1.2）。修改完成后，审稿人回复中承诺的具体论文变更均已落实：

- **Lemma A4' 精化命题**：见修改点 3
- **GBP 协议单调高度不变量的形式化陈述**：见修改点 2
- **Gap 同步性能代价分析**：见修改点 1 和修改点 4
- **跨窗口两种情形（正常交付 vs. 乱序交付）的完整论证**：见 Lemma A4' 的 Case 1 和 Case 2
