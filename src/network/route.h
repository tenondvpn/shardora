#pragma once

#include <condition_variable>
#include <mutex>

#include "common/utils.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

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
    void Init();
    void Destroy();
    dht::BaseDhtPtr GetDht(const std::string& dht_key);
    void RouteByUniversal(const transport::MessagePtr& header);

private:
    Route();
    ~Route();
    void HandleMessage(const transport::MessagePtr& header);
    void HandleDhtMessage(const transport::MessagePtr& header);
    void Broadcast(uint8_t thread_idx, const transport::MessagePtr& header);
    void Broadcasting(uint8_t thread_idx);

    static const uint64_t kBroadcastPeriod = 10000lu;

    transport::MessageProcessor message_processor_[common::kLegoMaxMessageTypeCount];
    broadcast::BroadcastPtr broadcast_{ nullptr };
    typedef common::ThreadSafeQueue<transport::MessagePtr> BroadcastQueue;
    BroadcastQueue* broadcast_queue_ = nullptr;
    common::Tick broadcast_tick_;

    // broadcast con and mutex
    std::condition_variable broadcast_con_;
    std::mutex broadcast_mu_;
    std::shared_ptr<std::thread> broadcast_thread_ = nullptr;
    bool destroy_ = false;

    DISALLOW_COPY_AND_ASSIGN(Route);
};

}  // namespace network

}  // namespace zjchain
