# EIP-1559 快速参考指南

## 快速开始

### 1. 发送 EIP-1559 转账

```python
from shardora_sdk import ShardoraWeb3Mock, _eth_sign_and_send

w3 = ShardoraWeb3Mock("127.0.0.1", 23001)
sender = w3.client.get_address(private_key)
nonce = w3.client.get_nonce(sender)

tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    bytes.fromhex(recipient),
    value=1000000,
    data=b'',
    nonce=nonce,
    gas_limit=21000,
    use_eip1559=True,
    max_priority_fee_per_gas=1,
    max_fee_per_gas=2
)
```

### 2. 部署合约（EIP-1559）

```python
from shardora_sdk import compile_and_link

# 编译合约
bytecode, abi = compile_and_link(contract_source, "ContractName")

# 部署
tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    b'',  # 空地址表示合约创建
    value=0,
    data=bytes.fromhex(bytecode),
    nonce=nonce,
    gas_limit=5000000,
    use_eip1559=True,
    max_priority_fee_per_gas=1,
    max_fee_per_gas=2
)
```

### 3. 调用合约（EIP-1559）

```python
from eth_abi import encode

# 编码函数调用
function_selector = bytes.fromhex("55241077")  # setValue(uint256)
encoded_data = function_selector + encode(['uint256'], [12345])

# 发送交易
tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    bytes.fromhex(contract_address),
    value=0,
    data=encoded_data,
    nonce=nonce,
    gas_limit=500000,
    use_eip1559=True,
    max_priority_fee_per_gas=1,
    max_fee_per_gas=2
)
```

## 参数说明

### EIP-1559 特有参数

| 参数 | 类型 | 说明 | 默认值 |
|------|------|------|--------|
| `use_eip1559` | bool | 是否使用 EIP-1559 格式 | False |
| `max_priority_fee_per_gas` | int | 最大优先费（小费） | gas_price |
| `max_fee_per_gas` | int | 最大总费用 | gas_price |

### 通用参数

| 参数 | 类型 | 说明 | 默认值 |
|------|------|------|--------|
| `nonce` | int | 交易序号 | 必需 |
| `gas_limit` | int | Gas 限制 | 5000000 |
| `gas_price` | int | Gas 价格（Legacy） | 1 |
| `value` | int | 转账金额 | 0 |
| `data` | bytes | 交易数据 | b'' |

## 交易类型对比

### Legacy (EIP-155)

```python
tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    to_address,
    value,
    data,
    nonce,
    gas_limit=21000,
    gas_price=1,
    use_eip1559=False  # 或省略此参数
)
```

**特点**:
- 使用固定 `gasPrice`
- 简单直接
- 兼容旧版本

### EIP-1559 (Type 2)

```python
tx_hash = _eth_sign_and_send(
    w3.client,
    private_key,
    to_address,
    value,
    data,
    nonce,
    gas_limit=21000,
    use_eip1559=True,
    max_priority_fee_per_gas=1,
    max_fee_per_gas=2
)
```

**特点**:
- 使用 `maxFeePerGas` 和 `maxPriorityFeePerGas`
- 更灵活的费用控制
- 以太坊主网标准

## 常见场景

### 场景 1: 普通转账

```python
# Legacy
tx_hash = _eth_sign_and_send(
    w3.client, key, to, 1000000, b'', nonce, 21000, 1
)

# EIP-1559
tx_hash = _eth_sign_and_send(
    w3.client, key, to, 1000000, b'', nonce, 21000,
    use_eip1559=True, max_priority_fee_per_gas=1, max_fee_per_gas=2
)
```

### 场景 2: 合约部署

```python
# Legacy
tx_hash = _eth_sign_and_send(
    w3.client, key, b'', 0, bytecode, nonce, 5000000, 1
)

# EIP-1559
tx_hash = _eth_sign_and_send(
    w3.client, key, b'', 0, bytecode, nonce, 5000000,
    use_eip1559=True, max_priority_fee_per_gas=1, max_fee_per_gas=2
)
```

### 场景 3: 合约调用

```python
# Legacy
tx_hash = _eth_sign_and_send(
    w3.client, key, contract_addr, 0, call_data, nonce, 500000, 1
)

# EIP-1559
tx_hash = _eth_sign_and_send(
    w3.client, key, contract_addr, 0, call_data, nonce, 500000,
    use_eip1559=True, max_priority_fee_per_gas=1, max_fee_per_gas=2
)
```

## Gas 费用建议

### 开发/测试环境

