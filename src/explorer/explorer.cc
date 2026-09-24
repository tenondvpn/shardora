#include "explorer/explorer.h"
#include "explorer/schema.h"

#include <cctype>
#include <sstream>
#include <set>

#include <nlohmann/json.hpp>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "network/network_utils.h"

namespace shardora {
namespace explorer {

using json = nlohmann::json;

static const std::set<int32_t> kUserStepTypes = {0, 6, 7, 8, 10, 11, 13};

static std::string HexStr(const std::string& bytes) {
    return "0x" + common::Encode::HexEncode(bytes);
}

static std::string JsonOk(const json& data) {
    json resp;
    resp["code"] = 0;
    resp["data"] = data;
    return resp.dump();
}

static std::string JsonList(const json& arr, int64_t next_cursor, bool has_more) {
    json resp;
    resp["code"]        = 0;
    resp["data"]        = arr;
    resp["next_cursor"] = next_cursor;
    resp["has_more"]    = has_more;
    return resp.dump();
}

static std::string JsonErr(const std::string& msg) {
    json resp;
    resp["code"] = -1;
    resp["msg"]  = msg;
    return resp.dump();
}

// ─── SQLite helpers ──────────────────────────────────────────────────────────

bool Explorer::ExecSQL(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        SHARDORA_ERROR("SQLite exec error: %s, SQL: %.200s", errmsg ? errmsg : "unknown", sql);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool Explorer::OpenDB(const std::string& path, sqlite3** db) {
    int rc = sqlite3_open_v2(path.c_str(), db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        SHARDORA_ERROR("SQLite open failed: %s, path: %s",
                       sqlite3_errmsg(*db), path.c_str());
        sqlite3_close(*db);
        *db = nullptr;
        return false;
    }
    return true;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

Explorer::Explorer(const std::string& db_path) : db_path_(db_path) {}

Explorer::~Explorer() {
    running_ = false;
    wait_cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
    if (write_db_) sqlite3_close(write_db_);
    if (read_db_)  sqlite3_close(read_db_);
}

bool Explorer::Init() {
    // Open write DB
    if (!OpenDB(db_path_, &write_db_)) return false;
    // Open separate read DB (WAL mode supports concurrent readers)
    if (!OpenDB(db_path_, &read_db_))  return false;

    // Apply PRAGMAs to write connection
    if (!ExecSQL(write_db_, kPragmaSQL)) return false;
    // Read connection only needs cache and timeout
    ExecSQL(read_db_, "PRAGMA journal_mode = WAL;");
    ExecSQL(read_db_, "PRAGMA cache_size = -32768;");
    ExecSQL(read_db_, "PRAGMA busy_timeout = 5000;");

    // Create tables
    if (!ExecSQL(write_db_, kCreateTablesSQL)) return false;
    // Migrate existing DB: add balance/nonce columns to addresses (ignore errors if already present)
    {
        char* errmsg = nullptr;
        // Split on ';' and exec each ALTER TABLE individually so one failure doesn't block the other
        sqlite3_exec(write_db_, "ALTER TABLE addresses ADD COLUMN balance INTEGER NOT NULL DEFAULT 0;",
                     nullptr, nullptr, &errmsg);
        sqlite3_free(errmsg); errmsg = nullptr;
        sqlite3_exec(write_db_, "ALTER TABLE addresses ADD COLUMN nonce INTEGER NOT NULL DEFAULT 0;",
                     nullptr, nullptr, &errmsg);
        sqlite3_free(errmsg);
    }
    // Migrate contracts table: add bytecode/source_code/abi columns (ignore errors if already present)
    {
        char* errmsg = nullptr;
        sqlite3_exec(write_db_, "ALTER TABLE contracts ADD COLUMN bytecode    TEXT;",
                     nullptr, nullptr, &errmsg);
        sqlite3_free(errmsg); errmsg = nullptr;
        sqlite3_exec(write_db_, "ALTER TABLE contracts ADD COLUMN source_code TEXT;",
                     nullptr, nullptr, &errmsg);
        sqlite3_free(errmsg); errmsg = nullptr;
        sqlite3_exec(write_db_, "ALTER TABLE contracts ADD COLUMN abi         TEXT;",
                     nullptr, nullptr, &errmsg);
        sqlite3_free(errmsg);
    }
    // Seed gas presets
    if (!ExecSQL(write_db_, kSeedGasPresetsSQL)) return false;

    running_ = true;
    flush_thread_ = std::thread([this]() { FlushLoop(); });
    SHARDORA_INFO("Explorer initialized, db: %s", db_path_.c_str());
    return true;
}

// ─── Write path ──────────────────────────────────────────────────────────────

bool Explorer::IsSystemTx(int32_t step_type) {
    return kUserStepTypes.find(step_type) == kUserStepTypes.end();
}

void Explorer::AddNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& view_block) {
    queue_.push(view_block);
    wait_cv_.notify_one();
}

void Explorer::FlushLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::milliseconds(200),
                          [this]() { return queue_.size() != 0 || !running_; });
        lock.unlock();

        if (!running_ && queue_.size() == 0) break;

        // Batch up to 200 blocks per transaction
        std::vector<std::shared_ptr<hotstuff::ViewBlock>> batch;
        batch.reserve(200);
        std::shared_ptr<hotstuff::ViewBlock> vb;
        while (batch.size() < 200 && queue_.pop(&vb)) {
            batch.push_back(std::move(vb));
        }

        if (batch.empty()) continue;

        ExecSQL(write_db_, "BEGIN TRANSACTION;");
        for (auto& b : batch) {
            WriteBlock(write_db_, b);
        }
        ExecSQL(write_db_, "COMMIT;");
    }
}

