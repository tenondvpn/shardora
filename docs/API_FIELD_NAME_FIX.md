# API Field Name Fix Summary

## 问题描述

查询账户信息 API (`/query_account`) 返回的 JSON 使用驼峰命名（camelCase），而不是下划线命名（snake_case）。

## 实际 API 响应

```json
{
  "balance": "0",
  "shardingId": 3,
  "poolIndex": 21,
  "addr": "shTzhb3S85Rsk5iM0vOCBjLS5M0=",
  "type": "kNormal",
  "bytesCode": "YIBgQFI0gBVgDldfX/1bUGAENhBgRFdfNWDgHIBjIJZSVRRgSFeAYz+k8kUUYF5XgGNVJBB3FGBlV4BjjaXLWxRgdldbX1/9W19UW2BAUZCBUmAgAVtgQFGAkQOQ81tgS19UgVZbYHRgcDZgBGDZVltgn1ZbAFtgAVRgiJBgAWABYKAbAxaBVltgQFGQYAFgAWCgGwOQkRaBUmAgAWBVVltfgZBVYEBRgYFSfwEseOK4QyWHixvZ0lDXcs/lvadyLXlfRQNvpeHm4wP8kGAgAWBAUYCRA5ChUFZbX2AggocDEhVg6FdfX/1bUDWRkFBW/qJkaXBmc1ggEiDV0gD6F+3HDSO+ALrgI+eWEuCh59Ls7c/TZ256EMd7vWRzb2xjQwAIHgAz",
  "latestHeight": "17",
  "txIndex": 0,
  "nonce": "0"
}
```

## 字段名对照表

| Python 代码期望 | API 实际返回 | 类型 | 说明 |
|----------------|-------------|------|------|
| `sharding_id` 或 `shard_id` | `shardingId` | int | 分片 ID |
| `pool_index` | `poolIndex` | int | 池索引 |
| `balance` | `balance` | string | 余额（字符串格式） |
| `nonce` | `nonce` | string | Nonce（字符串格式） |
| `latest_height` | `latestHeight` | string | 最新高度 |
| `tx_index` | `txIndex` | int | 交易索引 |
| `bytes_code` | `bytesCode` | string | 字节码（Base64） |
| `addr` | `addr` | string | 地址（Base64） |
| `type` | `type` | string | 地址类型 |

## 修复前的代码

❌ **错误：只检查下划线命名**
```python
def query_address_info(w3, address: str, max_wait: int = 60):
    # ...
    if result.status_code == 200:
        data = result.json()
        
        # 只检查下划线命名，会失败
        shard_id = data.get('sharding_id') or data.get('shard_id')
        pool_index = data.get('pool_index')
        
        if shard_id is not None and pool_index is not None:
            # 永远不会执行到这里！
            return {
                'shard_id': int(shard_id),
                'pool_index': int(pool_index),
                # ...
            }
```

## 修复后的代码

✅ **正确：优先检查驼峰命名，兼容下划线命名**
```python
def query_address_info(w3, address: str, max_wait: int = 60):
    """
    Query address information from the blockchain, including shard and pool.
    
    Args:
        w3: Web3 mock instance
        address: Address to query (without 0x prefix)
        max_wait: Maximum wait time in seconds
    
    Returns:
        dict: Address info with 'shard_id' and 'pool_index', or None if failed
    """
    print(f"  🔍 Querying address info from blockchain...")
    
    # Clean address format
    clean_addr = address.replace('0x', '')
    
    start_time = time.time()
    check_interval = 2
    
    while time.time() - start_time < max_wait:
        try:
            # Use the query_url from the client
            result = requests.post(
                w3.client.query_url, 
                data={"address": clean_addr}, 
                timeout=5, 
                verify=w3.client.verify_ssl
            )
            
            if result.status_code == 200:
                data = result.json()
                
                # Check if address exists and has shard/pool info
                # API returns camelCase: 'shardingId' and 'poolIndex'
                # Also check snake_case for backward compatibility
                shard_id = data.get('shardingId') or data.get('sharding_id') or data.get('shard_id')
                pool_index = data.get('poolIndex') or data.get('pool_index')
                
                if shard_id is not None and pool_index is not None:
                    elapsed = time.time() - start_time
                    print(f"  ✅ Address info retrieved! (took {elapsed:.1f}s)")
                    print(f"     Shard: {shard_id}, Pool: {pool_index}")
                    print(f"     Balance: {data.get('balance', 0)}")
                    return {
                        'shard_id': int(shard_id),
                        'pool_index': int(pool_index),
                        'balance': int(data.get('balance', 0)),
                        'nonce': int(data.get('nonce', 0))
                    }
                else:
                    # Address exists but no shard/pool info yet
                    print(f"  ⏳ Address found but shard/pool not yet assigned...")
                    print(f"     Debug: data keys = {list(data.keys())}")
                    
        except requests.exceptions.RequestException as e:
            print(f"  ⚠️  Request error: {e}")
        except Exception as e:
            print(f"  ⚠️  Parse error: {e}")
        
        time.sleep(check_interval)
    
    # Timeout - return None
    elapsed = time.time() - start_time
    print(f"  ⚠️  Timeout after {elapsed:.1f}s, could not retrieve address info")
    return None
```

