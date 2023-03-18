#pragma once

#include <clickhouse/client.h>

#include "common/utils.h"

namespace zjchain {

namespace ck {

static const std::string kClickhouseTransTableName = "zjc_ck_transaction_table";
static const std::string kClickhouseBlockTableName = "zjc_ck_block_table";
static const std::string kClickhouseAccountTableName = "zjc_ck_account_table";
static const std::string kClickhouseAccountKvTableName = "zjc_ck_account_key_value_table";
static const std::string kClickhouseStatisticTableName = "zjc_ck_statistic_table";
static const std::string kClickhouseShardStatisticTableName = "zjc_ck_shard_statistic_table";
static const std::string kClickhousePoolStatisticTableName = "zjc_ck_pool_statistic_table";

};  // namespace ck

};  // namespace zjchain
