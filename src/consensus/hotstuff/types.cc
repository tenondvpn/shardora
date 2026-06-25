#include <common/log.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include "network/network_utils.h"

namespace shardora {

namespace hotstuff {

HashStr GetQCMsgHash(const view_block::protobuf::QcItem& qc_item) {
    auto* proto_qc = &qc_item;
    std::stringstream ss;    
    //assert(proto_qc->network_id() <= network::kConsensusShardEndNetworkId);
    //assert(proto_qc->pool_index() < common::kInvalidPoolIndex);
    ss << proto_qc->network_id() << proto_qc->pool_index() << proto_qc->view() <<
        proto_qc->view_block_hash() <<
        proto_qc->elect_height() << proto_qc->leader_idx();
    std::string msg = ss.str();
    auto msg_hash = common::Hash::keccak256(msg); 
    SHARDORA_DEBUG("success get qc msg hash net: %u, pool: %u, view: %lu, view_block_hash: %s, "
        " elect_height: %lu, leader_idx: %u, msg_hash: %s",
        proto_qc->network_id(),
        proto_qc->pool_index(),
        proto_qc->view(), 
        common::Encode::HexEncode(proto_qc->view_block_hash()).c_str(),
        proto_qc->elect_height(),
        proto_qc->leader_idx(),
        common::Encode::HexEncode(msg_hash).c_str());
    return msg_hash; 
}

HashStr GetTCMsgHash(const view_block::protobuf::QcItem& tc_item) {
    //assert(!tc_item.has_view_block_hash());
    return GetQCMsgHash(tc_item);
}

bool IsQcTcValid(const view_block::protobuf::QcItem& qc_item) {
    return qc_item.has_sign_x();
}

std::shared_ptr<SyncInfo> new_sync_info() {
    return std::make_shared<SyncInfo>();
}

}

} // namespace shardora