```python
max_priority_fee_per_gas = 1
max_fee_per_gas = 2
gas_limit = 21000  # 转账
gas_limit = 500000  # 合约调用
gas_limit = 5000000  # 合约部署
```

### 生产环境

```python
# 根据网络拥堵情况调整
max_priority_fee_per_gas = 2  # 小费
max_fee_per_gas = 10  # 最大费用
gas_limit = 根据实际需求估算
```

## 错误处理

### 常见错误

1. **签名验证失败**
```python
try:
    tx_hash = _eth_sign_and_send(...)
except Exception as e:
    if "signature recovery failed" in str(e):
        print("检查私钥和 chainId")
```

2. **Gas 不足**
```python
try:
    tx_hash = _eth_sign_and_send(..., gas_limit=21000)
except Exception as e:
    if "out of gas" in str(e):
        print("增加 gas_limit")
```

3. **Nonce 错误**
```python
try:
    tx_hash = _eth_sign_and_send(..., nonce=nonce)
except Exception as e:
    if "nonce" in str(e):
        nonce = w3.client.get_nonce(sender)
        print(f"使用新 nonce: {nonce}")
```

## 调试技巧

### 1. 启用详细日志

```python
import logging
logging.basicConfig(level=logging.DEBUG)
```

### 2. 检查交易状态

```python
import time

tx_hash = _eth_sign_and_send(...)
print(f"TX Hash: {tx_hash}")

# 等待确认
time.sleep(10)

# 检查余额变化
new_balance = w3.client.get_balance(recipient)
print(f"New balance: {new_balance}")
```

### 3. 验证交易参数

```python
print(f"Nonce: {nonce}")
print(f"Gas Limit: {gas_limit}")
print(f"Max Fee: {max_fee_per_gas}")
print(f"Max Priority Fee: {max_priority_fee_per_gas}")
print(f"Value: {value}")
print(f"Data length: {len(data)}")
```

## MetaMask 集成

### JavaScript 示例

```javascript
// 发送 EIP-1559 交易
const tx = await ethereum.request({
  method: 'eth_sendTransaction',
  params: [{
    from: accounts[0],
    to: '0x...',
    value: '0xF4240',  // 1000000 in hex
    maxFeePerGas: '0x2',
    maxPriorityFeePerGas: '0x1',
    gas: '0x5208',  // 21000 in hex
  }],
});

console.log('TX Hash:', tx);
```

## 性能优化

### 1. 批量交易

```python
nonce = w3.client.get_nonce(sender)

for i in range(10):
    tx_hash = _eth_sign_and_send(
        w3.client, key, to, 1000, b'', nonce + i,
        21000, use_eip1559=True, max_priority_fee_per_gas=1, max_fee_per_gas=2
    )
    print(f"TX {i}: {tx_hash}")
```

### 2. 并行发送

```python
from concurrent.futures import ThreadPoolExecutor

def send_tx(i):
    return _eth_sign_and_send(
        w3.client, key, to, 1000, b'', nonce + i,
        21000, use_eip1559=True, max_priority_fee_per_gas=1, max_fee_per_gas=2
    )

with ThreadPoolExecutor(max_workers=5) as executor:
    futures = [executor.submit(send_tx, i) for i in range(10)]
    results = [f.result() for f in futures]
```

## 测试清单

- [ ] Legacy 转账正常
- [ ] EIP-1559 转账正常
- [ ] Legacy 合约部署正常
- [ ] EIP-1559 合约部署正常
- [ ] Legacy 合约调用正常
- [ ] EIP-1559 合约调用正常
- [ ] Gas 费用计算正确
- [ ] 签名验证通过
- [ ] Nonce 管理正确
- [ ] 错误处理完善

## 相关命令

```bash
# 运行完整测试
python clipy/test_eip1559.py --host 127.0.0.1 --port 23001 --key <key>

# 运行简单示例
python clipy/eip1559_example.py

# 查看日志
tail -f /path/to/shardora/logs/shardora.log | grep EIP-1559
```

## 更多资源

- 完整文档: `EIP1559_IMPLEMENTATION.md`
- 测试脚本: `clipy/test_eip1559.py`
- 示例代码: `clipy/eip1559_example.py`
- Shardora SDK: `clipy/shardora_sdk.py`

## 支持

如有问题，请：
1. 查看完整文档 `EIP1559_IMPLEMENTATION.md`
2. 检查日志文件
3. 运行测试脚本验证环境
4. 联系 Shardora 开发团队
