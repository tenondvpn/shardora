#include "common/global_info.h"

#include "common/random.h"
#include "common/hash.h"
#include "common/country_code.h"
#include "common/log.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "transport/transport_utils.h"

#include <cstdlib>

namespace shardora {

namespace common {

static const std::string kAccountAddress("");

namespace {

std::string NormalizePath(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    return path.empty() ? "." : path;
}

}  // namespace

GlobalInfo* GlobalInfo::Instance() {
    static GlobalInfo ins;
    return &ins;
}

GlobalInfo::GlobalInfo() {
}

GlobalInfo::~GlobalInfo() {
}

void GlobalInfo::Timer() {
    for (uint32_t i = 0; i < 64; ++i) {
        auto count = shared_obj_count_[i].fetch_add(0);
        if (count <= 64) {
            continue;
        }

        if (count > shared_obj_max_count_[i]) {
            shared_obj_max_count_[i] = count;
        }

        SHARDORA_DEBUG("index %d get all shared object count now: %d, max: %d", 
            i, count, shared_obj_max_count_[i]);
    }

    tick_ptr_->CutOff(20000000lu, std::bind(&GlobalInfo::Timer, this));
}

int GlobalInfo::Init(const common::Config& config) {
#ifndef NDEBUG
    tick_ptr_ = std::make_shared<common::Tick>();
    tick_ptr_->CutOff(2000000lu, std::bind(&GlobalInfo::Timer, this));
#endif
    begin_run_timestamp_ms_ = common::TimeUtils::TimestampMs() + 10000lu;
    config.Get("shardora", "consensus_thread_count", hotstuff_thread_count_);
    message_handler_thread_count_ = hotstuff_thread_count_ + 2;

    if (!config.Get("shardora", "local_ip", config_local_ip_)) {
        SHARDORA_ERROR("get shardora local_ip from config failed.");
    }

    config.Get("shardora", "local_port", config_local_port_);
    if (!config.Get("shardora", "http_port", http_port_)) {
        http_port_ = 0;
    }
       
    config.Get("shardora", "sharding_min_nodes_count", sharding_min_nodes_count_);
    config.Get("shardora", "for_ck", for_ck_server_);
    config.Get("shardora", "each_shard_max_members", each_shard_max_members_);
    config.Get("shardora", "join_root", join_root_);
    std::string str_contry;
    if (!config.Get("shardora", "country", str_contry) || str_contry.empty()) {
        SHARDORA_ERROR("get shardora country from config failed.");
    }

    if (!config.Get("shardora", "first_node", config_first_node_)) {
        SHARDORA_ERROR("get shardora first_node from config failed.");
    }

    config.Get("shardora", "public_ip", config_public_ip_);
    config.Get("shardora", "public_port", config_public_port_);
    config.Get("shardora", "node_tag", node_tag_);
    ip_db_path_ = "./conf/GeoLite2-City.mmdb";
    config.Get("shardora", "ip_db_path", ip_db_path_);
    const char* env_root_path = std::getenv("SHARDORA_ROOT");
    if (env_root_path != nullptr && env_root_path[0] != '\0') {
        root_path_ = env_root_path;
    }
    config.Get("shardora", "root_path", root_path_);
    root_path_ = NormalizePath(root_path_);
    server_cert_path_ = RootPathFile("server-cert.pem");
    server_key_path_ = RootPathFile("server-key.pem");
    config.Get("shardora", "server_cert_path", server_cert_path_);
    config.Get("shardora", "server_key_path", server_key_path_);
    config.Get("shardora", "missing_node", missing_node_);
    config.Get("shardora", "ck_port", ck_port_);
    config.Get("shardora", "ck_host", ck_host_);
    config.Get("shardora", "ck_user", ck_user_);
    config.Get("shardora", "ck_pass", ck_pass_);
    config.Get("shardora", "tx_user_qps_limit_window_sconds", tx_user_qps_limit_window_sconds_);
    config.Get("shardora", "tx_user_qps_limit_window", tx_user_qps_limit_window_);
    config.Get("shardora", "each_tx_pool_max_txs", each_tx_pool_max_txs_);
    config.Get("shardora", "test_pool_index", test_pool_index_);
    config.Get("shardora", "test_tx_tps", test_tx_tps_);
    config.Get("shardora", "leader_change_init_tm", leader_change_init_tm_);

    if (each_tx_pool_max_txs_ < 10240) {
        each_tx_pool_max_txs_ = 10240;
    }
  
    return kCommonSuccess;
}

std::string GlobalInfo::RootPathFile(const std::string& file_name) const {
    if (file_name.empty()) {
        return root_path_;
    }
    if (file_name.front() == '/' || file_name.front() == '\\') {
        return file_name;
    }
    return root_path_ + "/" + file_name;
}

uint8_t GlobalInfo::get_thread_index() {
    auto now_thread_id_tmp = std::this_thread::get_id();
    uint32_t now_thread_id = *(uint32_t*)&now_thread_id_tmp;
    uint8_t thread_idx = 0;
    if (should_check_thread_all_valid_) {
        std::lock_guard<std::mutex> g(now_valid_thread_index_mutex_);
        auto iter = thread_with_index_.find(now_thread_id);
        if (iter == thread_with_index_.end()) {
            thread_idx = now_valid_thread_index_++;
            thread_with_index_[now_thread_id] = thread_idx;
            SHARDORA_DEBUG("success add thread: %u, thread_index: %d", now_thread_id, thread_idx);
        } else {
            thread_idx = iter->second;
        }
        
        auto now_tm_ms = common::TimeUtils::TimestampMs();
        if (main_inited_success_ && begin_run_timestamp_ms_ <= now_tm_ms) {
            should_check_thread_all_valid_ = false;
        }
    } else {
        auto iter = thread_with_index_.find(now_thread_id);
        if (iter == thread_with_index_.end()) {
            SHARDORA_FATAL("invalid get new thread index: %u", now_thread_id);
        }
            
        thread_idx = iter->second;
    }

    return thread_idx;
}

}  // namespace common

}  // namespace shardora
