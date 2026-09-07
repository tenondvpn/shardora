#pragma once

namespace shardora {
namespace explorer {

static const char* kPragmaSQL = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA cache_size   = -65536;
PRAGMA busy_timeout = 5000;
PRAGMA temp_store   = MEMORY;
)SQL";

static const char* kCreateTablesSQL = R"SQL(
CREATE TABLE IF NOT EXISTS sync_cursors (
    shard_id      INTEGER NOT NULL,
    pool_index    INTEGER NOT NULL,
    synced_height INTEGER NOT NULL DEFAULT 0,
    updated_at    INTEGER NOT NULL,
    PRIMARY KEY (shard_id, pool_index)
);

CREATE TABLE IF NOT EXISTS blocks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    shard_id      INTEGER NOT NULL,
    pool_index    INTEGER NOT NULL,
    height        INTEGER NOT NULL,
    hash          TEXT    NOT NULL UNIQUE,
    parent_hash   TEXT    NOT NULL,
    timestamp     INTEGER NOT NULL,
    tx_count      INTEGER NOT NULL DEFAULT 0,
    all_gas       INTEGER NOT NULL DEFAULT 0,
    is_root_shard INTEGER NOT NULL DEFAULT 0,
    elect_height  INTEGER NOT NULL DEFAULT 0,
    leader_idx    INTEGER NOT NULL DEFAULT 0
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_blk_shard_pool_height
    ON blocks(shard_id, pool_index, height);
CREATE INDEX IF NOT EXISTS idx_blk_hash      ON blocks(hash);
CREATE INDEX IF NOT EXISTS idx_blk_timestamp ON blocks(timestamp DESC);

CREATE TABLE IF NOT EXISTS transactions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_hash        TEXT    NOT NULL UNIQUE,
    block_hash     TEXT    NOT NULL,
    shard_id       INTEGER NOT NULL,
    pool_index     INTEGER NOT NULL,
    height         INTEGER NOT NULL,
    timestamp      INTEGER NOT NULL,
    from_addr      TEXT,
    to_addr        TEXT,
    amount         INTEGER NOT NULL DEFAULT 0,
    gas_limit      INTEGER NOT NULL DEFAULT 0,
    gas_used       INTEGER NOT NULL DEFAULT 0,
    gas_price      INTEGER NOT NULL DEFAULT 0,
    step_type      INTEGER NOT NULL DEFAULT 0,
    is_system_tx   INTEGER NOT NULL DEFAULT 0,
    status         INTEGER NOT NULL DEFAULT 0,
    nonce          INTEGER NOT NULL DEFAULT 0,
    contract_input TEXT,
    output         TEXT
);

CREATE INDEX IF NOT EXISTS idx_tx_block_hash ON transactions(block_hash);
CREATE INDEX IF NOT EXISTS idx_tx_from       ON transactions(from_addr, id DESC);
CREATE INDEX IF NOT EXISTS idx_tx_to         ON transactions(to_addr,   id DESC);
CREATE INDEX IF NOT EXISTS idx_tx_timestamp  ON transactions(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_tx_step       ON transactions(step_type);

CREATE TABLE IF NOT EXISTS addresses (
    addr        TEXT PRIMARY KEY,
    addr_type   INTEGER NOT NULL DEFAULT 0,
    shard_id    INTEGER NOT NULL DEFAULT 0,
    pool_index  INTEGER NOT NULL DEFAULT 0,
    is_contract INTEGER NOT NULL DEFAULT 0,
    first_seen  INTEGER NOT NULL DEFAULT 0,
    last_seen   INTEGER NOT NULL DEFAULT 0,
    tx_count    INTEGER NOT NULL DEFAULT 0,
    updated_at  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS contracts (
    addr              TEXT PRIMARY KEY,
    creator_addr      TEXT,
    create_tx_hash    TEXT,
    create_height     INTEGER NOT NULL DEFAULT 0,
    create_timestamp  INTEGER NOT NULL DEFAULT 0,
    shard_id          INTEGER NOT NULL DEFAULT 0,
    pool_index        INTEGER NOT NULL DEFAULT 0,
    code_hash         TEXT,
    is_library        INTEGER NOT NULL DEFAULT 0,
    is_clone          INTEGER NOT NULL DEFAULT 0,
    updated_at        INTEGER NOT NULL DEFAULT 0
);

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

CREATE TABLE IF NOT EXISTS gas_presets (
    id          INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL,
    step_type   INTEGER,
    gas_amount  INTEGER NOT NULL,
    description TEXT
);
)SQL";

static const char* kSeedGasPresetsSQL = R"SQL(
INSERT OR IGNORE INTO gas_presets(id, name, step_type, gas_amount, description) VALUES
(1,  '普通转账',          0,    21000,  'kNormalFrom'),
(2,  '合约创建',          6,    53000,  'kCreateContract'),
(3,  '合约调用',          7,    21000,  'kContractExcute'),
(4,  'Gas预充值',         8,    21000,  'kContractGasPrefund'),
(5,  '加入选举',          11,   21000,  'kJoinElect'),
(6,  '库合约创建',        13,   53000,  'kCreateLibrary'),
(7,  'Calldata非零字节',  NULL, 16,     'EIP-2028 non-zero byte'),
(8,  'Calldata零字节',    NULL, 4,      'EIP-2028 zero byte'),
(9,  'SSTORE新槽',        NULL, 20000,  'EIP-2200 first write'),
(10, 'SSTORE修改槽',      NULL, 2900,   'EIP-2200 update');
)SQL";

}  // namespace explorer
}  // namespace shardora
