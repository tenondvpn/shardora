#pragma once

#include <clickhouse/client.h>

#include "common/encode.h"
#include "common/limit_hash_map.h"
#include "common/log.h"
#include "common/tick.h"
#include "common/utils.h"
#include "init/init_utils.h"
#include "protos/prefix_db.h"
#include "protos/ws.pb.h"
#include "security/security.h"
#include "transport/multi_thread.h"
#include "websocket/websocket_server.h"

namespace zjchain {

namespace init {

class WsServer {
public:
    WsServer();
    ~WsServer();
    int Init(
        std::shared_ptr<protos::PrefixDb> prefix_db, 
        std::shared_ptr<security::Security> security, 
        transport::MultiThreadHandler* net_handler);

private:
    int StartWebsocket();
    void NewTxClient(websocketpp::connection_hdl hdl, const std::string& msg);
    void NewBandwidthMessage(websocketpp::connection_hdl hdl, const std::string& msg);
    void C2cPrepayment(websocketpp::connection_hdl hdl, const std::string& msg);
    void C2cNewSell(websocketpp::connection_hdl hdl, const std::string& msg);
    void C2cCancelSell(websocketpp::connection_hdl hdl, const std::string& msg);
    void C2cManagerCancelSell(websocketpp::connection_hdl hdl, const std::string& encode_msg);
    void C2cManagerResetSell(websocketpp::connection_hdl hdl, const std::string& encode_msg);
    void C2cManagerCancelForceSell(websocketpp::connection_hdl hdl, const std::string& encode_msg);
    void GetAllSells(websocketpp::connection_hdl hdl, const std::string& msg);
    void C2cStatus(websocketpp::connection_hdl hdl, const std::string& msg);
    void Transaction(websocketpp::connection_hdl hdl, const std::string& msg);
    void Purchase(websocketpp::connection_hdl hdl, const std::string& msg);
    void CancelOrder(websocketpp::connection_hdl hdl, const std::string& msg);
    void ConfirmOrder(websocketpp::connection_hdl hdl, const std::string& msg);
    void Appeal(websocketpp::connection_hdl hdl, const std::string& msg);
    void CloseCallback(websocketpp::connection_hdl hdl);
    void BroadcastTxInfo(uint8_t thread_idx);
    void C2cResponse(websocketpp::connection_hdl hdl, uint64_t msg_id, int status, const std::string& msg);
    void C2cResponse(websocketpp::connection_hdl hdl, const ws::protobuf::StatusInfo& status);
    std::string GetTxMessageHash(const pools::protobuf::TxMessage& tx_info);
    int CreateTransactionWithAttr(
        const ws::protobuf::TxMessage& tx_info,
        transport::protobuf::OldHeader& msg);
    int GetContractPrepayment(const std::string& hex_id, uint64_t* prepayment);
    void GetTxs(ws::protobuf::WsMessage& ws_tx_res);
    void GetAllTxs();
    void GetPrepayment(ws::protobuf::WsMessage& ws_tx_res);
    void GetC2cs(ws::protobuf::WsMessage& ws_tx_res);
    int GetAllC2cs();
    void GetAllBalance();
    void PopUserInfo();
    void CheckC2cStatus(ws::protobuf::WsMessage& ws_tx_res);

    struct UserInfoItem {
        std::string id;
        uint64_t balance;
        uint64_t prepayment;
    };

    ws::WebSocketServer ws_server_;
    std::unordered_set<std::shared_ptr<void>> refresh_hdls_;
    std::mutex refresh_hdls_mutex_;
    common::Tick refresh_hdls_tick_;
    uint64_t latest_timestamp_ = 0;
    std::unordered_map<std::string, bool> invalid_users_;
    std::mutex invalid_users_mutex_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<ws::protobuf::SellInfo>> sell_map_;
    std::unordered_map<std::string, std::shared_ptr<ws::protobuf::SellInfo>> order_map_;
    std::mutex sell_map_mutex_;
    common::ThreadSafeQueue<std::shared_ptr<ws::protobuf::SellInfo>> update_c2c_queue_;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::unordered_map<std::string, uint64_t> user_balance_;
    std::unordered_map<std::string, uint64_t> contract_prepayment_;
    uint64_t latest_prepayment_height_ = 0;
    transport::MultiThreadHandler* net_handler_ = nullptr;
    uint64_t max_c2c_height_ = 0;
    common::LimitHashMap<uint64_t, std::shared_ptr<ws::protobuf::StatusInfo>> status_map_ = {1024};
    common::ThreadSafeQueue<std::shared_ptr<UserInfoItem>> user_info_queue_;

    DISALLOW_COPY_AND_ASSIGN(WsServer);
};

}  // namespace init

}  // namespace zjchain
