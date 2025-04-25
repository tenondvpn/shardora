#include "ck/ck_client.h"
#include <ck/ck_utils.h>

#include <google/protobuf/util/json_util.h>
#include <json/json.hpp>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/time_utils.h"
#include "zjcvm/execution.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace ck {

ClickHouseClient::ClickHouseClient(
        const std::string& host,
        const std::string& user,
        const std::string& passwd,
        std::shared_ptr<db::Db> db_ptr,
        std::shared_ptr<contract::ContractManager> contract_mgr) : contract_mgr_(contract_mgr) {
    CreateTable(true, db_ptr);
    ResetColumns();
    flush_to_ck_thread_ = std::make_shared<std::thread>(
        std::bind(&ClickHouseClient::FlushToCk, this));

}

ClickHouseClient::~ClickHouseClient() {
    stop_ = true;
    flush_to_ck_thread_->join();
}

bool ClickHouseClient::AddNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& view_block_item) {
    return true;
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    block_queues_[thread_idx].push(view_block_item);
// #ifndef NDEBUG
//     auto* block_item = &view_block_item->block_info();
//     const auto& tx_list = block_item->tx_list();
//     for (int32_t i = 0; i < tx_list.size(); ++i) {
//         ZJC_DEBUG("ck new block coming sharding id: %u_%d_%lu, "
//             "tx size: %u, hash: %s, elect height: %lu, "
//             "tm height: %lu, nonce: %s, status: %d, step: %d",
//             view_block_item->qc().network_id(),
//             view_block_item->qc().pool_index(),
//             block_item->height(),
//             block_item->tx_list_size(),
//             common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
//             view_block_item->qc().elect_height(),
//             block_item->timeblock_height(),
//             tx_list[i].nonce(),
//             tx_list[i].status(),
//             tx_list[i].step());
//     }
// #endif
        
    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_con_.notify_one();
    return true;
}

void ClickHouseClient::FlushToCk() {
    return;
    common::GlobalInfo::Instance()->get_thread_index();
    while (!stop_) {
        for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
            std::shared_ptr<hotstuff::ViewBlock> view_block_ptr;
            while (block_queues_[i].pop(&view_block_ptr)) {
                if (!view_block_ptr) {
                    break;
                }

                HandleNewBlock(view_block_ptr);
                if (batch_count_ >= kBatchCountToCk) {
                    break;
                }
            }

            if (batch_count_ >= kBatchCountToCk) {
                break;
            }
        }

        FlushToCkWithData();
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(100));
    }   
}

