# Shardora TNSE 落地方案 TODO

> 基于 docs/UNIFIED_FORMAL_PROOF.md 形式化证明，将 TNSE 密钥复用协议、Root 准入限速、M1-M6 机制落地到工程实现。

---

## 阶段 0 — 前置条件确认（Week 1）

- [ ] 审计 `kEachShardMaxNodeCount=1024` 与 `kRotationPeriod=600s` 是否与定理全局一致
  - 文件：`src/common/utils.h`
- [ ] 确认 Protobuf `verify_vec` 字段能否承载差分承诺（TNSE Phase A）
  - 文件：`src/bls/bls_dkg.cc:297`
- [ ] 评估 `dkg_cache.cc/.h` 是否可扩展为跨 Epoch 承诺缓存
  - 文件：`src/bls/dkg_cache.cc`

---

## 阶段 1 — TNSE 密钥复用协议（Week 2-5）

> 目标：单轮 DKG 总数据量 133MB → 73MB，带宽 700KB/s → 385KB/s  
> 对应：形式化证明 §13.11，定理 13.T2 / 13.15 / 引理 13.T4

### P1.1 Protobuf 协议扩展

- [ ] 在 `bls.proto` 的 `VerifyBrd` 消息中新增字段：
  - `uint64 epoch_id` — 当前 Epoch 序号（TNSE Phase O Registration）
  - `repeated G2Point delta_commit` — 差分承诺 Δc_i（仅承诺变化时发送）
  - `bool is_full_commit` — 首 Epoch 或强制刷新时为 true，发完整 65KB；否则只发差分 6.5KB

### P1.2 差分承诺生成

- [ ] 在 `src/bls/bls_dkg.cc` `g2_vec_` 构造段（约 L1146）：
  - 首轮 Epoch：生成完整 `g2_vec_`，持久化到 `dkg_cache`
  - 后续 Epoch：生成扰动多项式 h(x)（h(0)=0，保证前向安全），只广播 `delta_commit`

### P1.3 接收方增量重建

- [ ] 在 `src/bls/bls_dkg.cc` 验证路径（约 L626 `GetNodeVerificationVector`）：
  - `is_full_commit=false`：从本地缓存取上一轮承诺，叠加 `delta_commit`
  - Epoch 不连续（节点断线）：回退 Complaint 流程，请求完整承诺

### P1.4 新委员会成员初始化（引理 13.T4）

- [ ] 在 `src/elect/elect_manager.cc` `StoreMembers` 路径：
  - 新入选成员无上轮缓存 → 发起 `FULL_COMMIT_REQUEST`
  - 至少 t+1 名老成员响应完整承诺

---

## 阶段 2 — Root 准入限速 & Lyapunov 稳定性（Week 6-8）

> 目标：N(t) → 10^5 收敛过程可审计，N_max = 10^5  
> 对应：形式化证明 §13.13，定理 13.16 / 13.16' / 13.16'' / 13.16'''

### P2.1 每 Epoch 准入配额上链

- [ ] 在 `src/consensus/zbft/join_elect_tx_item.cc` Root Shard 路径：
  - 新增链上状态 `epoch_join_count`
  - 超过 λ_adm（初始值：每 Epoch ≤1% 现有池规模）时拒绝新 JoinElectTx
  - 配额参数写入 Root Shard 治理交易，不硬编码

### P2.2 Λ_out 自然衰减验证

- [ ] 确认 `src/consensus/zbft/elect_tx_item.cc` 强制轮换覆盖每轮正常退出节点量
  - 验证 Λ_out ≈ k × rotation_ratio 成立

### P2.3 Shard 裂变触发器

- [ ] 在 `src/pools/shard_statistic.cc`：
  - 监控候选池 N(t)
  - N(t) > P(t)·k_pool 时触发子分片新建信号
  - 初期 P=2（冷启动），稳态 P=100（对应推论 13.16''）

---

## 阶段 3 — M1-M6 机制补全（Week 9-11，可与阶段2并行）

### M1 三层奖励

- [ ] `src/consensus/zbft/elect_tx_item.cc:588` `MiningToken` 基础逻辑已有，需补全：
  - Layer1：委员会活跃奖励
  - Layer2：候选池保留奖励
  - Layer3：Root 协调奖励
  - 三个预算池独立核算

### M2 延迟委员会推导

- [ ] `src/elect/elect_manager.cc:169` `prev_elect_height` 路径：
  - 确认推导延迟 Δ_elect ≥ 1 Epoch
  - 防止选举结果在 Epoch 内提前泄露

### M3 强制轮换

- [ ] 审计 `src/consensus/zbft/elect_tx_item.cc` committee 替换段：
  - 确认每轮强制替换比例 ≥ f+1 席位（拜占庭阈值以上）

### M4 自适应委员会规模

- [ ] 将 `kEachShardMaxNodeCount=1024` 改为动态计算：
  - k(t) = min(1024, ⌈N(t) / k_ratio⌉)
  - 文件：`src/common/utils.h` + `elect_tx_item.cc`

### M5 证明反女巫（Attestation）

- [ ] `src/consensus/zbft/join_elect_tx_item.cc` 注册路径：
  - 新节点提交 τ 个历史 Epoch 出块证明
  - 缺省拒绝准入，保证 τ_Sybil ≥ M_s/λ_adm

### M6 随机中继广播

- [ ] `src/sync/key_value_sync.cc` BLS 广播路径：
  - 收到广播后以概率 p 随机转发给 r 个邻居
  - 防止拜占庭节点在 T_bls 窗口内屏蔽消息

---

## 阶段 4 — 集成测试 & 参数校准（Week 12-13）

- [ ] **DKG 带宽测试**：1024 节点仿真，限速 ≤500KB/s，验证 190s 窗口内完成（定理 13.T2）
- [ ] **前向安全测试**：注入 ≤10% 拜占庭节点，跨 Epoch 尝试积累份额，验证旧 share 无法重建新秘密（定理 13.15）
- [ ] **Sybil 准入限速测试**：λ_adm×10 速率提交 JoinElectTx，验证超配额请求被拒绝，N(t) 受控（定理 13.16）
- [ ] **冷启动裂变测试**：N(t) 从 0 → 10^5，验证 P 从 2 → 100 触发正确裂变时序（推论 13.16''）

---

## 里程碑

```
Week 1:    阶段0 — Protobuf 协议字段确认，常量审计
Week 2-5:  阶段1 — TNSE 差分承诺生成/验证/新成员初始化   ← 关键路径
Week 6-8:  阶段2 — Root 准入限速 + Shard 裂变触发         ← 关键路径
Week 9-11: 阶段3 — M1-M6 补全（可与阶段2并行）
Week 12-13:阶段4 — 集成测试，参数校准，文档对齐
```

**关键路径：阶段1 → 阶段2**（TNSE 新委员会初始化依赖准入限速知道池大小）  
M1-M6 可与阶段2并行推进。
