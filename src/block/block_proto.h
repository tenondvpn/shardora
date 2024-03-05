#pragma once

#include "common/utils.h"
#include "common/global_info.h"
#include "dht/dht_utils.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "network/universal.h"
#include "network/universal_manager.h"
#include "protos/block.pb.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace block {

class BlockProto {
public:
    static void CreateGetBlockResponse(
            const dht::NodePtr& local_node,
            const transport::protobuf::Header& header,
            const std::string& block_data,
            transport::protobuf::Header& msg) {
//         msg.set_src_dht_key(local_node->dht_key());
//         msg.set_des_dht_key(header.src_dht_key());
//         msg.set_priority(transport::kTransportPriorityLow);
//         msg.set_id(header.id());
//         msg.set_type(common::kBlockMessage);
//         msg.set_universal(header.universal());
//         msg.set_client(header.client());
//         msg.set_from_ip(header.from_ip());
//         msg.set_from_port(header.from_port());
//         msg.set_transport_type(header.transport_type());
//         if (header.has_debug()) {
//             msg.set_debug(header.debug());
//         }
// 
//         if (header.client()) {
//             msg.set_client_relayed(true);
//             msg.set_client_proxy(header.client_proxy());
//             msg.set_client_dht_key(header.client_dht_key());
//             msg.set_client_handled(true);
//         }
//         msg.set_hop_count(0);
//         msg.set_data(block_data);
    }

    static void AccountAttrRequest(
            const dht::NodePtr& local_node,
            const std::string& account,
            const std::string& attr,
            uint64_t height,
            transport::protobuf::Header& msg) {
//         msg.set_src_dht_key(local_node->dht_key());
//         uint32_t des_net_id = common::GlobalInfo::Instance()->network_id();
//         dht::DhtKeyManager dht_key(des_net_id);
//         msg.set_des_dht_key(dht_key.StrKey());
//         msg.set_des_dht_key_hash(common::Hash::Hash64(dht_key.StrKey()));
//         msg.set_priority(transport::kTransportPriorityMiddle);
//         msg.set_id(common::GlobalInfo::Instance()->MessageId());
//         msg.set_universal(false);
//         msg.set_type(common::kBlockMessage);
//         msg.set_hop_count(0);
//         msg.set_client(false);
//         block::protobuf::BlockMessage block_msg;
//         auto attr_req = block_msg.mutable_acc_attr_req();
//         attr_req->set_account(account);
//         attr_req->set_attr_key(attr);
//         attr_req->set_height(height);
//         msg.set_data(block_msg.SerializeAsString());
    }

    static void CreateAccountShardRequest(transport::protobuf::Header& msg) {
//         auto uni_dht = std::dynamic_pointer_cast<network::Universal>(
//             network::UniversalManager::Instance()->GetUniversal(
//                 network::kUniversalNetworkId));
//         if (!uni_dht) {
//             return;
//         }
// 
//         msg.set_src_dht_key(uni_dht->local_node()->dht_key());
//         uint32_t des_net_id = network::kRootCongressNetworkId;
//         dht::DhtKeyManager dht_key(des_net_id);
//         msg.set_des_dht_key(dht_key.StrKey());
//         msg.set_priority(transport::kTransportPriorityHighest);
//         msg.set_id(common::GlobalInfo::Instance()->MessageId());
//         msg.set_type(common::kBlockMessage);
//         msg.set_client(false);
//         msg.set_version(common::GlobalInfo::Instance()->version());
//         msg.set_hop_count(0);
//         block::protobuf::BlockMessage block_msg;
//         auto acc_shard_req = block_msg.mutable_acc_shard_req();
//         acc_shard_req->set_id(common::GlobalInfo::Instance()->id());
//         msg.set_data(block_msg.SerializeAsString());
    }

    static void CreateAccountShardResponse(
            const transport::protobuf::Header& header,
            const std::string& req_id,
            uint32_t shard_id,
            transport::protobuf::Header& msg) {
//         msg.set_src_dht_key(header.des_dht_key());
//         msg.set_des_dht_key(header.src_dht_key());
//         msg.set_priority(transport::kTransportPriorityHighest);
//         msg.set_id(common::GlobalInfo::Instance()->MessageId());
//         msg.set_type(common::kBlockMessage);
//         msg.set_client(false);
//         msg.set_hop_count(0);
//         block::protobuf::BlockMessage block_msg;
//         auto acc_shard_req = block_msg.mutable_acc_shard_res();
//         acc_shard_req->set_id(req_id);
//         acc_shard_req->set_shard_id(shard_id);
//         msg.set_data(block_msg.SerializeAsString());
    }

private:
    BlockProto();
    ~BlockProto();
    DISALLOW_COPY_AND_ASSIGN(BlockProto);
};

}  // namespace block

}  // namespace zjchain
