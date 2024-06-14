#include <common/log.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include "network/network_utils.h"

namespace shardora {

namespace hotstuff {

HashStr GetQCMsgHash(
        uint32_t net_id,
        uint32_t pool_index,
        const View &view,
        const HashStr &view_block_hash,
        const HashStr& commit_view_block_hash,
        uint64_t elect_height,
        uint32_t leader_idx) {
    std::stringstream ss;
    assert(net_id <= network::kConsensusShardEndNetworkId);
    assert(pool_index < common::kInvalidPoolIndex);
    ss << net_id << pool_index << view <<
        view_block_hash << commit_view_block_hash <<
        elect_height << leader_idx;
    std::string msg = ss.str();
    auto msg_hash = common::Hash::keccak256(msg); 
    ZJC_DEBUG("success get qc msg hash net: %u, pool: %u, view: %lu, view_block_hash: %s, "
        "commit_view_block_hash: %s, elect_height: %lu, leader_idx: %u, msg_hash: %s",
        net_id,
        pool_index,
        view, 
        common::Encode::HexEncode(view_block_hash).c_str(),
        common::Encode::HexEncode(commit_view_block_hash).c_str(),
        elect_height,
        leader_idx,
        common::Encode::HexEncode(msg_hash).c_str());
    return msg_hash; 
}

HashStr GetViewHash(
        uint32_t net_id,
        uint32_t pool_index, 
        const View& view, 
        uint64_t elect_height, 
        uint32_t leader_idx) {
    return GetQCMsgHash(net_id, pool_index, view, "", "", elect_height, leader_idx);
}

std::string QC::Serialize() const {
    auto qc_proto = view_block::protobuf::QC();
        
    qc_proto.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->X));
    qc_proto.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->Y));
    qc_proto.set_sign_z(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->Z));
    qc_proto.set_view(view);
    qc_proto.set_view_block_hash(view_block_hash);
    qc_proto.set_commit_view_block_hash(commit_view_block_hash);
    qc_proto.set_elect_height(elect_height);
    qc_proto.set_leader_idx(leader_idx);
    qc_proto.set_network_id(network_id);
    qc_proto.set_pool_index(pool_index);
    // TODO 不同版本 pb 结果不一样
    return qc_proto.SerializeAsString();
}

bool QC::Unserialize(const std::string& str) {
    auto qc_proto = view_block::protobuf::QC();
    bool ok = qc_proto.ParseFromString(str);
    if (!ok) {
        return false;
    }
    libff::alt_bn128_G1 sign = libff::alt_bn128_G1::zero();
    try {
        if (qc_proto.sign_x() != "") {
            sign.X = libff::alt_bn128_Fq(qc_proto.sign_x().c_str());
        }
        if (qc_proto.sign_y() != "") {
            sign.Y = libff::alt_bn128_Fq(qc_proto.sign_y().c_str());
        }
        if (qc_proto.sign_z() != "") {
            sign.Z = libff::alt_bn128_Fq(qc_proto.sign_z().c_str());
        }
    } catch (...) {
        return false;
    }
    
    *bls_agg_sign = sign;
    view = qc_proto.view();
    view_block_hash = qc_proto.view_block_hash();
    commit_view_block_hash = qc_proto.commit_view_block_hash();
    elect_height = qc_proto.elect_height();
    leader_idx = qc_proto.leader_idx();
    network_id = qc_proto.network_id();
    pool_index = qc_proto.pool_index();
    
    return true;
}

HashStr ViewBlock::DoHash() const {
    std::string qc_hash;
    std::string block_hash;
    std::string leader_consen_stat_hash;
    if (qc) {
        qc_hash = qc->msg_hash();
    }
    if (block) {
        block_hash = GetBlockHash(*block);
    }
    if (leader_consen_stat) {
        leader_consen_stat_hash = leader_consen_stat->GetHash();
    }

    std::string msg;
    msg.reserve(qc_hash.size() + block_hash.size() + parent_hash.size() + sizeof(leader_idx) + sizeof(view) + leader_consen_stat_hash.size());
    msg.append(qc_hash);
    msg.append(block_hash);
    msg.append(parent_hash);
    msg.append((char*)&(leader_idx), sizeof(leader_idx));
    msg.append((char*)&(view), sizeof(view));
    msg.append(leader_consen_stat_hash);

    ZJC_DEBUG("do hash qc_hash: %s, block hash: %s, parent_hash: %s, leader_idx: %u, view: %lu", 
        common::Encode::HexEncode(qc_hash).c_str(), 
        common::Encode::HexEncode(block_hash).c_str(),
        common::Encode::HexEncode(parent_hash).c_str(),
        leader_idx,
        view);
    return common::Hash::keccak256(msg);
}

void ViewBlock2Proto(const std::shared_ptr<ViewBlock>& view_block, view_block::protobuf::ViewBlockItem* view_block_proto) {
    view_block_proto->set_hash(view_block->hash);
    view_block_proto->set_parent_hash(view_block->parent_hash);
    view_block_proto->set_leader_idx(view_block->leader_idx);
    if (view_block->block) {
        auto* block_info = view_block_proto->mutable_block_info();
        *block_info = *view_block->block;
    }
    if (view_block->qc) {
        view_block_proto->set_qc_str(view_block->qc->Serialize());
    } else {
        view_block_proto->set_qc_str("");
    }
    if (view_block->leader_consen_stat) {
        auto stat = view_block_proto->mutable_leader_consen_stat();
        stat->set_succ_num(view_block->leader_consen_stat->succ_num);
        stat->set_fail_num(view_block->leader_consen_stat->fail_num);
    }
    view_block_proto->set_view(view_block->view);
}

Status Proto2ViewBlock(const view_block::protobuf::ViewBlockItem& view_block_proto, std::shared_ptr<ViewBlock>& view_block) {
    view_block->hash = view_block_proto.hash();
    view_block->parent_hash = view_block_proto.parent_hash();
    view_block->leader_idx = view_block_proto.leader_idx();
    view_block->view = view_block_proto.view();
    
    if (!view_block_proto.has_block_info()) {
        view_block->block = nullptr;
    } else {
        view_block->block = std::make_shared<block::protobuf::Block>(view_block_proto.block_info());
    }

    if (!view_block_proto.has_qc_str() || view_block_proto.qc_str() == "") {
        view_block->qc = nullptr;
    } else {
        view_block->qc = std::make_shared<QC>();
        if (!view_block->qc->Unserialize(view_block_proto.qc_str())) {
            return Status::kError;
        }
    }

    if (!view_block_proto.has_leader_consen_stat()) {
        view_block->leader_consen_stat = nullptr;
    } else {
        view_block->leader_consen_stat = std::make_shared<MemberConsensusStat>(
                view_block_proto.leader_consen_stat().succ_num(),
                view_block_proto.leader_consen_stat().fail_num());
    }
    
    return Status::kSuccess;
}

std::shared_ptr<SyncInfo> new_sync_info() {
    return std::make_shared<SyncInfo>();
}

}

} // namespace shardora

