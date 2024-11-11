#pragma once

#include <clickhouse/client.h>
#include <json/json.hpp>

#include "ck/ck_utils.h"
#include "common/utils.h"
#include "common/tick.h"
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
    bool InsertBlsElectInfo(const BlsElectInfo& info);
    bool InsertBlsBlockInfo(const BlsBlockInfo& info);
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
    bool CreateBlsElectInfoTable();
    bool CreateBlsBlockInfoTable();    
    void Statistic();
    void TickStatistic();
    bool QueryContract(const std::string& from, const std::string& contract_addr, nlohmann::json* res);

    common::Tick statistic_tick_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ClickHouseClient);
};

};  // namespace ck

};  // namespace shardora
