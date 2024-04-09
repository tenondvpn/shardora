#pragma once

#include <common/time_utils.h>
#include <sstream>
#include <common/hash.h>
#include <consensus/zbft/zbft_utils.h>
#include <string>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

namespace shardora {
namespace consensus {

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

typedef uint64_t View;
typedef std::string HashStr;

struct QC {
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    std::vector<uint32_t> participants; // 与之签名的贡献者
    View view; // view_block_hash 对应的 view
    HashStr view_block_hash;

    QC(const std::shared_ptr<libff::alt_bn128_G1>& sign, const View& v, const HashStr& hash) :
        bls_agg_sign(sign), view(v), view_block_hash(hash) {}
    
    std::string Serialize() const;
    bool Unserialize(const std::string& str);
};

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

struct ViewBlock {
    HashStr hash;
    HashStr parent_hash;

    uint32_t leader_idx;
    std::shared_ptr<block::protobuf::Block> block;

    std::shared_ptr<QC> qc;
    View view;

    uint64_t created_time_us;

    ViewBlock(const HashStr& parent, const std::shared_ptr<QC>& qc, const std::shared_ptr<block::protobuf::Block>& block, const View& view, const uint32_t& leader_idx) :
        parent_hash(parent),
        leader_idx(leader_idx),
        block(block),
        qc(qc),
        view(view),
        created_time_us(common::TimeUtils::TimestampUs()) {
        hash = GetHash();
    };

    ViewBlock() {};

    inline bool Valid() {
        return hash != "" && hash == GetHash(); 
    }
    
    HashStr GetHash() const;
};

HashStr ViewBlock::GetHash() const {
    std::string qc_str;
    std::string block_hash;
    if (qc) {
        qc_str = qc->Serialize();
    }
    if (block) {
        block_hash = consensus::GetBlockHash(*block);
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

struct SyncInfo {
    // std::shared_ptr<QC> qc;
    std::shared_ptr<ViewBlock> view_block;

    SyncInfo() {};
};



enum class Status : int {
  kSuccess = 0,
  kError = 1,
  kNotFound = 2,
  kInvalidArgument = 3,
};

    
}
}
