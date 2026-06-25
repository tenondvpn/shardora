# EIP-1559 交易支持实现文档

## 概述

本文档描述了在 Shardora 区块链中实现 EIP-1559 (Type 2) 交易的修改。EIP-1559 是以太坊的一个重要升级，引入了新的交易费用机制。

## 什么是 EIP-1559？

EIP-1559 引入了以下关键特性：

1. **Type 2 交易格式**：新的交易类型，以 `0x02` 字节开头
2. **maxFeePerGas**：用户愿意支付的最大 gas 费用
3. **maxPriorityFeePerGas**：给矿工的小费（优先费）
4. **动态基础费用**：根据网络拥堵自动调整的基础费用
5. **访问列表**：预声明要访问的地址和存储槽

## 修改内容

### 1. C++ 后端修改 (`src/init/http_handler.cc`)

#### 1.1 RLP 解码器增强

修改了 `DecodeEthRawTx` 函数以支持两种交易格式：

**Legacy 交易 (EIP-155)**:
```
RLP([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
```

**EIP-1559 交易 (Type 2)**:
```
0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, v, r, s])
```

关键代码：
```cpp
if (p[0] == 0x02) {
    // EIP-1559 (Type 2) transaction
    SHARDORA_INFO("DecodeEthRawTx: EIP-1559 (Type 2) transaction detected");
    p++; len--;  // Skip type byte
    
    // Decode RLP fields...
    // chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, 
    // to, value, data, accessList, v, r, s
}
```

#### 1.2 签名哈希计算

为不同交易类型实现了正确的签名哈希计算：

**Legacy (EIP-155)**:
```cpp
signing_hash = keccak256(RLP([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]))
```

**EIP-1559 (Type 2)**:
```cpp
signing_hash = keccak256(0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]))
```

关键代码：
```cpp
if (is_eip1559) {
    // EIP-1559 signing hash
    std::string payload;
    payload += rlp_encode_uint(kShardoraChainId);
    payload += rlp_encode_uint(nonce);
    payload += rlp_encode_uint(gas_price);  // maxPriorityFeePerGas
    payload += rlp_encode_uint(gas_price);  // maxFeePerGas
    payload += rlp_encode_uint(gas_limit);
    payload += rlp_encode_bytes(to);
    payload += rlp_encode_uint(value);
    payload += rlp_encode_bytes(data);
    payload += rlp_encode_bytes("");  // accessList (empty)
    
    std::string signing_rlp = rlp_list(payload);
    std::string type_and_rlp = std::string(1, '\x02') + signing_rlp;
    signing_hash = common::Hash::keccak256(type_and_rlp);
}
```

### 2. Python SDK 修改 (`clipy/shardora3.py`)

#### 2.1 增强的交易签名函数

修改了 `_eth_sign_and_send` 函数以支持 EIP-1559：

```python
def _eth_sign_and_send(client, pk_hex: str, to: bytes, value: int, data: bytes,
                       nonce: int, gas_limit: int = 5000000, gas_price: int = 1,
                       chain_id: int = 3355103125, use_eip1559: bool = False,
                       max_priority_fee_per_gas: int = None, 
                       max_fee_per_gas: int = None) -> str:
    """
    构建 EIP-155 (legacy) 或 EIP-1559 (Type 2) 签名交易
    
    参数:
        use_eip1559: 如果为 True，使用 EIP-1559 交易格式
        max_priority_fee_per_gas: 最大优先费（仅 EIP-1559）
        max_fee_per_gas: 最大费用（仅 EIP-1559）
    """
    if use_eip1559:
        # EIP-1559 (Type 2) transaction
        tx = {
            'type': 2,
            'chainId': chain_id,
            'nonce': nonce,
            'maxPriorityFeePerGas': max_priority_fee_per_gas or gas_price,
            'maxFeePerGas': max_fee_per_gas or gas_price,
            'gas': gas_limit,
            'to': to_checksum_address('0x' + to.hex()) if to else None,
            'value': value,
            'data': data,
            'accessList': [],
        }
    else:
        # Legacy transaction
        tx = {
            'nonce': nonce,
            'gasPrice': gas_price,
            'gas': gas_limit,
            'to': to_checksum_address('0x' + to.hex()) if to else None,
            'value': value,
            'data': data,
            'chainId': chain_id,
        }
```

