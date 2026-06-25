# 当前 HotStuff 实现中 ViewChange 与 HighQC 一致性分析

## 1. 核心结论

**当前实现没有独立的 viewchange 消息流程，也没有阈值签名聚合步骤。**

视图变更完全通过 `GetLeader()` 的时间戳计算来驱动：所有节点基于**相同的区块时间戳**和**相同的 30 秒窗口对齐**，独立计算出相同的 leader 和相同的 view 跳跃量，从而实现无需通信的确定性 leader 轮换。

代码中 HighQC 就是上一轮共识产生的 QC，leader 在每次 propose 时直接携带自己本地保存的 HighQC 广播给所有副本。

---

## 2. GetLeader() 的时间一致性机制

### 2.1 时间基准：30 秒窗口对齐

```cpp
// hotstuff.h
inline uint64_t get_consensus_timestamp(uint64_t window_size) {
    uint64_t current_utc = ...秒级 UTC 时间戳...;
    uint64_t consensus_time = (current_utc / window_size) * window_size;
    return consensus_time;  // 向下取整到 30 秒边界
}

// GetLeader() 中使用
int64_t now = get_consensus_timestamp(30);  // window_size = 30
```

`get_consensus_timestamp(30)` 把当前时间向下取整到 30 秒边界。例如：
- 节点 A 的本地时间是 `14:00:17` → `now = 14:00:00`
- 节点 B 的本地时间是 `14:00:28` → `now = 14:00:00`
- 节点 C 的本地时间是 `14:00:31` → `now = 14:00:30`

**只要节点间时差 < 30 秒，同一个 30 秒窗口内的所有节点得到相同的 `now`。**

### 2.2 时差容忍：15 秒

时差检查在 `BlockAcceptor::IsBlockValid()` 中完成，副本在接受区块前会验证时间戳合法性：

```cpp
// block_acceptor.cc - IsBlockValid()
auto cur_time = common::TimeUtils::TimestampMs();
uint64_t preblock_time = pools_mgr_->latest_timestamp(pool_idx());

// 区块时间戳必须满足：
// 1. 大于上一个区块的时间戳（单调递增）
// 2. 不能超过当前本地时间 10 秒以上（防止未来时间戳）
if (shardora_block->timestamp() <= preblock_time && 
    shardora_block->timestamp() + 10000lu >= cur_time) {
    // 时间戳非法，丢弃该区块
    return false;
}
```

这个检查保证了：
- **时间戳单调递增**：新块时间戳必须大于上一个已提交块的时间戳
- **不接受未来时间戳**：区块时间戳不能超过本地时间 10 秒，即隐含要求节点间时差 < 10 秒

`GetLeader()` 中使用 propose 消息携带的 leader 时间戳作为 `now`：

```cpp
if (leader_tm_ms != 0) {
    // if (std::abs((leader_tm_ms / 1000lu) - common::TimeUtils::TimestampSeconds()) < 15) {
        now = leader_tm_ms / 1000lu;
    // }
}
```

由于 `IsBlockValid()` 已经在更早阶段过滤掉时间戳异常的区块，`GetLeader()` 收到的 `leader_tm_ms` 必然是合法的时间戳。因此 **节点间时差约束是整个机制的前提**：时差 < 10 秒 → leader 时间戳通过 `IsBlockValid()` 验证 → `GetLeader()` 使用合法时间戳计算 `elapsed` → 所有节点在同一个 30 秒窗口内得出相同的 `k` → leader 和 view 计算一致。

### 2.3 超时判断与 k 的计算

```cpp
// utils.h
static const int32_t kLeaderRoatationBaseTimeoutSec = 30;

// GetLeader() 中
int64_t prev_qc_timestamp_sec = high_view_block->block_info().timestamp() / 1000lu;
int64_t timeout = kLeaderRoatationBaseTimeoutSec * pow(2, min(consecutive_failures_, 6u));
int64_t elapsed = now - prev_qc_timestamp_sec;

if (elapsed < timeout) {
    // 未超时，保持当前 leader
    return (*members)[last_stable_leader_member_index_ % members->size()];
}

// 已超时，计算跳跃量
auto k = (elapsed / kLeaderRoatationBaseTimeoutSec) + 7;
```

**`k` 的计算完全由 `elapsed` 决定，而 `elapsed = now - 区块时间戳`。**

- `now` 由 30 秒窗口对齐保证所有节点一致
- `区块时间戳` 来自 propose 消息里的 `block_info().timestamp()`，所有节点看到的是同一个值
- 因此所有节点计算出的 `k` 相同，进而 leader index 和 out_view 相同

### 2.4 view 跳跃计算

