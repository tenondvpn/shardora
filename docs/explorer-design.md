# Shardora 区块链浏览器集成方案

> 目标：以 SQLite 替代 ClickHouse，将区块浏览器直接内嵌到节点进程中；前端在 **chainbaas** 项目中新增"Block Explorer"菜单，使用 Vue 3 + Element Plus 实现。

---

## 一、整体集成架构

```
┌─────────────────────────────────────────────────────────────────┐
│   chainbaas（Vue 3 前端）                                        │
│   顶部导航新增 "Block Explorer" 菜单                             │
│   /explorer/* 路由 → ExplorerLayout + 子页面组件                 │
└────────────────────┬────────────────────────────────────────────┘
                     │ HTTP GET /explorer/*（Vite proxy → 节点）
┌────────────────────▼────────────────────────────────────────────┐
│   Shardora 节点进程                                              │
│                                                                  │
│   BlockManager::AddNewBlock()                                    │
│     ├── ck_client_->AddNewBlock(...)   ← 保留（可选）            │
│     └── explorer_->AddNewBlock(...)    ← 新增 SQLite 写入        │
│                                                                  │
│   HttpHandler::Run()                                             │
│     ├── /transaction, /query_account … ← 现有路由不变           │
│     └── /explorer/*                   ← 新增浏览器 API 路由      │
│                                                                  │
│   src/explorer/                        ← 新增 C++ 模块           │
│     ├── explorer.h / explorer.cc       ← 核心类                  │
│     ├── schema.h                       ← SQL 建表常量             │
│     └── query_handlers.cc             ← HTTP 路由处理函数        │
│                                                                  │
│   explorer.db（SQLite WAL 模式）                                 │
└─────────────────────────────────────────────────────────────────┘
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

## 九、前端 — chainbaas Vue 集成

### 9.1 技术栈确认

| 项 | 现有版本 | 说明 |
|---|---|---|
| Vue | 3.5.18 | Composition API + `<script setup>` |
| Element Plus | 2.11.1 | UI 组件库，直接复用 el-table、el-tag、el-descriptions 等 |
| Vue Router | 4.5.1 | 新增嵌套路由 |
| Axios | 1.11.0 | 复用现有 `api/api.ts` 的 `GET` helper |
| Vite | 7.1.2 | 通过 proxy 转发 `/explorer/*` 到节点 |

---

### 9.2 Vite 代理配置

**文件：`chainbaas/vite.config.js`**（在 `server.proxy` 中追加）

```js
export default defineConfig({
  server: {
    proxy: {
      // 已有代理（保留）
      '/rest_token': { target: 'http://127.0.0.1:30301', changeOrigin: true },
      // 新增：浏览器 API 转发到 Shardora 节点
      '/explorer': {
        target: 'http://127.0.0.1:30301',   // 节点 HTTP 端口
        changeOrigin: true,
      },
      // 新增：透传查询账户（余额实时查）
      '/query_account': {
        target: 'http://127.0.0.1:30301',
        changeOrigin: true,
      },
    },
  },
})
```

生产部署时由 Nginx 完成同等转发，无需修改前端代码。

---

### 9.3 API 模块

**新建文件：`chainbaas/api/modules/explorer.ts`**

```ts
import { GET } from '../api'

// 区块
export const getBlocks = (params: {
  shard_id?: number
  pool_index?: number
  is_root?: boolean
  before_id?: number
  limit?: number
}) => GET('/explorer/blocks', params)

export const getBlock = (hash: string) =>
  GET(`/explorer/blocks/${hash}`, {})

// 交易
export const getTransactions = (params: {
  shard_id?: number
  block_hash?: string
  is_system?: boolean
  step_type?: number
  before_id?: number
  limit?: number
}) => GET('/explorer/transactions', params)

export const getTransaction = (txHash: string) =>
  GET(`/explorer/transactions/${txHash}`, {})

// 地址
export const getAddress = (addr: string) =>
  GET(`/explorer/address/${addr}`, {})

export const getAddressTxs = (addr: string, params: {
  before_id?: number
  limit?: number
}) => GET(`/explorer/address/${addr}/txs`, params)

// 合约
export const getContracts = (params: {
  is_library?: boolean
  before_id?: number
  limit?: number
}) => GET('/explorer/contracts', params)

export const getContract = (addr: string) =>
  GET(`/explorer/contracts/${addr}`, {})

// Gas 预置 & 链信息
export const getGasPresets = () => GET('/explorer/gas-presets', {})
export const getChainInfo  = () => GET('/explorer/chain-info', {})
```

---

### 9.4 顶部菜单集成

**修改文件：`chainbaas/src/App.vue`**

在现有 `<el-menu>` 中，于 `Faucet` 菜单项之前插入：

```html
<el-menu-item index="7" @click="toExplorer">Block Explorer</el-menu-item>
```

对应 `<script setup>` 中新增：

```js
const toExplorer = () => router.push('/explorer')
```

菜单高亮通过现有 mitt 事件总线 `change_el_menu_item` 自动同步（路由守卫已有此逻辑，无需额外处理）。

---

### 9.5 路由配置

**修改文件：`chainbaas/src/router/index.js`**

```js
import ExplorerLayout   from '@/components/Explorer/ExplorerLayout.vue'
import ExplorerOverview from '@/components/Explorer/ExplorerOverview.vue'
import BlockList        from '@/components/Explorer/BlockList.vue'
import BlockDetail      from '@/components/Explorer/BlockDetail.vue'
import TxList           from '@/components/Explorer/TxList.vue'
import TxDetail         from '@/components/Explorer/TxDetail.vue'
import AddressDetail    from '@/components/Explorer/AddressDetail.vue'
import ContractList     from '@/components/Explorer/ContractList.vue'
import ContractDetail   from '@/components/Explorer/ContractDetail.vue'
import GasPresets       from '@/components/Explorer/GasPresets.vue'

// 追加到 routes 数组
{
  path: '/explorer',
  component: ExplorerLayout,          // 含侧边栏 + <router-view>
  redirect: '/explorer/overview',
  children: [
    { path: 'overview',              component: ExplorerOverview },
    { path: 'blocks',                component: BlockList },
    { path: 'blocks/:hash',          component: BlockDetail },
    { path: 'transactions',          component: TxList },
    { path: 'transactions/:txHash',  component: TxDetail },
    { path: 'address/:addr',         component: AddressDetail },
    { path: 'contracts',             component: ContractList },
    { path: 'contracts/:addr',       component: ContractDetail },
    { path: 'gas-presets',           component: GasPresets },
  ],
},
```

---

### 9.6 组件目录结构

```
chainbaas/src/components/Explorer/
├── ExplorerLayout.vue     # 布局：左侧竖向菜单 + 右侧 <router-view>
├── ExplorerOverview.vue   # 概览 Dashboard（链信息 + 最新区块/交易）
├── BlockList.vue          # 区块列表（分页 + 分片筛选）
├── BlockDetail.vue        # 区块详情
├── TxList.vue             # 交易列表（分页 + 多维过滤）
├── TxDetail.vue           # 交易详情（含 EVM Logs）
├── AddressDetail.vue      # 地址详情 + 交易历史分页
├── ContractList.vue       # 合约列表
├── ContractDetail.vue     # 合约详情
├── GasPresets.vue         # Gas 预置表
└── composables/
    ├── usePagination.js   # 游标分页逻辑复用
    └── useAddrShorten.js  # 地址截断 + 复制工具
```

---

### 9.7 ExplorerLayout.vue

侧边栏竖向导航 + 右侧内容区，整体高度填满顶部菜单以下空间。

```vue
<template>
  <div class="explorer-container">
    <!-- 左侧竖向导航 -->
    <el-menu
      :default-active="$route.path"
      mode="vertical"
      router
      class="explorer-sidebar"
    >
      <el-menu-item index="/explorer/overview">
        <el-icon><DataAnalysis /></el-icon> Overview
      </el-menu-item>
      <el-sub-menu index="chain">
        <template #title>
          <el-icon><Connection /></el-icon> Chain
        </template>
        <el-menu-item index="/explorer/blocks">Blocks</el-menu-item>
        <el-menu-item index="/explorer/transactions">Transactions</el-menu-item>
      </el-sub-menu>
      <el-sub-menu index="network">
        <template #title>
          <el-icon><Share /></el-icon> Network
        </template>
        <el-menu-item index="/explorer/contracts">Contracts</el-menu-item>
        <el-menu-item index="/explorer/gas-presets">Gas Presets</el-menu-item>
      </el-sub-menu>
    </el-menu>

    <!-- 右侧内容 -->
    <div class="explorer-content">
      <router-view />
    </div>
  </div>
</template>

<style scoped>
.explorer-container {
  display: flex;
  height: calc(100vh - 44px);  /* 44px = 顶部菜单高度 */
}
.explorer-sidebar {
  width: 200px;
  flex-shrink: 0;
  border-right: 1px solid var(--el-border-color);
  overflow-y: auto;
}
.explorer-content {
  flex: 1;
  padding: 16px 24px;
  overflow-y: auto;
}
</style>
```

---

### 9.8 各页面组件设计

#### ExplorerOverview.vue — 链概览 Dashboard

```
┌────────────────────────────────────────────────────┐
│  统计卡片行（el-row + el-col）                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │总区块数  │ │总交易数  │ │分片数量  │            │
│  └──────────┘ └──────────┘ └──────────┘            │
├────────────────────────────────────────────────────┤
│  最新区块（el-table，10条，自动刷新 5s）            │
│  高度 | 分片 | Pool | Hash | 时间 | 交易数          │
├────────────────────────────────────────────────────┤
│  最新交易（el-table，10条）                         │
│  Hash | 类型 | From | To | Amount | 时间            │
└────────────────────────────────────────────────────┘
```

数据来源：`getChainInfo()` + `getBlocks({limit:10})` + `getTransactions({limit:10})`

---

#### BlockList.vue — 区块列表

```
┌──────────────────────────────────────────────────────────┐
│  筛选栏                                                   │
│  [分片类型 ▼ All/Root/Consensus] [Shard ID 输入]          │
├──────────────────────────────────────────────────────────┤
│  el-table                                                  │
│  Height | Shard | Pool | Hash(截断) | 时间 | Gas | Txns   │
│  每行 Hash 可点击 → /explorer/blocks/:hash                │
├──────────────────────────────────────────────────────────┤
│  分页按钮：[← 上一页] [下一页 →]（游标翻页，无页码）      │
└──────────────────────────────────────────────────────────┘
```

**分页逻辑（usePagination.js 封装）**：
```js
// composables/usePagination.js
export function usePagination(fetchFn, defaultParams = {}) {
  const list      = ref([])
  const hasMore   = ref(false)
  const cursorStack = ref([])   // 历史游标栈，支持上一页
  const loading   = ref(false)

  async function load(beforeId = null) {
    loading.value = true
    const res = await fetchFn({ ...defaultParams, before_id: beforeId, limit: 20 })
    const items = res.data.data ?? []
    // 多取了1条用于判断 has_more
    hasMore.value = items.length > 20
    list.value = items.slice(0, 20)
    loading.value = false
    return res.data.next_cursor
  }

  async function nextPage() {
    const lastId = list.value.at(-1)?.id
    cursorStack.value.push(lastId)
    await load(lastId)
  }

  async function prevPage() {
    cursorStack.value.pop()
    const cursor = cursorStack.value.at(-1) ?? null
    await load(cursor)
  }

  onMounted(() => load())
  return { list, hasMore, loading, nextPage, prevPage,
           hasPrev: computed(() => cursorStack.value.length > 0) }
}
```

---

#### BlockDetail.vue — 区块详情

```
┌─────────────────────────────────────────────────────┐
│  面包屑：Blocks > 0xabc...def                        │
├─────────────────────────────────────────────────────┤
│  el-descriptions（两列）                             │
│  Hash        │ 0xabc...（可复制）                    │
│  Height      │ 10230                                 │
│  分片类型    │ [Root] / [Consensus] el-tag            │
│  Shard ID    │ 2                                     │
│  Pool Index  │ 5                                     │
│  Parent Hash │ 0xdef...（可点击跳转）                 │
│  时间        │ 2024-09-07 12:00:00                   │
│  交易数      │ 12                                    │
│  Gas Used    │ 252,000                               │
│  选举高度    │ 100（Root 分片时显示）                 │
├─────────────────────────────────────────────────────┤
│  交易列表（el-table）                                │
│  [显示系统交易 el-switch]                            │
│  Hash | 类型 el-tag | From | To | Amount | Status   │
└─────────────────────────────────────────────────────┘
```

`is_root_shard=1` 时顶部显示橙色 `<el-tag type="warning">Root Shard</el-tag>`；  
`is_system_tx=1` 的交易行显示灰色 `<el-tag type="info">System</el-tag>`。

---

#### TxList.vue — 交易列表

```
┌──────────────────────────────────────────────────────────────┐
│  筛选栏                                                       │
│  [系统交易 el-switch] [StepType ▼] [Shard ID]                │
├──────────────────────────────────────────────────────────────┤
│  el-table                                                      │
│  Hash | 类型 | Shard | From | To | Amount | Gas | 时间        │
│  类型列使用 el-tag，颜色按 step_type 区分：                   │
│    kNormalFrom → primary                                       │
│    kCreateContract → success                                   │
│    kContractExcute → warning                                   │
│    System tx → info（灰色）                                    │
├──────────────────────────────────────────────────────────────┤
│  [← 上一页] [下一页 →]                                        │
└──────────────────────────────────────────────────────────────┘
```

StepType 下拉选项：

| 标签 | step_type |
|---|---|
| All | — |
| Transfer | 0 |
| Create Contract | 6 |
| Call Contract | 7 |
| Join Elect | 11 |
| System Only | is_system=true |

---

#### TxDetail.vue — 交易详情

```
┌──────────────────────────────────────────────────────┐
│  面包屑：Transactions > 0xabc...                      │
├──────────────────────────────────────────────────────┤
│  el-descriptions（两列）                              │
│  TX Hash     │ 0xabc（可复制）                        │
│  类型        │ [Create Contract] el-tag               │
│  状态        │ [Success] / [Failed] el-tag            │
│  Block       │ 10230（可点击跳转区块详情）             │
│  Shard/Pool  │ 3 / 5                                 │
│  时间        │ 2024-09-07 12:00:00                   │
│  From        │ 0x...（可点击跳转地址详情）             │
│  To          │ 0x...                                 │
│  Amount      │ 1,000,000                             │
│  Gas Limit   │ 53,000                                │
│  Gas Used    │ 50,000                                │
│  Nonce       │ 42                                    │
├──────────────────────────────────────────────────────┤
│  Input Data（contract_input 非空时显示）              │
│  el-input type=textarea readonly，hex 格式            │
├──────────────────────────────────────────────────────┤
│  EVM Logs（events 非空时显示）                        │
│  el-collapse，每条 log 展示 contract/topics/data      │
└──────────────────────────────────────────────────────┘
```

---

#### AddressDetail.vue — 地址详情

```
┌──────────────────────────────────────────────────────┐
│  搜索栏：输入地址 → 跳转（顶部提供全局搜索入口）      │
├──────────────────────────────────────────────────────┤
│  el-descriptions                                      │
│  地址       │ 0x...（可复制）                         │
│  类型       │ [Normal] / [Contract] / [Library] tag  │
│  余额       │ 实时查节点 /query_account               │
│  Nonce      │ 实时查节点                              │
│  归属分片   │ Shard 3 / Pool 5                        │
│  首次出现   │ 2024-09-01                              │
│  交易总数   │ 127（来自 SQLite）                      │
├──────────────────────────────────────────────────────┤
│  交易历史（el-table + 游标分页）                      │
│  Hash | 类型 | 方向（IN/OUT） | Amount | 时间         │
│  方向用 el-tag: IN=success，OUT=danger                │
└──────────────────────────────────────────────────────┘
```

`is_contract=1` 时页面底部追加"合约详情"跳转按钮。

---

#### ContractList.vue — 合约列表

```
┌───────────────────────────────────────────────────────┐
│  筛选：[库合约 el-switch] [克隆合约 el-switch]         │
├───────────────────────────────────────────────────────┤
│  el-table                                              │
│  地址 | 类型 | 创建者 | 创建区块 | 分片 | 时间         │
│  类型列：[Library] / [Clone] / [Contract] el-tag       │
├───────────────────────────────────────────────────────┤
│  [← 上一页] [下一页 →]                                 │
└───────────────────────────────────────────────────────┘
```

---

#### ContractDetail.vue — 合约详情

```
┌──────────────────────────────────────────────────────┐
│  el-descriptions                                      │
│  合约地址   │ 0x...                                   │
│  类型       │ [Library] / [Clone] / [Contract]        │
│  创建者     │ 0x...（可点击）                          │
│  创建交易   │ 0x...（可点击）                          │
│  创建区块   │ 10230（可点击）                          │
│  归属分片   │ Shard 3 / Pool 5                        │
├──────────────────────────────────────────────────────┤
│  合约字节码（实时查节点 eth_getCode）                 │
│  el-input type=textarea readonly，hex 格式            │
├──────────────────────────────────────────────────────┤
│  相关交易（el-table + 游标分页）                      │
│  以 to_addr = 合约地址 过滤                           │
└──────────────────────────────────────────────────────┘
```

---

#### GasPresets.vue — Gas 预置

```
┌───────────────────────────────────────────────────┐
│  el-table（全量，无分页）                          │
│  交易类型 | StepType | Gas 用量 | 说明             │
│  通用规则（calldata/SSTORE 等）独立成一组          │
└───────────────────────────────────────────────────┘
```

数据来源：`getGasPresets()` → 静态，无需刷新。

---

### 9.9 全局搜索

在 `ExplorerLayout.vue` 顶部放一个搜索框，输入内容自动识别类型后跳转：

```js
function onSearch(val) {
  const v = val.trim()
  if (v.length === 66)      router.push(`/explorer/blocks/${v}`)       // 区块 hash (0x+64)
  else if (v.length === 64) router.push(`/explorer/transactions/${v}`) // tx hash
  else if (v.length === 42) router.push(`/explorer/address/${v}`)      // 地址 (0x+40)
  else ElMessage.warning('请输入有效的 Hash 或地址')
}
```

---

### 9.10 StepType 标签与颜色映射

`composables/useStepType.js` 提供统一的枚举描述，供各列表页复用：

```js
export const STEP_TYPE_MAP = {
  0:  { label: 'Transfer',       type: 'primary',   isSystem: false },
  1:  { label: 'NormalTo',       type: 'info',      isSystem: true  },
  2:  { label: 'Elect',          type: 'warning',   isSystem: true  },
  3:  { label: 'TimeBlock',      type: 'info',      isSystem: true  },
  4:  { label: 'Genesis',        type: 'info',      isSystem: true  },
  5:  { label: 'LocalTos',       type: 'info',      isSystem: true  },
  6:  { label: 'CreateContract', type: 'success',   isSystem: false },
  7:  { label: 'CallContract',   type: 'warning',   isSystem: false },
  8:  { label: 'GasPrefund',     type: '',          isSystem: false },
  9:  { label: 'CreateAddr',     type: 'info',      isSystem: true  },
  10: { label: 'Refund',         type: '',          isSystem: false },
  11: { label: 'JoinElect',      type: 'primary',   isSystem: false },
  12: { label: 'Statistic',      type: 'info',      isSystem: true  },
  13: { label: 'Library',        type: 'success',   isSystem: false },
  15: { label: 'Cross',          type: 'info',      isSystem: true  },
  16: { label: 'RootCross',      type: 'info',      isSystem: true  },
  17: { label: 'PoolStat',       type: 'info',      isSystem: true  },
  19: { label: 'CloneDeploy',    type: 'warning',   isSystem: true  },
}
```

---

## 十、文件变更汇总

### 10.1 后端（Shardora C++ 节点）

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

### 10.2 前端（chainbaas Vue 项目）

| 文件 | 变更类型 | 变更内容 |
|---|---|---|
| `vite.config.js` | 修改 | 新增 `/explorer`、`/query_account` proxy |
| `src/App.vue` | 修改 | 顶部菜单新增 "Block Explorer" 菜单项 |
| `src/router/index.js` | 修改 | 新增 `/explorer` 嵌套路由组 |
| `api/modules/explorer.ts` | 新增 | 所有浏览器 API 请求函数 |
| `src/components/Explorer/ExplorerLayout.vue` | 新增 | 侧边栏布局容器 |
| `src/components/Explorer/ExplorerOverview.vue` | 新增 | 链概览 Dashboard |
| `src/components/Explorer/BlockList.vue` | 新增 | 区块列表 |
| `src/components/Explorer/BlockDetail.vue` | 新增 | 区块详情 |
| `src/components/Explorer/TxList.vue` | 新增 | 交易列表 |
| `src/components/Explorer/TxDetail.vue` | 新增 | 交易详情 |
| `src/components/Explorer/AddressDetail.vue` | 新增 | 地址详情 + 历史交易 |
| `src/components/Explorer/ContractList.vue` | 新增 | 合约列表 |
| `src/components/Explorer/ContractDetail.vue` | 新增 | 合约详情 |
| `src/components/Explorer/GasPresets.vue` | 新增 | Gas 预置表 |
| `src/components/Explorer/composables/usePagination.js` | 新增 | 游标分页逻辑复用 |
| `src/components/Explorer/composables/useStepType.js` | 新增 | StepType 枚举映射 |
| `src/components/Explorer/composables/useAddrShorten.js` | 新增 | 地址截断 + 复制工具 |
