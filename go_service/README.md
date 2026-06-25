# Shardora Purchase Service - HTTPS Server with ECDSA Verification

这是一个用 Go 语言实现的 HTTPS 服务，用于验证用户的购买凭证并通过 Shardora 客户端向用户地址转账 100 个币。

## 功能特性

1. **HTTPS 服务**: 使用自签名证书的安全 HTTPS 服务
2. **ECDSA 签名验证**: 验证用户提交的 ECDSA 签名凭证
3. **防重放攻击**: 每个凭证只能使用一次
4. **时间戳验证**: 凭证必须在 5 分钟内有效
5. **自动转账**: 验证通过后自动向用户地址转账 100 个币

## 项目结构

```
go_service/
├── main.go              # 主服务器代码
├── shardora_client.go       # Shardora 区块链客户端
├── client_example.go    # 客户端示例代码
├── README.md           # 本文档
└── go.mod              # Go 模块文件
```

## 安装依赖

```bash
cd go_service
go mod init shardora-purchase-service
go mod tidy
```

## 配置

在 `main.go` 中修改以下配置：

```go
port := 8443                                    // HTTPS 端口
shardoraNodeIP := "127.0.0.1"                      // Shardora 节点 IP
shardoraNodePort := 13001                          // Shardora 节点端口
senderPrivateKey := "your_sender_private_key_hex"  // 发送者私钥（十六进制）
```

## 运行服务器

```bash
go run main.go shardora_client.go
```

服务器将在 `https://localhost:8443` 启动。

## API 端点

### 1. POST /purchase

提交购买凭证并接收转账。

**请求格式**:
```json
{
  "address": "1234567890abcdef1234567890abcdef12345678",
  "timestamp": 1713542400,
  "nonce": "a1b2c3d4e5f6...",
  "signature": "3045022100...",
  "public_key": "04a1b2c3d4..."
}
```

**字段说明**:
- `address`: Shardora 地址（十六进制编码，40 字符）
- `timestamp`: Unix 时间戳（秒）
- `nonce`: 随机数（十六进制编码，防止重放）
- `signature`: ECDSA 签名（十六进制编码，r||s 格式，64 字节）
- `public_key`: 公钥（十六进制编码，未压缩格式，65 字节，以 04 开头）

**响应格式**:
```json
{
  "success": true,
  "message": "Coins transferred successfully",
  "tx_hash": "abcdef123456..."
}
```

### 2. GET /health

健康检查端点。

**响应**: `OK`

## 凭证创建流程

### 1. 生成 ECDSA 密钥对

```go
privKey, _ := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
```

### 2. 构造消息

```go
message := fmt.Sprintf("%s:%d:%s", address, timestamp, nonce)
```

### 3. 签名消息

```go
messageHash := sha256.Sum256([]byte(message))
r, s, _ := ecdsa.Sign(rand.Reader, privKey, messageHash[:])
```

### 4. 编码签名

```go
signature := make([]byte, 64)
copy(signature[0:32], r.Bytes())
copy(signature[32:64], s.Bytes())
signatureHex := hex.EncodeToString(signature)
```

### 5. 编码公钥

```go
pubKeyBytes := make([]byte, 65)
pubKeyBytes[0] = 0x04
copy(pubKeyBytes[1:33], privKey.PublicKey.X.Bytes())
copy(pubKeyBytes[33:65], privKey.PublicKey.Y.Bytes())
publicKeyHex := hex.EncodeToString(pubKeyBytes)
```

## 客户端示例

```go
package main

import (
    "fmt"
)

func main() {
    // 创建客户端
    client, err := NewClientExample("https://localhost:8443")
    if err != nil {
        fmt.Printf("Failed to create client: %v\n", err)
        return
    }

    // Shardora 地址（十六进制）
    shardoraAddress := "1234567890abcdef1234567890abcdef12345678"

    // 创建凭证
    cred, err := client.CreateCredential(shardoraAddress)
    if err != nil {
        fmt.Printf("Failed to create credential: %v\n", err)
        return
    }

    // 提交凭证
    resp, err := client.SubmitCredential(cred)
    if err != nil {
        fmt.Printf("Failed to submit credential: %v\n", err)
        return
    }

    fmt.Printf("Success: %v\n", resp.Success)
    fmt.Printf("Message: %s\n", resp.Message)
    fmt.Printf("TxHash: %s\n", resp.TxHash)
}
```

