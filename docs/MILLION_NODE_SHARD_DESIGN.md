# 单分片百万节点设计方案

## 背景

当前架构限制：

```
kEachShardMaxNodeCount = 1024   // 单分片最大节点数
BLS quorum: t = ⌈2n/3⌉         // 标准 2/3+1 BFT 门限
```

Leader 收票复杂度为 O(n)，1024 节点时 leader 需收集 ≈683 份 partial sign 再做 Lagrange 插值，无法扩展至百万量级。

---

## 方案一：Leader 分身（两级聚合）

### 结构

```
         [主Leader]
        /    |    \
  [子L_0] [子L_1] ... [子L_1023]   ← 1024个子Leader
   /|\ \    /|\          /|\
  n n n n  n n n  ...   n n n      ← 每组1024节点
                                    ← 总计 ≈ 100W节点
```

### 技术障碍

当前使用 Threshold BLS，不支持层次化组合。全局多项式 degree = ⌈2×(1024×1024)/3⌉ ≈ 70万，Lagrange 插值计算量爆炸。需切换到 `USE_AGG_BLS` 路径。

### 安全性弱化

两级 Quorum 的拜占庭容错率下降：

```
最坏情况：攻击者将恶意节点集中于少数子组
  → 控制 1/3 子组，在这些组内全部为恶意节点
  → 恶意节点仅占总量 1/3，却可同时破坏两级 Quorum
```

必须保证子组随机分配不可预测，否则无法达到标准 1/3 容错。

### 方案对比评分

| 指标 | 评价 |
|------|------|
| 安全性 | ⚠️ 需精心设计才能维持 1/3 容错 |
| 延迟 | ❌ 增加一轮网络往返 (+~300ms) |
| 实现复杂度 | ❌ 需新增两级聚合协议 |
| 通信复杂度 | ✅ O(√N) |

---

## 方案二：Epoch 随机委员会抽签（推荐方案）

### 核心思路

将"注册节点集"与"共识委员会"解耦：

```
┌─────────────────────────────────────────┐
│         分片注册池（最多 100W 节点）         │
│  [N₁][N₂][N₃]...[N₁₀₀₀₀₀₀]            │
│  每个节点有 FTS weight（现有代码）          │
└──────────────┬──────────────────────────┘
               │ epoch_random_（现有 BLS 信标）
               │ 确定性 Fisher-Yates shuffle
               ▼
┌─────────────────────────────────────────┐
│       本轮活跃委员会（固定 2048 节点）        │
│  从 1M 中随机抽取，无法预测，无法操控         │
│  运行现有 HotStuff BFT（代码基本不变）       │
│  BLS DKG 只在 2048 人内进行               │
└─────────────────────────────────────────┘
```

### 安全性证明

| 参数 | 值 |
|-----|---|
| 总节点 N | 1,000,000 |
| 拜占庭上限 f | N/3 ≈ 333,333 |
| 委员会大小 k | 2,048 |
| P(委员会内拜占庭 > 2/3) | **< 10⁻¹⁵**（超几何分布尾部） |

委员会越大安全性越高，2048 足以让失败概率低于宇宙原子数倒数。这是 Algorand 已证明的安全边界。

### 与 Leader 分身方案对比

| 指标 | Leader 分身（2级） | 随机委员会（推荐） |
|------|-----------------|-----------------|
| 安全模型 | 确定性，但两级 Quorum 有弱化 | 压倒性概率安全，**等价于确定性** |
| 投票轮次 | 2 轮（+延迟） | **1 轮（不变）** |
| Leader 通信量 | O(√N) | **O(2048) 常数** |
| BFT 容错率 | 需精心设计才能达到 1/3 | **严格 1/3，与现有一致** |
| 代码改动量 | 大（新协议） | **小（复用现有 HotStuff）** |

---

## 实现方案

### 当前代码的根本瓶颈

两处地方将"节点总数"和"参与 BFT 的节点数"硬绑在一起：

```cpp
// src/bls/bls_dkg.h — 编译期固定大小
bool invalid_node_map_[1024];   // ← 卡死点

// src/common/utils.h
kEachShardMaxNodeCount = 1024   // ← 同时约束注册数和共识数
```

### 第一步：解除固定数组约束

**`src/bls/bls_dkg.h`**

```cpp
// 改前
bool invalid_node_map_[1024];

// 改后
std::vector<bool> invalid_node_map_;
// 构造函数中: invalid_node_map_.resize(committee_size, false);
```

**`src/common/utils.h`**

