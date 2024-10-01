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
  kNotExpectHash = 14,
};

enum WaitingBlockType {
    kRootBlock,
    kSyncBlock,
    kToBlock,
};

HashStr GetQCMsgHash(const view_block::protobuf::QcItem& qc_item);
HashStr GetTCMsgHash(const view_block::protobuf::QcItem& tc_item);
// HashStr GetViewBlockHash(const view_block::protobuf::ViewBlockItem& view_block_item);

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

using ViewBlock = view_block::protobuf::ViewBlockItem;
using QC = view_block::protobuf::QcItem;
using TC = QC;

inline static void CreateTc(
        uint32_t network_id, 
        uint32_t pool_index, 
        uint64_t view, 
        uint64_t elect_height, 
        uint32_t leader_idx, 
        TC* tc) {
    tc->set_network_id(network_id);
    tc->set_pool_index(pool_index);
    tc->set_view(view);
    tc->set_elect_height(elect_height);
    tc->set_leader_idx(leader_idx);
}

struct SyncInfo : public std::enable_shared_from_this<SyncInfo> {
    std::shared_ptr<QC> qc;
    std::shared_ptr<TC> tc;
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

} // namespace hotstuff

} // namespace shardora