bool ClickHouseClient::HandleNewBlock(const std::shared_ptr<hotstuff::ViewBlock>& view_block_item)  {
    std::string cmd;
    auto* block_item = &view_block_item->block_info();
    const auto& tx_list = block_item->tx_list();
    std::string bitmap_str;
    std::string commit_bitmap_str;
    block_shard_id->Append(view_block_item->qc().network_id());
    block_pool_index->Append(view_block_item->qc().pool_index());
    block_height->Append(block_item->height());
    block_prehash->Append(common::Encode::HexEncode(view_block_item->parent_hash()));
    block_hash->Append(common::Encode::HexEncode(view_block_item->qc().view_block_hash()));
    block_version->Append(block_item->version());
    block_vss->Append(block_item->consistency_random());
    block_elect_height->Append(view_block_item->qc().elect_height());
    block_bitmap->Append(common::Encode::HexEncode(bitmap_str));
    block_commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
    block_timestamp->Append(block_item->timestamp());
    block_timeblock_height->Append(block_item->timeblock_height());
    block_bls_agg_sign_x->Append(common::Encode::HexEncode(view_block_item->qc().sign_x()));
    block_bls_agg_sign_y->Append(common::Encode::HexEncode(view_block_item->qc().sign_y()));
    block_date->Append(common::MicTimestampToDate(block_item->timestamp()));
    block_tx_size->Append(tx_list.size());

    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto& tx = tx_list[i];
        shard_id->Append(view_block_item->qc().network_id());
        pool_index->Append(view_block_item->qc().pool_index());
        height->Append(block_item->height());
        prehash->Append(common::Encode::HexEncode(view_block_item->parent_hash()));
        hash->Append(common::Encode::HexEncode(view_block_item->qc().view_block_hash()));
        version->Append(block_item->version());
        vss->Append(block_item->consistency_random());
        elect_height->Append(view_block_item->qc().elect_height());
        bitmap->Append(common::Encode::HexEncode(bitmap_str));
        commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
        timestamp->Append(block_item->timestamp());
        timeblock_height->Append(block_item->timeblock_height());
        bls_agg_sign_x->Append(common::Encode::HexEncode(view_block_item->qc().sign_x()));
        bls_agg_sign_y->Append(common::Encode::HexEncode(view_block_item->qc().sign_y()));
        date->Append(common::MicTimestampToDate(block_item->timestamp()));
        gid->Append(std::to_string(tx.nonce()));
        from->Append(common::Encode::HexEncode(tx.from()));
        from_pubkey->Append("");
        from_sign->Append("");
        to->Append(common::Encode::HexEncode(tx.to()));
        amount->Append(tx.amount());
        if (view_block_item->qc().network_id() == 2 && tx.step() == 5) {
            gas_limit->Append(0);
            gas_used->Append(0);
            gas_price->Append(0);
            type->Append(3);
        } else {
            gas_limit->Append(tx.gas_limit());
            gas_used->Append(tx.gas_used());
            gas_price->Append(tx.gas_price());
            type->Append(tx.step());
        }
        balance->Append(tx.balance());
        to_add->Append(false);
        attrs->Append("");
        status->Append(tx.status());
        tx_hash->Append(std::to_string(tx.nonce()));
        call_contract_step->Append(tx.step());
        std::string storage_str;
        // if (tx.storages_size() > 0 && tx.storages(0).value().size() < 2048) {
        //     storage_str = tx.storages(0).value();
        // }

        // if (tx.step() == pools::protobuf::kContractExcute) {
        //     for (int32_t st_idx = 0; st_idx < tx.storages_size(); ++st_idx) {
        //         if (tx.storages(st_idx).key().find(std::string("ars_create_agg_sign")) != std::string::npos && tx.storages(st_idx).value().size() < 2048) {
        //             storage_str += "," + tx.storages(st_idx).value();
        //             ZJC_DEBUG("success get key: %s, value: %s",
        //                 tx.storages(st_idx).key().c_str(), 
        //                 tx.storages(st_idx).value().c_str());
        //         }
        //     }
        // }
      
        storages->Append(common::Encode::HexEncode(storage_str));
        transfers->Append("");
        if (tx.step() == pools::protobuf::kNormalTo) {
            acc_account->Append(common::Encode::HexEncode(tx.to()));
            acc_shard_id->Append(view_block_item->qc().network_id());
            acc_pool_index->Append(view_block_item->qc().pool_index());
            acc_balance->Append(tx.balance());
        } else {
            acc_account->Append(common::Encode::HexEncode(tx.from()));
            acc_shard_id->Append(view_block_item->qc().network_id());
            acc_pool_index->Append(view_block_item->qc().pool_index());
            acc_balance->Append(tx.balance());
        }

        // for (int32_t j = 0; j < tx.storages_size(); ++j) {
        //     attr_account->Append(common::Encode::HexEncode(tx.from()));
        //     attr_tx_type->Append(tx.step());
        //     attr_to->Append(common::Encode::HexEncode(tx.to()));
        //     attr_shard_id->Append(view_block_item->qc().network_id());
        //     std::string val;
        //     attr_key->Append(common::Encode::HexEncode(tx.storages(j).key()));
        //     attr_value->Append(common::Encode::HexEncode(tx.storages(j).value()));
        //     ZJC_DEBUG("hash to ck add key: %s, val: %s", 
        //         tx.storages(j).key().c_str(), 
        //         "common::Encode::HexEncode(tx.storages(j).value()).c_str()");
        // }

        if (tx.step() == pools::protobuf::kContractExcute /*&& tx.to() == common::GlobalInfo::Instance()->c2c_to()*/) {
            {
                auto contract = tx.to();
                auto user = tx.from();
                prepay_contract->Append(common::Encode::HexEncode(contract));
                prepay_user->Append(common::Encode::HexEncode(user));
                prepay_height->Append(block_item->height());
                prepay_amount->Append(tx.balance());
                ZJC_DEBUG("success add prepayment contract: %s, address: %s, nonce: %lu, balance: %lu",
                    common::Encode::HexEncode(contract).c_str(), 
                    common::Encode::HexEncode(user).c_str(), 
                    tx.nonce(), 
                    tx.balance());
            }
            
            nlohmann::json res;
            ZJC_DEBUG("now handle contract: %s", ProtobufToJson(tx).c_str());
            bool ret = false;//QueryContract(tx.from(), tx.to(), &res);
            if (ret) {
                for (auto iter = res.begin(); iter != res.end(); ++iter) {
                    auto item = *iter;
                    c2c_r->Append(item["r"].get<std::string>());
                    c2c_seller->Append(item["a"].get<std::string>());
                    c2c_buyer->Append(item["b"].get<std::string>());
                    auto all = common::Encode::HexDecode(item["m"].get<std::string>());
                    evmc_bytes32 bytes32;
                    memcpy(bytes32.bytes, all.c_str(), 32);
                    uint64_t a = zjcvm::EvmcBytes32ToUint64(bytes32);
                    c2c_all->Append(a);
                    auto price = common::Encode::HexDecode(item["p"].get<std::string>());
                    memcpy(bytes32.bytes, price.c_str(), 32);
                    uint64_t p = zjcvm::EvmcBytes32ToUint64(bytes32);
                    c2c_now->Append(p);
                    uint32_t mr = item["mr"].get<bool>();
                    c2c_mc->Append(mr);
                    uint32_t sr = item["sr"].get<bool>();
                    c2c_sc->Append(sr);
                    uint32_t rp = item["rp"].get<bool>();
                    c2c_report->Append(rp);
                    auto order = common::Encode::HexDecode(item["o"].get<std::string>());
                    memcpy(bytes32.bytes, order.c_str(), 32);
                    uint64_t o = zjcvm::EvmcBytes32ToUint64(bytes32);
                    c2c_order_id->Append(o);
                    auto tmp_height = common::Encode::HexDecode(item["h"].get<std::string>());
                    memcpy(bytes32.bytes, tmp_height.c_str(), 32);
                    uint64_t h = zjcvm::EvmcBytes32ToUint64(bytes32);
                    c2c_height->Append(h);
                    auto amount = common::Encode::HexDecode(item["bm"].get<std::string>());
                    memcpy(bytes32.bytes, amount.c_str(), 32);
                    uint64_t am = zjcvm::EvmcBytes32ToUint64(bytes32);
                    c2c_amount->Append(am);
                    c2c_contract_addr->Append(common::Encode::HexEncode(tx.to()));
                }
            }
        }

        while (tx.step() == pools::protobuf::kConsensusLocalTos) {
            ZJC_DEBUG("now handle local to txs.");
            auto& to_txs = block_item->local_to();
            ZJC_DEBUG("now handle local to txs: %d", to_txs.tos_size());
            for (int32_t to_tx_idx = 0; to_tx_idx < to_txs.tos_size(); ++to_tx_idx) {
                ZJC_DEBUG("0 now handle local to idx: %d", to_tx_idx);
                if (to_txs.tos(to_tx_idx).to().size() == common::kUnicastAddressLength * 2) {
                    auto contract = to_txs.tos(to_tx_idx).to().substr(0, common::kUnicastAddressLength);
                    auto user = to_txs.tos(to_tx_idx).to().substr(common::kUnicastAddressLength, common::kUnicastAddressLength);
                    prepay_contract->Append(common::Encode::HexEncode(contract));
                    prepay_user->Append(common::Encode::HexEncode(user));
                    prepay_height->Append(block_item->height());
                    prepay_amount->Append(to_txs.tos(to_tx_idx).balance());
                    ZJC_DEBUG("success add prepayment contract: %s, address: %s, nonce: %lu, balance: %lu",
                        common::Encode::HexEncode(contract).c_str(), 
                        common::Encode::HexEncode(user).c_str(), 
                        tx.nonce(), 
                        to_txs.tos(to_tx_idx).balance());
                }

                if (to_txs.tos(to_tx_idx).to().size() != common::kUnicastAddressLength) {
                    //assert(false);
                    continue;
                }
                
                ZJC_DEBUG("1 now handle local to idx: %d", i);
                ZJC_DEBUG("now handle local to txs: %s", common::Encode::HexEncode(to_txs.tos(to_tx_idx).to()).c_str());
                shard_id->Append(view_block_item->qc().network_id());
                pool_index->Append(view_block_item->qc().pool_index());
                height->Append(block_item->height());
                prehash->Append(common::Encode::HexEncode(view_block_item->parent_hash()));
                hash->Append(common::Encode::HexEncode(view_block_item->qc().view_block_hash()));
                version->Append(block_item->version());
                ZJC_DEBUG("1 0 now handle local to idx: %d", i);
                vss->Append(block_item->consistency_random());
                elect_height->Append(view_block_item->qc().elect_height());
                bitmap->Append(common::Encode::HexEncode(bitmap_str));
                commit_bitmap->Append(common::Encode::HexEncode(commit_bitmap_str));
                timestamp->Append(block_item->timestamp());
                timeblock_height->Append(block_item->timeblock_height());
                ZJC_DEBUG("1 1 now handle local to idx: %d", i);
                bls_agg_sign_x->Append(common::Encode::HexEncode(view_block_item->qc().sign_x()));
                bls_agg_sign_y->Append(common::Encode::HexEncode(view_block_item->qc().sign_y()));
                ZJC_DEBUG("1 2 now handle local to idx: %d", i);
                date->Append(common::MicTimestampToDate(block_item->timestamp()));
                ZJC_DEBUG("1 2 0 now handle local to idx: %d", i);
                gid->Append(std::to_string(tx.nonce()));
                ZJC_DEBUG("1 2 1 now handle local to idx: %d", i);
                from->Append("");
                from_pubkey->Append("");
                from_sign->Append("");
                ZJC_DEBUG("1 2 2 now handle local to idx: %d", i);
                to->Append(common::Encode::HexEncode(to_txs.tos(to_tx_idx).to()));
                ZJC_DEBUG("1 3 now handle local to idx: %d", i);
                amount->Append(0);
                gas_limit->Append(0);
                gas_used->Append(0);
                gas_price->Append(0);
                type->Append(tx.step());
                balance->Append(to_txs.tos(to_tx_idx).balance());
                ZJC_DEBUG("1 4 now handle local to idx: %d", i);
                to_add->Append(true);
                attrs->Append("");
                status->Append(tx.status());
                tx_hash->Append(std::to_string(tx.nonce()));
                call_contract_step->Append(tx.step());
                storages->Append("");
                transfers->Append("");

                ZJC_DEBUG("2 now handle local to idx: %d", i);
                acc_account->Append(common::Encode::HexEncode(to_txs.tos(to_tx_idx).to()));
                acc_shard_id->Append(view_block_item->qc().network_id());
                acc_pool_index->Append(view_block_item->qc().pool_index());
                acc_balance->Append(to_txs.tos(to_tx_idx).balance());
                ZJC_DEBUG("3 now handle local to idx: %d", i);
            }

            break;
        }
    }

    ++batch_count_;
    // ZJC_INFO("%u, add new ck block %u_%u_%lu", idx++, view_block_item->qc().network_id(), view_block_item->qc().pool_index(), block_item->height());
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhouseTransTableName + " FINAL");
    // ZJC_INFO("%u, add new ck block %u_%u_%lu", idx++, view_block_item->qc().network_id(), view_block_item->qc().pool_index(), block_item->height());
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhouseBlockTableName + " FINAL");
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhouseAccountTableName + " FINAL");
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhouseAccountKvTableName + " FINAL");
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhouseC2cTableName + " FINAL");
    // ck_client.Execute(std::string("optimize TABLE ") + kClickhousePrepaymentTableName + " FINAL");