```cpp
// 超时时的 view 跳跃
*out_view = high_view_block->qc().view() + k + 1;

// leader index 跳跃
auto index = (last_stable_leader_member_index_ + k + pool_idx_) 
             % elect_item->valid_leaders()->size();
auto leader_idx = elect_item->valid_leaders()->at(index)->index;
```

`k = (elapsed / 30) + 7`，最小值为 8（elapsed 刚超过 30 秒时）。这意味着：

- **view 至少跳跃 8 个**，跳过的 view 不会有任何 propose，直接作废
- 跳跃量与 elapsed 成正比，elapsed 越大跳得越多
- 所有节点基于相同的 `elapsed` 计算出相同的跳跃量，因此对新 leader 和新 view 的判断完全一致

---

## 3. HighQC 一致性的保证机制

### 3.1 HighQC 通过 propose 传播，不通过 viewchange 消息

当前实现中，HighQC（即 `latest_qc_item_ptr_`）在以下时机更新：

**时机 1：收到 propose，处理其中携带的 HighQC**

```cpp
// hotstuff.cc - HandleTC()（代码中函数名为 HandleTC，实质处理的是 HighQC）
if (pro_msg.has_tc() && pro_msg.tc().has_view_block_hash()) {
    // 验证 HighQC 的阈值签名（正常 vote 阶段产生的 QC 签名）
    if (VerifyQC(pro_msg.tc()) != Status::kSuccess) {
        return Status::kError;
    }
    // 推进视图
    pacemaker()->NewTc(tc_ptr);
    view_block_chain()->UpdateHighViewBlock(qc);
    TryCommit(view_block_chain(), msg_ptr, qc);
    // 更新本地 HighQC
    if (tc_ptr->view() >= latest_qc_item_ptr_->view()) {
        UpdateLatestQcItemPtr(tc_ptr);
    }
}
```

**时机 2：收到同步块**

```cpp
if (latest_qc_item_ptr_ == nullptr ||
        vblock->qc().view() >= latest_qc_item_ptr_->view()) {
    if (IsQcTcValid(vblock->qc())) {
        UpdateLatestQcItemPtr(make_shared<QcItem>(vblock->qc()));
    }
}
```

**时机 3：启动时从 DB 加载**

```cpp
// InitLoadLatestBlock()
UpdateLatestQcItemPtr(make_shared<QcItem>(latest_view_block->qc()));
```

### 3.2 为什么不需要"保证所有副本持有相同 HighQC"

**关键洞察：HighQC 的一致性不是在 viewchange 阶段保证的，而是通过 propose 消息的传播来收敛的。**

```
正常共识阶段：
  2f+1 个副本对 H(view || view_block_hash || ...) 产生 BLS 签名 share
  → leader 聚合出阈值签名 → 形成 QC（即 HighQC）
  → leader 在下一个 propose 里携带这个 HighQC 广播给所有副本
  → 所有副本收到 propose 后验证 HighQC 的阈值签名
  → 验证通过则更新本地 HighQC

超时/ViewChange 阶段：
  GetLeader() 基于时间戳计算，所有节点独立得出相同的新 leader 和新 view
  → 新 leader 用自己的 HighQC 发 propose
  → 副本收到 propose，验证 HighQC 的阈值签名（正常 vote 阶段已产生，含 view_block_hash）
  → 验证通过则接受，更新本地 HighQC
  → 副本之间的 HighQC 通过接受同一个 propose 来收敛
```

**HighQC 的阈值签名不是在 viewchange 时重新聚合的，而是正常 vote 阶段已经产生的 QC 签名。** 因此不存在"不同副本持有不同 HighQC 导致无法聚合"的问题。

### 3.3 副本落后时的追赶机制

如果某个副本本地 HighQC 落后，收到 propose 时 `GetLeader()` 计算出的 `out_view` 会小于 propose 的 view，此时有追赶逻辑：

```cpp
// hotstuff.cc - HandleProposeMsgImpl()
if (view_item.qc().view() > out_view && view_item.qc().view() > 0) {
    // 本地视图落后，用 propose 里的 HighQC 追赶
    view_block_chain_->UpdateHighViewBlock(propose_qc);
    pacemaker()->NewQcView(propose_qc.view());
    // 重新计算 leader
    auto new_leader = GetLeader(
        view_item.qc().leader_idx(),
        msg_ptr->header.hotstuff().pro_msg().tc(),
        &new_out_view, ...);
    if (new_leader && view_item.qc().view() == new_out_view) {
        goto view_matched;  // 追赶成功，继续处理
    }
}
```

---

## 4. 完整 ViewChange 流程图

