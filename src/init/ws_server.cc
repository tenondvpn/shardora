#include "init/ws_server.h"

#include "common/encode.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/time_utils.h"
#include "consensus/consensus_utils.h"
#include "protos/transport.pb.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace init {

WsServer::WsServer() {}

WsServer::~WsServer() {}

int WsServer::Init(
        std::shared_ptr<protos::PrefixDb> prefix_db, 
        std::shared_ptr<security::Security> security,
        transport::MultiThreadHandler* net_handler) {
    prefix_db_ = prefix_db;
    security_ = security;
    net_handler_ = net_handler;
    latest_timestamp_ = 0;
    GetAllTxs();
    std::vector<std::shared_ptr<ws::protobuf::SellInfo>> sells;
    std::vector<std::shared_ptr<ws::protobuf::SellInfo>> orders;
    prefix_db_->GetAllSellout(&sells);
    prefix_db_->GetAllOrder(&orders);
    for (auto iter = orders.begin(); iter != orders.end(); ++iter) {
        order_map_[(*iter)->buyer()] = *iter;
        ZJC_DEBUG("success init get order: %s, buyer: %s, all: %lu, price: %lu, status: %d", 
            common::Encode::HexEncode((*iter)->seller()).c_str(), 
            common::Encode::HexEncode((*iter)->buyer()).c_str(),
            (*iter)->all(), 
            (*iter)->price(),
            (*iter)->status());
    }

    for (auto iter = sells.begin(); iter != sells.end(); ++iter) {
        sell_map_[(*iter)->seller()] = *iter;
        ZJC_DEBUG("success init get sell: %s, buyer: %s, all: %lu, price: %lu, status: %d", 
            common::Encode::HexEncode((*iter)->seller()).c_str(), 
            common::Encode::HexEncode((*iter)->buyer()).c_str(),
            (*iter)->all(), 
            (*iter)->price(),
            (*iter)->status());
    }

    if (GetAllC2cs() != 0) {
        return 1;
    }

    refresh_hdls_tick_.CutOff(
            1000000, 
            std::bind(&WsServer::BroadcastTxInfo, this, std::placeholders::_1));
    return StartWebsocket();
}

void WsServer::BroadcastTxInfo(uint8_t thread_idx) {
    ws::protobuf::WsMessage ws_tx_res;
    GetTxs(ws_tx_res);
    GetC2cs(ws_tx_res);
    GetPrepayment(ws_tx_res);
    CheckC2cStatus(ws_tx_res);
    if (ws_tx_res.txs_size() > 0 || ws_tx_res.has_init_info()) {
        // std::lock_guard<std::mutex> g(refresh_hdls_mutex_);
        ZJC_INFO("success broadcast ws message: %d, %d", ws_tx_res.txs_size(), refresh_hdls_.size());
        std::string msg = common::Encode::HexEncode(ws_tx_res.SerializeAsString());
        for (auto iter = refresh_hdls_.begin(); iter != refresh_hdls_.end(); ++iter) {
            websocketpp::connection_hdl hdl = *iter;
            ws_server_.Send(hdl, msg.c_str(), msg.size());
        }
    }
    
    refresh_hdls_tick_.CutOff(
        3000000llu, 
        std::bind(&WsServer::BroadcastTxInfo, this, std::placeholders::_1));
}

void WsServer::GetAllTxs() {
    uint32_t got_count = 0;
    do {
        std::string cmd = "select  \"from\", to, amount, balance, height, to_add, timestamp, status, type from zjc_ck_transaction_table where shard_id = 3 and type != 8 and balance > 0 and balance < 10000000000000 and timestamp > " + std::to_string(latest_timestamp_) + " limit 1000;";
        uint32_t all_transactions = 0;
        try {
            clickhouse::Client ck_client0(clickhouse::ClientOptions().
                SetHost(common::GlobalInfo::Instance()->ck_host()).
                SetPort(common::GlobalInfo::Instance()->ck_port()).
                SetUser(common::GlobalInfo::Instance()->ck_user()).
                SetPassword(common::GlobalInfo::Instance()->ck_pass()));
            ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
                ZJC_INFO("run cmd: %s, get count: %d", cmd.c_str(), ck_block.GetRowCount());
                if (ck_block.GetRowCount() > got_count) {
                    got_count = ck_block.GetRowCount();
                }

                for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                    std::string from_str(ck_block[0]->As<clickhouse::ColumnString>()->At(i));
                    std::string to_str(ck_block[1]->As<clickhouse::ColumnString>()->At(i));
                    auto balance = ck_block[3]->As<clickhouse::ColumnUInt64>()->At(i);
                    auto timestamp = ck_block[6]->As<clickhouse::ColumnUInt64>()->At(i);
                    auto to_add = ck_block[5]->As<clickhouse::ColumnUInt32>()->At(i) == 0 ? false : true;
                    if (timestamp > latest_timestamp_) {
                        latest_timestamp_ = timestamp;
                    }

                    auto user_info = std::make_shared<UserInfoItem>();
                    if (to_add) {
                        user_info->id = common::Encode::HexDecode(to_str);
                    } else {
                        user_info->id = common::Encode::HexDecode(from_str);
                    }

                    user_info->balance = balance;
                    auto type = ck_block[8]->As<clickhouse::ColumnUInt32>()->At(i);
                    if (type != ws::protobuf::kContractExcute) {
                        user_balance_[user_info->id] = user_info->balance;
                    }

                    ZJC_INFO("new tx coming: %s, %s, %lu, realid: %s", 
                        from_str.c_str(), to_str.c_str(), user_info->balance, common::Encode::HexEncode(user_info->id).c_str());
                }
            });
        } catch (std::exception& e) {
            ZJC_ERROR("catch error: %s", e.what());
        }
    } while (got_count >= 1000);
}

void WsServer::GetTxs(ws::protobuf::WsMessage& ws_tx_res) {
    std::string cmd = "select  \"from\", to, amount, balance, height, to_add, timestamp, status, type from zjc_ck_transaction_table where shard_id = 3 and type != 8 and balance > 0 and balance < 10000000000000 and timestamp > " + std::to_string(latest_timestamp_) + " limit 1000;";
    uint32_t all_transactions = 0;
    try {
        clickhouse::Client ck_client0(clickhouse::ClientOptions().
            SetHost(common::GlobalInfo::Instance()->ck_host()).
            SetPort(common::GlobalInfo::Instance()->ck_port()).
            SetUser(common::GlobalInfo::Instance()->ck_user()).
            SetPassword(common::GlobalInfo::Instance()->ck_pass()));
        ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
            ZJC_INFO("run cmd: %s, get count: %d", cmd.c_str(), ck_block.GetRowCount());
            for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                auto* ws_item = ws_tx_res.add_txs();
                std::string from_str(ck_block[0]->As<clickhouse::ColumnString>()->At(i));
                ws_item->set_from(common::Encode::HexDecode(from_str));
                std::string to_str(ck_block[1]->As<clickhouse::ColumnString>()->At(i));
                ws_item->set_to(common::Encode::HexDecode(to_str));
                ws_item->set_amount(ck_block[2]->As<clickhouse::ColumnUInt64>()->At(i));
                ws_item->set_balance(ck_block[3]->As<clickhouse::ColumnUInt64>()->At(i));
                ws_item->set_height(ck_block[4]->As<clickhouse::ColumnUInt64>()->At(i));
                ws_item->set_to_add(ck_block[5]->As<clickhouse::ColumnUInt32>()->At(i) == 0 ? false : true);
                auto item = ck_block[6];
                ws_item->set_timestamp(ck_block[6]->As<clickhouse::ColumnUInt64>()->At(i));
                auto item2 = ck_block[7];
                ws_item->set_status(ck_block[7]->As<clickhouse::ColumnUInt32>()->At(i));
                if (ws_item->timestamp() > latest_timestamp_) {
                    latest_timestamp_ = ws_item->timestamp();
                }

                auto user_info = std::make_shared<UserInfoItem>();
                if (ws_item->to_add() > 0) {
                    user_info->id = ws_item->to();
                } else {
                    user_info->id = ws_item->from();
                }

                user_info->balance = ws_item->balance();
                auto type = ck_block[8]->As<clickhouse::ColumnUInt32>()->At(i);
                if (type != ws::protobuf::kContractExcute) {
                    user_info_queue_.push(user_info);
                }

                ZJC_INFO("new tx coming: %s, %s, %lu, %lu, realid: %s", 
                    from_str.c_str(), to_str.c_str(), ws_item->amount(), 
                    ws_item->balance(), common::Encode::HexEncode(user_info->id).c_str());
            }
        });
    } catch (std::exception& e) {
        ZJC_ERROR("catch error: %s", e.what());
    }
}