## 关键改进

### 1. 优先检查驼峰命名
```python
# 优先检查 API 实际返回的驼峰命名
shard_id = data.get('shardingId') or data.get('sharding_id') or data.get('shard_id')
pool_index = data.get('poolIndex') or data.get('pool_index')
```

### 2. 添加调试信息
```python
else:
    # 如果没有找到 shard/pool 信息，打印所有可用的键
    print(f"  ⏳ Address found but shard/pool not yet assigned...")
    print(f"     Debug: data keys = {list(data.keys())}")
```

这样可以帮助调试，看到 API 实际返回了哪些字段。

### 3. 类型转换
```python
return {
    'shard_id': int(shard_id),      # 确保是整数
    'pool_index': int(pool_index),  # 确保是整数
    'balance': int(data.get('balance', 0)),  # 字符串转整数
    'nonce': int(data.get('nonce', 0))       # 字符串转整数
}
```

## 为什么 API 使用驼峰命名？

### C++ 后端的 JSON 序列化
在 C++ 后端，protobuf 字段通常使用下划线命名：
```protobuf
message AddressInfo {
    uint32 sharding_id = 1;
    uint32 pool_index = 2;
    // ...
}
```

但是在序列化为 JSON 时，很多 protobuf 库会自动转换为驼峰命名：
- `sharding_id` → `shardingId`
- `pool_index` → `poolIndex`
- `latest_height` → `latestHeight`

这是 protobuf JSON 序列化的标准行为。

## 兼容性策略

代码使用链式 `or` 操作符来兼容多种命名方式：

```python
shard_id = data.get('shardingId') or data.get('sharding_id') or data.get('shard_id')
```

这样可以处理：
1. **驼峰命名**（当前 API）：`shardingId`
2. **下划线命名**（可能的旧版本）：`sharding_id`
3. **简短命名**（可能的别名）：`shard_id`

## 测试验证

### 测试命令
```bash
# 启动区块链
./build/shardora --show_cmd -g 1 -n 1 -c 1 -m 1 -s 1 -d 1

# 运行 demo
cd clipy
python3 test_contract_chain_demo.py
```

### 预期输出
```
[Phase 2] User1 deploys ContractA (no shard/pool check)
📋 ContractA (predicted):
   Address: abc123...
   Shard: 2, Pool: 4

✅ ContractA deployed at: abc123...

🔍 Querying ContractA's actual shard/pool from blockchain...
  🔍 Querying address info from blockchain...
  ✅ Address info retrieved! (took 3.2s)
     Shard: 3, Pool: 21
     Balance: 0

✅ ContractA info retrieved from blockchain:
   Actual Shard: 3
   Actual Pool: 21
```

### 如果仍然失败
如果查询仍然失败，会看到调试信息：
```
  ⏳ Address found but shard/pool not yet assigned...
     Debug: data keys = ['balance', 'shardingId', 'poolIndex', 'addr', 'type', 'bytesCode', 'latestHeight', 'txIndex', 'nonce']
```

这样可以清楚地看到 API 返回了哪些字段。

## 其他需要注意的字段

### balance 和 nonce 是字符串
API 返回的 `balance` 和 `nonce` 是字符串格式：
```json
{
  "balance": "0",
  "nonce": "0"
}
```

需要转换为整数：
```python
'balance': int(data.get('balance', 0)),
'nonce': int(data.get('nonce', 0))
```

### bytesCode 是 Base64 编码
如果需要使用合约字节码：
```python
import base64
bytes_code_b64 = data.get('bytesCode', '')
bytes_code = base64.b64decode(bytes_code_b64)
```

### addr 也是 Base64 编码
地址字段也是 Base64 编码：
```python
addr_b64 = data.get('addr', '')
addr_bytes = base64.b64decode(addr_b64)
addr_hex = addr_bytes.hex()
```

## 修改的文件

1. **clipy/test_contract_chain_demo.py**
   - 修复 `query_address_info()` 函数
   - 优先检查驼峰命名
   - 添加调试信息

2. **CONTRACT_CHAIN_QUERY_LOGIC.md**
   - 更新 API 响应格式文档
   - 说明驼峰命名规则

3. **CONTRACT_CHAIN_QUERY_QUICKSTART.md**
   - 更新 API 示例
   - 添加字段名说明

## 总结

这次修复确保了：
1. ✅ 正确解析 API 返回的驼峰命名字段
2. ✅ 兼容多种命名方式（驼峰、下划线）
3. ✅ 添加调试信息帮助排查问题
4. ✅ 正确处理字符串到整数的类型转换
5. ✅ 更新文档反映实际 API 格式

现在 `query_address_info()` 函数可以正确从区块链查询地址的 shard 和 pool 信息了！
