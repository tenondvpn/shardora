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
#include "common/time_utils.h"
#include "common/utils.h"

namespace shardora {

namespace transport {
    struct TransportMessage;
}

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

    void set_main_inited_success() {
        main_inited_success_ = true;
    }

    bool main_inited_success() const {
        return main_inited_success_;
    }

    // After running for a period of time, ensure that all threads have been created successfully and cancel the lock.
    uint8_t get_thread_index(std::shared_ptr<transport::TransportMessage> msg_ptr = nullptr);

    void set_global_stoped() {
        global_stoped_ = true;
    }

    bool global_stoped() const {
        return global_stoped_;
    }

    uint8_t get_consensus_thread_idx(uint8_t thread_idx) {
        return consensus_thread_index_map_[thread_idx];
    }

    uint8_t SetConsensusRealThreadIdx(uint8_t thread_idx) {
        std::lock_guard<std::mutex> g(now_valid_thread_index_mutex_);
        auto bft_thread = message_handler_thread_count_;
        for (uint8_t i = 0; i < bft_thread; ++i) {
            if (consensus_thread_index_map_[i] == common::kInvalidUint8) {
                consensus_thread_index_map_[i] = thread_idx;
                if (i == message_handler_thread_count_ - 1) {
                    uint32_t tmp_pools_with_thread[common::kInvalidPoolIndex] = { 0 };
                    for (uint8_t src_thread_idx = 0; src_thread_idx < i; ++src_thread_idx) {
                        std::string tmp_str;
                        auto valid_thread_idx = consensus_thread_index_map_[src_thread_idx];
                        thread_with_pools_[valid_thread_idx].clear();
                        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
                            if (pools_with_thread_[pool_idx] == src_thread_idx) {
                                tmp_pools_with_thread[pool_idx] = valid_thread_idx;
                                thread_with_pools_[valid_thread_idx].insert(pool_idx);
                                tmp_str += std::to_string(pool_idx) + ", ";
                            }
                        }

                        ZJC_DEBUG("thread: %d, src_thread_idx: %d, pools: %s", 
                            valid_thread_idx, src_thread_idx, tmp_str.c_str());
                    }

                    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
                        pools_with_thread_[pool_idx] = tmp_pools_with_thread[pool_idx];
                    }
                }

                ZJC_INFO("thread index %d set cosensus index: %d", thread_idx, i);
                return i;
            }
        }

        ZJC_FATAL("invalid thread idx: %d, bft_thread: %d", thread_idx, bft_thread);
        return common::kInvalidUint8;
    }

    uint32_t pools_each_thread_max_messages() const {
        return pools_each_thread_max_messages_;
    }

    uint32_t each_tx_pool_max_txs() const {
        return each_tx_pool_max_txs_;
    }

private:
    GlobalInfo();
    ~GlobalInfo();

    static const uint32_t kDefaultTestNetworkShardId = 4u;
    static const uint32_t kTickThreadPoolCount = 1U;

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
    uint8_t message_handler_thread_count_ = 8;
    bool for_ck_server_ = false;
    std::string ck_host_ = "127.0.0.1";
    uint16_t ck_port_ = 9000;
    std::string ck_user_ = "default";
    std::string ck_pass_ = "";
    uint32_t each_shard_max_members_ = 1024u;
    uint32_t sharding_min_nodes_count_ = 2u;
    int32_t join_root_ = common::kJoinRoot;
    std::set<uint32_t>* thread_with_pools_ = nullptr;
    uint8_t consensus_thread_index_map_[common::kMaxThreadCount] = {common::kInvalidUint8};
    uint32_t pools_with_thread_[common::kInvalidPoolIndex] = { 0 };
    uint8_t now_valid_thread_index_ = 0;
    std::mutex now_valid_thread_index_mutex_;
    uint64_t begin_run_timestamp_ms_ = 0;
    volatile bool should_check_thread_all_valid_ = true;
    std::unordered_map<int, uint8_t> valid_thread_index_;
    volatile bool global_stoped_ = false;
    volatile bool main_inited_success_ = false;
    uint32_t pools_each_thread_max_messages_ = 2048u;
    uint32_t each_tx_pool_max_txs_ = 20240u;

    DISALLOW_COPY_AND_ASSIGN(GlobalInfo);
};

}  // namespace common

}  // namespace shardora
