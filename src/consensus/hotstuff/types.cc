#include <common/log.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>

namespace shardora {

namespace hotstuff {

HashStr GetQCMsgHash(
        const View &view,
        const HashStr &view_block_hash,
        const HashStr& commit_view_block_hash,
        const uint64_t& elect_height,
        const uint32_t& leader_idx) {
    std::stringstream ss;
    ss << view << view_block_hash << commit_view_block_hash << elect_height << leader_idx;
    std::string msg = ss.str();
    return common::Hash::keccak256(msg); 
}

HashStr GetViewHash(const View& view, const uint64_t& elect_height, const uint32_t& leader_idx) {
    return GetQCMsgHash(view, "", "", elect_height, leader_idx);
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
    
    return true;
}

HashStr ViewBlock::DoHash() const {
    std::string qc_str;
    std::string block_hash;
    if (qc) {
        qc_str = qc->Serialize();
    }
    if (block) {
        block_hash = GetBlockHash(*block);
    }

    std::string msg;
    msg.reserve(qc_str.size() + block_hash.size() + parent_hash.size() + sizeof(leader_idx) + sizeof(view));
    msg.append(qc_str);
    msg.append(block_hash);
    msg.append(parent_hash);
    msg.append((char*)&(leader_idx), sizeof(leader_idx));
    msg.append((char*)&(view), sizeof(view));

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
    
    return Status::kSuccess;
}

std::shared_ptr<SyncInfo> new_sync_info() {
    return std::make_shared<SyncInfo>();
}

}

} // namespace shardora

