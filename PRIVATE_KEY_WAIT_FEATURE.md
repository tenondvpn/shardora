# 私钥等待功能说明

## 功能概述

当本地 `shardora.conf` 配置文件中的 `privatekey` 为空或不存在时，程序将不会立即退出，而是启动 HTTP 服务器并等待通过 `/update_private_key` 接口接收私钥，然后继续完成初始化。

## 实现细节

### 1. 修改的文件

- `src/init/init_utils.h` - 添加新的状态码 `kInitWaitingForPrivateKey`
- `src/init/network_init.h` - 添加私钥等待相关的方法和成员变量
- `src/init/network_init.cc` - 实现私钥等待逻辑

### 2. 工作流程

```
启动程序
    ↓
InitConfigWithArgs() - 加载配置文件
    ↓
InitSecurity() - 尝试加载私钥
    ↓
私钥是否存在？
    ├─ 是 → 继续正常初始化流程
    └─ 否 → 进入私钥等待模式
        ↓
    InitHttpServerForPrivateKeyWait() - 启动最小化HTTP服务器
        ↓
    WaitForPrivateKeyUpdate() - 等待私钥更新
        ↓
    用户通过 POST /update_private_key 发送私钥
        ↓
    UpdatePrivateKey() - 更新私钥并通知等待线程
        ↓
    继续完成剩余的初始化流程
```

### 3. 关键代码变更

#### InitSecurity() 方法
```cpp
int NetworkInit::InitSecurity() {
    std::string prikey;
    if (!conf_.Get("shardora", "prikey", prikey) || prikey.empty()) {
        INIT_WARN("Private key is empty or not found in config, waiting for UpdatePrivateKey...");
        return kInitWaitingForPrivateKey;  // 返回特殊状态码
    }
    // ... 正常的私钥初始化逻辑
}
```

#### Init() 方法
```cpp
int NetworkInit::Init(int argc, char** argv) {
    // ... 前置初始化
    
    int security_init_result = InitSecurity();
    if (security_init_result == kInitWaitingForPrivateKey) {
        // 初始化最小化组件
        // 启动HTTP服务器
        InitHttpServerForPrivateKeyWait();
        
        // 等待私钥更新
        WaitForPrivateKeyUpdate();
        
        // 继续初始化
    }
    // ... 剩余初始化逻辑
}
```

#### UpdatePrivateKey() 方法
```cpp
int NetworkInit::UpdatePrivateKey(const std::string& new_private_key) {
    // 验证并设置新私钥
    // ...
    
    // 通知等待线程
    {
        std::lock_guard<std::mutex> lock(private_key_wait_mutex_);
        private_key_received_ = true;
    }
    private_key_wait_cv_.notify_one();
    
    return kInitSuccess;
}
```

### 4. 新增的成员变量

```cpp
// 私钥等待机制
std::mutex private_key_wait_mutex_;
std::condition_variable private_key_wait_cv_;
std::atomic<bool> private_key_received_{false};
```

## 使用方法

### 场景1：配置文件中没有私钥

1. 编辑 `conf/shardora.conf`，将 `prikey` 设置为空或删除该配置项：
```ini
[shardora]
prikey=
```

2. 启动程序：
```bash
./shardora -c conf/shardora.conf
```

3. 程序会输出类似以下信息：
```
[WARN] Private key is empty or not found in config, waiting for UpdatePrivateKey...
[INFO] HTTP server started, waiting for private key update via /update_private_key endpoint...
[INFO] Please send POST request to http://0.0.0.0:8080/update_private_key with private_key parameter
[INFO] Waiting for private key update...
```

4. 通过 HTTP 接口发送私钥：
```bash
curl -X POST http://localhost:8080/update_private_key \
  -d "private_key=YOUR_HEX_ENCODED_PRIVATE_KEY"
```

5. 程序接收到私钥后会继续初始化：
```
[INFO] Private key received, resuming initialization...
[INFO] Private key updated successfully! New address: 0x...
```

### 场景2：配置文件中已有私钥

程序会正常启动，不会进入等待模式。

## 安全考虑

1. **HTTP vs HTTPS**: 当前实现使用 HTTP 接口接收私钥。在生产环境中，建议：
   - 使用 HTTPS 加密传输
   - 添加身份验证机制
   - 限制访问 IP 白名单

2. **私钥持久化**: 接收到的私钥会被保存到配置文件中，确保配置文件的访问权限设置正确。

3. **日志安全**: 私钥不会被完整记录到日志中，只记录地址信息。

## 测试

### 测试步骤

1. 备份原配置文件：
```bash
cp conf/shardora.conf conf/shardora.conf.bak
```

2. 清空私钥配置：
```bash
sed -i 's/^prikey=.*/prikey=/' conf/shardora.conf
```

3. 启动程序并观察日志

4. 发送私钥更新请求

5. 验证程序继续运行

### 预期结果

- 程序不会因为缺少私钥而退出
- HTTP 服务器成功启动
- 接收私钥后程序继续初始化
- 所有组件正常工作

## 故障排查

### 问题1：程序仍然退出
- 检查 HTTP 端口配置是否正确
- 查看日志中的错误信息

### 问题2：私钥更新失败
- 验证私钥格式是否正确（十六进制编码）
- 检查私钥长度是否符合要求
- 查看 HTTP 响应中的错误信息

### 问题3：程序一直等待
- 确认 HTTP 服务器已启动
- 检查网络连接
- 验证请求格式是否正确

## 相关文件

- `src/init/init_utils.h` - 状态码定义
- `src/init/network_init.h` - 类声明
- `src/init/network_init.cc` - 实现代码
- `src/init/http_handler.cc` - HTTP 接口处理
- `PRIVATE_KEY_UPDATE_SUMMARY.md` - 私钥更新功能总结