#ifndef NDEBUG
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        ZJC_DEBUG("ck success new block coming sharding id: %u_%d_%lu, "
            "tx size: %u, hash: %s, elect height: %lu, "
            "tm height: %lu, nonce: %lu, status: %d, step: %d",
            view_block_item->qc().network_id(),
            view_block_item->qc().pool_index(),
            block_item->height(),
            block_item->tx_list_size(),
            common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
            view_block_item->qc().elect_height(),
            block_item->timeblock_height(),
            tx_list[i].nonce(),
            tx_list[i].status(),
            tx_list[i].step());
    }
#endif
    return true;
}
// catch (std::exception& e) {
//     ZJC_ERROR("add new block failed[%s]", e.what());
//     return false;
// }

void ClickHouseClient::FlushToCkWithData() try {
    int64_t now_tm_ms = common::TimeUtils::TimestampMs();
    if (batch_count_ >= kBatchCountToCk || (pre_time_out_ + 1000 < now_tm_ms)) {
        if (batch_count_ > 0) {
            clickhouse::Block trans;
            clickhouse::Block blocks;
            clickhouse::Block accounts;
            clickhouse::Block account_attrs;
            clickhouse::Block c2cs;
            clickhouse::Block prepay;
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

            c2cs.AppendColumn("seller", c2c_seller);
            c2cs.AppendColumn("all", c2c_all);
            c2cs.AppendColumn("now", c2c_now);
            c2cs.AppendColumn("receivable", c2c_r);
            c2cs.AppendColumn("mchecked", c2c_mc);
            c2cs.AppendColumn("schecked", c2c_sc);
            c2cs.AppendColumn("reported", c2c_report);
            c2cs.AppendColumn("orderId", c2c_order_id);
            c2cs.AppendColumn("height", c2c_height);
            c2cs.AppendColumn("buyer", c2c_buyer);
            c2cs.AppendColumn("amount", c2c_amount);
            c2cs.AppendColumn("contract", c2c_contract_addr);

            prepay.AppendColumn("contract", prepay_contract);
            prepay.AppendColumn("user", prepay_user);
            prepay.AppendColumn("prepayment", prepay_amount);
            prepay.AppendColumn("height", prepay_height);

            uint32_t idx = 0;
            clickhouse::Client ck_client(clickhouse::ClientOptions().
                SetHost(common::GlobalInfo::Instance()->ck_host()).
                SetPort(common::GlobalInfo::Instance()->ck_port()).
                SetUser(common::GlobalInfo::Instance()->ck_user()).
                SetPassword(common::GlobalInfo::Instance()->ck_pass()));
            ck_client.Insert(kClickhouseTransTableName, trans);
            ck_client.Insert(kClickhouseBlockTableName, blocks);
            ck_client.Insert(kClickhouseAccountTableName, accounts);
            ck_client.Insert(kClickhouseAccountKvTableName, account_attrs);
            ck_client.Insert(kClickhouseC2cTableName, c2cs);
            ck_client.Insert(kClickhousePrepaymentTableName, prepay);
        }

        HandleBlsMessage();
        ZJC_DEBUG("success flush to db: %u", batch_count_);
        batch_count_ = 0;
        pre_time_out_ = now_tm_ms;
        ResetColumns();
    }
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
}

