# Shardora 区块链浏览器集成方案

> 目标：以 SQLite 替代 ClickHouse，将区块浏览器直接内嵌到节点进程中，复用现有 BlockManager 和 HttpHandler 基础设施，不引入独立外部服务。

---

## 一、整体集成架构

```
Shardora 节点进程
│
├── BlockManager::AddNewBlock()
│       │
│       ├── ck_client_->AddNewBlock(...)   ← 保留（可选）
│       └── explorer_->AddNewBlock(...)    ← 新增 SQLite 写入
│
├── HttpHandler::Run()
│       ├── /transaction, /query_account … ← 现有路由不变
│       └── /explorer/*                   ← 新增浏览器 API 路由
│
└── src/explorer/                          ← 新模块
        ├── explorer.h / explorer.cc       ← 核心类（异步写入 + 查询）
        ├── schema.h                       ← SQL 建表语句常量
        ├── query_handlers.h / .cc         ← HTTP 路由处理函数
        └── CMakeLists.txt
```

**集成原则：**
- 镜像 `ck_client_` 的接入模式，在同一位置（`block_manager.cc:551`）调用
- `explorer_` 为 `nullptr` 时自动跳过，功能开关由配置项 `for_explorer` 控制
- 异步批量写入，不阻塞共识主流程
- SQLite 文件与 RocksDB 数据目录并列存放

---

## 二、新增模块：`src/explorer/`

### 2.1 目录结构

```
src/explorer/
├── CMakeLists.txt
├── explorer.h
├── explorer.cc
├── schema.h          // 所有 CREATE TABLE / CREATE INDEX SQL 字符串常量
└── query_handlers.cc // HttpHandler 使用的静态函数
```

### 2.2 核心类接口 `explorer.h`

```cpp
namespace explorer {

class Explorer {
public:
    // db_path: SQLite 文件路径，如 "data/explorer.db"
    explicit Explorer(const std::string& db_path,
                      std::shared_ptr<contract::ContractManager> contract_mgr);
    ~Explorer();

    // 初始化：建表、建索引、启动后台写入线程
    bool Init();

    // 供 BlockManager::AddNewBlock() 调用，线程安全
    void AddNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& view_block);

    // 供 HttpHandler 路由函数调用（同步读，使用独立读连接）
    std::string QueryBlocks(uint32_t shard_id, int pool_index,
                            int64_t before_id, int limit);
    std::string QueryBlock(const std::string& hash);
    std::string QueryTransactions(uint32_t shard_id, bool include_system,
                                  int step_type_filter,
                                  int64_t before_id, int limit);
    std::string QueryTransaction(const std::string& tx_hash);
    std::string QueryAddress(const std::string& addr,
                             int64_t before_id, int limit);
    std::string QueryContracts(int64_t before_id, int limit);
    std::string QueryGasPresets();
    std::string QueryChainInfo();

private:
    std::string db_path_;
    sqlite3* write_db_;     // 写连接（后台线程独占）
    sqlite3* read_db_;      // 读连接（API 线程共享，WAL 模式允许并发读）

    common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlock>> queue_;
    std::thread flush_thread_;
    std::atomic<bool> running_{false};

    void FlushLoop();       // 后台线程：消费队列 → 批量写 SQLite
    void WriteBlock(const std::shared_ptr<hotstuff::ViewBlock>& vb);
    bool IsSystemTx(int32_t step_type);
};

}  // namespace explorer
```

---

## 三、SQLite Schema

### 3.1 PRAGMA 配置（Init 时执行）

```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA cache_size   = -65536;   -- 64 MB
PRAGMA busy_timeout = 5000;
PRAGMA temp_store   = MEMORY;
```

### 3.2 建表语句

