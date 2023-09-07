#include "ck/ck_client.h"

#include <google/protobuf/util/json_util.h>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/time_utils.h"

namespace zjchain {

namespace ck {

ClickHouseClient::ClickHouseClient(
        const std::string& host,
        const std::string& user,
        const std::string& passwd,
        std::shared_ptr<db::Db> db_ptr) {
    CreateTable(true, db_ptr);
}

ClickHouseClient::~ClickHouseClient() {}

bool ClickHouseClient::AddNewBlock(const std::shared_ptr<block::protobuf::Block>& block_item) try {
    std::string cmd;
    const auto& tx_list = block_item->tx_list();
    clickhouse::Block trans;
    clickhouse::Block blocks;
    clickhouse::Block accounts;
    clickhouse::Block account_attrs;
    clickhouse::Block c2cs;
    auto shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    auto height = std::make_shared<clickhouse::ColumnUInt64>();
    auto prehash = std::make_shared<clickhouse::ColumnString>();
    auto hash = std::make_shared<clickhouse::ColumnString>();
    auto version = std::make_shared<clickhouse::ColumnUInt32>();
    auto vss = std::make_shared<clickhouse::ColumnUInt64>();
    auto elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto bitmap = std::make_shared<clickhouse::ColumnString>();
    auto timestamp = std::make_shared<clickhouse::ColumnUInt64>();
    auto timeblock_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto bls_agg_sign_x = std::make_shared<clickhouse::ColumnString>();
    auto bls_agg_sign_y = std::make_shared<clickhouse::ColumnString>();
    auto commit_bitmap = std::make_shared<clickhouse::ColumnString>();
    auto gid = std::make_shared<clickhouse::ColumnString>();
    auto from = std::make_shared<clickhouse::ColumnString>();
    auto from_pubkey = std::make_shared<clickhouse::ColumnString>();
    auto from_sign = std::make_shared<clickhouse::ColumnString>();
    auto to = std::make_shared<clickhouse::ColumnString>();
    auto amount = std::make_shared<clickhouse::ColumnUInt64>();
    auto gas_limit = std::make_shared<clickhouse::ColumnUInt64>();
    auto gas_used = std::make_shared<clickhouse::ColumnUInt64>();
    auto gas_price = std::make_shared<clickhouse::ColumnUInt64>();
    auto balance = std::make_shared<clickhouse::ColumnUInt64>();
    auto to_add = std::make_shared<clickhouse::ColumnUInt32>();
    auto type = std::make_shared<clickhouse::ColumnUInt32>();
    auto attrs = std::make_shared<clickhouse::ColumnString>();
    auto status = std::make_shared<clickhouse::ColumnUInt32>();
    auto tx_hash = std::make_shared<clickhouse::ColumnString>();
    auto call_contract_step = std::make_shared<clickhouse::ColumnUInt32>();
    auto storages = std::make_shared<clickhouse::ColumnString>();
    auto transfers = std::make_shared<clickhouse::ColumnString>();
    auto date = std::make_shared<clickhouse::ColumnUInt32>();

    auto block_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto block_pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    auto block_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto block_prehash = std::make_shared<clickhouse::ColumnString>();
    auto block_hash = std::make_shared<clickhouse::ColumnString>();
    auto block_version = std::make_shared<clickhouse::ColumnUInt32>();
    auto block_vss = std::make_shared<clickhouse::ColumnUInt64>();
    auto block_elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto block_bitmap = std::make_shared<clickhouse::ColumnString>();
    auto block_timestamp = std::make_shared<clickhouse::ColumnUInt64>();
    auto block_timeblock_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto block_bls_agg_sign_x = std::make_shared<clickhouse::ColumnString>();
    auto block_bls_agg_sign_y = std::make_shared<clickhouse::ColumnString>();
    auto block_commit_bitmap = std::make_shared<clickhouse::ColumnString>();
    auto block_tx_size = std::make_shared<clickhouse::ColumnUInt32>();
    auto block_date = std::make_shared<clickhouse::ColumnUInt32>();

    auto acc_account = std::make_shared<clickhouse::ColumnString>();
    auto acc_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto acc_pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    auto acc_balance = std::make_shared<clickhouse::ColumnUInt64>();

    auto attr_account = std::make_shared<clickhouse::ColumnString>();
    auto attr_to = std::make_shared<clickhouse::ColumnString>();
    auto attr_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto attr_tx_type = std::make_shared<clickhouse::ColumnUInt32>();
    auto attr_key = std::make_shared<clickhouse::ColumnString>();
    auto attr_value = std::make_shared<clickhouse::ColumnString>();

    auto c2c_r = std::make_shared<clickhouse::ColumnString>();
    auto c2c_seller = std::make_shared<clickhouse::ColumnString>();
    auto c2c_all = std::make_shared<clickhouse::ColumnUInt64>();
    auto c2c_now = std::make_shared<clickhouse::ColumnUInt64>();
    auto c2c_mc = std::make_shared<clickhouse::ColumnUInt32>();
    auto c2c_sc = std::make_shared<clickhouse::ColumnUInt32>();
    auto c2c_r = std::make_shared<clickhouse::ColumnUInt32>();
    auto c2c_order_id = std::make_shared<clickhouse::ColumnUInt64>();

    std::string bitmap_str;
    std::string commit_bitmap_str;
    block_shard_id->Append(block_item->network_id());
    block_pool_index->Append(block_item->pool_index());
    block_height->Append(block_item->height());
    block_prehash->Append(common::Encode::HexEncode(block_item->prehash()));
    block_hash->Append(common::Encode::HexEncode(block_item->hash()));
    block_version->Append(block_item->version());
    block_vss->Append(block_item->consistency_random());
    block_elect_height->Append(block_item->electblock_height());
    block_bitmap->Append(common::Encode::HexEncode(bitmap_str));
    block_commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
    block_timestamp->Append(block_item->timestamp());
    block_timeblock_height->Append(block_item->timeblock_height());
    block_bls_agg_sign_x->Append(common::Encode::HexEncode(block_item->bls_agg_sign_x()));
    block_bls_agg_sign_y->Append(common::Encode::HexEncode(block_item->bls_agg_sign_y()));
    block_date->Append(common::MicTimestampToDate(block_item->timestamp()));
    block_tx_size->Append(tx_list.size());

    for (int32_t i = 0; i < tx_list.size(); ++i) {
        shard_id->Append(block_item->network_id());
        pool_index->Append(block_item->pool_index());
        height->Append(block_item->height());
        prehash->Append(common::Encode::HexEncode(block_item->prehash()));
        hash->Append(common::Encode::HexEncode(block_item->hash()));
        version->Append(block_item->version());
        vss->Append(block_item->consistency_random());
        elect_height->Append(block_item->electblock_height());
        bitmap->Append(common::Encode::HexEncode(bitmap_str));
        commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
        timestamp->Append(block_item->timestamp());
        timeblock_height->Append(block_item->timeblock_height());
        bls_agg_sign_x->Append(common::Encode::HexEncode(block_item->bls_agg_sign_x()));
        bls_agg_sign_y->Append(common::Encode::HexEncode(block_item->bls_agg_sign_y()));
        date->Append(common::MicTimestampToDate(block_item->timestamp()));
        gid->Append(common::Encode::HexEncode(tx_list[i].gid()));
        from->Append(common::Encode::HexEncode(tx_list[i].from()));
        from_pubkey->Append("");
        from_sign->Append("");
        to->Append(common::Encode::HexEncode(tx_list[i].to()));
        amount->Append(tx_list[i].amount());
        if (block_item->network_id() == 2 && tx_list[i].step() == 5) {
            gas_limit->Append(0);
            gas_used->Append(0);
            gas_price->Append(0);
            type->Append(3);
        } else {
            gas_limit->Append(tx_list[i].gas_limit());
            gas_used->Append(tx_list[i].gas_used());
            gas_price->Append(tx_list[i].gas_price());
            type->Append(tx_list[i].step());
        }
        balance->Append(tx_list[i].balance());
        to_add->Append(false);
        attrs->Append("");
        status->Append(tx_list[i].status());
        tx_hash->Append(common::Encode::HexEncode(tx_list[i].gid()));
        call_contract_step->Append(tx_list[i].step());
        storages->Append("");
        transfers->Append("");
        if (tx_list[i].step() == pools::protobuf::kNormalTo) {
            acc_account->Append(common::Encode::HexEncode(tx_list[i].to()));
            acc_shard_id->Append(block_item->network_id());
            acc_pool_index->Append(block_item->pool_index());
            acc_balance->Append(tx_list[i].balance());
        }
        else {
            acc_account->Append(common::Encode::HexEncode(tx_list[i].from()));
            acc_shard_id->Append(block_item->network_id());
            acc_pool_index->Append(block_item->pool_index());
            acc_balance->Append(tx_list[i].balance());
        }

        for (int32_t j = 0; j < tx_list[i].storages_size(); ++j) {
            attr_account->Append(common::Encode::HexEncode(tx_list[i].from()));
            attr_tx_type->Append(tx_list[i].step());
            attr_to->Append(common::Encode::HexEncode(tx_list[i].to()));
            attr_shard_id->Append(block_item->network_id());
//             attr_key->Append(common::Encode::HexEncode(tx_list[i].storages(j).key()));
//             attr_value->Append(common::Encode::HexEncode(tx_list[i].storages(j).val_hash()));

            std::string val;
            if (prefix_db_->GetTemporaryKv(tx_list[i].storages(j).val_hash(), &val)) {
                if (tx_list[i].storages(j).key() == protos::kElectNodeAttrElectBlock) {
                    elect::protobuf::ElectBlock elect_block;
                    if (elect_block.ParseFromString(val)) {
                        std::string json_str;
                        auto st = google::protobuf::util::MessageToJsonString(elect_block, &json_str);
                        if (st.ok()) {
                            attr_key->Append(tx_list[i].storages(j).key());
                            attr_value->Append(json_str);
                            continue;
                        }
                    }
                }
                    
                attr_key->Append(common::Encode::HexEncode(tx_list[i].storages(j).key()));
                attr_value->Append(common::Encode::HexEncode(val));
            } else {
                attr_key->Append(common::Encode::HexEncode(tx_list[i].storages(j).key()));
                attr_value->Append(common::Encode::HexEncode(tx_list[i].storages(j).val_hash()));
            }
        }

        if (tx_list[i].step() == pools::protobuf::kContractExcute /*&& tx_list[i].to() == common::GlobalInfo::Instance()->c2c_to()*/) {
            QueryContract(tx_list[i].to())
        }

        while (tx_list[i].step() == pools::protobuf::kConsensusLocalTos) {
            ZJC_DEBUG("now handle local to txs.");
            std::string to_txs_str;
            auto& tx = tx_list[i];
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kConsensusLocalNormalTos) {
                    if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &to_txs_str)) {
                        ZJC_DEBUG("handle local to tx failed get val hash error: %s",
                            common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                        break;
                    }

                    break;
                }
            }

