#pragma once

#include <unordered_set>

#include <common/time_utils.h>
#include <sstream>
#include <string>

#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

#include <common/hash.h>
#include <consensus/hotstuff/utils.h>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include <protos/prefix_db.h>
#include "network/network_utils.h"
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

typedef uint64_t View;
typedef std::string HashStr;

static const View GenesisView = 1;
static const View BeforeGenesisView = 0;
// ViewDuration Init Params
static const uint64_t ViewDurationSampleSize = 10;
static const double ViewDurationStartTimeoutMs = 300;
static const double ViewDurationMaxTimeoutMs = 60000;
static const double ViewDurationMultiplier = 1.3; // 选过大会造成卡住的成本很高，一旦卡住则恢复时间很长（如 leader 不一致），过小会导致没有交易时 CPU 长时间降不下来

// 本 elect height 中共识情况统计
struct MemberConsensusStat {
    uint32_t succ_num; // 共识成功的次数
    uint32_t fail_num; // 共识失败的次数

    MemberConsensusStat() {
        succ_num = 0;
        fail_num = 0;
    }

    MemberConsensusStat(uint16_t succ_num, uint16_t fail_num) : succ_num(succ_num), fail_num(fail_num) {}

    inline HashStr GetHash() {
        std::stringstream ss;
        ss << succ_num << fail_num;
        return common::Hash::keccak256(ss.str());
    }
};

struct QC {  
    QC(
            uint32_t net_id,
            uint32_t pool_idx,
            const std::shared_ptr<libff::alt_bn128_G1>& sign,
            const View& v,
            const HashStr& hash,
            const HashStr& commit_hash,
            uint64_t elect_height,
            uint32_t leader_idx) :
            network_id(net_id), pool_index(pool_idx),
            bls_agg_sign(sign), view_(v), view_block_hash_(hash),
            commit_view_block_hash(commit_hash), elect_height(elect_height),
            leader_idx(leader_idx) {
        if (network_id >= network::kConsensusShardEndNetworkId) {
            network_id = network_id - network::kConsensusWaitingShardOffset;
        }

        if (sign == nullptr) {
            bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::zero());
        }

        hash_ = GetQCMsgHash(
            network_id, 
            pool_index, 
            view_, 
            view_block_hash_, 
            commit_view_block_hash, 
            elect_height, 
            leader_idx);
        valid_ = true;
    }

    QC(const std::string& s) {
        if (!Unserialize(s)) {
            assert(false);
            return;
        }

        if (bls_agg_sign == nullptr) {
            bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::zero());
        }

        hash_ = GetQCMsgHash(
            network_id, 
            pool_index, 
            view_, 
            view_block_hash_, 
            commit_view_block_hash, 
            elect_height, 
            leader_idx);
        valid_ = true;
    };
    
    std::string Serialize() const;
    bool Unserialize(const std::string& str);
    inline bool valid() const {
        return valid_;
    }

    inline const HashStr& msg_hash() const {
        return hash_;
    }

    inline const HashStr& view_block_hash() const {
        return view_block_hash_;
    }

    inline View view() const {
        return view_;
    }

protected:
    HashStr GetViewHash(
        uint32_t net_id,
        uint32_t pool_idx,
        const View& view, 
        uint64_t elect_height, 
        uint32_t leader_idx);
    HashStr GetQCMsgHash(
        uint32_t net_id,
        uint32_t pool_idx,
        const View &view,
        const HashStr &view_block_hash,
        const HashStr& commit_view_block_hash,
        uint64_t elect_height,
        uint32_t leader_idx);
        
    std::string hash_;
    bool valid_ = false;
    std::shared_ptr<libff::alt_bn128_G1> bls_agg_sign;
    View view_; // view_block_hash 对应的 view，TODO 校验正确性，避免篡改
    HashStr view_block_hash_; // 是 view_block_hash 的 prepareQC
    HashStr commit_view_block_hash; // 是 commit_view_block_hash 的 commitQC
    uint64_t elect_height; // 确定 epoch，用于验证 QC，理论上与 view_block_hash elect_height 相同，但对于同步场景，作为 commit_qc 时有时候 view_block 无法获取，故将 elect_height 放入 QC 中
    uint32_t leader_idx;
    uint32_t network_id;
    uint32_t pool_index;
};

