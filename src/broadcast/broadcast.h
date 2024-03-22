#pragma once

#include <memory>
#include <unordered_map>

#include "broadcast/broadcast_utils.h"
#include "common/utils.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace dht {
    class BaseDht;
    typedef std::shared_ptr<BaseDht> BaseDhtPtr;
    class Node;
    typedef std::shared_ptr<Node> NodePtr;
}  // namespace dht

namespace broadcast {

class Broadcast {
public:
    virtual void Broadcasting(
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& message) = 0;

protected:
    Broadcast();
    virtual ~Broadcast();
    void Send(
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& message,
        const std::vector<dht::NodePtr>& nodes);
    inline uint32_t GetNeighborCount(const transport::protobuf::Header& message) {
        if (message.broadcast().has_neighbor_count()) {
            return message.broadcast().neighbor_count();
        }

        return kBroadcastDefaultNeighborCount;
    }

private:
    static const uint32_t kMaxMessageHashCount = 10u * 1024u * 1024u;

    DISALLOW_COPY_AND_ASSIGN(Broadcast);
};

}  // namespace broadcast

}  // namespace shardora