            if (to_txs_str.empty()) {
                ZJC_WARN("get local tos info failed!");
                break;
            }

            block::protobuf::ConsensusToTxs to_txs;
            if (!to_txs.ParseFromString(to_txs_str)) {
                assert(false);
                break;
            }

            ZJC_DEBUG("now handle local to txs: %d", to_txs.tos_size());
            for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
                if (to_txs.tos(i).to().size() != security::kUnicastAddressLength) {
                    //assert(false);
                    continue;
                }
                
                ZJC_DEBUG("now handle local to txs: %s", common::Encode::HexEncode(to_txs.tos(i).to()).c_str());
                shard_id->Append(block_item->network_id());
                pool_index->Append(block_item->pool_index());
                height->Append(block_item->height());
                prehash->Append(common::Encode::HexEncode(block_item->prehash()));
                hash->Append(common::Encode::HexEncode(block_item->hash()));
                version->Append(block_item->version());
                vss->Append(block_item->consistency_random());
                elect_height->Append(block_item->electblock_height());
                bitmap->Append(common::Encode::HexEncode(bitmap_str));
                commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
                timestamp->Append(block_item->timestamp());
                timeblock_height->Append(block_item->timeblock_height());
                bls_agg_sign_x->Append(common::Encode::HexEncode(block_item->bls_agg_sign_x()));
                bls_agg_sign_y->Append(common::Encode::HexEncode(block_item->bls_agg_sign_y()));
                date->Append(common::MicTimestampToDate(block_item->timestamp()));
                gid->Append(common::Encode::HexEncode(tx_list[i].gid()));
                from->Append("");
                from_pubkey->Append("");
                from_sign->Append("");
                to->Append(common::Encode::HexEncode(to_txs.tos(i).to()));
                amount->Append(0);
                gas_limit->Append(0);
                gas_used->Append(0);
                gas_price->Append(0);
                type->Append(tx_list[i].step());
                balance->Append(to_txs.tos(i).balance());
                to_add->Append(true);
                attrs->Append("");
                status->Append(tx_list[i].status());
                tx_hash->Append(common::Encode::HexEncode(tx_list[i].gid()));
                call_contract_step->Append(tx_list[i].step());
                storages->Append("");
                transfers->Append("");