bool ClickHouseClient::QueryContract(const std::string& from, const std::string& contract_addr, nlohmann::json* res) {
    zjcvm::ZjchainHost zjc_host;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = 0;
    zjc_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = 0;
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.contract_mgr_ = contract_mgr_;
    zjc_host.my_address_ = contract_addr;
    zjc_host.tx_context_.block_gas_limit = 10000000000lu;
    // user caller prepayment 's gas
    uint64_t from_balance = 10000000000lu;
    auto contract_addr_info = prefix_db_->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        ZJC_DEBUG("failed get contract.");
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
        common::Encode::HexDecode("cdfd45bb"),
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
        ZJC_INFO("query contract error: %s.", res.c_str());
        return false;
    }

    std::string qdata((char*)result.output_data, result.output_size);
    evmc_bytes32 len_bytes;
    memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    uint64_t len = zjcvm::EvmcBytes32ToUint64(len_bytes);
    std::string http_res(qdata.c_str() + 64, len);
    *res = nlohmann::json::parse(http_res);
    ZJC_DEBUG("success query contract: %s", res->dump().c_str());
    return true;
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
    ZJC_DEBUG("create table now [%s][%u][%s][%s]\n",
        common::GlobalInfo::Instance()->ck_host().c_str(),
        common::GlobalInfo::Instance()->ck_port(),
        common::GlobalInfo::Instance()->ck_user().c_str(),
        common::GlobalInfo::Instance()->ck_pass().c_str());
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreateC2cTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseC2cTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`seller` String COMMENT 'seller' CODEC(LZ4), "
        "`buyer` String COMMENT 'buyer' CODEC(LZ4), "
        "`contract` String COMMENT 'contract' CODEC(LZ4), "
        "`amount` UInt64 COMMENT 'amount' CODEC(LZ4), "
        "`receivable` String COMMENT 'receivable' CODEC(LZ4), "
        "`all` UInt64 COMMENT 'all' CODEC(LZ4), "
        "`now` UInt64 COMMENT 'now' CODEC(LZ4), "
        "`mchecked` UInt32 COMMENT 'mchecked' CODEC(LZ4), "
        "`schecked` UInt32 COMMENT 'schecked' CODEC(LZ4), "
        "`reported` UInt32 COMMENT 'reported' CODEC(LZ4), "
        "`orderId` UInt64 COMMENT 'orderId' CODEC(LZ4), "
        "`height` UInt64 COMMENT 'height' CODEC(LZ4), "
        "`update` DateTime DEFAULT now() COMMENT 'update' "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(seller) "
        "ORDER BY(seller, orderId) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Execute(create_cmd);
    return true;
}

