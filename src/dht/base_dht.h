#pragma once

#include <vector>
#include <unordered_map>
#include <condition_variable>

#include "common/utils.h"
#include "common/tick.h"
#include "dht/dht_utils.h"
#include "protos/dht.pb.h"
#include "security/security.h"
#include "protos/transport.pb.h"
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"

namespace shardora {

namespace nat {
    class Detection;
}

namespace dht {

typedef std::vector<NodePtr> Dht;
typedef std::shared_ptr<Dht> DhtPtr;
typedef std::shared_ptr<const Dht> ConstDhtPtr;

#ifndef NDEBUG
#define CheckThreadIdValid() { \
    auto now_thread_id = std::this_thread::get_id(); \
     \
    if (local_thread_id_count_ >= 1) { \
        assert(local_thread_id_ == now_thread_id); \
    } else { \
        local_thread_id_ = now_thread_id; \
    } \
    if (local_thread_id_ != now_thread_id) { ++local_thread_id_count_; } \
}
#else
#define CheckThreadIdValid()
#endif

class BaseDht : public std::enable_shared_from_this<BaseDht> {
public:
    BaseDht(NodePtr& local_node);
    virtual ~BaseDht();
    virtual int Init(
        std::shared_ptr<security::Security>& security,
        BootstrapResponseCallback boot_cb,
        NewNodeJoinCallback node_join_cb);
    virtual int Destroy();
    virtual int Join(NodePtr& node);
    virtual void UniversalJoin(const NodePtr& node);
    virtual int Drop(NodePtr& node);
    virtual int Drop(const std::vector<std::string>& ids);
    virtual int Drop(const std::string& id);
    virtual int Drop(const std::string& ip, uint16_t port);
    virtual int Bootstrap(
        const std::vector<NodePtr>& boot_nodes,
        bool wait,
        int32_t sharding_id);
    virtual void HandleMessage(const transport::MessagePtr& msg);
    virtual bool CheckDestination(const std::string& des_dht_key, bool closest);
    virtual bool IsUniversal() { return false; }
    void SendToClosestNode(const transport::MessagePtr& msg);
    void RandomSend(const transport::MessagePtr& msg);
    void SendToDesNetworkNodes(const transport::MessagePtr& msg);
    int CheckJoin(NodePtr& node);

    ConstDhtPtr readonly_hash_sort_dht() {
        return readonly_hash_sort_dht_[valid_dht_idx];
    }

    const NodePtr& local_node() {
        return local_node_;
    }

    void SetBootstrapResponseCallback(BootstrapResponseCallback bootstrap_response_cb) {
        bootstrap_response_cb_ = bootstrap_response_cb;
    }

    void SetNewNodeJoinCallback(NewNodeJoinCallback node_join_cb) {
        node_join_cb_ = node_join_cb;
    }

    BootstrapResponseCallback bootstrap_response_cb() {
        return bootstrap_response_cb_;
    }

    NewNodeJoinCallback node_join_cb() {
        return node_join_cb_;
    }

    std::shared_ptr<security::Security> security() {
        return security_;
    }

    uint32_t valid_count() const {
        return valid_count_;
    }

protected:
    bool NodeValid(NodePtr& node);
    bool NodeJoined(NodePtr& node);
    void DhtDispatchMessage(const transport::MessagePtr& header);
    void ProcessBootstrapRequest(const transport::MessagePtr& header);
    void ProcessBootstrapResponse(const transport::MessagePtr& header);
    void ProcessRefreshNeighborsRequest(const transport::MessagePtr& header);
    void ProcessRefreshNeighborsResponse(const transport::MessagePtr& header);
    void ProcessConnectRequest(const transport::MessagePtr& header);
    void ProcessTimerRequest();
    void RefreshNeighbors();
    NodePtr FindNodeDirect(transport::protobuf::Header& message);
    void Connect(
        const std::string& des_ip,
        uint16_t des_port,
        const std::string& des_pubkey,
        int32_t src_sharding_id,
        bool response);
    void PrintDht();

    static const uint32_t kRefreshNeighborPeriod = 3 * 1000 * 1000u;
    static const uint32_t kHeartbeatMaxSendTimes = 5u;
    static const uint32_t kSendToClosestNodeCount = 3u;
    static const uint64_t kConnectTimeoutMs = 3000u;

    Dht dht_;
    ConstDhtPtr readonly_hash_sort_dht_[2] = {nullptr};
    uint32_t valid_dht_idx = 0;
    DhtPtr readonly_dht_ = nullptr;
    NodePtr local_node_{ nullptr };
    std::unordered_map<uint64_t, NodePtr> node_map_;
    volatile bool joined_{ false };
    bool wait_vpn_res_{ false };
    std::atomic<uint32_t> boot_res_count_{ 0 };
    common::Tick refresh_neighbors_tick_;
    BootstrapResponseCallback bootstrap_response_cb_{ nullptr };
    NewNodeJoinCallback node_join_cb_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    uint32_t prev_refresh_neighbor_tm_ = 0;
    std::unordered_map<uint64_t, uint64_t> connect_timeout_map_;
    bool is_universal_ = false;
    uint32_t valid_count_ = 0;
    common::Tick dht_tick_;
    std::unordered_map<std::string, std::vector<NodePtr>> waiting_refresh_nodes_map_;
    common::SpinMutex join_mutex_;

#ifndef NDEBUG
    std::thread::id local_thread_id_;
    std::atomic<uint64_t> local_thread_id_count_ = 0;
#endif

    DISALLOW_COPY_AND_ASSIGN(BaseDht);
};

typedef std::shared_ptr<BaseDht> BaseDhtPtr;

}  // namespace dht

}  // namespace shardora