```sql
-- 同步进度（每个 shard × pool 独立游标，断点续同步）
CREATE TABLE IF NOT EXISTS sync_cursors (
    shard_id      INTEGER NOT NULL,
    pool_index    INTEGER NOT NULL,
    synced_height INTEGER NOT NULL DEFAULT 0,
    updated_at    INTEGER NOT NULL,
    PRIMARY KEY (shard_id, pool_index)
);

-- 区块表
CREATE TABLE IF NOT EXISTS blocks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    shard_id      INTEGER NOT NULL,
    pool_index    INTEGER NOT NULL,
    height        INTEGER NOT NULL,
    hash          TEXT    NOT NULL UNIQUE,
    parent_hash   TEXT    NOT NULL,
    timestamp     INTEGER NOT NULL,   -- Unix 毫秒
    tx_count      INTEGER NOT NULL DEFAULT 0,
    all_gas       INTEGER NOT NULL DEFAULT 0,
    is_root_shard INTEGER NOT NULL DEFAULT 0,  -- shard_id=2 置1
    elect_height  INTEGER,
    leader_idx    INTEGER
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_blk_shard_pool_height
    ON blocks(shard_id, pool_index, height);
CREATE INDEX IF NOT EXISTS idx_blk_hash      ON blocks(hash);
CREATE INDEX IF NOT EXISTS idx_blk_timestamp ON blocks(timestamp DESC);

-- 交易表
CREATE TABLE IF NOT EXISTS transactions (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_hash      TEXT    NOT NULL UNIQUE,
    block_hash   TEXT    NOT NULL,
    shard_id     INTEGER NOT NULL,
    pool_index   INTEGER NOT NULL,
    height       INTEGER NOT NULL,
    timestamp    INTEGER NOT NULL,
    from_addr    TEXT,
    to_addr      TEXT,
    amount       INTEGER NOT NULL DEFAULT 0,
    gas_limit    INTEGER NOT NULL DEFAULT 0,
    gas_used     INTEGER NOT NULL DEFAULT 0,
    gas_price    INTEGER NOT NULL DEFAULT 0,
    step_type    INTEGER NOT NULL,
    is_system_tx INTEGER NOT NULL DEFAULT 0,
    status       INTEGER NOT NULL DEFAULT 0,
    nonce        INTEGER,
    contract_input TEXT,
    output         TEXT
);
CREATE INDEX IF NOT EXISTS idx_tx_block_hash  ON transactions(block_hash);
CREATE INDEX IF NOT EXISTS idx_tx_from        ON transactions(from_addr, id DESC);
CREATE INDEX IF NOT EXISTS idx_tx_to          ON transactions(to_addr,   id DESC);
CREATE INDEX IF NOT EXISTS idx_tx_timestamp   ON transactions(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_tx_step        ON transactions(step_type);

-- 地址表（写入时 UPSERT，余额不在此存储，实时查节点）
CREATE TABLE IF NOT EXISTS addresses (
    addr        TEXT PRIMARY KEY,
    addr_type   INTEGER NOT NULL DEFAULT 0,
    shard_id    INTEGER,
    pool_index  INTEGER,
    is_contract INTEGER NOT NULL DEFAULT 0,
    first_seen  INTEGER,
    last_seen   INTEGER,
    tx_count    INTEGER NOT NULL DEFAULT 0,
    updated_at  INTEGER NOT NULL
);

-- 合约表（从 kCreateContract 交易中提取）
CREATE TABLE IF NOT EXISTS contracts (
    addr              TEXT PRIMARY KEY,
    creator_addr      TEXT,
    create_tx_hash    TEXT,
    create_height     INTEGER,
    create_timestamp  INTEGER,
    shard_id          INTEGER,
    pool_index        INTEGER,
    code_hash         TEXT,
    is_library        INTEGER NOT NULL DEFAULT 0,
    is_clone          INTEGER NOT NULL DEFAULT 0,
    updated_at        INTEGER NOT NULL
);

-- EVM 事件日志（来自 BlockTx.events）
CREATE TABLE IF NOT EXISTS tx_logs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_hash       TEXT NOT NULL,
    log_index     INTEGER NOT NULL,
    contract_addr TEXT,
    topic0        TEXT,
    topic1        TEXT,
    topic2        TEXT,
    topic3        TEXT,
    data          TEXT
);
CREATE INDEX IF NOT EXISTS idx_log_tx       ON tx_logs(tx_hash);
CREATE INDEX IF NOT EXISTS idx_log_contract ON tx_logs(contract_addr, topic0);

-- Gas 预置（静态数据，Init 时 INSERT OR IGNORE 种入）
CREATE TABLE IF NOT EXISTS gas_presets (
    id          INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL,
    step_type   INTEGER,      -- 对应 StepType 枚举，NULL 表示通用
    gas_amount  INTEGER NOT NULL,
    description TEXT
);
```

### 3.3 Gas 预置种子数据

对应 `src/consensus/consensus_utils.h` 中的常量：