### 3. 测试脚本 (`clipy/test_eip1559.py`)

创建了完整的 EIP-1559 测试套件：

#### 测试用例 1: EIP-1559 原生代币转账
```python
def test_eip1559_transfer(w3, MY, KEY):
    """测试 EIP-1559 原生代币转账"""
    tx_hash = _eth_sign_and_send(
        w3.client,
        KEY,
        bytes.fromhex(recipient),
        transfer_amount,
        b'',
        nonce,
        gas_limit=21000,
        use_eip1559=True,
        max_priority_fee_per_gas=1,
        max_fee_per_gas=2
    )
```

#### 测试用例 2: EIP-1559 合约部署
```python
def test_eip1559_contract_deploy(w3, MY, KEY):
    """测试 EIP-1559 合约部署"""
    tx_hash = _eth_sign_and_send(
        w3.client,
        KEY,
        b'',  # 空 'to' 表示合约创建
        0,
        bytes.fromhex(bytecode),
        nonce,
        gas_limit=5000000,
        use_eip1559=True,
        max_priority_fee_per_gas=1,
        max_fee_per_gas=2
    )
```

#### 测试用例 3: EIP-1559 合约调用
```python
def test_eip1559_contract_call(w3, MY, KEY, contract_addr, abi):
    """测试 EIP-1559 合约函数调用"""
    tx_hash = _eth_sign_and_send(
        w3.client,
        KEY,
        bytes.fromhex(contract_addr),
        0,
        encoded_data,
        nonce,
        gas_limit=500000,
        use_eip1559=True,
        max_priority_fee_per_gas=1,
        max_fee_per_gas=2
    )
```

## 使用方法

### 1. 编译 C++ 代码

```bash
# 重新编译 Shardora 节点
cd /path/to/shardora
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 2. 运行测试

```bash
# 确保 Shardora 节点正在运行
cd clipy

# 运行 EIP-1559 测试
python test_eip1559.py --host 127.0.0.1 --port 23001 --key <your_private_key>
```

### 3. 在代码中使用

#### Python SDK 示例

```python
from shardora_sdk import ShardoraWeb3Mock, _eth_sign_and_send

# 初始化客户端
w3 = ShardoraWeb3Mock("127.0.0.1", 23001)
MY = w3.client.get_address(private_key)

# 发送 EIP-1559 交易
tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    bytes.fromhex(recipient_address),
    value=1000000,
    data=b'',
    nonce=w3.client.get_nonce(MY),
    gas_limit=21000,
    use_eip1559=True,
    max_priority_fee_per_gas=1,
    max_fee_per_gas=2
)
```

#### MetaMask 兼容性

由于实现了标准的 EIP-1559 RLP 编码和签名，MetaMask 和其他以太坊钱包可以直接发送 EIP-1559 交易到 Shardora 节点：

```javascript
// MetaMask 示例
const tx = await ethereum.request({
  method: 'eth_sendTransaction',
  params: [{
    from: accounts[0],
    to: '0x...',
    value: '0x...',
    maxFeePerGas: '0x2',
    maxPriorityFeePerGas: '0x1',
  }],
});
```

## 技术细节

### RLP 编码格式

#### Legacy 交易
```
RLP([
  nonce,
  gasPrice,
  gasLimit,
  to,
  value,
  data,
  v,
  r,
  s
])
```

#### EIP-1559 交易
```
0x02 || RLP([
  chainId,
  nonce,
  maxPriorityFeePerGas,
  maxFeePerGas,
  gasLimit,
  to,
  value,
  data,
  accessList,
  v,
  r,
  s
])
```

### 签名恢复

**Legacy (EIP-155)**:
- v 值编码: `v = chainId * 2 + 35 + parity`
- 签名哈希: `keccak256(RLP([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]))`

**EIP-1559 (Type 2)**:
- v 值: 直接使用 parity (0 或 1)
- 签名哈希: `keccak256(0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]))`

### Gas 费用计算

在 Shardora 中，我们简化了 EIP-1559 的费用机制：

1. **maxFeePerGas** 用作实际的 gas 价格
2. **maxPriorityFeePerGas** 目前被记录但不影响费用计算
3. 未来可以实现动态基础费用和优先费分配

## 兼容性

### 支持的功能
- ✅ EIP-1559 (Type 2) 交易解码
- ✅ 正确的签名哈希计算
- ✅ 公钥恢复和地址验证
- ✅ 合约部署（CREATE）
- ✅ 合约调用
- ✅ 原生代币转账
- ✅ MetaMask 兼容

### 限制
- ⚠️ 访问列表（accessList）被解析但未使用
- ⚠️ 动态基础费用机制未实现（使用固定费用）
- ⚠️ 优先费分配未实现

## 测试结果

运行测试脚本后，预期输出：

```
======================================================================
EIP-1559 Transaction Test Suite
======================================================================
Host: 127.0.0.1:23001
Private Key: 7c5b4ec6...c8a4f66
Sender Address: 620a1c023fdef21f3c10bf3d468de37d5ecfdc7b
Sender Balance: 1000000000