```
副本收到 propose 消息
         ↓
  IsBlockValid()：验证区块时间戳合法性
    → block.timestamp <= prev_committed_timestamp → 丢弃（时间戳未单调递增）
    → block.timestamp > cur_time + 10s → 丢弃（未来时间戳，节点时差过大）
         ↓
  GetLeader()：用 propose 携带的 leader 时间戳计算
  elapsed = leader_tm_ms/1000 - block.timestamp（秒）
         ↓
  elapsed >= 30s ?
    ├── 否：保持当前 leader，out_view = last_HighQC.view + 1
    └── 是：k = (elapsed/30) + 7
              new_leader_idx = (last_stable_idx + k + pool_idx) % n
              out_view = last_HighQC.view + k + 1
              （所有节点独立计算，结果相同）
         ↓
  验证 propose.view == out_view
    → 不一致 → 丢弃
    → 一致 → 继续处理
         ↓
  验证 HighQC 阈值签名（正常 vote 阶段已产生）
    → 验证通过：推进视图，更新本地 HighQC
    → 通过则投票
```

---

## 5. 与论文方案的差异及修改建议

### 5.1 差异对比

| 维度 | 当前代码实现 | 论文描述的方案 |
|------|-------------|--------------|
| HighQC 的来源 | 正常 vote 阶段产生的 QC，leader 直接复用 | viewchange 阶段由 2f+1 个副本的 timeout 签名聚合后独立选取 |
| viewchange 消息 | 不存在 | 副本向新 leader 发送携带 HighQC_i 的 viewchange 消息 |
| leader 选举机制 | 基于时间戳的确定性计算，所有节点独立得出相同结果 | 新 leader 收集 2f+1 个 viewchange 消息后确认 |
| HighQC 一致性 | 通过接受同一个 propose 收敛 | 需要 2f+1 个副本持有相同 HighQC 才能聚合视图变更证明 |
| 活性风险 | 无（不需要聚合 viewchange 签名） | 有（不同副本 HighQC 不同时无法聚合） |

### 5.2 论文方案的根本矛盾

论文方案中，副本对以下消息产生阈值签名 share：

```
σ_i^vc = SignShare( H(ViewChange || v+1 || H(HighQC_i)) )
```

由于不同副本的 `HighQC_i` 可能不同，签名的消息也不同，**无法聚合成同一个阈值签名**。

当前代码通过"HighQC = 正常 vote 阶段的 QC"完全绕开了这个问题：HighQC 的阈值签名在正常共识阶段已经产生，不需要在 viewchange 时重新聚合。

### 5.3 论文方案的修改建议

如果论文要引入真正的 viewchange 消息和视图变更证明聚合，必须将视图变更证明的签名消息与 HighQC 解耦：

**视图变更证明只签 view，不绑定 HighQC：**

```
σ_i^vc = SignShare( H(ViewChange || v+1 || network_id || pool_idx) )
```

所有副本签名的是同一个消息，可以正常聚合：

```
ViewChangeCert_{v+1} = ThreshSigAgg({σ_i^vc})
```

该证书只证明：**2f+1 个副本进入了 view v+1**。

**HighQC* 独立选取：**

```
HighQC* = max{ HighQC_i | i ∈ Q, |Q| ≥ 2f+1 }
```

leader 从收到的 2f+1 个 viewchange 消息中选最高的有效 QC，单独验证其阈值签名（正常 vote 阶段产生，包含 view_block_hash）。

**NewView 消息结构：**

```
NewView = ⟨NewView, v+1, ViewChangeCert_{v+1}, HighQC*⟩
```

副本收到 NewView 后分别验证：
1. `ViewChangeCert_{v+1}` 是对 `H(ViewChange || v+1 || ...)` 的合法阈值签名
2. `HighQC*` 是对某个区块的合法阈值签名（含 view_block_hash，独立验证）
3. `HighQC*` 满足本地 lock rule

---

## 6. 总结

**当前代码中不存在论文描述的活性问题**，原因如下：

1. **没有 viewchange 消息**，没有视图变更阈值签名聚合步骤
2. **HighQC 就是正常 vote 阶段产生的 QC**，直接由 leader 携带在 propose 里广播，副本验证其阈值签名后接受并更新本地 HighQC
3. **leader 轮换基于时间戳确定性计算**：`get_consensus_timestamp(30)` 将时间对齐到 30 秒窗口，节点间时差 < 15 秒时所有节点计算出相同的 `now`，进而得出相同的 `k`、相同的新 leader、相同的新 view
4. **HighQC 通过 propose 传播收敛**，副本接受同一个 propose 后本地 HighQC 自然一致

**论文方案引入真正的 viewchange 流程时**，必须将视图变更证明的签名消息与 HighQC 解耦：视图变更证明只对 view 编号签名（保证可聚合），HighQC 作为独立字段单独验证（保证安全性）。这样视图同步机制、视图变更证明和 HighQC 三者的职责边界才清晰，活性也得以保证。
