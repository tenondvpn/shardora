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

GlobalInfo::~GlobalInfo() {}

int GlobalInfo::Init(const common::Config& config) {
    message_handler_thread_count_ = 1;
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

    return kCommonSuccess;
}

}  // namespace common

}  // namespace zjchain
