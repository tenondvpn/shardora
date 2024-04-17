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

typedef int64_t View;
typedef std::string HashStr;

static const View GenesisView  = 0;

struct QC {
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    View view; // view_block_hash 对应的 view
    HashStr view_block_hash;

    QC(const std::shared_ptr<libff::alt_bn128_G1>& sign, const View& v, const HashStr& hash) :
        bls_agg_sign(sign), view(v), view_block_hash(hash) {
        if (sign == nullptr) {
            bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::zero());
        }
    }

    QC() {
        bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::zero());
    };
    
    std::string Serialize() const;
    bool Unserialize(const std::string& str);
};

struct TC : public QC {
    TC(const std::shared_ptr<libff::alt_bn128_G1>& sign, const View& v) :
        QC(sign, v, "") {
    }

    TC() : QC() {}
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

struct SyncInfo {
    std::shared_ptr<QC> qc;
    std::shared_ptr<TC> tc;
    // std::shared_ptr<ViewBlock> view_block;

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



void ViewBlock2Proto(const std::shared_ptr<ViewBlock> &view_block, view_block::protobuf::ViewBlockItem *view_block_proto);
Status Proto2ViewBlock(const view_block::protobuf::ViewBlockItem& view_block_proto, std::shared_ptr<ViewBlock>& view_block);

} // namespace hotstuff

} // namespace shardora