void Explorer::WriteBlock(sqlite3* db, const std::shared_ptr<hotstuff::ViewBlock>& vb) {
    const auto& block  = vb->block_info();
    uint32_t shard_id  = vb->qc().network_id();
    uint32_t pool_idx  = vb->qc().pool_index();
    uint64_t height    = block.height();
    int64_t  ts        = static_cast<int64_t>(block.timestamp());
    int      is_root   = (shard_id == network::kRootCongressNetworkId) ? 1 : 0;

    std::string hash_hex       = HexStr(vb->qc().view_block_hash());
    std::string parent_hex     = HexStr(vb->parent_hash());
    int         tx_count       = block.tx_list_size();
    int64_t     all_gas        = static_cast<int64_t>(block.all_gas());
    int64_t     elect_height   = static_cast<int64_t>(vb->qc().elect_height());
    int32_t     leader_idx     = static_cast<int32_t>(vb->qc().leader_idx());

    // Insert block (OR IGNORE — idempotent on restart replay)
    {
        const char* sql =
            "INSERT OR IGNORE INTO blocks"
            "(shard_id,pool_index,height,hash,parent_hash,timestamp,"
            " tx_count,all_gas,is_root_shard,elect_height,leader_idx)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int  (stmt, 1,  static_cast<int>(shard_id));
            sqlite3_bind_int  (stmt, 2,  static_cast<int>(pool_idx));
            sqlite3_bind_int64(stmt, 3,  static_cast<int64_t>(height));
            sqlite3_bind_text (stmt, 4,  hash_hex.c_str(),   -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (stmt, 5,  parent_hex.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 6,  ts);
            sqlite3_bind_int  (stmt, 7,  tx_count);
            sqlite3_bind_int64(stmt, 8,  all_gas);
            sqlite3_bind_int  (stmt, 9,  is_root);
            sqlite3_bind_int64(stmt, 10, elect_height);
            sqlite3_bind_int  (stmt, 11, leader_idx);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Insert transactions
    for (int i = 0; i < block.tx_list_size(); ++i) {
        const auto& tx = block.tx_list(i);
        int32_t step      = static_cast<int32_t>(tx.step());
        int     is_sys    = IsSystemTx(step) ? 1 : 0;

        std::string tx_hash_hex  = tx.tx_hash().empty() ? "" : HexStr(tx.tx_hash());
        std::string from_hex     = tx.from().empty()    ? "" : HexStr(tx.from());
        std::string to_hex       = tx.to().empty()      ? "" : HexStr(tx.to());
        std::string input_hex    = tx.contract_input().empty() ? "" : HexStr(tx.contract_input());
        std::string output_hex   = tx.output().empty()         ? "" : HexStr(tx.output());

        if (tx_hash_hex.empty()) continue;  // skip txs without hash

        {
            const char* sql =
                "INSERT OR IGNORE INTO transactions"
                "(tx_hash,block_hash,shard_id,pool_index,height,timestamp,"
                " from_addr,to_addr,amount,gas_limit,gas_used,gas_price,"
                " step_type,is_system_tx,status,nonce,contract_input,output)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text (stmt, 1,  tx_hash_hex.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 2,  hash_hex.c_str(),     -1, SQLITE_TRANSIENT);
                sqlite3_bind_int  (stmt, 3,  static_cast<int>(shard_id));
                sqlite3_bind_int  (stmt, 4,  static_cast<int>(pool_idx));
                sqlite3_bind_int64(stmt, 5,  static_cast<int64_t>(height));
                sqlite3_bind_int64(stmt, 6,  ts);
                sqlite3_bind_text (stmt, 7,  from_hex.c_str(),     -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 8,  to_hex.c_str(),       -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 9,  static_cast<int64_t>(tx.amount()));
                sqlite3_bind_int64(stmt, 10, static_cast<int64_t>(tx.gas_limit()));
                sqlite3_bind_int64(stmt, 11, static_cast<int64_t>(tx.gas_used()));
                sqlite3_bind_int64(stmt, 12, static_cast<int64_t>(tx.gas_price()));
                sqlite3_bind_int  (stmt, 13, step);
                sqlite3_bind_int  (stmt, 14, is_sys);
                sqlite3_bind_int  (stmt, 15, static_cast<int>(tx.status()));
                sqlite3_bind_int64(stmt, 16, static_cast<int64_t>(tx.nonce()));
                sqlite3_bind_text (stmt, 17, input_hex.c_str(),    -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 18, output_hex.c_str(),   -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // Upsert from/to addresses
        int64_t now_ms = static_cast<int64_t>(common::TimeUtils::TimestampMs());
        auto upsert_addr = [&](const std::string& addr_hex, int is_contract_hint) {
            if (addr_hex.empty() || addr_hex == "0x") return;
            const char* sql =
                "INSERT INTO addresses(addr,shard_id,pool_index,is_contract,"
                " first_seen,last_seen,tx_count,updated_at)"
                " VALUES(?,?,?,?,?,?,1,?)"
                " ON CONFLICT(addr) DO UPDATE SET"
                " last_seen=excluded.last_seen,"
                " tx_count=tx_count+1,"
                " updated_at=excluded.updated_at;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text (stmt, 1, addr_hex.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int  (stmt, 2, static_cast<int>(shard_id));
                sqlite3_bind_int  (stmt, 3, static_cast<int>(pool_idx));
                sqlite3_bind_int  (stmt, 4, is_contract_hint);
                sqlite3_bind_int64(stmt, 5, ts);
                sqlite3_bind_int64(stmt, 6, ts);
                sqlite3_bind_int64(stmt, 7, now_ms);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        };

        upsert_addr(from_hex, 0);
        // kCreateContract = 6, kCreateLibrary = 13, kCrossShardDeployClone = 19
        int is_contract_to = (step == 6 || step == 13 || step == 19) ? 1 : 0;
        upsert_addr(to_hex, is_contract_to);

        // Insert contract record for create transactions
        if ((step == 6 || step == 13 || step == 19) && !to_hex.empty()) {
            int is_lib   = (step == 13) ? 1 : 0;
            int is_clone = (step == 19) ? 1 : 0;

            // Extract bytecode hex from contract_code field
            std::string bytecode_hex = tx.contract_code().empty() ? "" : HexStr(tx.contract_code());

            // Try to decode contract_input as source code JSON for step=6
            std::string src_code, abi_json;
            if (step == 6 && !input_hex.empty()) {
                std::string decoded;
                decoded.reserve(input_hex.size() / 2);
                for (size_t k = 0; k + 1 < input_hex.size(); k += 2) {
                    char buf[3] = { input_hex[k], input_hex[k+1], '\0' };
                    decoded += static_cast<char>(static_cast<uint8_t>(strtol(buf, nullptr, 16)));
                }
                if (decoded.find("\"__type\":\"SHRDORA_CONTRACT_SRC\"") != std::string::npos) {
                    // extract "source" string value
                    auto s0 = decoded.find("\"source\":\"");
                    if (s0 != std::string::npos) {
                        s0 += 10;
                        // find closing quote, skipping escaped quotes
                        size_t s1 = s0;
                        while (s1 < decoded.size()) {
                            if (decoded[s1] == '\\') { s1 += 2; continue; }
                            if (decoded[s1] == '"') break;
                            ++s1;
                        }
                        if (s1 < decoded.size()) {
                            src_code = decoded.substr(s0, s1 - s0);
                            // unescape escape sequences in source string
                            for (size_t p = 0; p + 1 < src_code.size(); ++p) {
                                if (src_code[p] == '\\' && src_code[p+1] == 'n') {
                                    src_code.replace(p, 2, "\n"); continue;
                                }
                                if (src_code[p] == '\\' && src_code[p+1] == '\\') {
                                    src_code.replace(p, 2, "\\"); continue;
                                }
                                if (src_code[p] == '\\' && src_code[p+1] == '"') {
                                    src_code.replace(p, 2, "\""); continue;
                                }
                            }
                        }
                    }
                    // extract "abi" string value
                    auto a0 = decoded.find("\"abi\":\"");
                    if (a0 != std::string::npos) {
                        a0 += 7;
                        size_t a1 = a0;
                        while (a1 < decoded.size()) {
                            if (decoded[a1] == '\\') { a1 += 2; continue; }
                            if (decoded[a1] == '"') break;
                            ++a1;
                        }
                        if (a1 < decoded.size()) {
                            abi_json = decoded.substr(a0, a1 - a0);
                            for (size_t p = 0; p + 1 < abi_json.size(); ++p) {
                                if (abi_json[p] == '\\' && abi_json[p+1] == '"') {
                                    abi_json.replace(p, 2, "\""); continue;
                                }
                                if (abi_json[p] == '\\' && abi_json[p+1] == '\\') {
                                    abi_json.replace(p, 2, "\\"); continue;
                                }
                            }
                        }
                    }
                }
            }

            // ON CONFLICT rather than OR IGNORE: the upsert path above can create
            // a row before sync reaches the deploy block, and it does so without a
            // shard. Ignoring the conflict would leave such a row at shard_id 0
            // forever, which no shard query can reach. Repair only the shard so a
            // sync never clobbers source_code/abi that the upsert may have stored.
            const char* sql =
                "INSERT INTO contracts"
                "(addr,creator_addr,create_tx_hash,create_height,create_timestamp,"
                " shard_id,pool_index,is_library,is_clone,bytecode,source_code,abi,updated_at)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"
                " ON CONFLICT(addr) DO UPDATE SET"
                "  shard_id=CASE WHEN contracts.shard_id=0 AND excluded.shard_id!=0"
                "                THEN excluded.shard_id ELSE contracts.shard_id END;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text (stmt, 1,  to_hex.c_str(),         -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 2,  from_hex.c_str(),       -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 3,  tx_hash_hex.c_str(),    -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 4,  static_cast<int64_t>(height));
                sqlite3_bind_int64(stmt, 5,  ts);
                sqlite3_bind_int  (stmt, 6,  static_cast<int>(shard_id));
                sqlite3_bind_int  (stmt, 7,  static_cast<int>(pool_idx));
                sqlite3_bind_int  (stmt, 8,  is_lib);
                sqlite3_bind_int  (stmt, 9,  is_clone);
                sqlite3_bind_text (stmt, 10, bytecode_hex.c_str(),   -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 11, src_code.c_str(),       -1, SQLITE_TRANSIENT);
                sqlite3_bind_text (stmt, 12, abi_json.c_str(),       -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 13, now_ms);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // Insert EVM logs
        for (int li = 0; li < tx.events_size(); ++li) {
            const auto& ev = tx.events(li);
            std::string contract_hex = "";
            std::string t0 = ev.topics_size() > 0 ? HexStr(ev.topics(0)) : "";
            std::string t1 = ev.topics_size() > 1 ? HexStr(ev.topics(1)) : "";
            std::string t2 = ev.topics_size() > 2 ? HexStr(ev.topics(2)) : "";
            std::string t3 = ev.topics_size() > 3 ? HexStr(ev.topics(3)) : "";
            std::string data_hex = ev.data().empty() ? "" : HexStr(ev.data());

            const char* sql =
                "INSERT OR IGNORE INTO tx_logs"
                "(tx_hash,log_index,contract_addr,topic0,topic1,topic2,topic3,data)"
                " VALUES(?,?,?,?,?,?,?,?);";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, tx_hash_hex.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (stmt, 2, li);
                sqlite3_bind_text(stmt, 3, contract_hex.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, t0.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, t1.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, t2.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 7, t3.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 8, data_hex.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Upsert address post-states from address_array (balance/nonce after each tx)
    {
        int64_t now_ms = static_cast<int64_t>(common::TimeUtils::TimestampMs());
        const char* sql =
            "INSERT INTO addresses(addr,addr_type,shard_id,pool_index,is_contract,"
            " balance,nonce,first_seen,last_seen,tx_count,updated_at)"
            " VALUES(?,?,?,?,?,?,?,?,?,0,?)"
            " ON CONFLICT(addr) DO UPDATE SET"
            " addr_type=excluded.addr_type,"
            " shard_id=excluded.shard_id,"
            " pool_index=excluded.pool_index,"
            " is_contract=MAX(is_contract,excluded.is_contract),"
            " balance=excluded.balance,"
            " nonce=excluded.nonce,"
            " last_seen=excluded.last_seen,"
            " updated_at=excluded.updated_at;";
        for (int ai = 0; ai < block.address_array_size(); ++ai) {
            const auto& ai_info = block.address_array(ai);
            if (ai_info.addr().empty()) continue;
            std::string addr_hex = HexStr(ai_info.addr());
            if (addr_hex == "0x") continue;
            int addr_type   = static_cast<int>(ai_info.type());
            int is_contract = ai_info.has_bytes_code() ? 1 : 0;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text (stmt, 1, addr_hex.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int  (stmt, 2, addr_type);
                sqlite3_bind_int  (stmt, 3, static_cast<int>(ai_info.sharding_id()));
                sqlite3_bind_int  (stmt, 4, static_cast<int>(ai_info.pool_index()));
                sqlite3_bind_int  (stmt, 5, is_contract);
                sqlite3_bind_int64(stmt, 6, static_cast<int64_t>(ai_info.balance()));
                sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(ai_info.nonce()));
                sqlite3_bind_int64(stmt, 8, ts);
                sqlite3_bind_int64(stmt, 9, ts);
                sqlite3_bind_int64(stmt, 10, now_ms);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Upsert sync cursor
    {
        const char* sql =
            "INSERT INTO sync_cursors(shard_id,pool_index,synced_height,updated_at)"
            " VALUES(?,?,?,?)"
            " ON CONFLICT(shard_id,pool_index) DO UPDATE SET"
            " synced_height=MAX(synced_height,excluded.synced_height),"
            " updated_at=excluded.updated_at;";
        sqlite3_stmt* stmt = nullptr;
        int64_t now_ms = static_cast<int64_t>(common::TimeUtils::TimestampMs());
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int  (stmt, 1, static_cast<int>(shard_id));
            sqlite3_bind_int  (stmt, 2, static_cast<int>(pool_idx));
            sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(height));
            sqlite3_bind_int64(stmt, 4, now_ms);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

// ─── Query helpers ────────────────────────────────────────────────────────────

static json BlockRowToJson(sqlite3_stmt* stmt) {
    json obj;
    obj["id"]            = sqlite3_column_int64(stmt, 0);
    obj["shard_id"]      = sqlite3_column_int(stmt, 1);
    obj["pool_index"]    = sqlite3_column_int(stmt, 2);
    obj["height"]        = sqlite3_column_int64(stmt, 3);
    auto hash_raw        = sqlite3_column_text(stmt, 4);
    obj["hash"]          = hash_raw ? (const char*)hash_raw : "";
    auto par_raw         = sqlite3_column_text(stmt, 5);
    obj["parent_hash"]   = par_raw  ? (const char*)par_raw  : "";
    obj["timestamp"]     = sqlite3_column_int64(stmt, 6);
    obj["tx_count"]      = sqlite3_column_int(stmt, 7);
    obj["all_gas"]       = sqlite3_column_int64(stmt, 8);
    obj["is_root_shard"] = sqlite3_column_int(stmt, 9);
    obj["elect_height"]  = sqlite3_column_int64(stmt, 10);
    obj["leader_idx"]    = sqlite3_column_int(stmt, 11);
    return obj;
}

static json TxRowToJson(sqlite3_stmt* stmt) {
    json obj;
    obj["id"]             = sqlite3_column_int64(stmt, 0);
    auto th = sqlite3_column_text(stmt, 1);
    obj["tx_hash"]        = th ? (const char*)th : "";
    auto bh = sqlite3_column_text(stmt, 2);
    obj["block_hash"]     = bh ? (const char*)bh : "";
    obj["shard_id"]       = sqlite3_column_int(stmt, 3);
    obj["pool_index"]     = sqlite3_column_int(stmt, 4);
    obj["height"]         = sqlite3_column_int64(stmt, 5);
    obj["timestamp"]      = sqlite3_column_int64(stmt, 6);
    auto fr = sqlite3_column_text(stmt, 7);
    obj["from_addr"]      = fr ? (const char*)fr : "";
    auto to = sqlite3_column_text(stmt, 8);
    obj["to_addr"]        = to ? (const char*)to : "";
    obj["amount"]         = sqlite3_column_int64(stmt, 9);
    obj["gas_limit"]      = sqlite3_column_int64(stmt, 10);
    obj["gas_used"]       = sqlite3_column_int64(stmt, 11);
    obj["gas_price"]      = sqlite3_column_int64(stmt, 12);
    obj["step_type"]      = sqlite3_column_int(stmt, 13);
    obj["is_system_tx"]   = sqlite3_column_int(stmt, 14);
    obj["status"]         = sqlite3_column_int(stmt, 15);
    obj["nonce"]          = sqlite3_column_int64(stmt, 16);
    auto ci = sqlite3_column_text(stmt, 17);
    obj["contract_input"] = ci ? (const char*)ci : "";
    auto op = sqlite3_column_text(stmt, 18);
    obj["output"]         = op ? (const char*)op : "";
    return obj;
}

// ─── Query methods ────────────────────────────────────────────────────────────

std::string Explorer::QueryBlocks(uint32_t shard_id, int pool_index,
                                  bool is_root_filter, bool root_only,
                                  int64_t before_id, int limit) {
    int fetch = limit + 1;
    std::ostringstream ss;
    ss << "SELECT id,shard_id,pool_index,height,hash,parent_hash,timestamp,"
          "tx_count,all_gas,is_root_shard,elect_height,leader_idx FROM blocks WHERE 1=1";
    if (before_id > 0)       ss << " AND id<" << before_id;
    if (shard_id > 0)        ss << " AND shard_id=" << shard_id;
    if (pool_index >= 0)     ss << " AND pool_index=" << pool_index;
    if (is_root_filter)      ss << " AND is_root_shard=" << (root_only ? 1 : 0);
    ss << " ORDER BY id DESC LIMIT " << fetch;

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = ss.str();
    if (sqlite3_prepare_v2(read_db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < fetch) {
            arr.push_back(BlockRowToJson(stmt));
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    bool has_more = (static_cast<int>(arr.size()) > limit);
    int64_t next_cursor = 0;
    if (has_more) {
        arr.erase(arr.begin() + limit, arr.end());
        next_cursor = arr.back()["id"].get<int64_t>();
    }
    return JsonList(arr, next_cursor, has_more);
}

std::string Explorer::QueryBlock(const std::string& hash) {
    const char* sql =
        "SELECT id,shard_id,pool_index,height,hash,parent_hash,timestamp,"
        "tx_count,all_gas,is_root_shard,elect_height,leader_idx"
        " FROM blocks WHERE hash=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    json result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = BlockRowToJson(stmt);
        sqlite3_finalize(stmt);

        // Fetch tx list for this block
        const char* tx_sql =
            "SELECT id,tx_hash,block_hash,shard_id,pool_index,height,timestamp,"
            "from_addr,to_addr,amount,gas_limit,gas_used,gas_price,"
            "step_type,is_system_tx,status,nonce,contract_input,output"
            " FROM transactions WHERE block_hash=? ORDER BY id ASC;";
        sqlite3_stmt* tx_stmt = nullptr;
        json txs = json::array();
        if (sqlite3_prepare_v2(read_db_, tx_sql, -1, &tx_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(tx_stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(tx_stmt) == SQLITE_ROW) {
                txs.push_back(TxRowToJson(tx_stmt));
            }
            sqlite3_finalize(tx_stmt);
        }
        result["transactions"] = txs;
        return JsonOk(result);
    }
    sqlite3_finalize(stmt);
    return JsonErr("block not found");
}

std::string Explorer::QueryTransactions(const std::string& block_hash,
                                        uint32_t shard_id, int step_type,
                                        int is_system,
                                        int64_t before_id, int limit) {
    int fetch = limit + 1;
    std::ostringstream ss;
    ss << "SELECT id,tx_hash,block_hash,shard_id,pool_index,height,timestamp,"
          "from_addr,to_addr,amount,gas_limit,gas_used,gas_price,"
          "step_type,is_system_tx,status,nonce,contract_input,output"
          " FROM transactions WHERE 1=1";
    if (before_id > 0)         ss << " AND id<" << before_id;
    if (!block_hash.empty())   ss << " AND block_hash='" << block_hash << "'";
    if (shard_id > 0)          ss << " AND shard_id=" << shard_id;
    if (step_type >= 0)        ss << " AND step_type=" << step_type;
    if (is_system == 0)        ss << " AND is_system_tx=0";
    else if (is_system == 1)   ss << " AND is_system_tx=1";
    ss << " ORDER BY id DESC LIMIT " << fetch;

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = ss.str();
    if (sqlite3_prepare_v2(read_db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < fetch) {
            arr.push_back(TxRowToJson(stmt));
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    bool has_more = (static_cast<int>(arr.size()) > limit);
    int64_t next_cursor = 0;
    if (has_more) {
        arr.erase(arr.begin() + limit, arr.end());
        next_cursor = arr.back()["id"].get<int64_t>();
    }
    return JsonList(arr, next_cursor, has_more);
}

std::string Explorer::QueryTransaction(const std::string& tx_hash) {
    const char* sql =
        "SELECT id,tx_hash,block_hash,shard_id,pool_index,height,timestamp,"
        "from_addr,to_addr,amount,gas_limit,gas_used,gas_price,"
        "step_type,is_system_tx,status,nonce,contract_input,output"
        " FROM transactions WHERE tx_hash=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text(stmt, 1, tx_hash.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json result = TxRowToJson(stmt);
        sqlite3_finalize(stmt);

        // Fetch logs
        const char* log_sql =
            "SELECT log_index,contract_addr,topic0,topic1,topic2,topic3,data"
            " FROM tx_logs WHERE tx_hash=? ORDER BY log_index ASC;";
        sqlite3_stmt* ls = nullptr;
        json logs = json::array();
        if (sqlite3_prepare_v2(read_db_, log_sql, -1, &ls, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(ls, 1, tx_hash.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(ls) == SQLITE_ROW) {
                json log;
                log["log_index"]     = sqlite3_column_int(ls, 0);
                auto ca = sqlite3_column_text(ls, 1);
                log["contract_addr"] = ca ? (const char*)ca : "";
                auto t0 = sqlite3_column_text(ls, 2);
                log["topic0"]        = t0 ? (const char*)t0 : "";
                auto t1 = sqlite3_column_text(ls, 3);
                log["topic1"]        = t1 ? (const char*)t1 : "";
                auto t2 = sqlite3_column_text(ls, 4);
                log["topic2"]        = t2 ? (const char*)t2 : "";
                auto t3 = sqlite3_column_text(ls, 5);
                log["topic3"]        = t3 ? (const char*)t3 : "";
                auto d = sqlite3_column_text(ls, 6);
                log["data"]          = d  ? (const char*)d  : "";
                logs.push_back(log);
            }
            sqlite3_finalize(ls);
        }
        result["logs"] = logs;
        return JsonOk(result);
    }
    sqlite3_finalize(stmt);
    return JsonErr("transaction not found");
}

std::string Explorer::QueryAddress(const std::string& addr) {
    const char* sql =
        "SELECT addr,addr_type,shard_id,pool_index,is_contract,"
        "first_seen,last_seen,tx_count"
        " FROM addresses WHERE addr=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text(stmt, 1, addr.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json obj;
        auto a = sqlite3_column_text(stmt, 0);
        obj["addr"]        = a ? (const char*)a : "";
        obj["addr_type"]   = sqlite3_column_int(stmt, 1);
        obj["shard_id"]    = sqlite3_column_int(stmt, 2);
        obj["pool_index"]  = sqlite3_column_int(stmt, 3);
        obj["is_contract"] = sqlite3_column_int(stmt, 4);
        obj["first_seen"]  = sqlite3_column_int64(stmt, 5);
        obj["last_seen"]   = sqlite3_column_int64(stmt, 6);
        obj["tx_count"]    = sqlite3_column_int64(stmt, 7);
        sqlite3_finalize(stmt);
        return JsonOk(obj);
    }
    sqlite3_finalize(stmt);
    return JsonErr("address not found");
}

std::string Explorer::QueryAddressTxs(const std::string& addr,
                                      int64_t before_id, int limit) {
    int fetch = limit + 1;
    std::ostringstream ss;
    ss << "SELECT id,tx_hash,block_hash,shard_id,pool_index,height,timestamp,"
          "from_addr,to_addr,amount,gas_limit,gas_used,gas_price,"
          "step_type,is_system_tx,status,nonce,contract_input,output"
          " FROM transactions"
          " WHERE (from_addr=? OR to_addr=?)";
    if (before_id > 0) ss << " AND id<" << before_id;
    ss << " ORDER BY id DESC LIMIT " << fetch;

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = ss.str();
    if (sqlite3_prepare_v2(read_db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, addr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, addr.c_str(), -1, SQLITE_TRANSIENT);
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < fetch) {
            arr.push_back(TxRowToJson(stmt));
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    bool has_more = (static_cast<int>(arr.size()) > limit);
    int64_t next_cursor = 0;
    if (has_more) {
        arr.erase(arr.begin() + limit, arr.end());
        next_cursor = arr.back()["id"].get<int64_t>();
    }
    return JsonList(arr, next_cursor, has_more);
}

std::string Explorer::QueryAddresses(uint32_t shard_id, int pool_index,
                                     int64_t before_id, int limit) {
    int fetch = limit + 1;
    std::ostringstream ss;
    ss << "SELECT rowid,addr,addr_type,shard_id,pool_index,is_contract,"
          "balance,nonce,first_seen,last_seen,tx_count"
          " FROM addresses WHERE 1=1";
    if (before_id > 0)   ss << " AND rowid<" << before_id;
    if (shard_id > 0)    ss << " AND shard_id=" << shard_id;
    if (pool_index >= 0) ss << " AND pool_index=" << pool_index;
    ss << " ORDER BY rowid DESC LIMIT " << fetch;

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = ss.str();
    if (sqlite3_prepare_v2(read_db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < fetch) {
            json obj;
            obj["id"]          = sqlite3_column_int64(stmt, 0);
            auto ad = sqlite3_column_text(stmt, 1);
            obj["addr"]        = ad ? (const char*)ad : "";
            obj["addr_type"]   = sqlite3_column_int(stmt, 2);
            obj["shard_id"]    = sqlite3_column_int(stmt, 3);
            obj["pool_index"]  = sqlite3_column_int(stmt, 4);
            obj["is_contract"] = sqlite3_column_int(stmt, 5);
            obj["balance"]     = sqlite3_column_int64(stmt, 6);
            obj["nonce"]       = sqlite3_column_int64(stmt, 7);
            obj["first_seen"]  = sqlite3_column_int64(stmt, 8);
            obj["last_seen"]   = sqlite3_column_int64(stmt, 9);
            obj["tx_count"]    = sqlite3_column_int64(stmt, 10);
            arr.push_back(obj);
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    bool has_more = (static_cast<int>(arr.size()) > limit);
    int64_t next_cursor = 0;
    if (has_more) {
        arr.erase(arr.begin() + limit, arr.end());
        next_cursor = arr.back()["id"].get<int64_t>();
    }
    return JsonList(arr, next_cursor, has_more);
}

std::string Explorer::SearchAddresses(const std::string& prefix, int limit) {
    std::string p = prefix;
    // Normalize: lowercase, strip 0x, keep hex only
    if (p.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p = p.substr(2);
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (p.empty()) return JsonErr("prefix required");

    // Stored rows are written by HexStr(), which prefixes "0x", so an account is
    // 42 chars and a prefund address 82. Rows written by other paths (and some
    // contracts rows) have no prefix. Match both spellings, since the caller's
    // 0x is stripped above.
    const std::string with_0x  = "0x" + p;
    const std::string bare     = p;

    // A prefix shorter than a full address cannot select anything meaningful —
    // it would just be a table scan returning unrelated rows.
    if (p.size() < 40) return JsonList(json::array(), 0, false);

    // One scan, both spellings. `exact` marks rows whose stored addr equals the
    // normalized input in either form, which sorts first.
    const char* sql =
        "SELECT rowid,addr,addr_type,shard_id,pool_index,is_contract,"
        "balance,nonce,first_seen,last_seen,tx_count"
        " FROM addresses"
        " WHERE (addr LIKE ? OR addr LIKE ?)"
        " ORDER BY (addr=? OR addr=?) DESC, rowid"
        " LIMIT ?;";

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        const std::string like_0x = with_0x + "%";
        const std::string like_b  = bare + "%";
        sqlite3_bind_text(stmt, 1, like_0x.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, like_b.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, with_0x.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, bare.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 5, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto ad = sqlite3_column_text(stmt, 1);
            const std::string addr = ad ? (const char*)ad : "";
            json obj;
            obj["id"]          = sqlite3_column_int64(stmt, 0);
            obj["addr"]        = addr;
            obj["addr_type"]   = sqlite3_column_int(stmt, 2);
            obj["shard_id"]    = sqlite3_column_int(stmt, 3);
            obj["pool_index"]  = sqlite3_column_int(stmt, 4);
            obj["is_contract"] = sqlite3_column_int(stmt, 5);
            obj["balance"]     = sqlite3_column_int64(stmt, 6);
            obj["nonce"]       = sqlite3_column_int64(stmt, 7);
            obj["first_seen"]  = sqlite3_column_int64(stmt, 8);
            obj["last_seen"]   = sqlite3_column_int64(stmt, 9);
            obj["tx_count"]    = sqlite3_column_int64(stmt, 10);
            obj["exact"]       = (addr == with_0x || addr == bare);
            arr.push_back(obj);
        }
        sqlite3_finalize(stmt);
    }

    return JsonList(arr, 0, false);
}

std::string Explorer::QueryContracts(int is_library, int is_clone,
                                     int64_t before_id, int limit) {
    int fetch = limit + 1;
    std::ostringstream ss;
    ss << "SELECT rowid,addr,creator_addr,create_tx_hash,create_height,"
          "create_timestamp,shard_id,pool_index,is_library,is_clone,"
          "bytecode,source_code,abi"
          " FROM contracts WHERE 1=1";
    if (before_id > 0)  ss << " AND rowid<" << before_id;
    if (is_library >= 0) ss << " AND is_library=" << is_library;
    if (is_clone >= 0)   ss << " AND is_clone=" << is_clone;
    ss << " ORDER BY rowid DESC LIMIT " << fetch;

    json arr = json::array();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = ss.str();
    if (sqlite3_prepare_v2(read_db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < fetch) {
            json obj;
            obj["id"]               = sqlite3_column_int64(stmt, 0);
            auto ad = sqlite3_column_text(stmt, 1);
            obj["addr"]             = ad ? (const char*)ad : "";
            auto cr = sqlite3_column_text(stmt, 2);
            obj["creator_addr"]     = cr ? (const char*)cr : "";
            auto ct = sqlite3_column_text(stmt, 3);
            obj["create_tx_hash"]   = ct ? (const char*)ct : "";
            obj["create_height"]    = sqlite3_column_int64(stmt, 4);
            obj["create_timestamp"] = sqlite3_column_int64(stmt, 5);
            obj["shard_id"]         = sqlite3_column_int(stmt, 6);
            obj["pool_index"]       = sqlite3_column_int(stmt, 7);
            obj["is_library"]       = sqlite3_column_int(stmt, 8);
            obj["is_clone"]         = sqlite3_column_int(stmt, 9);
            auto bc = sqlite3_column_text(stmt, 10);
            obj["bytecode"]         = bc ? (const char*)bc : "";
            auto sc = sqlite3_column_text(stmt, 11);
            obj["source_code"]      = sc ? (const char*)sc : "";
            auto ab = sqlite3_column_text(stmt, 12);
            obj["abi"]              = ab ? (const char*)ab : "";
            arr.push_back(obj);
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    bool has_more = (static_cast<int>(arr.size()) > limit);
    int64_t next_cursor = 0;
    if (has_more) {
        arr.erase(arr.begin() + limit, arr.end());
        next_cursor = arr.back()["id"].get<int64_t>();
    }
    return JsonList(arr, next_cursor, has_more);
}

std::string Explorer::QueryContract(const std::string& addr) {
    const char* sql =
        "SELECT rowid,addr,creator_addr,create_tx_hash,create_height,"
        "create_timestamp,shard_id,pool_index,is_library,is_clone,"
        "bytecode,source_code,abi"
        " FROM contracts WHERE addr=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text(stmt, 1, addr.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json obj;
        obj["id"]               = sqlite3_column_int64(stmt, 0);
        auto ad = sqlite3_column_text(stmt, 1);
        obj["addr"]             = ad ? (const char*)ad : "";
        auto cr = sqlite3_column_text(stmt, 2);
        obj["creator_addr"]     = cr ? (const char*)cr : "";
        auto ct = sqlite3_column_text(stmt, 3);
        obj["create_tx_hash"]   = ct ? (const char*)ct : "";
        obj["create_height"]    = sqlite3_column_int64(stmt, 4);
        obj["create_timestamp"] = sqlite3_column_int64(stmt, 5);
        obj["shard_id"]         = sqlite3_column_int(stmt, 6);
        obj["pool_index"]       = sqlite3_column_int(stmt, 7);
        obj["is_library"]       = sqlite3_column_int(stmt, 8);
        obj["is_clone"]         = sqlite3_column_int(stmt, 9);
        auto bc = sqlite3_column_text(stmt, 10);
        obj["bytecode"]         = bc ? (const char*)bc : "";
        auto sc = sqlite3_column_text(stmt, 11);
        obj["source_code"]      = sc ? (const char*)sc : "";
        auto ab = sqlite3_column_text(stmt, 12);
        obj["abi"]              = ab ? (const char*)ab : "";
        sqlite3_finalize(stmt);
        return JsonOk(obj);
    }
    sqlite3_finalize(stmt);
    return JsonErr("contract not found");
}

std::string Explorer::QueryGasPresets() {
    const char* sql = "SELECT id,name,step_type,gas_amount,description FROM gas_presets ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    json arr = json::array();
    if (sqlite3_prepare_v2(read_db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json obj;
            obj["id"]          = sqlite3_column_int(stmt, 0);
            auto nm = sqlite3_column_text(stmt, 1);
            obj["name"]        = nm ? (const char*)nm : "";
            if (sqlite3_column_type(stmt, 2) == SQLITE_NULL)
                obj["step_type"] = nullptr;
            else
                obj["step_type"] = sqlite3_column_int(stmt, 2);
            obj["gas_amount"]  = sqlite3_column_int64(stmt, 3);
            auto ds = sqlite3_column_text(stmt, 4);
            obj["description"] = ds ? (const char*)ds : "";
            arr.push_back(obj);
        }
        sqlite3_finalize(stmt);
    }
    return JsonOk(arr);
}

std::string Explorer::QueryChainInfo() {
    // Block stats
    int64_t total_blocks = 0, total_txs = 0;
    {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(read_db_, "SELECT COUNT(*) FROM blocks;", -1, &s, nullptr) == SQLITE_OK) {
            if (sqlite3_step(s) == SQLITE_ROW) total_blocks = sqlite3_column_int64(s, 0);
            sqlite3_finalize(s);
        }
        if (sqlite3_prepare_v2(read_db_, "SELECT COUNT(*) FROM transactions;", -1, &s, nullptr) == SQLITE_OK) {
            if (sqlite3_step(s) == SQLITE_ROW) total_txs = sqlite3_column_int64(s, 0);
            sqlite3_finalize(s);
        }
    }

    // Sync cursors per shard
    json shards = json::array();
    {
        const char* sql =
            "SELECT shard_id, COUNT(*) as pool_count,"
            " MAX(synced_height) as max_height, MAX(updated_at) as last_update"
            " FROM sync_cursors GROUP BY shard_id ORDER BY shard_id;";
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(read_db_, sql, -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                json sh;
                sh["shard_id"]    = sqlite3_column_int(s, 0);
                sh["pool_count"]  = sqlite3_column_int(s, 1);
                sh["max_height"]  = sqlite3_column_int64(s, 2);
                sh["last_update"] = sqlite3_column_int64(s, 3);
                shards.push_back(sh);
            }
            sqlite3_finalize(s);
        }
    }

    json result;
    result["total_blocks"] = total_blocks;
    result["total_txs"]    = total_txs;
    result["shard_count"]  = static_cast<int>(shards.size());
    result["shards"]       = shards;
    return JsonOk(result);
}

std::string Explorer::UpdateContract(const std::string& addr,
                                     const std::string& source_code,
                                     const std::string& abi,
                                     const std::string& bytecode,
                                     int shard_id) {
    // UPSERT: insert if not exists, update source_code/abi/bytecode if it does.
    //
    // shard_id is written on insert only when the caller knows it (> 0). On
    // conflict the stored shard is kept unless the row still holds the column
    // default of 0 and the caller can now supply the real one — that repairs
    // rows written before the shard was threaded through, without letting a
    // stale or defaulted value overwrite a shard that sync already resolved.
    const char* sql =
        "INSERT INTO contracts(addr,shard_id,source_code,abi,bytecode,updated_at)"
        " VALUES(?,?,?,?,?,?)"
        " ON CONFLICT(addr) DO UPDATE SET"
        "  shard_id=CASE WHEN contracts.shard_id=0 AND excluded.shard_id!=0"
        "                THEN excluded.shard_id ELSE contracts.shard_id END,"
        "  source_code=excluded.source_code,"
        "  abi=excluded.abi,"
        "  bytecode=CASE WHEN excluded.bytecode!='' THEN excluded.bytecode ELSE bytecode END,"
        "  updated_at=excluded.updated_at;";
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(write_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text (stmt, 1, addr.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 2, shard_id);
    sqlite3_bind_text (stmt, 3, source_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 4, abi.c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 5, bytecode.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, now_ms);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return JsonErr(std::string("update failed: ") + sqlite3_errmsg(write_db_));
    return JsonOk(json::object());
}

std::string Explorer::DeleteContract(const std::string& addr) {
    const char* sql = "DELETE FROM contracts WHERE addr=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(write_db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return JsonErr("prepare failed");
    sqlite3_bind_text(stmt, 1, addr.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return JsonErr(std::string("delete failed: ") + sqlite3_errmsg(write_db_));
    return JsonOk(json::object());
}

}  // namespace explorer
}  // namespace shardora
