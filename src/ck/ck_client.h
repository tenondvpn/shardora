#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <clickhouse/client.h>
#include <json/json.hpp>

#include "ck/ck_utils.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/utils.h"
#include "contract/contract_manager.h"
#include "db/db.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h"

namespace shardora {

namespace ck {

class ClickHouseClient {
public:
    ClickHouseClient(
        const std::string& host, 
        const std::string& user, 
        const std::string& passwd, 
        std::shared_ptr<db::Db> db_ptr, 
        std::shared_ptr<contract::ContractManager> contract_mgr);
    ~ClickHouseClient();
    bool CreateTable(bool statistic, std::shared_ptr<db::Db> db_ptr);
    bool AddNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& block_item);

private:
    bool CreateTransactionTable();
    bool CreateBlockTable();
    bool CreateAccountTable();
    bool CreateAccountKeyValueTable();
    bool CreateStatisticTable();
    bool CreatePrivateKeyTable();
    bool CreateC2cTable();
    bool CreatePrepaymentTable();
    void Statistic();
    void TickStatistic();
    bool QueryContract(const std::string& from, const std::string& contract_addr, nlohmann::json* res);
    bool HandleNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& block_item);
    void FlushToCk();

    common::Tick statistic_tick_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlock>> block_queues_[common::kMaxThreadCount];
    std::shared_ptr<std::thread> flush_to_ck_thread_;
    volatile bool stop_ = false;
    std::mutex wait_mutex_;
    std::condition_variable wait_con_;

    DISALLOW_COPY_AND_ASSIGN(ClickHouseClient);
};

};  // namespace ck

};  // namespace shardora