                acc_account->Append(common::Encode::HexEncode(to_txs.tos(i).to()));
                acc_shard_id->Append(block_item->network_id());
                acc_pool_index->Append(block_item->pool_index());
                acc_balance->Append(to_txs.tos(i).balance());
            }

            break;
        }
    }

    ZJC_INFO("add new ck block block_shard_id: %d, block_height: %lu", block_item->network_id(), block_item->height());

    blocks.AppendColumn("shard_id", block_shard_id);
    blocks.AppendColumn("pool_index", block_pool_index);
    blocks.AppendColumn("height", block_height);
    blocks.AppendColumn("prehash", block_prehash);
    blocks.AppendColumn("hash", block_hash);
    blocks.AppendColumn("version", block_version);
    blocks.AppendColumn("vss", block_vss);
    blocks.AppendColumn("elect_height", block_elect_height);
    blocks.AppendColumn("bitmap", block_bitmap);
    blocks.AppendColumn("timestamp", block_timestamp);
    blocks.AppendColumn("timeblock_height", block_timeblock_height);
    blocks.AppendColumn("bls_agg_sign_x", block_bls_agg_sign_x);
    blocks.AppendColumn("bls_agg_sign_y", block_bls_agg_sign_y);
    blocks.AppendColumn("commit_bitmap", block_commit_bitmap);
    blocks.AppendColumn("date", block_date);
    blocks.AppendColumn("tx_size", block_tx_size);

    trans.AppendColumn("shard_id", shard_id);
    trans.AppendColumn("pool_index", pool_index);
    trans.AppendColumn("height", height);
    trans.AppendColumn("prehash", prehash);
    trans.AppendColumn("hash", hash);
    trans.AppendColumn("version", version);
    trans.AppendColumn("vss", vss);
    trans.AppendColumn("elect_height", elect_height);
    trans.AppendColumn("bitmap", bitmap);
    trans.AppendColumn("timestamp", timestamp);
    trans.AppendColumn("timeblock_height", timeblock_height);
    trans.AppendColumn("bls_agg_sign_x", bls_agg_sign_x);
    trans.AppendColumn("bls_agg_sign_y", bls_agg_sign_y);
    trans.AppendColumn("commit_bitmap", commit_bitmap);
    trans.AppendColumn("gid", gid);
    trans.AppendColumn("from", from);
    trans.AppendColumn("from_pubkey", from_pubkey);
    trans.AppendColumn("from_sign", from_sign);
    trans.AppendColumn("to", to);
    trans.AppendColumn("amount", amount);
    trans.AppendColumn("gas_limit", gas_limit);
    trans.AppendColumn("gas_used", gas_used);
    trans.AppendColumn("gas_price", gas_price);
    trans.AppendColumn("balance", balance);
    trans.AppendColumn("to_add", to_add);
    trans.AppendColumn("type", type);
    trans.AppendColumn("attrs", attrs);
    trans.AppendColumn("status", status);
    trans.AppendColumn("tx_hash", tx_hash);
    trans.AppendColumn("call_contract_step", call_contract_step);
    trans.AppendColumn("storages", storages);
    trans.AppendColumn("transfers", transfers);
    trans.AppendColumn("date", date);

    accounts.AppendColumn("id", acc_account);
    accounts.AppendColumn("shard_id", acc_shard_id);
    accounts.AppendColumn("pool_index", acc_pool_index);
    accounts.AppendColumn("balance", acc_balance);

    account_attrs.AppendColumn("from", attr_account);
    account_attrs.AppendColumn("shard_id", attr_shard_id);
    account_attrs.AppendColumn("key", attr_key);
    account_attrs.AppendColumn("value", attr_value);
    account_attrs.AppendColumn("to", attr_to);
    account_attrs.AppendColumn("type", attr_tx_type);

    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Insert(kClickhouseTransTableName, trans);
    ck_client.Insert(kClickhouseBlockTableName, blocks);
    ck_client.Insert(kClickhouseAccountTableName, accounts);
    ck_client.Insert(kClickhouseAccountKvTableName, account_attrs);
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
    return false;
}

