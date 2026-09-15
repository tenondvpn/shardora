# 详细错误日志增强

## 修改内容

在三个关键的错误日志位置添加了更详细的调试信息，帮助快速定位问题。

### 1. block_acceptor.cc - GetAndAddTxsLocally 错误

**位置**: `src/consensus/hotstuff/block_acceptor.cc` 行 ~1211

**修改前**:
```cpp
SHARDORA_ERROR("invalid consensus, txs not equal to leader %u, %u",
    txs_ptr->txs.size(), tx_propose.txs_size());
```

**修改后**:
```cpp
SHARDORA_ERROR("invalid consensus, txs not equal to leader: local_txs=%zu, leader_txs=%d, pool_idx=%u, "
    "local_first_tx_hash=%s, leader_first_tx_hash=%s",
    txs_ptr->txs.size(), tx_propose.txs_size(), pool_idx_,
    (txs_ptr->txs.empty() ? "empty" : common::Encode::HexEncode(
        pools::GetTxMessageHash(txs_ptr->txs[0])).substr(0, 16).c_str()),
    (tx_propose.txs_size() == 0 ? "empty" : common::Encode::HexEncode(
        pools::GetTxMessageHash(tx_propose.txs(0))).substr(0, 16).c_str()));
```

**新增信息**:
- `local_txs`: 本地交易数量
- `leader_txs`: Leader 交易数量
- `pool_idx`: 所属 pool 索引
- `local_first_tx_hash`: 本地第一个交易的哈希（前 16 字符）
- `leader_first_tx_hash`: Leader 第一个交易的哈希（前 16 字符）

### 2. block_acceptor.cc - Accept 函数错误

**位置**: `src/consensus/hotstuff/block_acceptor.cc` 行 ~234

**修改前**:
```cpp
SHARDORA_WARN("GetAndAddTxsLocally error!");
```

**修改后**:
```cpp
SHARDORA_WARN("GetAndAddTxsLocally error! status=%d, pool_idx=%u, view_height=%lu, "
    "parent_hash=%s, get_txs_time=%lums, txs_count=%zu",
    (int)s, pool_idx(), view_block.height(), 
    common::Encode::HexEncode(view_block.parent_hash()).substr(0, 16).c_str(),
    (get_txs_end_ms - get_txs_begin_ms),
    (txs_ptr ? txs_ptr->txs.size() : 0));
```

**新增信息**:
- `status`: 错误状态码
- `pool_idx`: 所属 pool 索引
- `view_height`: 当前 view 高度
- `parent_hash`: 父块哈希（前 16 字符）
- `get_txs_time`: 获取交易耗时（毫秒）
- `txs_count`: 交易数量

### 3. hotstuff.cc - HandleProposeMsgImpl 错误

**位置**: `src/consensus/hotstuff/hotstuff.cc` 行 ~739

**修改前**:
```cpp
SHARDORA_ERROR("handle propose message failed hash: %lu, propose_debug: %s",
    msg_ptr->header.hash64(),
    ProtobufToJson(msg_ptr->header).c_str());
```

**修改后**:
```cpp
SHARDORA_ERROR("handle propose message failed: status=%d, hash=%lu, view=%lu, height=%lu, "
    "pool_idx=%u, txs_count=%d, propose_debug=%s",
    (int)st,
    msg_ptr->header.hash64(),
    msg_ptr->header.hotstuff().pro_msg().view(),
    msg_ptr->header.hotstuff().pro_msg().height(),
    msg_ptr->header.hotstuff().pro_msg().pool_index(),
    msg_ptr->header.hotstuff().pro_msg().tx_propose().txs_size(),
    ProtobufToJson(msg_ptr->header).c_str());
```

**新增信息**:
- `status`: 错误状态码
- `hash`: 消息哈希
- `view`: 当前 view 号
- `height`: 块高度
- `pool_idx`: 所属 pool 索引
- `txs_count`: 交易数量
- `propose_debug`: 完整的 protobuf 信息

## 日志示例

### 修改前
```
error [async_file] [block_acceptor.cc][GetAndAddTxsLocally][1211] invalid consensus, txs not equal to leader 0, 12
warning [async_file] [block_acceptor.cc][Accept][234] GetAndAddTxsLocally error!
error [async_file] [hotstuff.cc][HandleProposeMsgImpl][739] handle propose message failed hash: 5616446173339192548, propose_debug: ...
```

### 修改后
```
error [async_file] [block_acceptor.cc][GetAndAddTxsLocally][1211] invalid consensus, txs not equal to leader: local_txs=0, leader_txs=12, pool_idx=0, local_first_tx_hash=empty, leader_first_tx_hash=a1b2c3d4e5f6g7h8

warning [async_file] [block_acceptor.cc][Accept][234] GetAndAddTxsLocally error! status=1, pool_idx=0, view_height=100, parent_hash=f1e2d3c4b5a6g7h8, get_txs_time=45ms, txs_count=0

error [async_file] [hotstuff.cc][HandleProposeMsgImpl][739] handle propose message failed: status=1, hash=5616446173339192548, view=100, height=100, pool_idx=0, txs_count=12, propose_debug: ...
```

## 调试价值

这些详细的日志信息可以帮助快速定位问题：

1. **交易数量不匹配**
   - 比较 `local_txs` 和 `leader_txs`
   - 查看第一个交易的哈希是否相同
   - 判断是本地交易池问题还是网络同步问题

2. **GetAndAddTxsLocally 失败**
   - 查看 `status` 码判断具体错误类型
   - 查看 `get_txs_time` 判断是否超时
   - 查看 `txs_count` 判断是否有交易

3. **Propose 消息处理失败**
   - 查看 `status` 码判断具体错误类型
   - 查看 `view` 和 `height` 判断是否是旧消息
   - 查看 `txs_count` 判断交易数量
   - 查看 `pool_idx` 判断是否是特定 pool 的问题

## 修改文件

- `src/consensus/hotstuff/block_acceptor.cc`:
  - 行 ~1211: 增强 GetAndAddTxsLocally 错误日志
  - 行 ~234: 增强 Accept 函数错误日志

- `src/consensus/hotstuff/hotstuff.cc`:
  - 行 ~739: 增强 HandleProposeMsgImpl 错误日志

## 编译和测试

1. 编译项目：`./build.sh shardora`
2. 运行测试：`./txcli 0 3 0 <ip> <port>`
3. 观察日志：当出现错误时，会看到更详细的信息

## 总结

这个修改提供了：
1. ✅ 更详细的错误上下文
2. ✅ 快速定位问题的关键信息
3. ✅ 便于调试和性能分析
4. ✅ 帮助理解系统状态