void WsServer::GetPrepayment(ws::protobuf::WsMessage& ws_tx_res) {
    std::string cmd = "select user, prepayment, height from zjc_ck_prepayment_table where contract = '" + 
        common::GlobalInfo::Instance()->c2c_contract_addr() + 
        "' and height > " + std::to_string(latest_prepayment_height_) + " limit 1000;";
    try {
        clickhouse::Client ck_client0(clickhouse::ClientOptions().
            SetHost(common::GlobalInfo::Instance()->ck_host()).
            SetPort(common::GlobalInfo::Instance()->ck_port()).
            SetUser(common::GlobalInfo::Instance()->ck_user()).
            SetPassword(common::GlobalInfo::Instance()->ck_pass()));
        ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
            ZJC_INFO("run cmd: %s, get count: %d", cmd.c_str(), ck_block.GetRowCount());
            for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                std::string user = common::Encode::HexDecode(std::string(ck_block[0]->As<clickhouse::ColumnString>()->At(i)));
                auto prepayment = ck_block[1]->As<clickhouse::ColumnUInt64>()->At(i);
                auto height = ck_block[2]->As<clickhouse::ColumnUInt64>()->At(i);
                if (latest_prepayment_height_ < height) {
                    latest_prepayment_height_ = height;
                }

                auto user_info = std::make_shared<UserInfoItem>();
                user_info->id = user;
                user_info->prepayment = prepayment;
                user_info_queue_.push(user_info);
                {
                    std::lock_guard g(sell_map_mutex_);
                    auto iter = sell_map_.find(user_info->id);
                    if (iter != sell_map_.end()) {
                        if (iter->second->status() == ws::protobuf::kSellWaitingPrepayment ||
                                iter->second->status() == ws::protobuf::kSellTxPrepaymentError) {
                            if (iter->second->all() <= prepayment) {
                                iter->second->set_status(ws::protobuf::kSellPrepayment);
                                prefix_db_->SaveSellout(user, *iter->second);
                                auto* sellinfo = ws_tx_res.mutable_init_info()->mutable_c2c()->add_sells();
                                *sellinfo = *iter->second;
                            }
                        }
                    }
                }

                ZJC_INFO("new prepayment coming: %s, %lu, %lu", 
                    common::Encode::HexEncode(user_info->id).c_str(), prepayment, height);
            }
        });
    } catch (std::exception& e) {
        ZJC_ERROR("catch error: %s", e.what());
    }
}

int WsServer::GetAllC2cs() {
    uint32_t got_count = 0;
    int status = 0;
    do {
        std::string cmd = "select seller, buyer, amount, receivable, all, now, mchecked, schecked, reported, orderId, height from zjc_ck_c2c_table where contract='" + 
            common::GlobalInfo::Instance()->c2c_contract_addr() + "' and height > " + std::to_string(max_c2c_height_) +  " order by height asc limit 1000;";
        uint32_t all_transactions = 0;
        try {
            clickhouse::Client ck_client0(clickhouse::ClientOptions().
                SetHost(common::GlobalInfo::Instance()->ck_host()).
                SetPort(common::GlobalInfo::Instance()->ck_port()).
                SetUser(common::GlobalInfo::Instance()->ck_user()).
                SetPassword(common::GlobalInfo::Instance()->ck_pass()));
            ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
                ZJC_INFO("run cmd: %s, get count: %d", cmd.c_str(), ck_block.GetRowCount());
                for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                    std::string user(ck_block[0]->As<clickhouse::ColumnString>()->At(i));
                    std::string encode_buyer(ck_block[1]->As<clickhouse::ColumnString>()->At(i));
                    auto amount = ck_block[2]->As<clickhouse::ColumnUInt64>()->At(i);
                    auto all = ck_block[4]->As<clickhouse::ColumnUInt64>()->At(i);
                    auto now = ck_block[5]->As<clickhouse::ColumnUInt64>()->At(i);
                    auto mchecked = ck_block[6]->As<clickhouse::ColumnUInt32>()->At(i);
                    auto schecked = ck_block[7]->As<clickhouse::ColumnUInt32>()->At(i);
                    auto reported = ck_block[8]->As<clickhouse::ColumnUInt32>()->At(i);
                    auto height = ck_block[10]->As<clickhouse::ColumnUInt64>()->At(i);
                    if (max_c2c_height_ < height) {
                        max_c2c_height_ = height;
                    }

                    auto id = common::Encode::HexDecode(user);
                    auto buyer = common::Encode::HexDecode(encode_buyer);
                    auto iter = sell_map_.find(id);
                    if (iter == sell_map_.end()) {
                        ZJC_ERROR("local sell info error: %s!", user.c_str());
                        continue;
                    }

                    ZJC_INFO("get all c2c seller: %s, buyer: %s, amount: %lu, height: %lu, exists height: %lu, status: %d, max height: %lu", 
                        user.c_str(), encode_buyer.c_str(), amount, height, iter->second->height(), iter->second->status(), max_c2c_height_);
                    auto order_iter = order_map_.find(buyer);
                    if (iter->second->height() > height) {
                        ZJC_ERROR("local sell info height error: %s %lu, %lu!", user.c_str(), iter->second->height(), height);
                        continue;
                    }

                    bool changed = false;
                    if (iter->second->buyer() != buyer) {
                        changed = true;
                        iter->second->set_buyer(buyer);
                    }

                    if (iter->second->amount() != amount) {
                        changed = true;
                        iter->second->set_amount(amount);
                    }

                    if (iter->second->all() != all) {
                        changed = true;
                        iter->second->set_all(all);
                    }

                    if (iter->second->now() != now) {
                        changed = true;
                        iter->second->set_now(now);
                    }

                    if (iter->second->mchecked() != mchecked) {
                        changed = true;
                        iter->second->set_mchecked(mchecked);
                    }

                    if (iter->second->reported() != reported) {
                        changed = true;
                        iter->second->set_reported(reported);
                    }

                    if (iter->second->height() != height) {
                        changed = true;
                        iter->second->set_height(height);
                    }

                    if (iter->second->schecked() != schecked) {
                        changed = true;
                        iter->second->set_schecked(schecked);
                    }

                    if (iter->second->schecked() > 0 && iter->second->mchecked() > 0) {
                        if (iter->second->status() != ws::protobuf::Status::kSellReleased) {
                            changed = true;
                            iter->second->set_status(ws::protobuf::Status::kSellReleased);
                        }
                    } else if (iter->second->schecked() > 0) {
                        if (iter->second->status() != ws::protobuf::Status::kSellUserReleased) {
                            changed = true;
                            iter->second->set_status(ws::protobuf::Status::kSellUserReleased);
                        }
                    } else if (iter->second->mchecked() > 0) {
                        if (iter->second->status() != ws::protobuf::Status::kSellManagerReleased) {
                            changed = true;
                            iter->second->set_status(ws::protobuf::Status::kSellManagerReleased);
                        }
                    }

                    if (iter->second->status() == ws::protobuf::Status::kSellWaitingCreate ||
                            iter->second->status() == ws::protobuf::Status::kSellTxCreateError) {
                        iter->second->set_status(ws::protobuf::Status::kSellCreated);
                        changed = true;
                    }

                    if (iter->second->status() == ws::protobuf::kSellWaitingConfirm || 
                            iter->second->status() == ws::protobuf::kSellWaitingConfirmTx ||
                            iter->second->status() == ws::protobuf::kSellWaitingConfirmTxError) {
                        if (buyer == iter->second->buyer() && amount == iter->second->amount()) {
                            iter->second->set_status(ws::protobuf::Status::kSellCreated);
                            changed = true;
                            if (order_iter != order_map_.end()) {
                                order_iter->second->set_status(ws::protobuf::Status::kConfirmed);
                                prefix_db_->SaveSellOrder(buyer, *order_iter->second);
                            }
                        }
                    }

                    if (changed) {
                        prefix_db_->SaveSellout(id, *iter->second);
                    }
                }

                if (ck_block.GetRowCount() > got_count) {
                    got_count = ck_block.GetRowCount();
                }
            });
        } catch (std::exception& e) {
            ZJC_ERROR("catch error: %s", e.what());
            status = 1;
        }
    } while (got_count >= 1000);

    return status;
}

