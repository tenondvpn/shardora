#pragma once

#include <httplib.h>

#include "block/account_manager.h"
#include "contract/contract_manager.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace init {

class HttpHandler {
public:
    HttpHandler();
    ~HttpHandler();
    void Init(
        std::shared_ptr<block::AccountManager>& acc_mgr,
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<protos::PrefixDb>& tmp_prefix_db,
        std::shared_ptr<contract::ContractManager>& tmp_contract_mgr，
        const std::string& ip,
        uint16_t port);

    std::shared_ptr<security::Security> security_ptr() {
        return security_ptr_;
    }

    transport::MultiThreadHandler* net_handler() {
        return net_handler_;
    }

    std::shared_ptr<block::AccountManager> acc_mgr() {
        return acc_mgr_;
    }

private:
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    transport::MultiThreadHandler* net_handler_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    httplib::Server svr;

    DISALLOW_COPY_AND_ASSIGN(HttpHandler);
};

};  // namespace init

};  // namespace shardora
