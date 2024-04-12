#pragma once

#include <common/time_utils.h>
#include <sstream>
#include <common/hash.h>
#include <consensus/hotstuff/utils.h>
#include <string>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

typedef uint64_t View;
typedef std::string HashStr;



struct QC {
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    std::vector<uint32_t> participants; // 与之签名的贡献者，没有用，因为 bls 无法验证正确性
    View view; // view_block_hash 对应的 view
    HashStr view_block_hash;

    QC(const std::shared_ptr<libff::alt_bn128_G1>& sign, const View& v, const HashStr& hash) :
        bls_agg_sign(sign), view(v), view_block_hash(hash) {}
    
    std::string Serialize() const;
    bool Unserialize(const std::string& str);
    inline bool IsGenesisQC() const {
        return view == View(0);
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
        created_time_us(common::TimeUtils::TimestampUs()) {
        hash = DoHash();
    };

    ViewBlock() {};

    inline bool Valid() {
        return hash != "" && hash == DoHash(); 
    }
    
    HashStr DoHash() const;

    inline uint64_t ElectHeight() const {
        return block->electblock_height();
    }
};

void ViewBlock2Proto(const std::shared_ptr<ViewBlock> &view_block, view_block::protobuf::ViewBlockItem *view_block_proto);
void Proto2ViewBlock(const view_block::protobuf::ViewBlockItem& view_block_proto, std::shared_ptr<ViewBlock> view_block);

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
  kBlsVerifyWaiting = 4,
  kBlsVerifyFailed = 5,
};

enum WaitingBlockType {
    kRootBlock,
    kSyncBlock,
    kToBlock,
};

static const View GenesisView  = 1; 
    
}
}
