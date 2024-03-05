#include "common/global_info.h"

#include "common/random.h"
#include "common/hash.h"
#include "common/country_code.h"
#include "common/log.h"
#include "common/encode.h"
#include "common/time_utils.h"

namespace zjchain {

namespace common {

static const std::string kAccountAddress("");

GlobalInfo* GlobalInfo::Instance() {
    static GlobalInfo ins;
    return &ins;
}

GlobalInfo::GlobalInfo() {}

GlobalInfo::~GlobalInfo() {
    if (thread_with_pools_ != nullptr) {
        delete[] thread_with_pools_;
    }
}

int GlobalInfo::Init(const common::Config& config) {
    message_handler_thread_count_ = 4;
    config.Get("zjchain", "consensus_thread_count", message_handler_thread_count_);
    ++message_handler_thread_count_;

    if (!config.Get("zjchain", "local_ip", config_local_ip_)) {
        ZJC_ERROR("get zjchain local_ip from config failed.");
        return kCommonError;
    }

    if (!config.Get("zjchain", "local_port", config_local_port_)) {
        ZJC_ERROR("get zjchain local_port from config failed.");
        return kCommonError;
    }

    if (!config.Get("zjchain", "http_port", http_port_)) {
        http_port_ = 0;
    }
       
    config.Get("zjchain", "sharding_min_nodes_count", sharding_min_nodes_count_);
    config.Get("zjchain", "for_ck", for_ck_server_);
    config.Get("zjchain", "each_shard_max_members", each_shard_max_members_);
    config.Get("zjchain", "join_root", join_root_);
    std::string str_contry;
    if (!config.Get("zjchain", "country", str_contry) || str_contry.empty()) {
        ZJC_ERROR("get zjchain country from config failed.");
        return kCommonError;
    }

    if (!config.Get("zjchain", "first_node", config_first_node_)) {
        ZJC_ERROR("get zjchain first_node from config failed.");
        return kCommonError;
    }

    config.Get("zjchain", "public_port", config_public_port_);
    config.Get("zjchain", "node_tag", node_tag_);
    ip_db_path_ = "./conf/GeoLite2-City.mmdb";
    config.Get("zjchain", "ip_db_path", ip_db_path_);
    config.Get("zjchain", "missing_node", missing_node_);
    config.Get("zjchain", "ck_port", ck_port_);
    config.Get("zjchain", "ck_host", ck_host_);
    config.Get("zjchain", "ck_user", ck_user_);
    config.Get("zjchain", "ck_pass", ck_pass_);
    config.Get("zjchain", "valid_free_bandwidth", valid_free_bandwidth_);
    config.Get("zjchain", "chain_ips", chain_ips_);
    config.Get("zjchain", "c2c_contract_addr", c2c_contract_addr_);
    config.Get("zjchain", "min_c2c_sellout_amount", min_c2c_sellout_amount_);
    config.Get("zjchain", "c2c_timeout_ms", c2c_timeout_ms_);
    config.Get("zjchain", "c2c_min_purchase_amount", c2c_min_purchase_amount_);
    min_c2c_sellout_amount_ *= 1000000lu;
    min_c2c_prepayment_ = min_c2c_sellout_amount_ + 10000000lu;

    std::string addr_name;
    if (config.Get("zjchain", "addr_name", addr_name) && !addr_name.empty()) {
        auto ip = GetIpWithAddrName(addr_name);
        if (!ip.empty()) {
            config_local_ip_ = ip;
            ZJC_INFO("get local ip with addr name: %s, ip: %s", addr_name.c_str(), ip.c_str());
        }
    }

    auto bft_thread = message_handler_thread_count_ - 1;
    thread_with_pools_ = new std::set<uint32_t>[bft_thread];
    auto each_thread_pools_count = common::kInvalidPoolIndex / bft_thread;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        auto thread_idx = (i / each_thread_pools_count) % bft_thread;
        thread_with_pools_[thread_idx].insert(i);
        pools_with_thread_[i] = thread_idx;
    }

    return kCommonSuccess;
}

}  // namespace common

}  // namespace zjchain