bool ClickHouseClient::CreatePrepaymentTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhousePrepaymentTableName + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`contract` String COMMENT 'contract' CODEC(LZ4), "
        "`user` String COMMENT 'user' CODEC(LZ4), "
        "`prepayment` UInt64 COMMENT 'prepayment' CODEC(LZ4), "
        "`height` UInt64 COMMENT 'height' CODEC(LZ4), "
        "`update` DateTime DEFAULT now() COMMENT 'update' "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(contract) "
        "ORDER BY(contract, user) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Execute(create_cmd);
    return true;
}

void ClickHouseClient::ResetColumns() {
    shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    height = std::make_shared<clickhouse::ColumnUInt64>();
    prehash = std::make_shared<clickhouse::ColumnString>();
    hash = std::make_shared<clickhouse::ColumnString>();
    version = std::make_shared<clickhouse::ColumnUInt32>();
    vss = std::make_shared<clickhouse::ColumnUInt64>();
    elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    bitmap = std::make_shared<clickhouse::ColumnString>();
    timestamp = std::make_shared<clickhouse::ColumnUInt64>();
    timeblock_height = std::make_shared<clickhouse::ColumnUInt64>();
    bls_agg_sign_x = std::make_shared<clickhouse::ColumnString>();
    bls_agg_sign_y = std::make_shared<clickhouse::ColumnString>();
    commit_bitmap = std::make_shared<clickhouse::ColumnString>();
    gid = std::make_shared<clickhouse::ColumnString>();
    from = std::make_shared<clickhouse::ColumnString>();
    from_pubkey = std::make_shared<clickhouse::ColumnString>();
    from_sign = std::make_shared<clickhouse::ColumnString>();
    to = std::make_shared<clickhouse::ColumnString>();
    amount = std::make_shared<clickhouse::ColumnUInt64>();
    gas_limit = std::make_shared<clickhouse::ColumnUInt64>();
    gas_used = std::make_shared<clickhouse::ColumnUInt64>();
    gas_price = std::make_shared<clickhouse::ColumnUInt64>();
    balance = std::make_shared<clickhouse::ColumnUInt64>();
    to_add = std::make_shared<clickhouse::ColumnUInt32>();
    type = std::make_shared<clickhouse::ColumnUInt32>();
    attrs = std::make_shared<clickhouse::ColumnString>();
    status = std::make_shared<clickhouse::ColumnUInt32>();
    tx_hash = std::make_shared<clickhouse::ColumnString>();
    call_contract_step = std::make_shared<clickhouse::ColumnUInt32>();
    storages = std::make_shared<clickhouse::ColumnString>();
    transfers = std::make_shared<clickhouse::ColumnString>();
    date = std::make_shared<clickhouse::ColumnUInt32>();

    block_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    block_pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    block_height = std::make_shared<clickhouse::ColumnUInt64>();
    block_prehash = std::make_shared<clickhouse::ColumnString>();
    block_hash = std::make_shared<clickhouse::ColumnString>();
    block_version = std::make_shared<clickhouse::ColumnUInt32>();
    block_vss = std::make_shared<clickhouse::ColumnUInt64>();
    block_elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    block_bitmap = std::make_shared<clickhouse::ColumnString>();
    block_timestamp = std::make_shared<clickhouse::ColumnUInt64>();
    block_timeblock_height = std::make_shared<clickhouse::ColumnUInt64>();
    block_bls_agg_sign_x = std::make_shared<clickhouse::ColumnString>();
    block_bls_agg_sign_y = std::make_shared<clickhouse::ColumnString>();
    block_commit_bitmap = std::make_shared<clickhouse::ColumnString>();
    block_tx_size = std::make_shared<clickhouse::ColumnUInt32>();
    block_date = std::make_shared<clickhouse::ColumnUInt32>();

    acc_account = std::make_shared<clickhouse::ColumnString>();
    acc_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    acc_pool_index = std::make_shared<clickhouse::ColumnUInt32>();
    acc_balance = std::make_shared<clickhouse::ColumnUInt64>();

    attr_account = std::make_shared<clickhouse::ColumnString>();
    attr_to = std::make_shared<clickhouse::ColumnString>();
    attr_shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    attr_tx_type = std::make_shared<clickhouse::ColumnUInt32>();
    attr_key = std::make_shared<clickhouse::ColumnString>();
    attr_value = std::make_shared<clickhouse::ColumnString>();

    c2c_r = std::make_shared<clickhouse::ColumnString>();
    c2c_seller = std::make_shared<clickhouse::ColumnString>();
    c2c_all = std::make_shared<clickhouse::ColumnUInt64>();
    c2c_now = std::make_shared<clickhouse::ColumnUInt64>();
    c2c_mc = std::make_shared<clickhouse::ColumnUInt32>();
    c2c_sc = std::make_shared<clickhouse::ColumnUInt32>();
    c2c_report = std::make_shared<clickhouse::ColumnUInt32>();
    c2c_order_id = std::make_shared<clickhouse::ColumnUInt64>();
    c2c_height = std::make_shared<clickhouse::ColumnUInt64>();
    c2c_buyer = std::make_shared<clickhouse::ColumnString>();
    c2c_amount = std::make_shared<clickhouse::ColumnUInt64>();
    c2c_contract_addr = std::make_shared<clickhouse::ColumnString>();

    prepay_contract = std::make_shared<clickhouse::ColumnString>();
    prepay_user = std::make_shared<clickhouse::ColumnString>();
    prepay_amount = std::make_shared<clickhouse::ColumnUInt64>();
    prepay_height = std::make_shared<clickhouse::ColumnUInt64>();
}

