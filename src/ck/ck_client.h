#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <clickhouse/client.h>
#include <nlohmann/json.hpp>

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
    bool HandleNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& block_item);
    void FlushToCk();
    void FlushToCkWithData();
    void ResetColumns();
    void HandleBlsMessage();
    void HandleBlsBlockMessage(const BlsBlockInfo& info);
    void HandleBlsElectMessage(const BlsElectInfo& info);

    static const uint32_t kBatchCountToCk = 1000;

    common::Tick statistic_tick_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlock>> block_queues_[common::kMaxThreadCount];
    std::shared_ptr<std::thread> flush_to_ck_thread_;
    std::atomic<bool> stop_ = false;
    std::mutex wait_mutex_;
    std::condition_variable wait_con_;

    std::shared_ptr<clickhouse::ColumnUInt32> shard_id;
    std::shared_ptr<clickhouse::ColumnUInt32> pool_index;
    std::shared_ptr<clickhouse::ColumnUInt64> height;
    std::shared_ptr<clickhouse::ColumnString> prehash;
    std::shared_ptr<clickhouse::ColumnString> hash;
    std::shared_ptr<clickhouse::ColumnUInt32> version;
    std::shared_ptr<clickhouse::ColumnUInt64> vss;
    std::shared_ptr<clickhouse::ColumnUInt64> elect_height;
    std::shared_ptr<clickhouse::ColumnString> bitmap;
    std::shared_ptr<clickhouse::ColumnUInt64> timestamp;
    std::shared_ptr<clickhouse::ColumnUInt64> timeblock_height;
    std::shared_ptr<clickhouse::ColumnString> bls_agg_sign_x;
    std::shared_ptr<clickhouse::ColumnString> bls_agg_sign_y;
    std::shared_ptr<clickhouse::ColumnString> commit_bitmap;
    std::shared_ptr<clickhouse::ColumnString> gid;
    std::shared_ptr<clickhouse::ColumnString> from;
    std::shared_ptr<clickhouse::ColumnString> from_pubkey;
    std::shared_ptr<clickhouse::ColumnString> from_sign;
    std::shared_ptr<clickhouse::ColumnString> to;
    std::shared_ptr<clickhouse::ColumnUInt64> amount;
    std::shared_ptr<clickhouse::ColumnUInt64> gas_limit;
    std::shared_ptr<clickhouse::ColumnUInt64> gas_used;
    std::shared_ptr<clickhouse::ColumnUInt64> gas_price;
    std::shared_ptr<clickhouse::ColumnUInt64> balance;
    std::shared_ptr<clickhouse::ColumnUInt32> to_add;
    std::shared_ptr<clickhouse::ColumnUInt32> type;
    std::shared_ptr<clickhouse::ColumnString> attrs;
    std::shared_ptr<clickhouse::ColumnUInt32> status;
    std::shared_ptr<clickhouse::ColumnString> tx_hash;
    std::shared_ptr<clickhouse::ColumnUInt32> call_contract_step;
    std::shared_ptr<clickhouse::ColumnString> storages;
    std::shared_ptr<clickhouse::ColumnString> transfers;
    std::shared_ptr<clickhouse::ColumnUInt32> date;

    std::shared_ptr<clickhouse::ColumnUInt32> block_shard_id;
    std::shared_ptr<clickhouse::ColumnUInt32> block_pool_index;
    std::shared_ptr<clickhouse::ColumnUInt64> block_height;
    std::shared_ptr<clickhouse::ColumnString> block_prehash;
    std::shared_ptr<clickhouse::ColumnString> block_hash;
    std::shared_ptr<clickhouse::ColumnUInt32> block_version;
    std::shared_ptr<clickhouse::ColumnUInt64> block_vss;
    std::shared_ptr<clickhouse::ColumnUInt64> block_elect_height;
    std::shared_ptr<clickhouse::ColumnString> block_bitmap;
    std::shared_ptr<clickhouse::ColumnUInt64> block_timestamp;
    std::shared_ptr<clickhouse::ColumnUInt64> block_timeblock_height;
    std::shared_ptr<clickhouse::ColumnString> block_bls_agg_sign_x;
    std::shared_ptr<clickhouse::ColumnString> block_bls_agg_sign_y;
    std::shared_ptr<clickhouse::ColumnString> block_commit_bitmap;
    std::shared_ptr<clickhouse::ColumnUInt32> block_tx_size;
    std::shared_ptr<clickhouse::ColumnUInt32> block_date;

    std::shared_ptr<clickhouse::ColumnString> acc_account;
    std::shared_ptr<clickhouse::ColumnUInt32> acc_shard_id;
    std::shared_ptr<clickhouse::ColumnUInt32> acc_pool_index;
    std::shared_ptr<clickhouse::ColumnUInt64> acc_balance;

    std::shared_ptr<clickhouse::ColumnString> attr_account;
    std::shared_ptr<clickhouse::ColumnString> attr_to;
    std::shared_ptr<clickhouse::ColumnUInt32> attr_shard_id;
    std::shared_ptr<clickhouse::ColumnUInt32> attr_tx_type;
    std::shared_ptr<clickhouse::ColumnString> attr_key;
    std::shared_ptr<clickhouse::ColumnString> attr_value;

    std::shared_ptr<clickhouse::ColumnString> c2c_r;
    std::shared_ptr<clickhouse::ColumnString> c2c_seller;
    std::shared_ptr<clickhouse::ColumnUInt64> c2c_all;
    std::shared_ptr<clickhouse::ColumnUInt64> c2c_now;
    std::shared_ptr<clickhouse::ColumnUInt32> c2c_mc;
    std::shared_ptr<clickhouse::ColumnUInt32> c2c_sc;
    std::shared_ptr<clickhouse::ColumnUInt32> c2c_report;
    std::shared_ptr<clickhouse::ColumnUInt64> c2c_order_id;
    std::shared_ptr<clickhouse::ColumnUInt64> c2c_height;
    std::shared_ptr<clickhouse::ColumnString> c2c_buyer;
    std::shared_ptr<clickhouse::ColumnUInt64> c2c_amount;
    std::shared_ptr<clickhouse::ColumnString> c2c_contract_addr;

    std::shared_ptr<clickhouse::ColumnString> prepay_contract;
    std::shared_ptr<clickhouse::ColumnString> prepay_user;
    std::shared_ptr<clickhouse::ColumnUInt64> prepay_amount;
    std::shared_ptr<clickhouse::ColumnUInt64> prepay_height;

    common::ThreadSafeQueue<std::shared_ptr<BlsElectInfo>> bls_elect_queue_;
    common::ThreadSafeQueue<std::shared_ptr<BlsBlockInfo>> bls_block_queue_;

    uint32_t batch_count_ = 0;
    int64_t pre_time_out_ = 0;

    DISALLOW_COPY_AND_ASSIGN(ClickHouseClient);
};

};  // namespace ck

};  // namespace shardora