| name | step_type | gas_amount | 对应常量 |
|---|---|---|---|
| 普通转账 | 0 (kNormalFrom) | 21000 | kTransferGas |
| 合约创建 | 6 (kCreateContract) | 53000 | kCreateContractDefaultUseGas |
| 合约调用 | 7 (kContractExcute) | 21000 | kCallContractDefaultUseGas |
| Gas 预充 | 8 (kContractGasPrefund) | 21000 | — |
| 加入选举 | 11 (kJoinElect) | 21000 | kJoinElectGas |
| 库合约创建 | 13 (kCreateLibrary) | 53000 | kCreateLibraryDefaultUseGas |
| Calldata 非零字节 | NULL | 16 | kCalldataNonZeroByteGas |
| Calldata 零字节 | NULL | 4 | kCalldataZeroByteGas |
| SSTORE 新槽 | NULL | 20000 | kSstoreNewSlotGas |
| SSTORE 修改槽 | NULL | 2900 | kSstoreDirtySlotGas |

---

## 四、数据写入流程

### 4.1 系统交易判断

对应 `src/pools/tx_utils.h::IsUserTransaction()`，以下 step_type 为**用户交易**，其余均置 `is_system_tx=1`：

```
0  kNormalFrom
6  kCreateContract
7  kContractExcute
8  kContractGasPrefund
10 kContractRefund
11 kJoinElect
13 kCreateLibrary
```

### 4.2 AddNewBlock 数据提取

```
ViewBlockItem
├── block_info.Block
│   ├── height, timestamp, all_gas, chain_id(=shard_id)
│   ├── qc.{elect_height, leader_idx, pool_index}
│   ├── tx_list[]  → transactions 表
│   │   ├── from, to, amount, gas_*, step, status, nonce
│   │   ├── contract_code (step=kCreateContract → contracts 表)
│   │   └── events[] → tx_logs 表
│   └── address_array[] → addresses 表（新账户）
└── parent_hash → blocks.parent_hash
```

### 4.3 后台写入线程

```
FlushLoop():
  while running_:
    批量取队列（最多 200 个 ViewBlock）
    若队列为空: sleep 100ms, continue
    BEGIN TRANSACTION
      for each ViewBlock:
        INSERT INTO blocks
        for each BlockTx:
          INSERT INTO transactions (OR IGNORE on UNIQUE conflict)
          UPSERT addresses (from_addr / to_addr)
          if step=kCreateContract: INSERT OR IGNORE contracts
          for each event: INSERT tx_logs
    UPDATE sync_cursors
    COMMIT
```

单事务批量提交，实测比逐条提交快约 100 倍。`OR IGNORE` 保证重放安全（节点重启后重扫已有块不会报错）。

---

## 五、HTTP API 路由

在 `HttpHandler::Run()` 的 uWS 路由链中，于 `.listen()` 之前追加以下路由：

### 5.1 路由清单

| Method | Path | 功能 |
|---|---|---|
| GET | `/explorer/blocks` | 区块列表（分页） |
| GET | `/explorer/blocks/:hash` | 区块详情 |
| GET | `/explorer/transactions` | 交易列表（分页） |
| GET | `/explorer/transactions/:tx_hash` | 交易详情 |
| GET | `/explorer/address/:addr` | 地址概览（余额透传节点） |
| GET | `/explorer/address/:addr/txs` | 地址交易历史（分页） |
| GET | `/explorer/contracts` | 合约列表（分页） |
| GET | `/explorer/contracts/:addr` | 合约详情（字节码透传节点） |
| GET | `/explorer/gas-presets` | Gas 预置表（全量，无分页） |
| GET | `/explorer/chain-info` | 链概览（分片数、最新高度、同步进度） |

### 5.2 通用分页参数

所有列表接口统一使用 **Keyset 游标分页**（不使用 OFFSET）：

| 参数 | 类型 | 说明 |
|---|---|---|
| `before_id` | int64 | 上一页最后一条的 `id`（自增主键），首页不传 |
| `limit` | int | 每页条数，默认 20，最大 100 |

```sql
-- 示例：区块列表（向前翻页）
SELECT * FROM blocks
WHERE id < :before_id       -- 无 before_id 时去掉此条件
  AND shard_id = :shard_id  -- 可选过滤
ORDER BY id DESC
LIMIT :limit + 1;           -- 多取1条判断是否有下一页
```

响应固定包含 `next_cursor`（下一页的 before_id）和 `has_more` 字段。

### 5.3 各路由关键参数

**`GET /explorer/blocks`**
```
shard_id=2          可选，不传返回全部分片
pool_index=0        可选
is_root=true        可选，只看 Root 分片（shard_id=2）
before_id=&limit=
```