void WsServer::CheckC2cStatus(ws::protobuf::WsMessage& ws_tx_res) {
    auto now_tm = common::TimeUtils::TimestampMs();
    std::unordered_map<std::string, std::shared_ptr<ws::protobuf::SellInfo>> sell_map;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        sell_map = sell_map_;
    }

    for (auto iter = sell_map.begin(); iter != sell_map.end(); ++iter) {
        auto old_status = iter->second->status();
        ZJC_DEBUG("get sell status %s %d update tm: %lu, peariod: %lu, now tm: %lu", 
            common::Encode::HexEncode(iter->first).c_str(), old_status, iter->second->timestamp(), common::GlobalInfo::Instance()->c2c_timeout_ms(), now_tm);
        if (old_status == ws::protobuf::kSellCreated) {
            continue;
        }

        if (old_status >= ws::protobuf::kSellTxPrepaymentError) {
            continue;
        }

        // if (old_status == ws::protobuf::kSellReleased && iter->second->seller() == common::Encode::HexDecode("4d20fc0bb62f67fb29ec13036ce3a84ddebc10e7")) {
        //     iter->second->set_status(ws::protobuf::kSellWaitingPrepayment);
        //     iter->second->set_timestamp(now_tm);
        // }

        if (old_status == ws::protobuf::kSellWaitingPrepayment) {
            if (iter->second->timestamp() + 4 * common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellTxPrepaymentError);
        }

        if (old_status == ws::protobuf::kSellWaitingCreate) {
            if (iter->second->timestamp() + common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellTxCreateError);
        }

        if (old_status == ws::protobuf::kSellUserWaitingRelease) {
            if (iter->second->timestamp() + common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellTxUserReleaseError);
        }

        if (old_status == ws::protobuf::kSellManagerWaitingRelease) {
            if (iter->second->timestamp() + common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellTxManagerReleaseError);
        }

        if (old_status == ws::protobuf::kSellWaitingConfirmTx) {
            if (iter->second->timestamp() + common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellWaitingConfirmTxError);
        }

        if (old_status == ws::protobuf::kSellForceReleaseWaitingTx) {
            if (iter->second->timestamp() + common::GlobalInfo::Instance()->c2c_timeout_ms() > now_tm) {
                continue;
            }

            iter->second->set_status(ws::protobuf::kSellForceReleaseWaitingTxError);
        }

        if (old_status != iter->second->status()) {
            prefix_db_->SaveSellout(iter->second->seller(), *iter->second);
            auto* init_info = ws_tx_res.mutable_init_info();
            auto* c2c = init_info->mutable_c2c();
            auto* sell_info = c2c->add_sells();
            *sell_info = *iter->second;
        }
    }
}

void WsServer::GetC2cs(ws::protobuf::WsMessage& ws_tx_res) {
    std::string cmd = "select seller, buyer, amount, receivable, all, now, mchecked, schecked, reported, orderId, height from zjc_ck_c2c_table where contract='" + 
        common::GlobalInfo::Instance()->c2c_contract_addr() + "' and height > " + std::to_string(max_c2c_height_) +  " limit 1000;";
    uint32_t all_transactions = 0;
    ZJC_DEBUG("get c2c run cmd: %s", cmd.c_str());
    try {
        clickhouse::Client ck_client0(clickhouse::ClientOptions().
            SetHost(common::GlobalInfo::Instance()->ck_host()).
            SetPort(common::GlobalInfo::Instance()->ck_port()).
            SetUser(common::GlobalInfo::Instance()->ck_user()).
            SetPassword(common::GlobalInfo::Instance()->ck_pass()));
        ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
            for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                std::string user(ck_block[0]->As<clickhouse::ColumnString>()->At(i));
                std::string encode_buyer(ck_block[1]->As<clickhouse::ColumnString>()->At(i));
                auto amount = ck_block[2]->As<clickhouse::ColumnUInt64>()->At(i);
                auto all = ck_block[4]->As<clickhouse::ColumnUInt64>()->At(i);
                auto now = ck_block[5]->As<clickhouse::ColumnUInt64>()->At(i);
                auto mchecked = ck_block[6]->As<clickhouse::ColumnUInt32>()->At(i);
                auto schecked = ck_block[7]->As<clickhouse::ColumnUInt32>()->At(i);
                auto reported = ck_block[8]->As<clickhouse::ColumnUInt32>()->At(i);
                auto height = ck_block[10]->As<clickhouse::ColumnUInt64>()->At(i);
                if (max_c2c_height_ < height) {
                    max_c2c_height_ = height;
                }

                auto id = common::Encode::HexDecode(user);
                auto buyer = common::Encode::HexDecode(encode_buyer);
                std::shared_ptr<ws::protobuf::SellInfo> sellptr = nullptr;
                std::shared_ptr<ws::protobuf::SellInfo> orderptr = nullptr;
                {
                    std::lock_guard<std::mutex> g(sell_map_mutex_);
                    auto iter = sell_map_.find(id);
                    if (iter == sell_map_.end()) {
                        ZJC_ERROR("local sell info error: %s!", user.c_str());
                        return;
                    }

                    sellptr = iter->second;
                    auto order_iter = order_map_.find(sellptr->buyer());
                    if (order_iter != order_map_.end()) {
                        orderptr = order_iter->second;
                    }
                }

                ZJC_INFO("get c2c seller: %s, buyer: %s, amount: %lu, height: %lu, exists height: %lu, status: %d, max height: %lu", 
                    user.c_str(), encode_buyer.c_str(), amount, height, sellptr->height(), sellptr->status(), max_c2c_height_);
                if (sellptr->height() >= height) {
                    ZJC_ERROR("local sell info height error: %s %lu, %lu!", user.c_str(), sellptr->height(), height);
                    continue;
                }
                
                bool changed = false;
                if (sellptr->buyer() != buyer) {
                    changed = true;
                    sellptr->set_buyer(buyer);
                }

                if (sellptr->amount() != amount) {
                    changed = true;
                    sellptr->set_amount(amount);
                }

                if (sellptr->all() != all) {
                    changed = true;
                    sellptr->set_all(all);
                }

                if (sellptr->now() != now) {
                    changed = true;
                    sellptr->set_now(now);
                }

                if (sellptr->mchecked() != mchecked) {
                    changed = true;
                    sellptr->set_mchecked(mchecked);
                }

                if (sellptr->schecked() != schecked) {
                    changed = true;
                    sellptr->set_schecked(schecked);
                }

                if (sellptr->schecked() > 0 && sellptr->mchecked() > 0) {
                    if (sellptr->status() != ws::protobuf::Status::kSellReleased) {
                        changed = true;
                        sellptr->set_status(ws::protobuf::Status::kSellReleased);
                    }
                } else if (sellptr->schecked() > 0) {
                    if (sellptr->status() != ws::protobuf::Status::kSellUserReleased) {
                        changed = true;
                        sellptr->set_status(ws::protobuf::Status::kSellUserReleased);
                    }
                } else if (sellptr->mchecked() > 0) {
                    if (sellptr->status() != ws::protobuf::Status::kSellManagerReleased) {
                        changed = true;
                        sellptr->set_status(ws::protobuf::Status::kSellManagerReleased);
                    }
                }

                if (sellptr->status() == ws::protobuf::Status::kSellWaitingCreate ||
                        sellptr->status() == ws::protobuf::Status::kSellTxCreateError) {
                    sellptr->set_status(ws::protobuf::Status::kSellCreated);
                    changed = true;
                }

                if (sellptr->status() == ws::protobuf::kSellWaitingConfirmTx || 
                        sellptr->status() == ws::protobuf::kSellWaitingConfirmTxError) {
                    if (buyer == sellptr->buyer() && amount == sellptr->amount()) {
                        sellptr->set_status(ws::protobuf::Status::kSellCreated);
                        changed = true;
                        if (orderptr != nullptr) {
                            orderptr->set_status(ws::protobuf::Status::kConfirmed);
                            prefix_db_->SaveSellOrder(buyer, *orderptr);
                            auto* sellinfo = ws_tx_res.mutable_init_info()->mutable_c2c()->add_sells();
                            *sellinfo = *orderptr;
                        }
                    }
                }

                if (sellptr->reported() != reported) {
                    changed = true;
                    sellptr->set_reported(reported);
                }

                if (sellptr->height() != height) {
                    changed = true;
                    sellptr->set_height(height);
                }

                if (changed) {
                    prefix_db_->SaveSellout(id, *sellptr);
                }

                auto* sellinfo = ws_tx_res.mutable_init_info()->mutable_c2c()->add_sells();
                *sellinfo = *sellptr;
                ZJC_INFO("get sell info seller: %s, buyer: %s, status: %d", 
                    user.c_str(), encode_buyer.c_str(), sellptr->status());
            }
        });
    } catch (std::exception& e) {
        ZJC_ERROR("catch error: %s", e.what());
    }
}

