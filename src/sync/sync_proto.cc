#include "stdafx.h"
#include "sync/proto/sync_proto.h"

#include "common/global_info.h"

namespace zjchain {

namespace sync {

void SyncProto::CreateSyncValueReqeust(
        const dht::NodePtr& local_node,
        const dht::NodePtr& des_node,
        const sync::protobuf::SyncMessage& sync_msg,
        transport::protobuf::Header& msg) {
    msg.set_src_dht_key(local_node->dht_key());
    msg.set_des_dht_key(des_node->dht_key());
    msg.set_priority(transport::kTransportPriorityHighest);
    msg.set_id(common::GlobalInfo::Instance()->MessageId());
    msg.set_type(common::kSyncMessage);
    msg.set_client(local_node->client_mode);
    msg.set_data(sync_msg.SerializeAsString());
    msg.set_hop_count(0);
}

void SyncProto::CreateSyncValueResponse(
        const dht::NodePtr& local_node,
        const transport::protobuf::Header& header,
        const sync::protobuf::SyncMessage& sync_msg,
        transport::protobuf::Header& msg) {
    msg.set_src_dht_key(local_node->dht_key());
    msg.set_des_dht_key(header.src_dht_key());
    msg.set_priority(header.priority());
    msg.set_id(header.id());
    msg.set_type(common::kSyncMessage);
    msg.set_from_ip(header.from_ip());
    msg.set_from_port(header.from_port());
    msg.set_transport_type(header.transport_type());
    if (header.has_debug()) {
        msg.set_debug(header.debug());
    }

    if (header.client()) {
        msg.set_client(header.client());
        msg.set_client_relayed(true);
        msg.set_client_proxy(header.client_proxy());
        msg.set_client_dht_key(header.client_dht_key());
        msg.set_client_handled(true);
    }

    msg.set_data(sync_msg.SerializeAsString());
    msg.set_hop_count(0);
}

}  // namespace sync

}  //namespace zjchain