**`GET /explorer/transactions`**
```
shard_id=3
pool_index=0
block_hash=0xabc    可选，某区块内的交易
is_system=false     false=只看用户交易，true=只看系统交易，不传=全部
step_type=6         可选，按 StepType 精确过滤
before_id=&limit=
```

**`GET /explorer/address/:addr/txs`**
```
before_id=&limit=
-- 内部执行：WHERE from_addr=? OR to_addr=?
```

**`GET /explorer/contracts`**
```
is_library=false    过滤库合约
is_clone=false      过滤克隆合约
before_id=&limit=
```

### 5.4 统一响应格式

```json
{
  "code": 0,
  "data": [ ... ],
  "next_cursor": 12300,
  "has_more": true,
  "total": null
}
```

`total` 不做 COUNT(*) 查询（大表下性能差），保持 `null`。

---

## 六、与现有代码的集成变更

### 6.1 `src/block/block_manager.h`

```cpp
// 新增成员
#include "explorer/explorer.h"

class BlockManager {
    // 已有
    std::shared_ptr<ck::ClickHouseClient> ck_client_;
    // 新增
    std::shared_ptr<explorer::Explorer> explorer_;
public:
    // 构造函数增加一个参数
    BlockManager(transport::MultiThreadHandler& net_handler,
                 std::shared_ptr<ck::ClickHouseClient> ck_client,
                 std::shared_ptr<explorer::Explorer> explorer);  // ← 新增
};
```

### 6.2 `src/block/block_manager.cc`

```cpp
// AddNewBlock() 中，紧跟 ck_client_ 调用之后
if (ck_client_) {
    ck_client_->AddNewBlock(view_block_item);
}
if (explorer_) {                            // ← 新增 4 行
    explorer_->AddNewBlock(view_block_item);
}
```

### 6.3 `src/init/network_init.cc`

```cpp
// 在构造 ck_client 的同一区域（:224 附近）
std::shared_ptr<explorer::Explorer> explorer_client = nullptr;
if (common::GlobalInfo::Instance()->for_explorer()) {
    auto db_path = common::GlobalInfo::Instance()->db_path() + "/explorer.db";
    explorer_client = std::make_shared<explorer::Explorer>(db_path, contract_mgr_);
    explorer_client->Init();
}

// 传入 BlockManager
block_mgr_ = std::make_shared<block::BlockManager>(
    net_handler_, block_ck_client, explorer_client);  // ← 多传一个参数
```

### 6.4 `src/common/global_info.h / .cc`

新增配置项读取（与 `for_ck` 同级）：

```cpp
bool for_explorer() const { return for_explorer_; }
// Init() 中：
for_explorer_ = conf.Get<bool>("db", "for_explorer", false);
```

配置文件新增：
```ini
[db]
for_explorer = true
```

### 6.5 `src/init/http_handler.cc`

```cpp
// Init() 函数签名增加 explorer 参数
void HttpHandler::Init(..., std::shared_ptr<explorer::Explorer> explorer);

// Run() 中路由链末尾，.listen() 之前追加
.get("/explorer/blocks",              safeHandler(ExplorerBlocks,       "/explorer/blocks"))
.get("/explorer/blocks/:hash",        safeHandler(ExplorerBlock,        "/explorer/blocks/:hash"))
.get("/explorer/transactions",        safeHandler(ExplorerTxList,       "/explorer/transactions"))
.get("/explorer/transactions/:hash",  safeHandler(ExplorerTx,           "/explorer/transactions/:hash"))
.get("/explorer/address/:addr",       safeHandler(ExplorerAddress,      "/explorer/address/:addr"))
.get("/explorer/address/:addr/txs",   safeHandler(ExplorerAddressTxs,   "/explorer/address/:addr/txs"))
.get("/explorer/contracts",           safeHandler(ExplorerContracts,    "/explorer/contracts"))
.get("/explorer/contracts/:addr",     safeHandler(ExplorerContract,     "/explorer/contracts/:addr"))
.get("/explorer/gas-presets",         safeHandler(ExplorerGasPresets,   "/explorer/gas-presets"))
.get("/explorer/chain-info",          safeHandler(ExplorerChainInfo,    "/explorer/chain-info"))
```

---

## 七、构建系统变更

### 7.1 引入 SQLite 单文件合并版

将官方 `sqlite3.h` + `sqlite3.c`（amalgamation）放入：
```
third_party/include/sqlite3.h
third_party/src/sqlite3.c      ← 编译为静态库
```