bool ClickHouseClient::QueryContract(const std::string& contract_addr) {
    zjcvm::ZjchainHost zjc_host;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = 0;
    zjc_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = 0;
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.thread_idx_ = 0;
    zjc_host.contract_mgr_ = contract_mgr;
    zjc_host.acc_mgr_ = nullptr;
    zjc_host.my_address_ = contract_addr;
    zjc_host.tx_context_.block_gas_limit = 10000000000lu;
    // user caller prepayment 's gas
    uint64_t from_balance = 10000000000lu;
    auto contract_addr_info = prefix_db_->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        return false;
    }
    uint64_t to_balance = contract_addr_info->balance();
    zjc_host.AddTmpAccountBalance(
        from,
        from_balance);
    zjc_host.AddTmpAccountBalance(
        contract_addr,
        to_balance);
    evmc_result evmc_res = {};
    evmc::Result result{ evmc_res };
    int exec_res = zjcvm::Execution::Instance()->execute(
        contract_addr_info->bytes_code(),
        input,
        from,
        contract_addr,
        from,
        0,
        10000000000lu,
        0,
        zjcvm::kJustCall,
        zjc_host,
        &result);
    if (exec_res != zjcvm::kZjcvmSuccess || result.status_code != EVMC_SUCCESS) {
        std::string res = "query contract failed: " + std::to_string(result.status_code);
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("query contract error: %s.", res.c_str());
        return;
    }

    std::string qdata((char*)result.output_data, result.output_size);
    evmc_bytes32 len_bytes;
    memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    uint64_t len = zjcvm::EvmcBytes32ToUint64(len_bytes);
    std::string http_res(qdata.c_str() + 64, len);
    std::cout << http_res << std::endl;
}

