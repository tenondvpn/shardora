#pragma once

#include <condition_variable>
#include <mutex>

#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/utils.h"
#include "network/network_utils.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace dht {
    class BaseDht;
    typedef std::shared_ptr<BaseDht> BaseDhtPtr;
}  // namespace dht

namespace broadcast {
    class Broadcast;
    typedef std::shared_ptr<Broadcast> BroadcastPtr;
}  // namespace broadcast

namespace network {

class Route {
public:
    static Route* Instance();
    int Send(const transport::MessagePtr& message);
    void RegisterMessage(uint32_t type, transport::MessageProcessor proc);
    void UnRegisterMessage(uint32_t type);
    void Init(std::shared_ptr<security::Security> sec_ptr);
    void Destroy();
    dht::BaseDhtPtr GetDht(const std::string& dht_key);
    void RouteByUniversal(const transport::MessagePtr& header);
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block);
private:
    Route();
    ~Route();
    void HandleMessage(const transport::MessagePtr& header);
    void HandleDhtMessage(const transport::MessagePtr& header);
    void Broadcast(const transport::MessagePtr& header);
    void Broadcasting();
    bool CheckPoolsMessage(const transport::MessagePtr& header_ptr, dht::BaseDhtPtr dht_ptr);

    static const uint64_t kBroadcastPeriod = 10000lu;

    transport::MessageProcessor message_processor_[common::kMaxMessageTypeCount];
    broadcast::BroadcastPtr broadcast_{ nullptr };
    typedef common::ThreadSafeQueue<transport::MessagePtr> BroadcastQueue;
    BroadcastQueue* broadcast_queue_ = nullptr;
    common::Tick broadcast_tick_;
    std::shared_ptr<security::Security> sec_ptr_ = nullptr;
    // broadcast con and mutex
    std::condition_variable broadcast_con_;
    std::mutex broadcast_mu_;
    std::shared_ptr<std::thread> broadcast_thread_ = nullptr;
    bool destroy_ = false;
    std::atomic<uint64_t> latest_elect_height_[network::kConsensusShardEndNetworkId + 1] = {0};
    std::atomic<common::MembersPtr> all_shard_members_[network::kConsensusShardEndNetworkId + 1] = {nullptr};

    DISALLOW_COPY_AND_ASSIGN(Route);
};

}  // namespace network

}  // namespace shardora
