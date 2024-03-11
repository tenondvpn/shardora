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

    uint32_t sharding_min_nodes_count() {
        return sharding_min_nodes_count_;
    }

    int32_t join_root() const {
        return join_root_;
    }

    std::string ck_host() const {
        return ck_host_;
    }

    uint16_t ck_port() const {
        return ck_port_;
    }

    std::string ck_user() const {
        return ck_user_;
    }

    std::string ck_pass() const {
        return ck_pass_;
    }

    const std::set<uint32_t>* thread_with_pools() const {
        return thread_with_pools_;
    }

    const uint32_t* pools_with_thread() const {
        return pools_with_thread_;
    }

    uint8_t now_valid_thread_index() {
        std::lock_guard<std::mutex> g(now_valid_thread_index_mutex_);
        if (now_valid_thread_index_ >= common::kMaxThreadCount) {
            ZJC_FATAL("invalid thread count max: %d", common::kMaxThreadCount);
        }

        ZJC_INFO("new thread index: %d", (now_valid_thread_index_ + 1));
        return ++now_valid_thread_index_;
    }

    // After running for a period of time, ensure that all threads have been created successfully and cancel the lock.
    uint8_t get_thread_index() {
        auto now_thread_id = std::thread::get_id();
        uint8_t thread_idx = 0;
        if (should_check_thread_all_valid_) {
            std::lock_guard<std::mutex> g(now_valid_thread_index_mutex_);
            auto iter = thread_with_index_.find(now_thread_id);
            if (iter == thread_with_index_.end()) {
                thread_idx = now_valid_thread_index_++;
                thread_with_index_[now_thread_id] = thread_idx;
            } else {
                thread_idx = iter->second;
            }
            
            auto now_tm = common::TimeUtils::TimestampMs();
            if (begin_run_timestamp_ms_ + kWaitingAllThreadValidPeriodMs < now_tm) {
                should_check_thread_all_valid_ = false;
            }
        } else {
            uint8_t thread_idx = 0;
            auto iter = thread_with_index_.find(now_thread_id);
            if (iter == thread_with_index_.end()) {
                ZJC_FATAL("invalid get new thread index.");
            }
                
            thread_idx = iter->second;
        }

        return thread_idx;
    }

private:
    GlobalInfo();
    ~GlobalInfo();

    static const uint32_t kDefaultTestNetworkShardId = 4u;
    static const uint32_t kTickThreadPoolCount = 1U;
    static const uint64_t kWaitingAllThreadValidPeriodMs = 10000lu;

    std::string config_local_ip_;
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
    int32_t tcp_server_thread_count_ = 4;
    std::string ip_db_path_;
    std::unordered_map<uint64_t, uint16_t> thread_with_index_;
    uint8_t now_thread_idx_ = 0;
    uint8_t message_handler_thread_count_ = 4;
    bool for_ck_server_ = false;
    std::string ck_host_ = "127.0.0.1";
    uint16_t ck_port_ = 9000;
    std::string ck_user_ = "default";
    std::string ck_pass_ = "";
    uint32_t each_shard_max_members_ = 1024u;
    uint32_t sharding_min_nodes_count_ = 2u;
    int32_t join_root_ = common::kRandom;
    std::set<uint32_t>* thread_with_pools_ = nullptr;
    uint32_t pools_with_thread_[common::kInvalidPoolIndex] = { 0 };
    uint8_t now_valid_thread_index_ = 0;
    std::mutex now_valid_thread_index_mutex_;
    uint64_t begin_run_timestamp_ms_ = 0;
    volatile bool should_check_thread_all_valid_ = true;
    std::unordered_map<int, uint8_t> valid_thread_index_;

    DISALLOW_COPY_AND_ASSIGN(GlobalInfo);
};

}  // namespace common

}  // namespace zjchain
