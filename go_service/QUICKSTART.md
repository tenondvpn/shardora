# Quick Start Guide

## 快速开始

### 1. 安装 Go

确保已安装 Go 1.20 或更高版本：

```bash
go version
```

### 2. 配置服务器

编辑 `main.go`，修改配置：

```go
port := 8443
shardoraNodeIP := "127.0.0.1"
shardoraNodePort := 13001
senderPrivateKey := "your_sender_private_key_hex"  // ← 替换为实际私钥
```

### 3. 启动服务器

```bash
cd go_service
go run main.go shardora_client.go
```

输出：
```
Generated self-signed certificate
Starting HTTPS server on port 8443...
Shardora node: 127.0.0.1:13001
Endpoint: https://localhost:8443/purchase
```

### 4. 测试服务器

在另一个终端运行测试客户端：

```bash
go run test_client.go
```

### 5. 查看结果

测试客户端会：
1. 生成 ECDSA 密钥对
2. 创建签名凭证
3. 提交凭证到服务器
4. 接收转账（100 币）
5. 尝试重复使用凭证（应该失败）

## 使用 curl 测试

```bash
# 健康检查
curl -k https://localhost:8443/health

# 提交凭证（需要先生成有效的签名）
curl -k -X POST https://localhost:8443/purchase \
  -H "Content-Type: application/json" \
  -d @credential.json
```

## 集成到你的应用

```go
package main

import (
    "fmt"
)

func main() {
    // 1. 创建客户端
    client, err := NewClientExample("https://your-server:8443")
    if err != nil {
        panic(err)
    }

    // 2. 创建凭证
    shardoraAddress := "your_shardora_address_hex"
    cred, err := client.CreateCredential(shardoraAddress)
    if err != nil {
        panic(err)
    }

    // 3. 提交凭证
    resp, err := client.SubmitCredential(cred)
    if err != nil {
        panic(err)
    }

    // 4. 检查结果
    if resp.Success {
        fmt.Printf("Success! TxHash: %s\n", resp.TxHash)
    } else {
        fmt.Printf("Failed: %s\n", resp.Message)
    }
}
```

## 常见问题

### Q: 如何获取 Shardora 地址？

A: Shardora 地址是 20 字节（40 个十六进制字符）。可以从 Shardora 客户端获取。

### Q: 如何生成发送者私钥？

A: 使用 Shardora 客户端生成账户，或使用现有账户的私钥。

### Q: 证书错误怎么办？

A: 测试时使用 `-k` 参数（curl）或 `InsecureSkipVerify: true`（Go）。生产环境使用真实证书。

### Q: 如何查看服务器日志？

A: 服务器会输出所有处理的凭证信息到标准输出。

## 下一步

- 阅读 [README.md](README.md) 了解详细文档
- 阅读 [GO_SERVICE_IMPLEMENTATION.md](GO_SERVICE_IMPLEMENTATION.md) 了解实现细节
- 修改代码以适应你的需求
- 部署到生产环境

## 支持

如有问题，请查看：
- README.md - 完整文档
- GO_SERVICE_IMPLEMENTATION.md - 实现细节
- test_client.go - 示例代码
