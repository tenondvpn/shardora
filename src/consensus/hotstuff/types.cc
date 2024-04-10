#include <consensus/hotstuff/types.h>

namespace shardora {

namespace consensus {

std::string QC::Serialize() const {
    auto qc_proto = view_block::protobuf::QC();
        
    std::stringstream ss;
    if (bls_agg_sign) {
        qc_proto.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->X));
        qc_proto.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->Y));
        qc_proto.set_sign_z(libBLS::ThresholdUtils::fieldElementToString(bls_agg_sign->Z));
    }
    qc_proto.set_view(view);
    qc_proto.set_view_block_hash(view_block_hash);
    for (auto parti : participants) {
        qc_proto.add_participants(parti);
    }
        
    return qc_proto.SerializeAsString();
}

bool QC::Unserialize(const std::string& str) {
    auto qc_proto = view_block::protobuf::QC();
    bool ok = qc_proto.ParseFromString(str);
    if (!ok) {
        return false;
    }
    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(qc_proto.sign_x().c_str());
    sign.Y = libff::alt_bn128_Fq(qc_proto.sign_y().c_str());
    sign.Z = libff::alt_bn128_Fq(qc_proto.sign_z().c_str());
        
    if (!bls_agg_sign) {
        bls_agg_sign = std::make_shared<libff::alt_bn128_G1>();
    }
    *bls_agg_sign = sign;
    view = qc_proto.view();
    view_block_hash = qc_proto.view_block_hash();

    participants.clear();
    for (uint32_t i = 0; i < qc_proto.participants_size(); i++) {
        participants.push_back(qc_proto.participants(i));
    }
        
    return true;
}

HashStr ViewBlock::GetHash() const {
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

}

} // namespace shardora