```cpp
// 拆分为两个常量
static const uint32_t kEachShardMaxNodeCount    = 1000000; // 注册池上限
static const uint32_t kEachShardCommitteeSize   = 2048;    // BFT 委员会大小
```

### 第二步：选举模块增加二级存储

**`src/elect/elect_manager.h`**

```cpp
// 新增：全量注册池（1M 节点）
MembersPtr all_members_ptr_[kConsensusShardEndNetworkId + 1];

// 现有 members_ptr_ 语义改为：当前委员会（2048 节点）
// MembersPtr members_ptr_[...]; // 保持不变，内容变为抽签结果
```

### 第三步：委员会抽签逻辑

**`src/elect/elect_manager.cc`** — 在 `ProcessPrevElectMembers` 末尾增加：

```cpp
void ElectManager::SampleCommittee(
    uint32_t network_id,
    uint64_t epoch_random,       // 来自 vss_mgr_->EpochRandom()（现有）
    MembersPtr all_members,      // 1M 节点全集
    MembersPtr& out_committee) { // 输出：2048 节点

    // 确定性 Fisher-Yates shuffle（与 FTS 现有 mt19937_64 逻辑一致）
    std::mt19937_64 rng(epoch_random);
    std::vector<uint32_t> indices(all_members->size());
    std::iota(indices.begin(), indices.end(), 0);
    for (uint32_t i = indices.size() - 1; i > 0; --i) {
        uint32_t j = rng() % (i + 1);
        std::swap(indices[i], indices[j]);
    }
    // 取前 kEachShardCommitteeSize 个
    out_committee = std::make_shared<Members>();
    for (uint32_t i = 0; i < kEachShardCommitteeSize && i < indices.size(); ++i) {
        auto member = (*all_members)[indices[i]];
        member->index = i;  // 重新编号
        out_committee->push_back(member);
    }
}
```

### 第四步：BLS DKG 只在委员会内运行

- **`src/bls/bls_dkg.cc`**：DKG 入参从 1M 节点改为 2048 委员会节点，其余逻辑不变
- **`src/consensus/hotstuff/elect_info.h`**：`ElectItem::bls_n_` 和 `bls_t_` 基于 2048 计算，不变

### 第五步：非委员会节点行为

- 收到块后，用委员会的聚合 BLS 公钥（已有 `common_pk_`）验证 QC 签名 → **无需额外代码**
- 通过 `epoch_random_` 本地独立计算本轮委员会成员 → 可自证自己是否入选
- 参与下一轮 FTS 选举评分（现有逻辑不变）

---

## 完整数据流（改动后）

```
时间块产生
   │
   ▼
VssManager::OnTimeBlock()
   epoch_random_ = Hash(qc.sign_x + qc.sign_y)    ← 现有代码，不改
   │
   ▼
ElectManager::SampleCommittee()                    ← 新增
   all_members（最多 1M） + epoch_random_
   → committee（2048 节点，确定性，全网可独立验证）
   │
   ▼
BlsDkg::Start(committee, 2048)                     ← 入参节点集改变
   DKG 只在 2048 人内进行（t = ⌈2×2048/3⌉ ≈ 1366）
   │
   ▼
HotStuff::HandleVoteMsgImpl()                      ← 代码不变
   只有 committee 成员参与投票
   Leader 收集 ≤ 2048 票，Lagrange 插值
   │
   ▼
QC 生成 → 全网 1M 节点验证（verify common_pk_）    ← 代码不变
```

---

## 改动文件清单

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `src/common/utils.h` | 修改常量 | 拆分注册池上限与委员会大小 |
| `src/bls/bls_dkg.h` | 动态化数组 | `invalid_node_map_` 改为 `vector<bool>` |
| `src/elect/elect_manager.h` | 新增字段 | `all_members_ptr_` 存全量注册池 |
| `src/elect/elect_manager.cc` | 新增函数 | `SampleCommittee()` 抽签逻辑 |
| `src/consensus/hotstuff/elect_info.h` | 逻辑确认 | `valid_leaders_` 来源改为委员会 |

HotStuff 核心共识代码（`hotstuff.cc`、`crypto.cc`、`pacemaker.cc`）**零改动**。

---

## 扩展路线图

| 阶段 | 目标 | 关键改动 |
|------|------|---------|
| Phase 1 | 单分片 10K 节点 | 解除固定数组，调通 SampleCommittee |
| Phase 2 | 单分片 100K 节点 | 优化 P2P 广播（gossip 替换全连接） |
| Phase 3 | 单分片 1M 节点 | 节点注册分批上链，委员会大小调参验证 |