// TODO TC 中可增加超时的 leader_idx，用于 Leader 选择黑名单
struct TC : public QC {
    TC(
            uint32_t net_id,
            uint32_t pool_idx,
            const std::shared_ptr<libff::alt_bn128_G1>& sign,
            const View& v,
            uint64_t elect_height,
            uint32_t leader_idx) :
        QC(net_id, pool_idx, sign, v, "", "", elect_height, leader_idx) {
    }

    TC(const std::string& s) : QC(s) {
    }
};

struct ViewBlock {
    HashStr hash;
    HashStr parent_hash;

    uint32_t leader_idx;
    std::shared_ptr<block::protobuf::Block> block;
    std::unordered_set<std::string> added_txs;
    std::shared_ptr<QC> qc;
    View view;
    std::shared_ptr<MemberConsensusStat> leader_consen_stat; // 计算后的共识统计，在 replica 接收块后，计算出 leader 应该的分数，写入次字段，类似交易执行

    uint64_t created_time_us;

    ViewBlock(
            const HashStr& parent,
            const std::shared_ptr<QC>& qc,
            const std::shared_ptr<block::protobuf::Block>& block,
            const View& view,
            uint32_t leader_idx) :
        parent_hash(parent),
        leader_idx(leader_idx),
        block(block),
        qc(qc),
        view(view),
        created_time_us(common::TimeUtils::TimestampUs()) {
        leader_consen_stat = std::make_shared<MemberConsensusStat>();
        hash = DoHash();
    };

    ViewBlock() : qc(nullptr), view(0), created_time_us(common::TimeUtils::TimestampUs()) {
        leader_consen_stat = std::make_shared<MemberConsensusStat>();
    };

    inline bool Valid() {
        return hash != "" && hash == DoHash() && block != nullptr; 
    }
    
    HashStr DoHash() const;

    inline uint64_t ElectHeight() const {
        if (!block) {
            return 0;
        } 
        return block->electblock_height();
    }

    inline void UpdateHash() {
        hash = DoHash();
    }
};

struct SyncInfo : public std::enable_shared_from_this<SyncInfo> {
    std::shared_ptr<QC> qc;
    std::shared_ptr<TC> tc;
    // std::shared_ptr<ViewBlock> view_block;

    SyncInfo() : qc(nullptr), tc(nullptr) {};

    std::shared_ptr<SyncInfo> WithQC(const std::shared_ptr<QC>& q) {
        qc = q;
        return shared_from_this();
    }

    std::shared_ptr<SyncInfo> WithTC(const std::shared_ptr<TC>& t) {
        tc = t;
        return shared_from_this();
    }
};

std::shared_ptr<SyncInfo> new_sync_info();

enum class Status : int {
  kSuccess = 0,
  kError = 1,
  kNotFound = 2,
  kInvalidArgument = 3,
  kBlsVerifyWaiting = 4,
  kBlsVerifyFailed = 5,
  kAcceptorTxsEmpty = 6,
  kAcceptorBlockInvalid = 7,
  kOldView = 8,
  kElectItemNotFound = 9,
  kWrapperTxsEmpty = 10,
  kBlsHandled = 11,
  kTxRepeated = 12,
  kLackOfParentBlock = 13,
};

enum WaitingBlockType {
    kRootBlock,
    kSyncBlock,
    kToBlock,
};



void ViewBlock2Proto(const std::shared_ptr<ViewBlock> &view_block, view_block::protobuf::ViewBlockItem *view_block_proto);
Status
Proto2ViewBlock(const view_block::protobuf::ViewBlockItem &view_block_proto,
                std::shared_ptr<ViewBlock> &view_block);

} // namespace hotstuff

} // namespace shardora

