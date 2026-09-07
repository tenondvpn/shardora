#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <sqlite3.h>

#include "common/thread_safe_queue.h"
#include "consensus/hotstuff/types.h"

namespace shardora {

namespace contract {
class ContractManager;
}

namespace explorer {

class Explorer {
public:
    explicit Explorer(const std::string& db_path);
    ~Explorer();

    bool Init();

    // Called from BlockManager::AddNewBlock — thread-safe
    void AddNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& view_block);

    // Query methods — called from HTTP handler threads (read_db_, WAL allows concurrent reads)
    std::string QueryBlocks(uint32_t shard_id, int pool_index,
                            bool is_root_filter, bool root_only,
                            int64_t before_id, int limit);
    std::string QueryBlock(const std::string& hash);
    std::string QueryTransactions(const std::string& block_hash,
                                  uint32_t shard_id, int step_type,
                                  int is_system,
                                  int64_t before_id, int limit);
    std::string QueryTransaction(const std::string& tx_hash);
    std::string QueryAddress(const std::string& addr);
    std::string QueryAddressTxs(const std::string& addr,
                                int64_t before_id, int limit);
    std::string QueryContracts(int is_library, int is_clone,
                               int64_t before_id, int limit);
    std::string QueryContract(const std::string& addr);
    std::string QueryGasPresets();
    std::string QueryChainInfo();

private:
    void FlushLoop();
    void WriteBlock(sqlite3* db, const std::shared_ptr<hotstuff::ViewBlock>& vb);
    bool IsSystemTx(int32_t step_type);
    bool ExecSQL(sqlite3* db, const char* sql);
    bool OpenDB(const std::string& path, sqlite3** db);

    std::string db_path_;
    sqlite3* write_db_ = nullptr;
    sqlite3* read_db_  = nullptr;

    common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlock>> queue_;
    std::thread flush_thread_;
    std::mutex  wait_mutex_;
    std::condition_variable wait_cv_;
    std::atomic<bool> running_{false};

    DISALLOW_COPY_AND_ASSIGN(Explorer);
};

}  // namespace explorer
}  // namespace shardora