bool ClickHouseClient::CreateBlsElectInfoTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseBlsElectInfo + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`elect_height` UInt64 COMMENT '' CODEC(T64, LZ4), "
        "`shard_id` UInt32 COMMENT '' CODEC(T64, LZ4), "
        "`member_idx` UInt32 COMMENT '' CODEC(T64, LZ4), "
        "`local_pri_keys` String COMMENT  '' CODEC(LZ4),"
        "`local_pub_keys` String COMMENT  '' CODEC(LZ4),"
        "`swap_sec_keys` String COMMENT  '' CODEC(LZ4),"
        "`local_sk` String COMMENT  '' CODEC(LZ4),"
        "`common_pk` String COMMENT  '' CODEC(LZ4),"
        "`update` DateTime DEFAULT now() COMMENT 'update' "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id) "
        "ORDER BY(elect_height, member_idx) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Execute(create_cmd);
    return true;    
}

bool ClickHouseClient::InsertBlsElectInfo(const BlsElectInfo& info) try {
    bls_elect_queue_.push(std::make_shared<BlsElectInfo>(info));
    ZJC_DEBUG("insert elect bls success: %d", bls_elect_queue_.size());
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
    return false;
}

bool ClickHouseClient::CreateBlsBlockInfoTable() {
    std::string create_cmd = std::string("CREATE TABLE if not exists ") + kClickhouseBlsBlockInfo + " ( "
        "`id` UInt64 COMMENT 'id' CODEC(T64, LZ4), "
        "`elect_height` UInt64 COMMENT '' CODEC(T64, LZ4), "
        "`view` UInt64 COMMENT '' CODEC(T64, LZ4), "
        "`shard_id` UInt32 COMMENT '' CODEC(T64, LZ4), "
        "`pool_idx` UInt32 COMMENT '' CODEC(T64, LZ4), "
        "`leader_idx` UInt32 COMMENT '' CODEC(T64, LZ4), "
        "`msg_hash` String COMMENT  '' CODEC(LZ4),"
        "`partial_sign_map` String COMMENT  '' CODEC(LZ4),"
        "`reconstructed_sign` String COMMENT  '' CODEC(LZ4),"
        "`common_pk` String COMMENT  '' CODEC(LZ4),"
        "`update` DateTime DEFAULT now() COMMENT 'update' "
        ") "
        "ENGINE = ReplacingMergeTree "
        "PARTITION BY(shard_id) "
        "ORDER BY(elect_height, view) "
        "SETTINGS index_granularity = 8192;";
    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Execute(create_cmd);
    return true;
}

