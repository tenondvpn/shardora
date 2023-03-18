#pragma once

#include <clickhouse/client.h>

#include "common/utils.h"
#include "common/tick.h"
#include "ck/ck_utils.h"
#include "protos/block.pb.h"

namespace zjchain {

namespace ck {

class ClickHouseClient {
public:
    ClickHouseClient(const std::string& host, const std::string& user, const std::string& passwd);
    ~ClickHouseClient();
    bool CreateTable(bool statistic);
    bool AddNewBlock(const std::shared_ptr<block::protobuf::Block>& block_item);

private:
    bool CreateTransactionTable();
    bool CreateBlockTable();
    bool CreateAccountTable();
    bool CreateAccountKeyValueTable();
    bool CreateStatisticTable();
    bool CreatePrivateKeyTable();
    void Statistic();
    void TickStatistic();

    clickhouse::Client client_;
    common::Tick statistic_tick_;

    DISALLOW_COPY_AND_ASSIGN(ClickHouseClient);
};

};  // namespace ck

};  // namespace zjchain
