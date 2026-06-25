# RIPEMD-160 预编译合约快速参考

## 基本信息

| 属性 | 值 |
|------|-----|
| 合约地址 | `0x0000000000000000000000000000000000000003` |
| 功能 | 计算 RIPEMD-160 哈希 |
| 输入 | 任意长度字节数据 |
| 输出 | 32 字节（12 字节零 + 20 字节哈希） |
| Gas 公式 | `600 + 120 * ceil(data_size / 32)` |

## 快速使用

### Solidity

```solidity
// 调用 RIPEMD-160
address constant RIPEMD160 = address(0x03);
(bool success, bytes memory result) = RIPEMD160.staticcall(data);
bytes32 hash = bytes32(result);
```

### Gas 计算

```
0 bytes   → 600 gas
1-32 bytes → 720 gas
33-64 bytes → 840 gas
65-96 bytes → 960 gas
```

## 输出格式

```
[12 bytes of zeros][20 bytes RIPEMD-160 hash]
0x000000000000000000000000 + [hash]
```

## 测试向量

```
""              → 0x0000000000000000000000009c1185a5c5e9fc54612808977ee8f548b2258d31
"a"             → 0x0000000000000000000000000bdc9d2d256b3ee9daae347be6f4dc835a467ffe
"abc"           → 0x0000000000000000000000008eb208f7e05d987a9b044a8e98c6b087f15a0bfc
"message digest" → 0x0000000000000000000000005d0689ef49d2fae572b881b123a85ffa21595f36
```

## 实现文件

- `src/contract/contract_ripemd160.h` - 头文件
- `src/contract/contract_ripemd160.cc` - 实现文件

## 关键代码

```cpp
// 计算 Gas
uint64_t word_count = (data_size + 31) / 32;
uint64_t gas_cost = 600 + 120 * word_count;

// 计算哈希
std::string hash = common::Hash::ripemd160(param.data);

// 格式化输出（32 字节）
res->output_data = new uint8_t[32];
memset(res->output_data, 0, 32);
memcpy(res->output_data + 12, hash.c_str(), 20);
```

## 与其他哈希对比

| 算法 | 地址 | 输出长度 | Base Gas | Per Word Gas |
|------|------|----------|----------|--------------|
| RIPEMD-160 | 0x03 | 20 bytes | 600 | 120 |
| SHA-256 | 0x02 | 32 bytes | 60 | 12 |
| Keccak-256 | - | 32 bytes | 30 | 6 |

## 常见用途

1. **比特币地址生成**: RIPEMD-160(SHA-256(pubkey))
2. **数据完整性验证**: 生成数据指纹
3. **密码学应用**: 作为哈希函数使用

## 注意事项

⚠️ RIPEMD-160 是较老的算法，Gas 消耗较高  
⚠️ 对于新应用，推荐使用 SHA-256 或 Keccak-256  
⚠️ 输出是 32 字节，但实际哈希只有 20 字节  
⚠️ 前 12 字节始终为零

## 完整文档

详见 `CONTRACT_RIPEMD160_IMPLEMENTATION.md`
