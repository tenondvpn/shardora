# EIP-1559 使用说明

## 快速运行

### 使用默认配置（推荐）

```bash
cd clipy

# 运行完整测试套件
python test_eip1559.py

# 运行简单示例
python eip1559_example.py
```

### 默认配置

- **Host**: 127.0.0.1
- **Port**: 23001
- **Private Key**: 71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6

这些默认值与 `amm.py` 和其他测试脚本保持一致。

### 自定义配置

```bash
# 指定自定义参数
python test_eip1559.py --host 192.168.1.100 --port 8545 --key <your_key>

# 查看帮助
python test_eip1559.py --help
```

## 测试内容

### test_eip1559.py

完整的测试套件，包括：

1. **EIP-1559 原生代币转账**
   - 发送 EIP-1559 格式的转账交易
   - 验证余额变化

2. **EIP-1559 合约部署**
   - 使用 EIP-1559 部署智能合约
   - 验证合约地址和代码

3. **EIP-1559 合约调用**
   - 使用 EIP-1559 调用合约函数
   - 验证状态变化

### eip1559_example.py

简单的转账示例，演示基本用法。

## 预期输出

### 成功运行

```
======================================================================
EIP-1559 Transaction Test Suite
======================================================================
Host: 127.0.0.1:23001
Private Key: 71e57186...8e73b2f6
Sender Address: 620a1c023fdef21f3c10bf3d468de37d5ecfdc7b
Sender Balance: 1000000000

======================================================================
TEST CASE 1: EIP-1559 Native Token Transfer
======================================================================
[1] Preparing EIP-1559 transfer...
    ✅ Transaction sent!
[2] Waiting for transaction confirmation...
    ✅ Transaction confirmed!

======================================================================
TEST CASE 2: EIP-1559 Contract Deployment
======================================================================
[1] Compiling contract...
    ✅ Contract compiled
[2] Deploying contract with EIP-1559...
    ✅ Contract deployed successfully!

======================================================================
TEST CASE 3: EIP-1559 Contract Function Call
======================================================================
[1] Creating contract instance...
    ✅ Contract instance created
[2] Setting value using EIP-1559...
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

## 常见问题

### Q: 为什么使用这个默认私钥？

A: 这是一个测试用的私钥，与其他测试脚本（如 amm.py）保持一致，方便开发和测试。**不要在生产环境使用！**

### Q: 如何使用自己的私钥？

A: 使用 `--key` 参数：

```bash
python test_eip1559.py --key <your_private_key_hex>
```

### Q: 测试失败怎么办？

A: 检查以下几点：

1. Shardora 节点是否正在运行
2. 节点地址和端口是否正确
3. 账户余额是否充足
4. 查看节点日志获取详细错误信息

### Q: 如何连接到远程节点？

A: 使用 `--host` 和 `--port` 参数：

```bash
python test_eip1559.py --host 192.168.1.100 --port 8545
```

## 参数说明

### test_eip1559.py

```
usage: test_eip1559.py [-h] [--host HOST] [--port PORT] [--key KEY]

EIP-1559 Transaction Test

optional arguments:
  -h, --help   show this help message and exit
  --host HOST  Shardora node host (default: 127.0.0.1)
  --port PORT  Shardora node port (default: 23001)
  --key KEY    Private key (hex, default: test key)
```

### eip1559_example.py

```
usage: eip1559_example.py [-h] [--host HOST] [--port PORT] [--key KEY]

EIP-1559 Transaction Simple Example

optional arguments:
  -h, --help   show this help message and exit
  --host HOST  Shardora node host (default: 127.0.0.1)
  --port PORT  Shardora node port (default: 23001)
  --key KEY    Private key (hex, default: test key)
```

## 与其他测试脚本的一致性

所有测试脚本使用相同的默认配置：

| 脚本 | 默认 Host | 默认 Port | 默认 Key |
|------|-----------|-----------|----------|
| amm.py | 127.0.0.1 | 23001 | 71e571... |
| test_eip1559.py | 127.0.0.1 | 23001 | 71e571... |
| eip1559_example.py | 127.0.0.1 | 23001 | 71e571... |

这确保了测试环境的一致性和可重复性。

## 下一步

- 查看 [EIP1559_README.md](EIP1559_README.md) 了解完整功能
- 查看 [EIP1559_QUICK_REFERENCE.md](EIP1559_QUICK_REFERENCE.md) 获取代码示例
- 查看 [EIP1559_IMPLEMENTATION.md](EIP1559_IMPLEMENTATION.md) 了解技术细节

## 支持

如需帮助，请查看文档或联系开发团队。
