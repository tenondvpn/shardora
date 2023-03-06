#pragma once

#include "bls/bls_manager.h"
#include "common/utils.h"
#include "consensus/zbft/zbft.h"
#include "dht/dht_utils.h"
#include "protos/hotstuff.pb.h"
#include "protos/transport.pb.h"
#include "pools/tx_pool.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class BftProto {
public:
    static bool LeaderCreatePrepare(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        transport::protobuf::Header& msg,
        hotstuff::protobuf::HotstuffMessage* pipeline_msg);
    static bool BackupCreatePrepare(
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const transport::protobuf::Header& from_header,
        const hotstuff::protobuf::HotstuffMessage& from_bft_msg,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg,
        hotstuff::protobuf::HotstuffMessage* pipeline_msg);
    static bool LeaderCreatePreCommit(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        bool oppose,
        transport::protobuf::Header& msg);
    static bool BackupCreatePreCommit(
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const transport::protobuf::Header& from_header,
        const hotstuff::protobuf::HotstuffMessage& from_bft_msg,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg);
    static bool LeaderCreateCommit(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg);
    static bool CreateLeaderBroadcastToAccount(
        uint32_t net_id,
        uint32_t message_type,
        uint32_t bft_step,
        bool universal,
        const std::shared_ptr<block::protobuf::Block>& block_ptr,
        uint32_t local_member_index,
        transport::protobuf::Header& msg);
    static void SetLocalPublicIpPort(
        const dht::NodePtr& local_node,
        hotstuff::protobuf::HotstuffMessage& bft_msg);

    DISALLOW_COPY_AND_ASSIGN(BftProto);
};

}  // namespace consensus

}  // namespace zjchain
