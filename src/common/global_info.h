#pragma once

#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/encode.h"
#include "common/hash.h"
#include "common/utils.h"

namespace zjchain {

namespace common {

class GlobalInfo {
public:
    static GlobalInfo* Instance();
    int Init(const common::Config& config);

    std::string config_local_ip() {
        return config_local_ip_;
    }

    uint16_t config_local_port() {
        return config_local_port_;
    }

    void set_config_local_ip(const std::string& ip) {
        config_local_ip_ = ip;
    }

    void set_config_local_port(uint16_t port) {
        config_local_port_ = port;
    }

    uint16_t http_port() {
        return http_port_;
    }

    bool config_first_node() {
        return config_first_node_;
    }

    const std::string& GetVersionInfo() {
        return version_info_;
    }

    void set_network_id(uint32_t netid) {
        network_id_ = netid;
    }

    uint32_t network_id() const {
        return network_id_;
    }

    void set_genesis_start() {
        genesis_start_ = true;
    }

    bool genesis_start() {
        return genesis_start_;
    }

    std::string& tcp_spec() {
        return tcp_spec_;
    }

    std::vector<uint32_t>& networks() {
        return networks_;
    }

    void set_config_public_port(uint16_t public_port) {
        config_public_port_ = public_port;
    }

    uint16_t config_public_port() {
        return config_public_port_;
    }

    uint32_t version() {
        return 1;
    }

    std::string node_tag() {
        return node_tag_;
    }

    uint64_t now_gas_price() {
        return now_gas_price_;
    }

    uint64_t gas_price() {
        return gas_price_;
    }

    bool missing_node() const {
        return missing_node_;
    }

    int32_t tcp_server_thread_count() const {
        return tcp_server_thread_count_;
    }

    std::string ip_db_path() {
        return ip_db_path_;
    }

    uint8_t message_handler_thread_count() const {
        return message_handler_thread_count_;
    }

    uint32_t tick_thread_pool_count() {
        return kTickThreadPoolCount;
    }

    bool for_ck_server() const {
        return for_ck_server_;
    }

    uint32_t each_shard_max_members() {
        return each_shard_max_members_;
    }

private:
    GlobalInfo();
    ~GlobalInfo();

    static const uint32_t kDefaultTestNetworkShardId = 4u;
    static const uint32_t kTickThreadPoolCount = 1U;

    std::string config_local_ip_;
    std::string tcp_spec_;
    uint16_t config_local_port_{ 0 };
    bool config_first_node_{ false };
    std::string version_info_;
    std::atomic<uint64_t> gid_idx_{ 0 };
    uint16_t http_port_{ 0 };
    bool genesis_start_{ false };
    std::vector<uint32_t> networks_;
    uint16_t config_public_port_{ 0 };
    std::string node_tag_;
    volatile uint32_t network_id_{ common::kInvalidUint32 };
    volatile uint64_t now_gas_price_{ 100llu };
    volatile uint64_t gas_price_{ 1 };
    bool missing_node_{ false };
    uint32_t consensus_shard_net_id_{ common::kInvalidUint32 };
    int32_t tcp_server_thread_count_ = 4;
    std::string ip_db_path_;
    std::unordered_map<uint64_t, uint16_t> thread_with_index_;
    uint8_t now_thread_idx_ = 0;
    uint8_t message_handler_thread_count_ = 4;
    bool for_ck_server_ = false;
    uint32_t each_shard_max_members_ = 1024u;

    DISALLOW_COPY_AND_ASSIGN(GlobalInfo);
};

}  // namespace common

}  // namespace zjchain
