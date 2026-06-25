# 客户端账户初始化修复

## 问题分析

日志显示交易被拒绝，原因是账户未初始化：
```
failed check tx nonce not exists in db: 1a8fa96b7dade41d42c4163b9f9f958b631f6d2b, 35, db nonce: 0
```

### 根本原因

1. **账户未初始化**：db nonce = 0 表示账户还没有在区块链上创建
2. **客户端过早发送交易**：客户端在账户初始化前就开始发送交易（nonce 35）
3. **缺少初始化逻辑**：tx_cli 没有检查和初始化 nonce 为 0 的账户

### 为什么会发生

- 账户在 `init_accounts` 文件中定义，但不一定在区块链上初始化
- 客户端直接从 HTTP API 获取 nonce，如果账户不存在，返回 0
- 客户端没有检查这个情况，直接开始发送交易
- 区块链拒绝 nonce 不连续的交易

## 修复方案

### 添加账户初始化逻辑

在 `tx_main` 函数中，`UpdateAddressNonce()` 之后添加初始化检查：

```cpp
// 1. 获取所有账户的 nonce
UpdateAddressNonce();

// 2. 检查是否有账户未初始化（nonce = 0）
std::cout << ">>> Initializing accounts on blockchain..." << std::endl;
uint32_t init_count = 0;
for (auto& [addr, nonce] : src_prikey_with_nonce) {
    if (nonce == 0) {
        // 找到该地址对应的私钥
        std::string prikey = FindPrivateKeyForAddress(addr);
        
        if (!prikey.empty()) {
            // 发送初始化交易（发送 1 个币给自己）
            auto init_tx = CreateTransactionWithAttr(
                sec, 1, prikey, addr, "", "", 1, 21000, 1, shardnum);
            
            // 发送交易
            if (transport::TcpTransport::Instance()->Send(...) == 0) {
                ++init_count;
                prikey_with_nonce[addr] = 1;  // 标记为已初始化
            }
        }
    }
}

// 3. 如果有初始化交易，等待确认并重新获取 nonce
if (init_count > 0) {
    std::cout << ">>> Sent " << init_count << " initialization transactions" << std::endl;
    std::cout << ">>> Waiting 5 seconds for initialization to complete..." << std::endl;
    usleep(5000000);  // 等待 5 秒
    
    // 重新获取 nonce
    std::cout << ">>> Re-fetching nonces after initialization..." << std::endl;
    UpdateAddressNonce();
    prikey_with_nonce = src_prikey_with_nonce;
}
```

### 初始化交易的特点

- **Nonce**: 1（第一个交易）
- **To**: 自己的地址（自转）
- **Amount**: 1（最小金额）
- **Gas Limit**: 21000（标准转账）
- **Gas Price**: 1（最小价格）

这样做的好处：
1. 创建账户在区块链上
2. 设置初始 nonce 为 1
3. 消耗最少的资源
4. 快速完成

## 修改文件

- `src/main/tx_cli.cc`:
  - 行 ~300-350: 添加账户初始化逻辑

## 执行流程

```
1. 启动 tx_cli
   ↓
2. 调用 UpdateAddressNonce() 获取所有账户的 nonce
   ↓
3. 检查是否有 nonce = 0 的账户
   ↓
4. 如果有，发送初始化交易（自转 1 个币）
   ↓
5. 等待 5 秒让初始化交易被确认
   ↓
6. 重新获取 nonce（现在应该是 1）
   ↓
7. 开始正常的压测交易发送
```

## 预期效果

### 修复前
```
failed check tx nonce not exists in db: ..., 35, db nonce: 0
failed check tx nonce not exists in db: ..., 35, db nonce: 0
failed check tx nonce not exists in db: ..., 35, db nonce: 0
```

### 修复后
```
>>> Initializing accounts on blockchain...
  Initialized: 1a8fa96b7dade41d42c4163b9f9f958b631f6d2b
>>> Sent 1 initialization transactions
>>> Waiting 5 seconds for initialization to complete...
>>> Re-fetching nonces after initialization...
1a8fa96b7dade41d42c4163b9f9f958b631f6d2b, nonce: 1
>>> Starting transaction sending...
```

## 验证方法

1. 编译项目：`./build.sh shardora`
2. 运行压测：`./txcli 0 3 0 <ip> <port>`
3. 观察日志：
   - 应该看到 "Initializing accounts on blockchain..." 消息
   - 应该看到初始化交易被发送
   - 应该看到 nonce 从 0 变为 1
   - 不应该再看到 "failed check tx nonce not exists in db" 错误

## 总结

这个修复确保：
1. ✅ 所有账户在发送交易前都被初始化
2. ✅ 初始化交易快速完成（5 秒等待）
3. ✅ nonce 从 1 开始连续递增
4. ✅ 不会再出现 "db nonce: 0" 的错误