void ClickHouseClient::HandleBlsMessage() {
    ZJC_DEBUG("call elect bls success.");
    std::shared_ptr<BlsBlockInfo> bls_block;
    while (bls_block_queue_.pop(&bls_block)) {
        HandleBlsBlockMessage(*bls_block);
    }

    ZJC_DEBUG("call elect bls success: %d", bls_elect_queue_.size());
    std::shared_ptr<BlsElectInfo> elect_block;
    while (bls_elect_queue_.pop(&elect_block)) {
        ZJC_DEBUG("success pop elect bls success.");
        HandleBlsElectMessage(*elect_block);
    }
}

void ClickHouseClient::HandleBlsBlockMessage(const BlsBlockInfo& info) {
    auto elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto view = std::make_shared<clickhouse::ColumnUInt64>();
    auto shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto pool_idx = std::make_shared<clickhouse::ColumnUInt32>();
    auto leader_idx = std::make_shared<clickhouse::ColumnUInt32>();
    auto msg_hash = std::make_shared<clickhouse::ColumnString>();
    auto partial_sign_map = std::make_shared<clickhouse::ColumnString>();
    auto reconstructed_sign = std::make_shared<clickhouse::ColumnString>();
    auto common_pk = std::make_shared<clickhouse::ColumnString>();

    elect_height->Append(info.elect_height);
    view->Append(info.view);
    shard_id->Append(info.shard_id);
    pool_idx->Append(info.pool_idx);
    leader_idx->Append(info.leader_idx);
    msg_hash->Append(info.msg_hash);
    partial_sign_map->Append(info.partial_sign_map);
    reconstructed_sign->Append(info.reconstructed_sign);
    common_pk->Append(info.common_pk);

    clickhouse::Block item;
    item.AppendColumn("elect_height", elect_height);
    item.AppendColumn("view", view);
    item.AppendColumn("shard_id", shard_id);
    item.AppendColumn("pool_idx", pool_idx);
    item.AppendColumn("leader_idx", leader_idx);
    item.AppendColumn("msg_hash", msg_hash);
    item.AppendColumn("partial_sign_map", partial_sign_map);
    item.AppendColumn("reconstructed_sign", reconstructed_sign);
    item.AppendColumn("common_pk", common_pk);

    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Insert(kClickhouseBlsBlockInfo, item);
}