bool ClickHouseClient::CreateTransactionTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseTransTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`shard_id` UInt32 COMMENT 'shard_id' CODEC(T64, LZ4), "
        "`pool_index` UInt32 COMMENT 'pool_index' CODEC(T64, LZ4), "
        "`height` UInt64 COMMENT 'height' CODEC(T64, LZ4), "
        "`prehash` String COMMENT 'prehash' CODEC(LZ4), "
        "`hash` String COMMENT 'hash' CODEC(LZ4), "
        "`version` UInt32 COMMENT 'version' CODEC(LZ4), "
        "`vss` UInt64 COMMENT 'vss' CODEC(T64, LZ4), "
        "`elect_height` UInt64 COMMENT 'elect_height' CODEC(T64, LZ4), "
        "`bitmap` String COMMENT 'success consensers' CODEC(LZ4), "
        "`timestamp` UInt64 COMMENT 'timestamp' CODEC(T64, LZ4), "
        "`timeblock_height` UInt64 COMMENT 'timeblock_height' CODEC(T64, LZ4), "
        "`bls_agg_sign_x` String COMMENT 'bls_agg_sign_x' CODEC(LZ4), "
        "`bls_agg_sign_y` String COMMENT 'bls_agg_sign_y' CODEC(LZ4), "
        "`commit_bitmap` String COMMENT 'commit_bitmap' CODEC(LZ4), "
        "`gid` String COMMENT 'gid' CODEC(LZ4), "
        "`from` String COMMENT 'from' CODEC(LZ4), "
        "`from_pubkey` String COMMENT 'from_pubkey' CODEC(LZ4), "
        "`from_sign` String COMMENT 'from_sign' CODEC(LZ4), "
        "`to` String COMMENT 'to' CODEC(LZ4), "
        "`amount` UInt64 COMMENT 'amount' CODEC(T64, LZ4), "
        "`gas_limit` UInt64 COMMENT 'gas_limit' CODEC(T64, LZ4), "
        "`gas_used` UInt64 COMMENT 'gas_used' CODEC(T64, LZ4), "
        "`gas_price` UInt64 COMMENT 'gas_price' CODEC(T64, LZ4), "
        "`balance` UInt64 COMMENT 'balance' CODEC(T64, LZ4), "
        "`to_add` UInt32 COMMENT 'to_add' CODEC(T64, LZ4), "
        "`type` UInt32 COMMENT 'type' CODEC(T64, LZ4), "
        "`attrs` String COMMENT 'attrs' CODEC(LZ4), "
        "`status` UInt32 COMMENT 'status' CODEC(T64, LZ4), "
        "`tx_hash` String COMMENT 'tx_hash' CODEC(LZ4), "
        "`call_contract_step` UInt32 COMMENT 'call_contract_step' CODEC(T64, LZ4), "
        "`storages` String COMMENT 'storages' CODEC(LZ4), "
        "`transfers` String COMMENT 'transfers' CODEC(LZ4), "
        "`date` UInt32 COMMENT 'date' CODEC(T64, LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id, date) "
        "ORDER BY(pool_index,height,type,from,to) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateBlockTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseBlockTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`shard_id` UInt32 COMMENT 'shard_id' CODEC(T64, LZ4), "
        "`pool_index` UInt32 COMMENT 'pool_index' CODEC(T64, LZ4), "
        "`height` UInt64 COMMENT 'height' CODEC(T64, LZ4), "
        "`prehash` String COMMENT 'prehash' CODEC(LZ4), "
        "`hash` String COMMENT 'hash' CODEC(LZ4), "
        "`version` UInt32 COMMENT 'version' CODEC(LZ4), "
        "`vss` UInt64 COMMENT 'vss' CODEC(T64, LZ4), "
        "`elect_height` UInt64 COMMENT 'elect_height' CODEC(T64, LZ4), "
        "`bitmap` String COMMENT 'success consensers' CODEC(LZ4), "
        "`timestamp` UInt64 COMMENT 'timestamp' CODEC(T64, LZ4), "
        "`timeblock_height` UInt64 COMMENT 'timeblock_height' CODEC(T64, LZ4), "
        "`bls_agg_sign_x` String COMMENT 'bls_agg_sign_x' CODEC(LZ4), "
        "`bls_agg_sign_y` String COMMENT 'bls_agg_sign_y' CODEC(LZ4), "
        "`commit_bitmap` String COMMENT 'commit_bitmap' CODEC(LZ4), "
        "`tx_size` UInt32 COMMENT 'type' CODEC(T64, LZ4), "
        "`date` UInt32 COMMENT 'date' CODEC(T64, LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id, date) "
        "ORDER BY(pool_index,height) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateAccountTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseAccountTableName + " ( "
        "`id` String COMMENT 'prehash' CODEC(LZ4), "
        "`shard_id` UInt32 COMMENT 'shard_id' CODEC(T64, LZ4), "
        "`pool_index` UInt32 COMMENT 'pool_index' CODEC(T64, LZ4), "
        "`balance` UInt64 COMMENT 'balance' CODEC(T64, LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id) "
        "ORDER BY(id,pool_index) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateAccountKeyValueTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseAccountKvTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`from` String COMMENT 'prehash' CODEC(LZ4), "
        "`to` String COMMENT 'prehash' CODEC(LZ4), "
        "`type` UInt32 COMMENT 'type' CODEC(T64, LZ4), "
        "`shard_id` UInt32 COMMENT 'shard_id' CODEC(T64, LZ4), "
        "`key` String COMMENT 'key' CODEC(LZ4), "
        "`value` String COMMENT 'value' CODEC(LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id) "
        "ORDER BY(type, key, from, to) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateStatisticTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseStatisticTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`time` UInt64 COMMENT 'time' CODEC(LZ4), "
        "`all_zjc` UInt64 COMMENT 'zjc' CODEC(LZ4), "
        "`all_address` UInt32 COMMENT 'address' CODEC(T64, LZ4), "
        "`all_contracts` UInt32 COMMENT 'contracts' CODEC(T64, LZ4), "
        "`all_transactions` UInt32 COMMENT 'transactions' CODEC(LZ4), "
        "`all_nodes` UInt32 COMMENT 'nodes' CODEC(LZ4), "
        "`all_waiting_nodes` UInt32 COMMENT 'waiting_nodes' CODEC(LZ4), "
        "`date` UInt32 COMMENT 'date' CODEC(T64, LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(date) "
        "ORDER BY(time) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreatePrivateKeyTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists private_key_table ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`seckey` String COMMENT 'seckey' CODEC(LZ4), "
        "`ecn_prikey` String COMMENT 'ecn_prikey' CODEC(LZ4), "
        "`date` UInt32 COMMENT 'date' CODEC(T64, LZ4) "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(date) "
        "ORDER BY(seckey) "
        "SETTINGS index_granularity = 8192;");
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateC2cTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists c2c_table ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`seller` String COMMENT 'seller' CODEC(LZ4), "
        "`receivable` String COMMENT 'receivable' CODEC(LZ4), "
        "`all` UInt64 COMMENT 'all' CODEC(LZ4), "
        "`now` UInt64 COMMENT 'now' CODEC(LZ4), "
        "`mchecked` UInt32 COMMENT 'mchecked' CODEC(LZ4), "
        "`schecked` UInt32 COMMENT 'schecked' CODEC(LZ4), "
        "`reported` UInt32 COMMENT 'reported' CODEC(LZ4), "
        "`orderId` UInt64 COMMENT 'orderId' CODEC(LZ4), "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(orderId) "
        "ORDER BY(orderId) "
        "SETTINGS index_granularity = 8192;");
    clickhouse::Client ck_client(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateTable(bool statistic, std::shared_ptr<db::Db> db_ptr) try {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_ptr);
    CreateTransactionTable();
    CreateBlockTable();
    CreateAccountTable();
    CreateAccountKeyValueTable();
    CreateStatisticTable();
    CreatePrivateKeyTable();
    CreateC2cTable();
    if (statistic) {
        statistic_tick_.CutOff(5000000l, std::bind(&ClickHouseClient::TickStatistic, this));
    }
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
    printf("add new block failed[%s]", e.what());
    return false;
}