或通过 `build_third.sh` / `build_third_mac.sh` 追加编译步骤生成 `third_party/lib/libsqlite3.a`。

### 7.2 `src/explorer/CMakeLists.txt`

```cmake
file(GLOB explorer_src *.cc)
add_library(explorer STATIC ${explorer_src})
target_link_libraries(explorer PRIVATE
    common block protos sqlite3
)
```

### 7.3 根 `CMakeLists.txt`

```cmake
add_subdirectory(src/explorer)   # 新增

# 在 LINK_ARGS 或主目标 target_link_libraries 中追加
# explorer sqlite3
```

---

## 八、关键设计决策说明

| 决策 | 理由 |
|---|---|
| 写入与 ck_client_ 并列，不替换 | 零侵入，两者可独立开关 |
| 异步队列 + 批量事务提交 | 不阻塞共识线程；单事务批量比逐条快 ~100x |
| WAL + 独立读写连接 | 读写并发：Indexer 写入时 API 线程仍可读 |
| Keyset 分页（非 OFFSET） | 大数据量下恒定 O(log n) 查询，OFFSET 退化为 O(n) |
| 余额/字节码透传节点实时查 | RocksDB 是权威数据源，避免 SQLite 与节点状态不一致 |
| OR IGNORE 写入幂等 | 节点重启后重放已有区块不报错 |
| is_system_tx 字段预计算 | 前端无需理解 StepType 枚举即可过滤 |
| for_explorer 配置开关 | 非浏览器节点（validator-only）不写 SQLite，零额外开销 |

---

## 九、前端页面清单

前端为纯静态 HTML/JS，可嵌入节点 HTTP 服务作为静态文件托管，或独立部署。

| 页面 | 数据来源 | 核心功能 |
|---|---|---|
| 首页 Dashboard | `/explorer/chain-info` + `/explorer/blocks?limit=10` | 最新区块、交易统计、分片状态 |
| 区块列表 | `/explorer/blocks` | 分片切换（Root/Consensus）、分页 |
| 区块详情 | `/explorer/blocks/:hash` | 块元数据 + 内嵌交易列表（含系统交易开关） |
| 交易列表 | `/explorer/transactions` | 按分片/StepType/系统交易过滤、分页 |
| 交易详情 | `/explorer/transactions/:hash` | 完整字段 + EVM Logs |
| 地址详情 | `/explorer/address/:addr` + 节点实时余额 | 账户类型、余额（实时）、分片归属 |
| 地址交易历史 | `/explorer/address/:addr/txs` | 分页，标注 IN/OUT |
| 合约列表 | `/explorer/contracts` | 区分库合约、克隆合约 |
| 合约详情 | `/explorer/contracts/:addr` + 节点字节码 | 元数据 + 字节码（实时） |
| Gas 预置 | `/explorer/gas-presets` | 各交易类型 Gas 标准展示 |

---

## 十、文件变更汇总

| 文件 | 变更类型 | 变更内容 |
|---|---|---|
| `src/explorer/explorer.h` | 新增 | Explorer 类声明 |
| `src/explorer/explorer.cc` | 新增 | 写入逻辑、查询逻辑、FlushLoop |
| `src/explorer/schema.h` | 新增 | 所有 SQL 常量字符串 |
| `src/explorer/query_handlers.cc` | 新增 | HTTP 路由处理函数 |
| `src/explorer/CMakeLists.txt` | 新增 | 模块构建规则 |
| `src/block/block_manager.h` | 修改 | 增加 `explorer_` 成员 |
| `src/block/block_manager.cc` | 修改 | 构造函数 + AddNewBlock 调用 explorer_ |
| `src/init/network_init.cc` | 修改 | 构造并初始化 Explorer，传入 BlockManager |
| `src/init/http_handler.h` | 修改 | Init() 增加 explorer 参数 |
| `src/init/http_handler.cc` | 修改 | 保存 explorer 引用，注册 /explorer/* 路由 |
| `src/common/global_info.h/cc` | 修改 | 新增 for_explorer 配置项 |
| `CMakeLists.txt` | 修改 | add_subdirectory + link explorer sqlite3 |
| `build_third.sh / build_third_mac.sh` | 修改 | 编译 sqlite3 amalgamation 为静态库 |
| `third_party/include/sqlite3.h` | 新增 | SQLite 头文件 |
| `third_party/src/sqlite3.c` | 新增 | SQLite 单文件实现 |
