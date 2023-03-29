#include "vss/proto/vss_proto.h"

#include "common/user_property_key_define.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "security/security.h"
#include "security/ecdh_create_key.h"
#include "security/crypto.h"
#include "transport/transport_utils.h"
#include "vss/vss_utils.h"

namespace zjchain {

namespace vss {

void VssProto::CreateHashMessage(
        const dht::NodePtr& local_node,
        uint64_t random_hash,
        uint64_t tm_height,
        uint64_t elect_height,
        transport::protobuf::Header& msg) {
    msg.set_src_dht_key(local_node->dht_key());
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId, 0);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_priority(transport::kTransportPriorityHigh);
    msg.set_id(common::GlobalInfo::Instance()->MessageId());
    msg.set_type(common::kVssMessage);
    msg.set_client(local_node->client_mode);
    msg.set_universal(false);
    msg.set_hop_count(0);
    vss::protobuf::VssMessage vss_msg;
    vss_msg.set_random_hash(random_hash);
    vss_msg.set_tm_height(tm_height);
    vss_msg.set_elect_height(elect_height);
    vss_msg.set_type(kVssRandomHash);
    std::string hash_str = std::to_string(random_hash) + "_" +
        std::to_string(tm_height) + "_" +
        std::to_string(elect_height) + "_" +
        common::GlobalInfo::Instance()->id();
    auto message_hash = common::Hash::keccak256(hash_str);
    security::Signature sign;
    bool sign_res = security::Security::Instance()->Sign(
        message_hash,
        *(security::Security::Instance()->prikey()),
        *(security::Security::Instance()->pubkey()),
        sign);
    if (!sign_res) {
        ZJC_ERROR("signature error.");
        return;
    }

    std::string sign_challenge_str;
    std::string sign_response_str;
    sign.Serialize(sign_challenge_str, sign_response_str);
    vss_msg.set_sign_ch(sign_challenge_str);
    vss_msg.set_sign_res(sign_response_str);
    vss_msg.set_pubkey(security::Security::Instance()->str_pubkey());
    auto broad_param = msg.mutable_broadcast();
    transport::SetDefaultBroadcastParam(broad_param);
    msg.set_data(vss_msg.SerializeAsString());
}

void VssProto::CreateRandomMessage(
        const dht::NodePtr& local_node,
        uint32_t type,
        uint64_t random,
        uint64_t tm_height,
        uint64_t elect_height,
        transport::protobuf::Header& msg) {
    msg.set_src_dht_key(local_node->dht_key());
    msg.set_priority(transport::kTransportPriorityHigh);
    msg.set_id(common::GlobalInfo::Instance()->MessageId());
    msg.set_type(common::kVssMessage);
    msg.set_client(local_node->client_mode);
    msg.set_universal(false);
    msg.set_hop_count(0);
    msg.set_debug("random_msg_debug");
    vss::protobuf::VssMessage vss_msg;
    vss_msg.set_random(random);
    vss_msg.set_tm_height(tm_height);
    vss_msg.set_elect_height(elect_height);
    vss_msg.set_type(type);
    std::string hash_str = std::to_string(random) + "_" +
        std::to_string(tm_height) + "_" +
        std::to_string(elect_height) + "_" +
        common::GlobalInfo::Instance()->id();
    auto message_hash = common::Hash::keccak256(hash_str);
    security::Signature sign;
    bool sign_res = security::Security::Instance()->Sign(
        message_hash,
        *(security::Security::Instance()->prikey()),
        *(security::Security::Instance()->pubkey()),
        sign);
    if (!sign_res) {
        ZJC_ERROR("signature error.");
        return;
    }

    std::string sign_challenge_str;
    std::string sign_response_str;
    sign.Serialize(sign_challenge_str, sign_response_str);
    vss_msg.set_sign_ch(sign_challenge_str);
    vss_msg.set_sign_res(sign_response_str);
    vss_msg.set_pubkey(security::Security::Instance()->str_pubkey());
    auto broad_param = msg.mutable_broadcast();
    transport::SetDefaultBroadcastParam(broad_param);
    msg.set_data(vss_msg.SerializeAsString());
}

}  // namespace vss

}  // namespace zjchain