void ClickHouseClient::HandleBlsElectMessage(const BlsElectInfo& info) try {
    ZJC_DEBUG("success handle elect bls success.");
    auto elect_height = std::make_shared<clickhouse::ColumnUInt64>();
    auto member_idx = std::make_shared<clickhouse::ColumnUInt32>();
    auto shard_id = std::make_shared<clickhouse::ColumnUInt32>();
    auto local_pri_keys = std::make_shared<clickhouse::ColumnString>();
    auto local_pub_keys = std::make_shared<clickhouse::ColumnString>();
    auto swap_sec_keys = std::make_shared<clickhouse::ColumnString>();
    auto local_sk = std::make_shared<clickhouse::ColumnString>();
    auto common_pk = std::make_shared<clickhouse::ColumnString>();

    elect_height->Append(info.elect_height);
    member_idx->Append(info.member_idx);
    shard_id->Append(info.shard_id);
    local_pri_keys->Append(info.local_pri_keys);
    local_pub_keys->Append(info.local_pub_keys);
    swap_sec_keys->Append(info.swaped_sec_keys);
    local_sk->Append(info.local_sk);
    common_pk->Append(info.common_pk);

    clickhouse::Block item;
    item.AppendColumn("elect_height", elect_height);
    item.AppendColumn("member_idx", member_idx);
    item.AppendColumn("shard_id", shard_id);
    item.AppendColumn("local_pri_keys", local_pri_keys);
    item.AppendColumn("local_pub_keys", local_pub_keys);
    item.AppendColumn("swap_sec_keys", swap_sec_keys);
    item.AppendColumn("local_sk", local_sk);
    item.AppendColumn("common_pk", common_pk);

    ZJC_DEBUG("success insert bls elect info elect_hegiht_: %lu, "
        "local_member_index_: %u, shard_id: %u, local_pri_keys: %s, "
        "local_pub_keys: %s, local_sk: %s, common_pk: %s, swaped_sec_keys: %s", 
        info.elect_height, info.member_idx, info.shard_id, 
        info.local_pri_keys.c_str(), info.local_pub_keys.c_str(), 
        info.local_sk.c_str(), info.common_pk.c_str(), info.swaped_sec_keys.c_str());

    clickhouse::Client ck_client(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client.Insert(kClickhouseBlsElectInfo, item);
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
}

bool ClickHouseClient::InsertBlsBlockInfo(const BlsBlockInfo& info) try {
    bls_block_queue_.push(std::make_shared<BlsBlockInfo>(info));
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
    return false;
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
    CreatePrepaymentTable();
    CreateBlsElectInfoTable();
    CreateBlsBlockInfoTable();    
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
    clickhouse::Client ck_client0(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client0.Select(cmd, [&all_transactions](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            all_transactions = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select count(*) from zjc_ck_account_table;";
    uint32_t all_address = 0;
    clickhouse::Client ck_client1(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client1.Select(cmd, [&all_address](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            all_address = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select sum(balance) from zjc_ck_account_table;";
    uint64_t sum_balance = 0;
    clickhouse::Client ck_client2(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client2.Select(cmd, [&sum_balance](const clickhouse::Block& ck_block) {
        if (ck_block.GetRowCount() > 0) {
            sum_balance = (*ck_block[0]->As<clickhouse::ColumnUInt64>())[0];
        }
    });

    cmd = "select count(*) from zjc_ck_account_key_value_table where type = 4 and key = '5f5f636279746573636f6465'";
    uint32_t all_contracts = 0;
    clickhouse::Client ck_client3(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
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
    clickhouse::Client ck_client4(clickhouse::ClientOptions().
        SetHost(common::GlobalInfo::Instance()->ck_host()).
        SetPort(common::GlobalInfo::Instance()->ck_port()).
        SetUser(common::GlobalInfo::Instance()->ck_user()).
        SetPassword(common::GlobalInfo::Instance()->ck_pass()));
    ck_client4.Insert(kClickhouseStatisticTableName, statistics);
} catch (std::exception& e) {
    ZJC_ERROR("add new block failed[%s]", e.what());
}

};  // namespace ck

};  // namespace shardora