void WsServer::GetAllBalance() {
    while (true) {
        int32_t get_count = 0;
        std::string cmd = "select id, balance from zjc_ck_account_table limit 10000;";
        uint32_t all_transactions = 0;
        try {
            clickhouse::Client ck_client0(clickhouse::ClientOptions().
                SetHost(common::GlobalInfo::Instance()->ck_host()).
                SetPort(common::GlobalInfo::Instance()->ck_port()).
                SetUser(common::GlobalInfo::Instance()->ck_user()).
                SetPassword(common::GlobalInfo::Instance()->ck_pass()));
            ck_client0.Select(cmd, [&](const clickhouse::Block& ck_block) {
                if (ck_block.GetRowCount() > 0) {
                    get_count = ck_block.GetRowCount();
                    for (uint32_t i = 0; i < ck_block.GetRowCount(); ++i) {
                        std::string from_str(ck_block[0]->As<clickhouse::ColumnString>()->At(i));
                        std::string id = common::Encode::HexDecode(from_str);
                        uint64_t balance = ck_block[1]->As<clickhouse::ColumnUInt64>()->At(i);
                        user_balance_[id] = balance;
                        ZJC_INFO("get balance: %s, balance: %lu", from_str.c_str(), balance);
                    }
                }
            });
        } catch (std::exception& e) {
            ZJC_ERROR("catch error: %s", e.what());
        }

        if (get_count < 10000) {
            break;
        }
    }
}

void WsServer::NewTxClient(websocketpp::connection_hdl hdl, const std::string& msg) {
    auto ptr = hdl.lock();
    // std::lock_guard<std::mutex> g(refresh_hdls_mutex_);
    refresh_hdls_.insert(ptr);
    std::string res("ok");
    ws_server_.Send(hdl, res.c_str(), res.size());
}

void WsServer::NewBandwidthMessage(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::BandwidthInfo bw_info;
    if (!bw_info.ParseFromString(msg)) {
        return;
    }

    ws::protobuf::WsMessage bw_res;
    for (int32_t i = 0; i < bw_info.bws_size(); ++i) {
        auto& bw = bw_info.bws(i);
        uint64_t all_bw = 0;
        prefix_db_->SaveIdBandwidth(bw.id(), bw.bandwidth(), &all_bw);
        std::lock_guard<std::mutex> g(invalid_users_mutex_);
        auto iter = invalid_users_.find(bw.id());
        if (all_bw > common::GlobalInfo::Instance()->valid_free_bandwidth()) {
            if (iter == invalid_users_.end()) {
                invalid_users_[bw.id()] = true;
            }

            auto res_item = bw_res.add_bws();
            res_item->set_id(bw.id());
            res_item->set_bandwidth(all_bw);
        } else {
            if (iter != invalid_users_.end()) {
                invalid_users_.erase(iter);
            }
        }
        
        ZJC_INFO("bandwidth message handle success %s: %lu", common::Encode::HexEncode(bw.id()).c_str(), all_bw);
    }
    
    if (bw_res.bws_size() > 0) {
        std::string res = common::Encode::HexEncode(bw_res.SerializeAsString());
        ws_server_.Send(hdl, res.c_str(), res.size());
    }
}

void WsServer::CloseCallback(websocketpp::connection_hdl hdl) {
    // std::lock_guard<std::mutex> g(refresh_hdls_mutex_);
    auto ptr = hdl.lock();
    auto iter = refresh_hdls_.find(ptr);
    if (iter != refresh_hdls_.end()) {
        refresh_hdls_.erase(iter);
    }
}

std::string WsServer::GetTxMessageHash(const pools::protobuf::TxMessage& tx_info) {
    std::string message;
    message.reserve(tx_info.ByteSizeLong());
    message.append(tx_info.gid());
    message.append(tx_info.pubkey());
    message.append(tx_info.to());
    uint64_t amount = tx_info.amount();
    message.append(std::string((char*)&amount, sizeof(amount)));
    uint64_t gas_limit = tx_info.gas_limit();
    message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = tx_info.gas_price();
    message.append(std::string((char*)&gas_price, sizeof(gas_price)));
    if (tx_info.has_step()) {
        uint64_t step = static_cast<uint64_t>(tx_info.step());
        message.append(std::string((char*)&step, sizeof(step)));
    }

    if (tx_info.has_contract_code()) {
        message.append(tx_info.contract_code());
    }

    if (tx_info.has_contract_input()) {
        message.append(tx_info.contract_input());
    }

    if (tx_info.has_contract_prepayment()) {
        uint64_t prepay = tx_info.contract_prepayment();
        message.append(std::string((char*)&prepay, sizeof(prepay)));
    }

    if (tx_info.has_key()) {
        message.append(tx_info.key());
        if (tx_info.has_value()) {
            message.append(tx_info.value());
        }
    }

    return common::Hash::keccak256(message);
}

