#include "broadcast/broadcast.h"

#include "common/utils.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "dht/base_dht.h"
#include "broadcast/broadcast_utils.h"

namespace shardora {

namespace broadcast {

Broadcast::Broadcast() {}

Broadcast::~Broadcast() {}

void Broadcast::Send(
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr,
        const std::vector<dht::NodePtr>& nodes) {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        transport::TcpTransport::Instance()->Send(
            nodes[i]->public_ip,
            nodes[i]->public_port,
            msg_ptr->header);
    }
}

}  // namespace broadcast

}  // namespace shardora
