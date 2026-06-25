#pragma once

#include <uWebSockets/App.h>
#include <thread>
#include <memory>
#include <string>
#include <mutex>

#include "block/account_manager.h"
#include "common/lru_map.h"
#include "contract/contract_manager.h"
#include "elect/elect_manager.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"

namespace seth {

namespace hotstuff {
    class ViewBlockChain;
}

namespace consensus {
    class HotstuffManager;
}

namespace init {

class HttpHandler {
public:
    HttpHandler();
    ~HttpHandler();
    void Init(
        std::shared_ptr<block::AccountManager> acc_mgr,
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security> security_ptr,
        std::shared_ptr<protos::PrefixDb> tmp_prefix_db,
        std::shared_ptr<contract::ContractManager> tmp_contract_mgr,
        const std::string& ip,
        uint16_t port);

    void set_elect_mgr(std::shared_ptr<elect::ElectManager> elect_mgr) {
        elect_mgr_ = elect_mgr;
    }

    std::shared_ptr<elect::ElectManager> elect_mgr() {
        return elect_mgr_;
    }

    void set_hotstuff_mgr(std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr) {
        hotstuff_mgr_ = hotstuff_mgr;
    }

    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr() {
        return hotstuff_mgr_;
    }

    std::shared_ptr<security::Security> security_ptr() {
        return security_ptr_;
    }

    transport::MultiThreadHandler* net_handler() {
        return net_handler_;
    }

    void set_net_handler(transport::MultiThreadHandler* net_handler) {
        net_handler_ = net_handler;
    }

    void set_contract_mgr(std::shared_ptr<contract::ContractManager> contract_mgr) {
        contract_mgr_ = contract_mgr;
    }

    std::shared_ptr<block::AccountManager> acc_mgr() {
        return acc_mgr_;
    }

    common::LRUMap<std::string, transport::MessagePtr>& tx_msg_map() {
        return tx_msg_map_;
    }

    std::shared_ptr<hotstuff::ViewBlockChain> view_block_chain() {
        return view_block_chain_;
    }

    std::mutex& tx_msg_map_mutex() {
        return tx_msg_map_mutex_;
    }

    // Set private key update callback function
    void SetPrivateKeyUpdateCallback(std::function<int(const std::string&)> callback) {
        private_key_update_callback_ = callback;
    }
    
    // Public access to private key update callback
    std::function<int(const std::string&)> private_key_update_callback_;
    
private:
    void Run();

    std::string http_ip_;
    uint16_t http_port_;
    std::string cert_file_;
    std::string key_file_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    transport::MultiThreadHandler* net_handler_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    std::shared_ptr<std::thread> http_svr_thread_ = nullptr;
    common::LRUMap<std::string, transport::MessagePtr> tx_msg_map_{10240};
    std::shared_ptr<hotstuff::ViewBlockChain> view_block_chain_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    std::mutex tx_msg_map_mutex_;
    std::atomic<bool> running_{false};

    DISALLOW_COPY_AND_ASSIGN(HttpHandler);
};

};  // namespace init

};  // namespace seth
