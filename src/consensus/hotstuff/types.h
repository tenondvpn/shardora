#pragma once

#include <common/hash.h>
#include <consensus/zbft/zbft_utils.h>
#include <string>
#include <protos/block.pb.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

namespace shardora {
namespace consensus {

typedef uint64_t View;

struct QC {
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    View view;
    std::string view_block_hash;
};

struct ViewBlock {
    std::string hash;
    std::string parent_hash;

    uint32_t leader_idx;
    std::shared_ptr<block::protobuf::Block> block;

    std::shared_ptr<QC> qc;
    View view;

    ViewBlock(const std::string& parent, std::shared_ptr<QC>& qc, std::shared_ptr<block::protobuf::Block>& block, const View& view, const uint32_t& leader_idx) :
        parent_hash(parent),
        leader_idx(leader_idx),
        block(block),
        qc(qc),
        view(view) {
        std::string msg = consensus::GetBlockHash(*block);
        msg.append((char*)&parent_hash, sizeof(parent_hash));
        msg.append((char*)&leader_idx, sizeof(leader_idx));
        
    };
};
    
}
}
