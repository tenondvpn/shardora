# Shardora 合约多级调用测试记录（2026-03-30）

## 目标

定位 `requestWithdrawToSolanaFromSHARDORA` 回执成功但状态不变的问题，验证是否为：

- 合约流程代码问题，或
- Shardora 节点对“多级合约内调用”执行异常。

## 本次新增测试文件

位于 `contracts/shardora/`：

- `ProbeCallee.sol`
- `ProbeCaller.sol`
- `test_call_value_probe.py`
- `ProbePool.sol`
- `ProbeTreasury.sol`
- `ProbeBridge.sol`
- `test_bridge_treasury_pool_probe.py`

## 测试一：基础 `call{value}` 能否工作

### 文件

- `ProbeCallee.sol`：`hit()` 为 payable，记录 `totalHits/lastValue`
- `ProbeCaller.sol`：`forwardHit()` 内部 `callee.call{value: msg.value}("hit()")`
- `test_call_value_probe.py`

### 关键交易

- Deploy `ProbeCallee`：`be373a04c668f72a4678190c7a505608b34389cb42f9bd088d265671c2dd3e3f`
- Deploy `ProbeCaller`：`b9c0c811cb3849d62c1230ccbcb04ab7464622ffa3e25e6d13a028871b660a12`
- 直接调用 `hit()`：`86930344f81e0104f8b74b7f4d5129815e788a2ecd20d06615132e6b48ce1ccf`
- 转发调用 `forwardHit()`：`e29c559bc702ff9787658ec8b499f35fa98ce8f5c1656e76d93d739f5ccb7a04`

### 结果

- `totalHits: 0 -> 2`
- `lastValue = 2`
- `forwards: 0 -> 1`

结论：**基础合约内 `call{value}` 可工作**。

---

## 测试二：最小复现 Bridge -> Treasury -> Pool 三段调用

### 文件

- `ProbePool.sol`：`sellSHARDORA(minOut)` 更新 `reserveSHARDORA/reserveUSDC`
- `ProbeTreasury.sol`：`swap(minOut)` 内部调用 `pool.sellSHARDORA`
- `ProbeBridge.sol`：`request(minOut)` 内部调用 `treasury.swap`
- `test_bridge_treasury_pool_probe.py`

### 关键交易

- Deploy `ProbePool`：`5b4d5562a77213d106027146a707b2460c7fac970b46342cbb50d3afc738edf2`
- Deploy `ProbeTreasury`：`174556cdf2ac012c7229bbb78b013d4333e3a863571ffb7d069b225e2d20b338`
- Deploy `ProbeBridge`：`42e9aefd732cffa6531f94549c4d7df8b1e0c75c5f9b57ab1da5c406edcb5fd0`
- `setBridge(...)`：`d794edb1c14bfef65917181f9f68ef747a65826a114c5ef6f85c38e60aab143a`
- `bridge.request(minOut=1, value=2)`：`79244666c7ca570bf5f5f7206d114a44e0c946e51d0aad3ecb1c8ed56dbde992`

### 结果（关键）

- 回执：`status=0`
- 但状态不变：
  - `ProbePool reserveSHARDORA/reserveUSDC: 10000/10000 -> 10000/10000`
  - `ProbeBridge totalRequests: 0 -> 0`
  - `ProbeTreasury totalSwaps: 0 -> 0`

结论：**多级链式调用（Bridge->Treasury->Pool）在当前 Shardora 环境表现为“回执成功但状态不落链”**。

---

## 与业务问题的对应

`requestWithdrawToSolanaFromSHARDORA` 的结构与测试二一致（多级合约调用），因此会出现同类现象：

- tx `status=0`
- `totalWithdrawRequests` 不增长
- Pool 储备不变化

该现象更偏向 **节点/执行环境问题**，不是单纯业务合约逻辑错误。

## 建议

1. 将本报告及上述 tx hash 提交 Shardora 节点侧排查（重点看 `status=0` 时是否实际执行状态提交）。
2. 在节点修复前，出金测试可暂用 legacy 路径：`requestWithdrawToSolana`（`transferFrom sUSDC`）。
3. 如需继续排查，可增加节点侧 trace / internal call 日志导出能力。

