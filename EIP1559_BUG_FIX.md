# EIP-1559 Bug 修复记录

## 问题描述

在测试 EIP-1559 交易时，遇到以下错误：

```
[eth_sendRawTransaction] {'error': {'code': -32602, 'message': 'invalid raw transaction'}, 'id': 1, 'jsonrpc': '2.0'}
```

## 根本原因

在 `src/init/http_handler.cc` 的 `DecodeEthRawTx` 函数中，`decode_item` lambda 函数无法处理 RLP 列表类型（字节 >= 0xc0）。

EIP-1559 交易包含一个 `accessList` 字段，即使为空也会被编码为 RLP 列表 `0xc0`。当解码器遇到这个字节时，会返回 false，导致整个交易解码失败。

## 修复方案

### 修改前

```cpp
auto decode_item = [](const uint8_t*& pp, size_t& ll, std::string& out) -> bool {
    if (ll < 1) return false;
    if (pp[0] <= 0x7f) {
        // Single byte
        ...
    } else if (pp[0] <= 0xb7) {
        // Short string
        ...
    } else if (pp[0] <= 0xbf) {
        // Long string
        ...
    } else {
        return false;  // ❌ 无法处理列表！
    }
    return true;
};
```

### 修改后

```cpp
auto decode_item = [](const uint8_t*& pp, size_t& ll, std::string& out) -> bool {
    if (ll < 1) return false;
    if (pp[0] <= 0x7f) {
        // Single byte
        ...
    } else if (pp[0] <= 0xb7) {
        // Short string
        ...
    } else if (pp[0] <= 0xbf) {
        // Long string
        ...
    } else if (pp[0] <= 0xf7) {
        // ✅ Short list (0xc0-0xf7) - for accessList
        size_t list_len = pp[0] - 0xc0;
        if (ll < 1 + list_len) return false;
        out = std::string((char*)pp + 1, list_len);
        pp += 1 + list_len; ll -= 1 + list_len;
    } else {
        // ✅ Long list (0xf8-0xff) - for accessList
        size_t hlen = pp[0] - 0xf7;
        if (ll < 1 + hlen) return false;
        size_t list_len = 0;
        for (size_t i = 1; i <= hlen; ++i) list_len = (list_len << 8) | pp[i];
        if (ll < 1 + hlen + list_len) return false;
        out = std::string((char*)pp + 1 + hlen, list_len);
        pp += 1 + hlen + list_len; ll -= 1 + hlen + list_len;
    }
    return true;
};
```

## RLP 编码规则

| 字节范围 | 类型 | 说明 |
|----------|------|------|
| 0x00-0x7f | 单字节 | 值本身 |
| 0x80-0xb7 | 短字符串 | 0-55 字节 |
| 0xb8-0xbf | 长字符串 | > 55 字节 |
| 0xc0-0xf7 | 短列表 | 0-55 字节 |
| 0xf8-0xff | 长列表 | > 55 字节 |

## EIP-1559 交易结构

```
0x02 || RLP([
  chainId,              // uint
  nonce,                // uint
  maxPriorityFeePerGas, // uint
  maxFeePerGas,         // uint
  gasLimit,             // uint
  to,                   // bytes20
  value,                // uint
  data,                 // bytes
  accessList,           // ✅ LIST (even if empty = 0xc0)
  v,                    // uint
  r,                    // bytes32
  s                     // bytes32
])
```

## 测试验证

### 测试用例

```python
# EIP-1559 交易
tx = {
    'type': 2,
    'chainId': 3355103125,
    'nonce': 8,
    'maxPriorityFeePerGas': 1,
    'maxFeePerGas': 2,
    'gas': 21000,
    'to': '0x0000000000000000000000000000000000000001',
    'value': 1000000,
    'data': b'',
    'accessList': [],  # 空列表，编码为 0xc0
}
```

### 预期结果

- ✅ 交易成功解码
- ✅ 签名验证通过
- ✅ 交易被接受并处理

## 影响范围

### 受影响的功能

- ✅ EIP-1559 (Type 2) 交易
- ✅ 包含 accessList 的交易
- ✅ MetaMask 发送的 EIP-1559 交易

### 不受影响的功能

- ✅ Legacy (EIP-155) 交易
- ✅ 不包含列表字段的交易

## 编译和部署

### 1. 重新编译

```bash
cd /root/shardora/build
make -j$(nproc)
```

### 2. 重启节点

```bash
# 停止旧节点
pkill shardora_node

# 启动新节点
./shardora_node --config config.json
```

### 3. 运行测试

```bash
cd /root/shardora/clipy
python3 test_eip1559.py
```

## 相关文件

- `src/init/http_handler.cc` - 修复的文件
- `clipy/test_eip1559.py` - 测试脚本
- `clipy/debug_eip1559_tx.py` - 调试脚本

## 参考资料

- [EIP-1559 规范](https://eips.ethereum.org/EIPS/eip-1559)
- [RLP 编码规范](https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/)
- [EIP-2930: Access Lists](https://eips.ethereum.org/EIPS/eip-2930)

## 版本历史

- **v1.0.0**: 初始 EIP-1559 实现
- **v1.0.1**: 修复 accessList 解码问题 ✅

---

**修复日期**: 2024年
**修复者**: Shardora 开发团队
**状态**: ✅ 已修复并测试