void ClickHouseClient::TickStatistic() {
    Statistic();
    statistic_tick_.CutOff(10000000l, std::bind(&ClickHouseClient::TickStatistic, this));
}

void ClickHouseClient::Statistic() try {
    std::string cmd = "select count(*) as cnt from zjc_ck_transaction_table;";
    uint32_t all_transactions = 0;
    clickhouse::Client ck_client0(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client0.Select(cmd, [&all_transactions](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            all_transactions = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select count(*) from zjc_ck_account_table;";
    uint32_t all_address = 0;
    clickhouse::Client ck_client1(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client1.Select(cmd, [&all_address](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            all_address = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select sum(balance) from zjc_ck_account_table;";
    uint64_t sum_balance = 0;
    clickhouse::Client ck_client2(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client2.Select(cmd, [&sum_balance](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            sum_balance = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select count(*) from zjc_ck_account_key_value_table where type = 4 and key = '5f5f636279746573636f6465'";
    uint32_t all_contracts = 0;
    clickhouse::Client ck_client3(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client3.Select(cmd, [&all_contracts](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            all_contracts = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    auto st_time = std::make_shared<clickhouse::ColumnUInt64>();
    auto st_zjc = std::make_shared<clickhouse::ColumnUInt64>();
    auto st_address = std::make_shared<clickhouse::ColumnUInt32>();
    auto st_contracts = std::make_shared<clickhouse::ColumnUInt32>();
    auto st_transactions = std::make_shared<clickhouse::ColumnUInt32>();
    auto st_nodes = std::make_shared<clickhouse::ColumnUInt32>();
    auto st_wnodes = std::make_shared<clickhouse::ColumnUInt32>();
    auto st_date = std::make_shared<clickhouse::ColumnUInt32>();
    st_time->Append(common::TimeUtils::TimestampSeconds());
    st_date->Append(common::TimeUtils::TimestampDays());
    st_zjc->Append(sum_balance);
    st_address->Append(all_address);
    st_contracts->Append(all_contracts);
    st_transactions->Append(all_transactions);
    st_nodes->Append(0);
    st_wnodes->Append(0);
    clickhouse::Block statistics;
    statistics.AppendColumn("time", st_time);
    statistics.AppendColumn("all_zjc", st_zjc);
    statistics.AppendColumn("all_address", st_address);
    statistics.AppendColumn("all_contracts", st_contracts);
    statistics.AppendColumn("all_transactions", st_transactions);
    statistics.AppendColumn("all_nodes", st_nodes);
    statistics.AppendColumn("all_waiting_nodes", st_wnodes);
    statistics.AppendColumn("date", st_date);
    clickhouse::Client ck_client4(clickhouse::ClientOptions().SetHost("127.0.0.1").SetPort(common::GlobalInfo::Instance()->ck_port()));
    ck_client4.Insert(kClickhouseStatisticTableName, statistics);
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
}

};  // namespace ck

};  // namespace zjchain
