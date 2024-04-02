#pragma once

#include <common/time_utils.h>
#include <sstream>
#include <common/hash.h>
#include <string>
#include <protos/block.pb.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

namespace shardora {
namespace consensus {

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

typedef uint64_t View;
typedef std::string HashStr;

struct QC {
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    View view; // view_block_hash 对应的 view
    HashStr view_block_hash;

    QC(const std::shared_ptr<libff::alt_bn128_G1>& sign, const View& v, const HashStr& hash) :
        bls_agg_sign(sign), view(v), view_block_hash(hash) {}
    ~QC() {}

    std::string Serialize() const {
        std::stringstream ss;
        if (bls_agg_sign) {
            auto x = std::to_string(bls_agg_sign->X.as_bigint().as_ulong());
            auto y = std::to_string(bls_agg_sign->Y.as_bigint().as_ulong());
            auto z = std::to_string(bls_agg_sign->Z.as_bigint().as_ulong());
            ss << x << y << z;
        }
        ss << view;
        ss << view_block_hash;
        return ss.str();
    }

    bool Unserialize(const std::string& str) {
        return true;
    }
};

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
        created_time_us(common::TimeUtils::TimestampUs()) {};

    ViewBlock() {};

    inline bool Valid() {
        return hash != "" && hash == GetHash(); 
    }
    
    HashStr GetHash() const {
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
};



enum class Status : int {
    kSuccess = 0,
    kError = 1,
};

    
}
}