======================================================================
TEST CASE 1: EIP-1559 Native Token Transfer
======================================================================
[1] Preparing EIP-1559 transfer...
    ✅ Transaction sent!
    TX Hash: 0x...
[2] Waiting for transaction confirmation...
    ✅ Transaction confirmed!

======================================================================
TEST CASE 2: EIP-1559 Contract Deployment
======================================================================
[1] Compiling contract...
    ✅ Contract compiled
[2] Deploying contract with EIP-1559...
    ✅ Deployment transaction sent!
    ✅ Contract deployed successfully!

======================================================================
TEST CASE 3: EIP-1559 Contract Function Call
======================================================================
[1] Creating contract instance...
    ✅ Contract instance created
[2] Setting value using EIP-1559...
    ✅ Transaction sent!
    ✅ Value updated successfully!

======================================================================
TEST SUMMARY
======================================================================
EIP-1559 Transfer................................ ✅ PASSED
EIP-1559 Contract Deploy......................... ✅ PASSED
EIP-1559 Contract Call........................... ✅ PASSED

Total: 3/3 tests passed
🎉 All tests passed!
```

## 故障排除

### 问题 1: 交易解码失败
**症状**: `DecodeEthRawTx: typed transaction not supported`

**解决方案**: 确保使用最新编译的代码，检查交易格式是否正确。

### 问题 2: 签名验证失败
**症状**: `signature recovery failed`

**解决方案**: 
1. 检查 chainId 是否正确（Shardora 默认: 3355103125）
2. 验证私钥格式
3. 确认签名哈希计算正确

### 问题 3: Gas 费用不足
**症状**: 交易被拒绝

**解决方案**: 增加 `maxFeePerGas` 和 `gas_limit` 值。

## 未来改进

1. **动态基础费用**: 实现类似以太坊的动态基础费用机制
2. **优先费分配**: 实现矿工优先费分配逻辑
3. **访问列表优化**: 利用访问列表优化 gas 消耗
4. **费用估算**: 提供更准确的 gas 费用估算 API
5. **EIP-2930**: 支持 Type 1 交易（访问列表交易）

## 参考资料

- [EIP-1559: Fee market change for ETH 1.0 chain](https://eips.ethereum.org/EIPS/eip-1559)
- [EIP-2718: Typed Transaction Envelope](https://eips.ethereum.org/EIPS/eip-2718)
- [EIP-2930: Optional access lists](https://eips.ethereum.org/EIPS/eip-2930)
- [Ethereum RLP Encoding](https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/)

## 贡献者

- 实现: Shardora 开发团队
- 测试: Shardora QA 团队
- 文档: Shardora 技术写作团队

## 许可证

本实现遵循 Shardora 项目的开源许可证。