## 使用 curl 测试

```bash
# 创建测试凭证（需要先生成签名）
curl -k -X POST https://localhost:8443/purchase \
  -H "Content-Type: application/json" \
  -d '{
    "address": "1234567890abcdef1234567890abcdef12345678",
    "timestamp": 1713542400,
    "nonce": "a1b2c3d4e5f67890",
    "signature": "3045022100...",
    "public_key": "04a1b2c3d4..."
  }'
```

## 安全特性

### 1. HTTPS 加密
- 使用 TLS 1.2+ 加密通信
- 自签名证书（生产环境应使用 CA 签发的证书）

### 2. 签名验证
- 使用 ECDSA P-256 曲线
- 验证消息签名的完整性和真实性

### 3. 防重放攻击
- 每个凭证只能使用一次
- 使用凭证哈希跟踪已使用的凭证

### 4. 时间戳验证
- 凭证必须在 ±5 分钟内有效
- 防止过期凭证被使用

### 5. Nonce 机制
- 每个凭证包含唯一的随机数
- 防止相同参数的重放攻击

## 错误处理

服务器会返回以下错误：

1. **"Credential timestamp is invalid or expired"**
   - 时间戳超出 ±5 分钟范围

2. **"Credential has already been used"**
   - 凭证已经被使用过

3. **"Invalid signature"**
   - 签名验证失败

4. **"Failed to transfer coins"**
   - Shardora 转账失败

## Shardora 客户端集成

服务器使用 `ShardoraClient` 与 Shardora 区块链交互：

```go
// 创建客户端
client := NewShardoraClient("127.0.0.1", 13001, "private_key_hex")

// 转账
txHash, err := client.TransferCoins(toAddress, 100)
```

### Shardora API 端点

- **获取账户信息**: `GET http://node:port/api/account?address=xxx`
- **发送交易**: `POST http://node:port/api/transaction`

## 生产环境部署建议

### 1. 使用真实证书
```go
// 使用 Let's Encrypt 或其他 CA 签发的证书
cert, err := tls.LoadX509KeyPair("server.crt", "server.key")
```

### 2. 持久化已使用凭证
```go
// 使用数据库（如 Redis、PostgreSQL）存储已使用的凭证
// 而不是内存 map
```

### 3. 添加速率限制
```go
// 限制每个 IP 的请求频率
// 防止 DDoS 攻击
```

### 4. 日志和监控
```go
// 记录所有交易
// 监控服务器健康状态
// 设置告警
```

### 5. 配置管理
```go
// 使用环境变量或配置文件
// 不要硬编码敏感信息
```

## 测试

### 单元测试
```bash
go test -v
```

### 集成测试
```bash
# 启动服务器
go run main.go shardora_client.go

# 在另一个终端运行客户端
go run client_example.go
```

## 故障排除

### 1. 证书错误
```
x509: certificate signed by unknown authority
```
**解决**: 客户端使用 `InsecureSkipVerify: true` 或安装证书

### 2. 连接被拒绝
```
connection refused
```
**解决**: 检查服务器是否运行，端口是否正确

### 3. 签名验证失败
```
Invalid signature
```
**解决**: 检查消息格式、签名算法、公钥格式

### 4. Shardora 转账失败
```
Failed to transfer coins
```
**解决**: 检查 Shardora 节点连接、账户余额、私钥配置

## 许可证

MIT License

## 作者

Kiro AI Assistant

## 更新日志

### v1.0.0 (2026-04-19)
- 初始版本
- HTTPS 服务器
- ECDSA 签名验证
- Shardora 客户端集成
- 防重放攻击
