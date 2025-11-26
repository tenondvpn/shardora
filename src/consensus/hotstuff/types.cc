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
    assert(proto_qc->network_id() <= network::kConsensusShardEndNetworkId);
    assert(proto_qc->pool_index() < common::kInvalidPoolIndex);
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
    assert(!tc_item.has_view_block_hash());
    return GetQCMsgHash(tc_item);
}

bool IsQcTcValid(const view_block::protobuf::QcItem& qc_item) {
#ifdef USE_AGG_BLS
    return qc_item.has_agg_sig();
#else
    return qc_item.has_sign_x();
#endif
}


// void QC::Serialize(std::string* res) {
//     proto_qc_->set_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign_->X));
//     proto_qc_->set_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign_->Y));
//     proto_qc_->set_sign_z(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign_->Z));
//     bool res = proto_qc_->SerializeToString(res);
//     if (!res) {
//         SHARDORA_FATAL("serilize protobuf failed!");
//     }
// }

// bool QC::Unserialize(const std::string& str) {
//     bool ok = proto_qc_->ParseFromString(str);
//     if (!ok) {
//         return false;
//     }
//     libff::alt_bn128_G1 sign = libff::alt_bn128_G1::zero();
//     try {
//         if (proto_qc_->sign_x() != "") {
//             sign.X = libff::alt_bn128_Fq(proto_qc_->sign_x().c_str());
//         }
//         if (proto_qc_->sign_y() != "") {
//             sign.Y = libff::alt_bn128_Fq(proto_qc_->sign_y().c_str());
//         }
//         if (proto_qc_->sign_z() != "") {
//             sign.Z = libff::alt_bn128_Fq(proto_qc_->sign_z().c_str());
//         }
//     } catch (...) {
//         return false;
//     }
    
//     *bls_agg_sign_ = sign;
//     return true;
// }

// HashStr GetViewBlockHash(const view_block::protobuf::ViewBlockItem& view_block_item) {
//     std::string qc_hash;
//     std::string block_hash;
//     std::string leader_consen_stat_hash;
//     if (view_block_item.has_qc()) {
//         qc_hash = GetQCMsgHash(view_block_item.qc());
//     }

//     if (view_block_item.has_block_info()) {
//         block_hash = GetBlockHash(view_block_item.block_info());
//     }

//     std::string msg;
//     msg.reserve(2048);
//     msg.append(qc_hash);
//     msg.append(block_hash);
//     msg.append(view_block_item.parent_hash());
//     auto leader_idx = view_block_item.qc().leader_idx();
//     msg.append((char*)&(leader_idx), sizeof(leader_idx));
//     auto view = view_block_item.qc().view();
//     msg.append((char*)&(view), sizeof(view));
//     msg.append(leader_consen_stat_hash);
//     if (view_block_item.has_leader_consen_stat()) {
//         uint32_t succ_num = view_block_item.leader_consen_stat().succ_num();
//         uint32_t fail_num = view_block_item.leader_consen_stat().fail_num();
//         msg.append((char*)&succ_num, sizeof(succ_num));
//         msg.append((char*)&fail_num, sizeof(fail_num));
//     }

//     SHARDORA_DEBUG("do hash qc_hash: %s, block hash: %s, parent_hash: %s, leader_idx: %u, view: %lu", 
//         common::Encode::HexEncode(qc_hash).c_str(), 
//         common::Encode::HexEncode(block_hash).c_str(),
//         common::Encode::HexEncode(view_block_item.parent_hash()).c_str(),
//         leader_idx,
//         view);
//     return common::Hash::keccak256(msg);
// }


std::shared_ptr<SyncInfo> new_sync_info() {
    return std::make_shared<SyncInfo>();
}

}

} // namespace shardora

