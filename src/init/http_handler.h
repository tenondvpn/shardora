#pragma once

// #define CPPHTTPLIB_RECV_BUFSIZ size_t(1 * 1024 * 1024)
// #define CPPHTTPLIB_SEND_BUFSIZ size_t(1 * 1024 * 1024)
// #define CPPHTTPLIB_COMPRESSION_BUFSIZ size_t(1 * 1024 * 1024)
// #define CPPHTTPLIB_RECV_BUFSIZ 104857600
#define CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH  size_t(10 * 1024 * 1024)
#define CPPHTTPLIB_THREAD_POOL_COUNT size_t(1)
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
        std::shared_ptr<contract::ContractManager>& tmp_contract_mgr,
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
    void Run();

    std::string http_ip_;
    uint16_t http_port_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    transport::MultiThreadHandler* net_handler_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    httplib::Server svr;
    std::shared_ptr<std::thread> http_svr_thread_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HttpHandler);
};

};  // namespace init

};  // namespace shardora