int WsServer::CreateTransactionWithAttr(
        const ws::protobuf::TxMessage& tx_info,
        transport::protobuf::OldHeader& msg) {
#pragma pack(push) 
#pragma pack(1)
    union DhtKey {
        DhtKey() {
            memset(dht_key, 0, sizeof(dht_key));
        }

        struct Construct {
            uint32_t net_id;
            char hash[28];
        } construct;
        char dht_key[32];
    };
#pragma pack(pop)
    DhtKey dht_key;
    dht_key.construct.net_id = 3;
    memcpy(dht_key.construct.hash, tx_info.pubkey().c_str(), 28);
    msg.set_src_sharding_id(3);
    msg.set_des_dht_key(std::string(dht_key.dht_key, sizeof(dht_key.dht_key)));
    msg.set_type(7);
    transport::TcpTransport::Instance()->SetMessageHash(msg, common::kMaxThreadCount - 1);
    auto* broadcast = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_gid(tx_info.gid());
    new_tx->set_pubkey(tx_info.pubkey());
    new_tx->set_step(static_cast<pools::protobuf::StepType>(tx_info.step()));
    new_tx->set_to(tx_info.to());
    new_tx->set_amount(tx_info.amount());
    new_tx->set_gas_limit(tx_info.gas_limit());
    new_tx->set_gas_price(tx_info.gas_price());
    if (!tx_info.key().empty()) {
        new_tx->set_key(tx_info.key());
        if (!tx_info.value().empty()) {
            new_tx->set_value(tx_info.value());
        }
    }

    if (tx_info.contract_code().empty()) {
        new_tx->set_contract_code(tx_info.contract_code());
    }

    if (!tx_info.contract_input().empty()) {
        new_tx->set_contract_input(tx_info.contract_input());
    }

    if (tx_info.contract_prepayment() > 0) {
        new_tx->set_contract_prepayment(tx_info.contract_prepayment());
    }

    int32_t tmp_sign_v = tx_info.signv()[0];
    if (tmp_sign_v < 27) {
        ZJC_ERROR("invalid sign v: %d", tmp_sign_v);
        return 1;
    }

    std::string sign = tx_info.signr() + tx_info.signs() + "0";// http_handler->security_ptr()->GetSign(sign_r, sign_s, sign_v);
    sign[64] = char(tmp_sign_v - 27);
    auto tx_hash = GetTxMessageHash(*new_tx);
    if (security_->Verify(tx_hash, tx_info.pubkey(), sign) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed tx_hash: %s, "
            "sign_r: %s, sign_s: %s, sign_v: %d %d, pk: %s",
            common::Encode::HexEncode(tx_hash).c_str(),
            common::Encode::HexEncode(tx_info.signr()).c_str(),
            common::Encode::HexEncode(tx_info.signs()).c_str(),
            tx_info.signv()[0],
            tmp_sign_v,
            common::Encode::HexEncode(tx_info.pubkey()).c_str());
        return 1;
    }

    msg.set_sign(sign);
    return 0;
}

void WsServer::C2cNewSell(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("new c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("new c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("new c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("new c2c sell exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    if (tx.amount() < common::GlobalInfo::Instance()->min_c2c_sellout_amount()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount");
        ZJC_DEBUG("new c2c sell invalid amount min invalid: %lu, %lu", 
            tx.amount(), common::GlobalInfo::Instance()->min_c2c_sellout_amount());
        return;
    }

    PopUserInfo();
    auto seller = security_->GetAddress(tx.pubkey());
    auto prepayment_iter = contract_prepayment_.find(seller);
    if (prepayment_iter == contract_prepayment_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("new c2c sell invalid amount prepayment not exists: %s.", 
            common::Encode::HexEncode(seller).c_str());
        return;
    }

    if (tx.amount() + 10000000lu > prepayment_iter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("new c2c sell invalid amount: %lu, prepayment: %lu", tx.amount(), prepayment_iter->second);
        return;
    }

    if (prepayment_iter->second < common::GlobalInfo::Instance()->min_c2c_prepayment()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid prepayment");
        ZJC_DEBUG("new c2c sell invalid prepayment.");
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }

    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("new c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellPrepayment && 
            sell_ptr->status() != ws::protobuf::kSellTxCreateError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("new c2c sell invalid sell, status invalid");
        return;
    }

    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("new c2c sell invalid sell, failed handle transaction.");
        return;
    }

    sell_ptr->set_status(ws::protobuf::Status::kSellWaitingCreate);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("new c2c sell invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingCreate, "ok");
    ZJC_INFO("create new sell success create tm: %lu, seller: %s, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::C2cResponse(websocketpp::connection_hdl hdl, const ws::protobuf::StatusInfo& status) {
    ws::protobuf::WsMessage ws_msg;
    auto init_info = ws_msg.mutable_init_info();
    auto status_info = init_info->mutable_status();
    *status_info = status;
    std::string res = common::Encode::HexEncode(ws_msg.SerializeAsString());
    ws_server_.Send(hdl, res.c_str(), res.size());
}

void WsServer::C2cResponse(
        websocketpp::connection_hdl hdl, 
        uint64_t msg_id, 
        int status, 
        const std::string& msg) {
    ws::protobuf::WsMessage ws_msg;
    auto init_info = ws_msg.mutable_init_info();
    auto status_info = init_info->mutable_status();
    status_info->set_status(status);
    status_info->set_message(msg);
    std::string res = common::Encode::HexEncode(ws_msg.SerializeAsString());
    ws_server_.Send(hdl, res.c_str(), res.size());
     if (!status_map_.KeyExists(msg_id)) {
        status_map_.Insert(msg_id, std::make_shared<ws::protobuf::StatusInfo>(*status_info));
    }
}

void WsServer::C2cStatus(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
        }
    } else {
        ws::protobuf::StatusInfo status;
        status.set_msg_id(c2c_msg.msg_id());
        status.set_status(-1);
        C2cResponse(hdl, status);
    }
}

void WsServer::GetAllSells(websocketpp::connection_hdl hdl, const std::string& msg) {
    ws::protobuf::WsMessage ws_msg;
    auto* init_info = ws_msg.mutable_init_info();
    auto* c2c = init_info->mutable_c2c();
    std::lock_guard<std::mutex> g(sell_map_mutex_);
    for (auto iter = sell_map_.begin(); iter != sell_map_.end(); ++iter) {
        auto* sell_info = c2c->add_sells();
        *sell_info = *iter->second;
    }

    for (auto iter = order_map_.begin(); iter != order_map_.end(); ++iter) {
        if (!iter->second->is_order()) {
            continue;
        }

        if (iter->second->status() != ws::protobuf::Status::kOrderCanceled &&
                iter->second->status() != ws::protobuf::Status::kSellManagerReleased &&
                iter->second->status() != ws::protobuf::Status::kReported &&
                iter->second->status() != ws::protobuf::Status::kConfirmed) {
            continue;
        }

        auto* order_info = c2c->add_sells();
        *order_info = *iter->second;
    }

    ZJC_DEBUG("success get all sells: %d", c2c->sells_size());
    auto res = common::Encode::HexEncode(ws_msg.SerializeAsString());
    ws_server_.Send(hdl, res.c_str(), res.size());
}

void WsServer::PopUserInfo() {
    while (user_info_queue_.size() > 0) {
        std::shared_ptr<UserInfoItem> user_info = nullptr;
        user_info_queue_.pop(&user_info);
        if (user_info->balance > 0) {
            user_balance_[user_info->id] = user_info->balance;
        }

        if (user_info->prepayment > 0) {
            contract_prepayment_[user_info->id] = user_info->prepayment;
        }

        ZJC_DEBUG("update balance %s, %lu, %lu", 
            common::Encode::HexEncode(user_info->id).c_str(), user_info->balance, user_info->prepayment);
    }
}

void WsServer::C2cPrepayment(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("new c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("new c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("new c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("new c2c sell exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    if (tx.amount() != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount");
        ZJC_DEBUG("new c2c sell invalid amount.");
        return;
    }

    if (tx.contract_prepayment() < common::GlobalInfo::Instance()->min_c2c_prepayment()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid prepayment");
        ZJC_DEBUG("new c2c sell invalid prepayment.");
        return;
    }

    auto seller = security_->GetAddress(tx.pubkey());
    PopUserInfo();
    auto biter = user_balance_.find(seller);
    if (biter == user_balance_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid balance");
        ZJC_DEBUG("new c2c sell invalid balance.");
        return;
    }

    if (tx.contract_prepayment() > biter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid balance");
        ZJC_DEBUG("new c2c sell invalid balance: %lu, %lu", tx.contract_prepayment(), biter->second);
        return;
    }

    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            if (iter->second->status() != ws::protobuf::kSellReleased) {
                C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, old exists");
                ZJC_DEBUG("new c2c sell invalid sell, old exists.");
                return;
            }
        }
    }

    ws::protobuf::SellInfo tmp_sell;
    if (!tmp_sell.ParseFromString(tx.value())) {
        ZJC_WARN("from string failed sell info: %s, to: %s, amount: %d",
            common::Encode::HexEncode(tx.pubkey()).c_str(),
            common::Encode::HexEncode(tx.to()).c_str(),
            tx.amount());
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "parse from string failed sell info.");
        ZJC_DEBUG("new c2c sell invalid sell, parse from string failed sell info.");
        return;
    }

    if (tmp_sell.price() <= 0) {
        ZJC_WARN("sell price invalid sell info: %s, to: %s, price: %d",
            common::Encode::HexEncode(tx.pubkey()).c_str(),
            common::Encode::HexEncode(tx.to()).c_str(),
            tmp_sell.price());
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid price.");
        return;
    }

    if (tmp_sell.receivable().receivable_size() <= 0) {
        ZJC_WARN("sell price invalid receivable info: %s, to: %s, price: %d",
            common::Encode::HexEncode(tx.pubkey()).c_str(),
            common::Encode::HexEncode(tx.to()).c_str(),
            tmp_sell.price());
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid receivable.");
        return;
    }

    auto sell_ptr = std::make_shared<ws::protobuf::SellInfo>();
    sell_ptr->set_contract(tx.to());
    sell_ptr->set_username(tmp_sell.username());
    sell_ptr->set_seller(seller);
    sell_ptr->set_all(tx.contract_prepayment());
    sell_ptr->set_price(tmp_sell.price());
    sell_ptr->set_status(ws::protobuf::kSellWaitingPrepayment);
    sell_ptr->set_create_timestamp(common::TimeUtils::TimestampMs());
    auto* receivable = sell_ptr->mutable_receivable();
    *receivable = tmp_sell.receivable();

    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("new c2c sell invalid sell, failed handle transaction.");
        return;
    }

    if (tx.amount() + 1000000llu > tx.contract_prepayment()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid prepayment!");
        ZJC_DEBUG("new c2c sell invalid sell, invalid prepayment.");
        return;
    }

    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("new c2c sell invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        sell_map_[seller] = sell_ptr;
    }
    
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingPrepayment, "ok");
    ZJC_INFO("create new sell success create tm: %lu, seller: %s, username: %s, all: %lu, price: %lu, receivable size: %d",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price(),
        sell_ptr->receivable().receivable_size());
}

