#include "common/global_info.h"

#include "common/random.h"
#include "common/hash.h"
#include "common/country_code.h"
#include "common/log.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace common {

static const std::string kAccountAddress("");

GlobalInfo* GlobalInfo::Instance() {
    static GlobalInfo ins;
    return &ins;
}

GlobalInfo::GlobalInfo() {
}

GlobalInfo::~GlobalInfo() {
    if (thread_with_pools_ != nullptr) {
        delete[] thread_with_pools_;
    }
}

void GlobalInfo::Timer() {
    for (uint32_t i = 0; i < 64; ++i) {
        auto count = shared_obj_count_[i].fetch_add(0);
        if (count <= 64) {
            continue;
        }
        
        ZJC_DEBUG("index %d get all shared object count now: %d", i, count);
    }

    tick_ptr_->CutOff(2000000lu, std::bind(&GlobalInfo::Timer, this));
}

int GlobalInfo::Init(const common::Config& config) {
    tick_ptr_ = std::make_shared<common::Tick>();
    tick_ptr_->CutOff(2000000lu, std::bind(&GlobalInfo::Timer, this));
    memset(consensus_thread_index_map_, common::kInvalidUint8, sizeof(consensus_thread_index_map_));
    begin_run_timestamp_ms_ = common::TimeUtils::TimestampMs() + 10000lu;
    message_handler_thread_count_ = 4;
    config.Get("zjchain", "consensus_thread_count", message_handler_thread_count_);
    message_handler_thread_count_ += 2;

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
    config.Get("zjchain", "each_tx_pool_max_txs", each_tx_pool_max_txs_);

    auto bft_thread = message_handler_thread_count_ - 1;
    thread_with_pools_ = new std::set<uint32_t>[common::kMaxThreadCount];
    auto each_thread_pools_count = common::kInvalidPoolIndex / bft_thread;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        auto thread_idx = (i / each_thread_pools_count) % bft_thread;
        pools_with_thread_[i] = thread_idx;
    }

    return kCommonSuccess;
}

uint8_t GlobalInfo::get_thread_index(std::shared_ptr<transport::TransportMessage> msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto now_thread_id_tmp = std::this_thread::get_id();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    uint32_t now_thread_id = *(uint32_t*)&now_thread_id_tmp;
    uint8_t thread_idx = 0;
    if (should_check_thread_all_valid_) {
        std::lock_guard<std::mutex> g(now_valid_thread_index_mutex_);
        auto iter = thread_with_index_.find(now_thread_id);
        if (iter == thread_with_index_.end()) {
            thread_idx = now_valid_thread_index_++;
            thread_with_index_[now_thread_id] = thread_idx;
            ZJC_DEBUG("success add thread: %u, thread_index: %d", now_thread_id, thread_idx);
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
            ZJC_FATAL("invalid get new thread index: %u", now_thread_id);
        }
            
        thread_idx = iter->second;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return thread_idx;
}

}  // namespace common

}  // namespace shardora
