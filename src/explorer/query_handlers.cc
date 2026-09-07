#include "explorer/query_handlers.h"
#include "init/uws_adapter.h"
#include "common/log.h"

#include <memory>
#include <string>

namespace shardora {
namespace explorer {

// Global explorer instance — set once at startup
static std::shared_ptr<Explorer> g_explorer;

void SetGlobalExplorer(std::shared_ptr<Explorer> explorer) {
    g_explorer = std::move(explorer);
}

static std::string ParamStr(const init::UWSRequest& req, const std::string& key) {
    return req.get_param_value(key);
}

static int64_t ParamInt64(const init::UWSRequest& req, const std::string& key,
                           int64_t def = -1) {
    std::string s = req.get_param_value(key);
    if (s.empty()) return def;
    try { return std::stoll(s); } catch (...) { return def; }
}

static int ParamInt(const init::UWSRequest& req, const std::string& key,
                    int def = -1) {
    std::string s = req.get_param_value(key);
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

static const char* kJson = "application/json";
static const std::string kNoExplorer = R"({"code":-1,"msg":"explorer not enabled"})";

// GET /explorer/blocks
// Params: shard_id, pool_index, is_root (0/1), before_id, limit
void ExplorerBlocks(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }

    uint32_t shard_id = static_cast<uint32_t>(ParamInt(req, "shard_id", 0));
    int pool_index    = ParamInt(req, "pool_index", -1);
    int is_root_p     = ParamInt(req, "is_root", -1);
    bool is_root_filter = (is_root_p >= 0);
    bool root_only      = (is_root_p == 1);
    int64_t before_id = ParamInt64(req, "before_id", 0);
    int limit         = ParamInt(req, "limit", 20);
    if (limit <= 0 || limit > 100) limit = 20;

    res.set_content(
        g_explorer->QueryBlocks(shard_id, pool_index,
                                is_root_filter, root_only,
                                before_id, limit),
        kJson);
}

// GET /explorer/block?hash=0x...
void ExplorerBlock(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    std::string hash = ParamStr(req, "hash");
    if (hash.empty()) {
        res.set_content(R"({"code":-1,"msg":"hash required"})", kJson);
        return;
    }
    res.set_content(g_explorer->QueryBlock(hash), kJson);
}

// GET /explorer/transactions
// Params: block_hash, shard_id, step_type, is_system (0=user,1=system,-1=all), before_id, limit
void ExplorerTxList(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }

    std::string block_hash = ParamStr(req, "block_hash");
    uint32_t shard_id  = static_cast<uint32_t>(ParamInt(req, "shard_id", 0));
    int step_type      = ParamInt(req, "step_type", -1);
    int is_system      = ParamInt(req, "is_system", -1);
    int64_t before_id  = ParamInt64(req, "before_id", 0);
    int limit          = ParamInt(req, "limit", 20);
    if (limit <= 0 || limit > 100) limit = 20;

    res.set_content(
        g_explorer->QueryTransactions(block_hash, shard_id, step_type,
                                      is_system, before_id, limit),
        kJson);
}

// GET /explorer/transaction?tx_hash=0x...
void ExplorerTx(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    std::string tx_hash = ParamStr(req, "tx_hash");
    if (tx_hash.empty()) {
        res.set_content(R"({"code":-1,"msg":"tx_hash required"})", kJson);
        return;
    }
    res.set_content(g_explorer->QueryTransaction(tx_hash), kJson);
}

// GET /explorer/address?addr=0x...
void ExplorerAddress(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    std::string addr = ParamStr(req, "addr");
    if (addr.empty()) {
        res.set_content(R"({"code":-1,"msg":"addr required"})", kJson);
        return;
    }
    res.set_content(g_explorer->QueryAddress(addr), kJson);
}

// GET /explorer/address_txs?addr=0x...&before_id=&limit=
void ExplorerAddressTxs(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    std::string addr  = ParamStr(req, "addr");
    int64_t before_id = ParamInt64(req, "before_id", 0);
    int limit         = ParamInt(req, "limit", 20);
    if (limit <= 0 || limit > 100) limit = 20;
    if (addr.empty()) {
        res.set_content(R"({"code":-1,"msg":"addr required"})", kJson);
        return;
    }
    res.set_content(g_explorer->QueryAddressTxs(addr, before_id, limit), kJson);
}

// GET /explorer/contracts?is_library=0&is_clone=0&before_id=&limit=
void ExplorerContracts(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    int is_library = ParamInt(req, "is_library", -1);
    int is_clone   = ParamInt(req, "is_clone",   -1);
    int64_t before_id = ParamInt64(req, "before_id", 0);
    int limit      = ParamInt(req, "limit", 20);
    if (limit <= 0 || limit > 100) limit = 20;
    res.set_content(
        g_explorer->QueryContracts(is_library, is_clone, before_id, limit),
        kJson);
}

// GET /explorer/contract?addr=0x...
void ExplorerContract(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    std::string addr = ParamStr(req, "addr");
    if (addr.empty()) {
        res.set_content(R"({"code":-1,"msg":"addr required"})", kJson);
        return;
    }
    res.set_content(g_explorer->QueryContract(addr), kJson);
}

// GET /explorer/gas-presets
void ExplorerGasPresets(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    res.set_content(g_explorer->QueryGasPresets(), kJson);
}

// GET /explorer/chain-info
void ExplorerChainInfo(const init::UWSRequest& req, init::UWSResponse& res) {
    if (!g_explorer) { res.set_content(kNoExplorer, kJson); return; }
    res.set_content(g_explorer->QueryChainInfo(), kJson);
}

}  // namespace explorer
}  // namespace shardora