void WsServer::C2cCancelSell(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("cancel c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("cancel c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("cancel c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("cancel c2c sell exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    PopUserInfo();
    auto seller = security_->GetAddress(tx.pubkey());
    auto prepayment_iter = contract_prepayment_.find(seller);
    if (prepayment_iter == contract_prepayment_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("cancel c2c sell invalid amount prepayment not exists: %s.", 
            common::Encode::HexEncode(seller).c_str());
        return;
    }

    if (1000000lu > prepayment_iter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("cancel c2c sell invalid amount: %lu, prepayment: %lu", tx.amount(), prepayment_iter->second);
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("cancel c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellCreated && 
            sell_ptr->status() != ws::protobuf::kSellTxUserReleaseError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("cancel c2c sell invalid sell, status invalid");
        return;
    }

    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("cancel c2c sell invalid sell, failed handle transaction.");
        return;
    }

    sell_ptr->set_status(ws::protobuf::Status::kSellUserWaitingRelease);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("cancel c2c sell invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellUserWaitingRelease, "ok");
    ZJC_INFO("cancelsell success create tm: %lu, seller: %s, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::C2cManagerCancelSell(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("manager cancel c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("manager cancel c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("manager cancel c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("manager cancel c2c sell exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    PopUserInfo();
    auto seller = c2c_msg.c2c().sell().seller();
    auto prepayment_iter = contract_prepayment_.find(seller);
    if (prepayment_iter == contract_prepayment_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("manager cancel c2c sell invalid amount prepayment not exists: %s.", 
            common::Encode::HexEncode(seller).c_str());
        return;
    }

    if (1000000lu > prepayment_iter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("manager cancel c2c sell invalid amount: %lu, prepayment: %lu", tx.amount(), prepayment_iter->second);
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("manager cancel c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellUserReleased && sell_ptr->status() != ws::protobuf::kSellTxManagerReleaseError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("manager cancel c2c sell invalid sell, status invalid");
        return;
    }

    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("manager cancel c2c sell invalid sell, failed handle transaction.");
        return;
    }

    sell_ptr->set_status(ws::protobuf::Status::kSellManagerWaitingRelease);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("manager cancel c2c sell invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellManagerWaitingRelease, "ok");
    ZJC_INFO("manager cancel sell success create tm: %lu, seller: %s, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::Transaction(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("new transaction comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("new transaction parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("new transaction message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("new transaction exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    if (tx.amount() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount");
        ZJC_DEBUG("new transaction invalid amount.");
        return;
    }

    auto from_address = security_->GetAddress(tx.pubkey());
    PopUserInfo();
    auto biter = user_balance_.find(from_address);
    if (biter == user_balance_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid balance");
        ZJC_DEBUG("new transaction invalid balance.");
        return;
    }

    if (tx.amount() + consensus::kTransferGas > biter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid balance");
        ZJC_DEBUG("new transaction invalid balance: %lu, %lu", tx.contract_prepayment(), biter->second);
        return;
    }

    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("new transaction invalid sell, failed handle transaction.");
        return;
    }

    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("new transaction invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    C2cResponse(hdl, c2c_msg.msg_id(), 0, "ok");
    ZJC_INFO("create transaction success create from: %s, amount: %lu",
        common::Encode::HexEncode(from_address).c_str(), 
        tx.amount());    
}

void WsServer::Purchase(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("purchase c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("purchase c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("purchase c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("purchase c2c sell exists message id.");
            return;
        }
    }

    PopUserInfo();
    auto seller = c2c_msg.c2c().order().seller();
    if (c2c_msg.c2c().order().buyer().size() != c2c_msg.c2c().order().seller().size() || 
            c2c_msg.c2c().order().buyer() == c2c_msg.c2c().order().seller()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, invalid buyer");
        ZJC_DEBUG("purchase c2c sell invalid sell, invalid buyer.");
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("purchase c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellCreated) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("purchase c2c sell invalid sell, status invalid");
        return;
    }

    if (c2c_msg.c2c().order().amount() < common::GlobalInfo::Instance()->c2c_min_purchase_amount()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, amount invalid");
        ZJC_DEBUG("purchase c2c sell invalid sell, amount invalid %lu, %lu", 
            c2c_msg.c2c().order().amount(), 
            common::GlobalInfo::Instance()->c2c_min_purchase_amount());
        return;
    }

    if (c2c_msg.c2c().order().amount() > sell_ptr->all()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, amount invalid");
        ZJC_DEBUG("purchase c2c sell invalid sell, amount invalid");
        return;
    }

    sell_ptr->set_amount(c2c_msg.c2c().order().amount());
    sell_ptr->set_buyer(c2c_msg.c2c().order().buyer());
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    sell_ptr->set_status(ws::protobuf::Status::kSellWaitingConfirm);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    prefix_db_->SaveSellout(seller, *sell_ptr);
    auto order_ptr = std::make_shared<ws::protobuf::SellInfo>(*sell_ptr);
    order_ptr->set_is_order(true);
    prefix_db_->SaveSellOrder(c2c_msg.c2c().order().buyer(), *order_ptr);
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        order_map_[c2c_msg.c2c().order().buyer()] = order_ptr;
    }
    ws::protobuf::WsMessage ws_msg;
    auto* sell_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *sell_info = *sell_ptr;
    auto brd_msg = common::Encode::HexEncode(ws_msg.SerializeAsString());
    for (auto iter = refresh_hdls_.begin(); iter != refresh_hdls_.end(); ++iter) {
        websocketpp::connection_hdl hdl = *iter;
        ws_server_.Send(hdl, brd_msg.c_str(), brd_msg.size());
    }

    // C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingConfirm, "ok");
    ZJC_INFO("purchase sell success create tm: %lu, seller: %s, buyer: %s, amount: %lu, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        common::Encode::HexEncode(sell_ptr->buyer()).c_str(), 
        sell_ptr->amount(),
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::CancelOrder(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("cancel order c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("cancel order c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("cancel order c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("cancel order c2c sell exists message id.");
            return;
        }
    }

    PopUserInfo();
    auto seller = c2c_msg.c2c().order().seller();
    if (c2c_msg.c2c().order().buyer().size() != c2c_msg.c2c().order().seller().size() || 
            c2c_msg.c2c().order().buyer() == c2c_msg.c2c().order().seller()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, invalid buyer");
        ZJC_DEBUG("cancel order c2c sell invalid sell, invalid buyer.");
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    std::shared_ptr<ws::protobuf::SellInfo> order_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }

        auto order_iter = order_map_.find(c2c_msg.c2c().order().buyer());
        if (order_iter != order_map_.end()) {
            order_ptr = order_iter->second;
        }
    }
    
    if (sell_ptr == nullptr || order_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("cancel order c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellWaitingConfirm && 
            sell_ptr->status() != ws::protobuf::kReported &&
            sell_ptr->status() != ws::protobuf::kSellWaitingConfirmTxError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("cancel order c2c sell invalid sell, status invalid");
        return;
    }

    if (c2c_msg.c2c().order().buyer() != c2c_msg.c2c().order().buyer()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, buyer invalid");
        ZJC_DEBUG("cancel order c2c sell invalid sell, buyer invalid");
        return;
    }

    order_ptr->set_status(ws::protobuf::Status::kOrderCanceled);
    order_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    sell_ptr->set_status(ws::protobuf::Status::kSellCreated);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    prefix_db_->SaveSellout(seller, *sell_ptr);
    prefix_db_->SaveSellOrder(c2c_msg.c2c().order().buyer(), *order_ptr);
    ws::protobuf::WsMessage ws_msg;
    auto* sell_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *sell_info = *sell_ptr;
    auto* order_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *order_info = *order_ptr;
    auto brd_msg = common::Encode::HexEncode(ws_msg.SerializeAsString());
    for (auto iter = refresh_hdls_.begin(); iter != refresh_hdls_.end(); ++iter) {
        websocketpp::connection_hdl hdl = *iter;
        ws_server_.Send(hdl, brd_msg.c_str(), brd_msg.size());
    }

    // C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingConfirm, "ok");
    ZJC_INFO("cancel order sell success create tm: %lu, seller: %s, buyer: %s, amount: %lu, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        common::Encode::HexEncode(sell_ptr->buyer()).c_str(), 
        sell_ptr->amount(),
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::ConfirmOrder(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("confirm order comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("confirm order parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("confirm order no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("confirm order exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    PopUserInfo();
    auto seller = security_->GetAddress(tx.pubkey());
    if (seller != c2c_msg.c2c().order().seller()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid seller.");
        ZJC_DEBUG("confirm order invalid seller: %s %s.", 
            common::Encode::HexEncode(seller).c_str(), common::Encode::HexEncode(c2c_msg.c2c().order().seller()).c_str());
        return;
    }

    auto prepayment_iter = contract_prepayment_.find(seller);
    if (prepayment_iter == contract_prepayment_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("confirm order invalid amount prepayment not exists: %s.", 
            common::Encode::HexEncode(seller).c_str());
        return;
    }

    if (tx.amount() != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount");
        ZJC_DEBUG("confirm order invalid sell, invalid amount %lu", tx.amount());
        return;
    }

    auto confirm_amount = c2c_msg.c2c().order().amount();
    if (100000lu > prepayment_iter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("confirm order invalid amount: %lu, prepayment: %lu", 100000lu, prepayment_iter->second);
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("confirm order invalid sell, not exists.");
        return;
    }
    
    if (sell_ptr->amount() != confirm_amount) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount");
        ZJC_DEBUG("confirm order invalid sell, invalid amount %lu, %lu.", sell_ptr->amount(), confirm_amount);
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellWaitingConfirm && 
            sell_ptr->status() != ws::protobuf::kSellWaitingConfirmTxError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("confirm order invalid sell, status invalid");
        return;
    }

    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("confirm order invalid sell, failed handle transaction.");
        return;
    }

    sell_ptr->set_status(ws::protobuf::Status::kSellWaitingConfirmTx);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("confirm order invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingConfirmTx, "ok");
    ZJC_INFO("confirm order success create tm: %lu, seller: %s, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::C2cManagerCancelForceSell(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("manager cancel force c2c sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("manager cancel force c2c sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("manager cancel force c2c sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("manager cancel force c2c sell exists message id.");
            return;
        }
    }

    auto& tx = c2c_msg.tx();
    PopUserInfo();
    auto seller = c2c_msg.c2c().sell().seller();
    auto prepayment_iter = contract_prepayment_.find(seller);
    if (prepayment_iter == contract_prepayment_.end()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("manager cancel force c2c sell invalid amount prepayment not exists: %s.", 
            common::Encode::HexEncode(seller).c_str());
        return;
    }

    if (1000000lu > prepayment_iter->second) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid amount and prepayment.");
        ZJC_DEBUG("manager cancel force c2c sell invalid amount: %lu, prepayment: %lu", tx.amount(), prepayment_iter->second);
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("manager cancel force c2c sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellReleased && 
            sell_ptr->status() != ws::protobuf::kSellForceReleaseWaitingTxError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("manager cancel force c2c sell invalid sell, status invalid");
        return;
    }

    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    // on chain
    transport::protobuf::OldHeader chain_msg;
    int status = CreateTransactionWithAttr(tx, chain_msg);
    if (status != 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "failed handle transaction.");
        ZJC_DEBUG("manager cancel force c2c sell invalid sell, failed handle transaction.");
        return;
    }

    sell_ptr->set_status(ws::protobuf::Status::kSellForceReleaseWaitingTx);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    auto chain_ips = common::Split<>(common::GlobalInfo::Instance()->chain_ips().c_str(), ',');
    if (chain_ips.Count() <= 0) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid chain!");
        ZJC_DEBUG("manager cancel force c2c sell invalid sell, invalid chain.");
        return;
    }

    for (uint32_t i = 0; i < chain_ips.Count(); ++i) {
        auto item = common::Split<>(chain_ips[i], ':');
        if (item.Count() != 2) {
            continue;
        }

        uint16_t port = 0;
        if (!common::StringUtil::ToUint16(item[1], &port)) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(common::kMaxThreadCount - 1, item[0], port, chain_msg);
        ZJC_DEBUG("success send chain message: %s:%d", item[0], port);
    }

    prefix_db_->SaveSellout(seller, *sell_ptr);
    C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellForceReleaseWaitingTx, "ok");
    ZJC_INFO("manager cancel force sell success create tm: %lu, seller: %s, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::C2cManagerResetSell(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("manager reset sell comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("manager reset sell parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("manager reset sell no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("manager reset sell exists message id.");
            return;
        }
    }

    PopUserInfo();
    auto seller = c2c_msg.c2c().order().seller();
    if (c2c_msg.c2c().order().buyer().size() != c2c_msg.c2c().order().seller().size() || 
            c2c_msg.c2c().order().buyer() == c2c_msg.c2c().order().seller()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, invalid buyer");
        ZJC_DEBUG("manager reset sell invalid sell, invalid buyer.");
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    std::shared_ptr<ws::protobuf::SellInfo> order_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto iter = sell_map_.find(seller);
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }

        auto order_iter = order_map_.find(c2c_msg.c2c().order().buyer());
        if (order_iter != order_map_.end()) {
            order_ptr = order_iter->second;
        }
    }
    
    if (sell_ptr == nullptr || order_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("manager reset sell invalid sell, not exists.");
        return;
    }

    if (sell_ptr->status() != ws::protobuf::kSellWaitingConfirm && 
            sell_ptr->status() != ws::protobuf::kSellWaitingConfirmTxError) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, status invalid");
        ZJC_DEBUG("manager reset sell invalid sell, status invalid");
        return;
    }

    order_ptr->set_status(ws::protobuf::Status::kSellManagerReleased);
    order_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    sell_ptr->set_status(ws::protobuf::Status::kSellCreated);
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    prefix_db_->SaveSellout(seller, *sell_ptr);
    prefix_db_->SaveSellOrder(c2c_msg.c2c().order().buyer(), *order_ptr);
    ws::protobuf::WsMessage ws_msg;
    auto* sell_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *sell_info = *sell_ptr;
    auto* order_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *order_info = *order_ptr;
    auto brd_msg = common::Encode::HexEncode(ws_msg.SerializeAsString());
    for (auto iter = refresh_hdls_.begin(); iter != refresh_hdls_.end(); ++iter) {
        websocketpp::connection_hdl hdl = *iter;
        ws_server_.Send(hdl, brd_msg.c_str(), brd_msg.size());
    }

    // C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingConfirm, "ok");
    ZJC_INFO("manager reset sell success create tm: %lu, seller: %s, buyer: %s, amount: %lu, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        common::Encode::HexEncode(sell_ptr->buyer()).c_str(), 
        sell_ptr->amount(),
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}

void WsServer::Appeal(websocketpp::connection_hdl hdl, const std::string& encode_msg) {
    ZJC_DEBUG("user appeal comming.");
    auto msg = common::Encode::HexDecode(encode_msg);
    ws::protobuf::InitInfo c2c_msg;
    if (!c2c_msg.ParseFromString(msg)) {
        ZJC_DEBUG("user appeal parse failed.");
        return;
    }

    if (!c2c_msg.has_msg_id() || c2c_msg.msg_id() <= 0) {
        ZJC_DEBUG("user appeal no message id.");
        return;
    }

    if (status_map_.KeyExists(c2c_msg.msg_id())) {
        std::shared_ptr<ws::protobuf::StatusInfo> status = nullptr;
        if (status_map_.Get(c2c_msg.msg_id(), &status)) {
            C2cResponse(hdl, *status);
            ZJC_DEBUG("user appeal exists message id.");
            return;
        }
    }

    PopUserInfo();
    auto seller = c2c_msg.c2c().appeal().seller();
    if (c2c_msg.c2c().appeal().buyer().size() != c2c_msg.c2c().appeal().seller().size() || 
            c2c_msg.c2c().appeal().buyer() == c2c_msg.c2c().appeal().seller()) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, invalid buyer");
        ZJC_DEBUG("user appeal invalid sell, invalid buyer.");
        return;
    }

    std::shared_ptr<ws::protobuf::SellInfo> sell_ptr = nullptr;
    std::shared_ptr<ws::protobuf::SellInfo> order_ptr = nullptr;
    {
        std::lock_guard<std::mutex> g(sell_map_mutex_);
        auto order_iter = order_map_.find(c2c_msg.c2c().appeal().buyer());
        if (order_iter != order_map_.end()) {
            order_ptr = order_iter->second;
        }

        if (order_ptr == nullptr) {
            C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
            ZJC_DEBUG("user appeal invalid sell, not exists.");
            return;
        }

        auto iter = sell_map_.find(order_ptr->seller());
        if (iter != sell_map_.end()) {
            sell_ptr = iter->second;
        }
    }
    
    if (sell_ptr == nullptr || order_ptr == nullptr) {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid sell, not exists");
        ZJC_DEBUG("user appeal invalid sell, not exists.");
        return;
    }

    if (c2c_msg.c2c().appeal().type() == 1) {
        if (order_ptr->status() == ws::protobuf::Status::kReported) {
            C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid status");
            ZJC_DEBUG("user appeal invalid sell, invalid status.");
            return;
        }

        if (sell_ptr->status() == ws::protobuf::Status::kSellWaitingConfirm || 
                sell_ptr->status() == ws::protobuf::Status::kReported || 
                sell_ptr->status() == ws::protobuf::Status::kReportedByOrder || 
                sell_ptr->status() == ws::protobuf::Status::kSellWaitingConfirmTx || 
                sell_ptr->status() == ws::protobuf::Status::kSellWaitingConfirmTxError) {
            C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid status");
            ZJC_DEBUG("user appeal invalid sell, invalid status.");
            return;
        }

        auto* appeal = order_ptr->mutable_appeal();
        *appeal = c2c_msg.c2c().appeal();
        auto peer_appeal = sell_ptr->mutable_peer_appeal();
        *peer_appeal = c2c_msg.c2c().appeal();
        order_ptr->set_status(ws::protobuf::Status::kReported);
        sell_ptr->set_status(ws::protobuf::Status::kReportedByOrder);
    } else if (c2c_msg.c2c().appeal().type() == 2) {
        if (sell_ptr->status() != ws::protobuf::Status::kReportedByOrder) {
            C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid status");
            ZJC_DEBUG("user appeal invalid sell, invalid status.");
            return;
        }

        auto* appeal = sell_ptr->mutable_appeal();
        *appeal = c2c_msg.c2c().appeal();
        auto peer_appeal = order_ptr->mutable_peer_appeal();
        *peer_appeal = c2c_msg.c2c().appeal();
        sell_ptr->set_status(ws::protobuf::Status::kReported);
    } else {
        C2cResponse(hdl, c2c_msg.msg_id(), -1, "invalid type");
        ZJC_DEBUG("user appeal invalid sell, invalid type.");
        return;
    }

    order_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    sell_ptr->set_timestamp(common::TimeUtils::TimestampMs());
    sell_ptr->set_buyer(order_ptr->buyer());
    prefix_db_->SaveSellout(seller, *sell_ptr);
    prefix_db_->SaveSellOrder(c2c_msg.c2c().appeal().buyer(), *order_ptr);
    ws::protobuf::WsMessage ws_msg;
    auto* sell_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *sell_info = *sell_ptr;
    auto* order_info = ws_msg.mutable_init_info()->mutable_c2c()->add_sells();
    *order_info = *order_ptr;
    auto brd_msg = common::Encode::HexEncode(ws_msg.SerializeAsString());
    for (auto iter = refresh_hdls_.begin(); iter != refresh_hdls_.end(); ++iter) {
        websocketpp::connection_hdl hdl = *iter;
        ws_server_.Send(hdl, brd_msg.c_str(), brd_msg.size());
    }

    // C2cResponse(hdl, c2c_msg.msg_id(), ws::protobuf::kSellWaitingConfirm, "ok");
    ZJC_INFO("user appeal success create tm: %lu, seller: %s, buyer: %s, amount: %lu, username: %s, all; %lu, price : %lu",
        sell_ptr->create_timestamp(), 
        common::Encode::HexEncode(seller).c_str(), 
        common::Encode::HexEncode(sell_ptr->buyer()).c_str(), 
        sell_ptr->amount(),
        sell_ptr->username().c_str(),
        sell_ptr->all(),
        sell_ptr->price());
}


int WsServer::StartWebsocket() {
    if (ws_server_.Init(
            "0.0.0.0", 
            9082, 
            std::bind(&WsServer::CloseCallback, this, std::placeholders::_1)) != 0) {
        ZJC_ERROR("init websocket failed!");
        return kInitError;
    }

    ws_server_.RegisterCallback(
        "test", 
        std::bind(&WsServer::NewTxClient, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "bandwidth", 
        std::bind(&WsServer::NewBandwidthMessage, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "newsell", 
        std::bind(&WsServer::C2cNewSell, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "cancelsell", 
        std::bind(&WsServer::C2cCancelSell, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "resetsell", 
        std::bind(&WsServer::C2cManagerResetSell, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "mgrcancelsell", 
        std::bind(&WsServer::C2cManagerCancelSell, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "mgrcancelsellforce", 
        std::bind(&WsServer::C2cManagerCancelForceSell, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "prepayment", 
        std::bind(&WsServer::C2cPrepayment, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "status", 
        std::bind(&WsServer::C2cStatus, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "c2call", 
        std::bind(&WsServer::GetAllSells, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "tx", 
        std::bind(&WsServer::Transaction, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "purchase", 
        std::bind(&WsServer::Purchase, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "cancelorder", 
        std::bind(&WsServer::CancelOrder, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "confirm", 
        std::bind(&WsServer::ConfirmOrder, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.RegisterCallback(
        "appeal", 
        std::bind(&WsServer::Appeal, this, std::placeholders::_1, std::placeholders::_2));
    ws_server_.Start();
    return kInitSuccess;
}

}  // namespace init

}  // namespace zjchain
