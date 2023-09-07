#pragma once

#include <clickhouse/client.h>

#include "common/utils.h"
#include "common/tick.h"
#include "ck/ck_utils.h"
#include "db/db.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace ck {

class ClickHouseClient {
public:
    ClickHouseClient(const std::string& host, const std::string& user, const std::string& passwd, std::shared_ptr<db::Db> db_ptr);
    ~ClickHouseClient();
    bool CreateTable(bool statistic, std::shared_ptr<db::Db> db_ptr);
    bool AddNewBlock(const std::shared_ptr<block::protobuf::Block>& block_item);

private:
    bool CreateTransactionTable();
    bool CreateBlockTable();
    bool CreateAccountTable();
    bool CreateAccountKeyValueTable();
    bool CreateStatisticTable();
    bool CreatePrivateKeyTable();
    bool CreateC2cTable();
    void Statistic();
    void TickStatistic();
    bool QueryContract(const std::string& contract_addr);

    common::Tick statistic_tick_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ClickHouseClient);
};

};  // namespace ck

};  // namespace zjchain
