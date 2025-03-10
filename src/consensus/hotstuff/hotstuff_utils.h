#pragma once

#include <functional>
#include <queue>

#include <common/hash.h>
#include <common/log.h>
#include "common/node_members.h"
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>
#include <protos/hotstuff.pb.h>
#include <protos/prefix_db.h>
#include <protos/transport.pb.h>
#include "transport/transport_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace hotstuff {

using Breakpoint = int;
using BalanceMap = std::unordered_map<std::string, int64_t>;
using BalanceMapPtr = std::shared_ptr<BalanceMap>;

class ProposeMsgWrapper {
public:
    ProposeMsgWrapper() {
        assert(false);
    }

    ~ProposeMsgWrapper() {
        common::GlobalInfo::Instance()->DecSharedObj(2);
    }

    // Context
    transport::MessagePtr msg_ptr;
    std::shared_ptr<ViewBlock> view_block_ptr;
    BalanceMapPtr acc_balance_map_ptr;
    std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr;
    Breakpoint breakpoint; // 断点位置
    int tried_times;
    common::BftMemberPtr leader;

    ProposeMsgWrapper(const transport::MessagePtr& mptr) 
        : msg_ptr(mptr), breakpoint(0), tried_times(0) {
            common::GlobalInfo::Instance()->AddSharedObj(2);
    }
};

struct CompareProposeMsg {
    bool operator()(
            const std::shared_ptr<ProposeMsgWrapper>& lhs, 
            const std::shared_ptr<ProposeMsgWrapper>& rhs) const {
        auto lview = lhs->msg_ptr->header.hotstuff().pro_msg().view_item().qc().view();
        auto rview = rhs->msg_ptr->header.hotstuff().pro_msg().view_item().qc().view();
        return lview > rview;
    }
};

// 等待队列，用于存放暂时处理不了的 Propose 消息
using ProposeMsgMinHeap =
    std::priority_queue<std::shared_ptr<ProposeMsgWrapper>,
                        std::vector<std::shared_ptr<ProposeMsgWrapper>>,
                        CompareProposeMsg>;
using StepFn = std::function<Status(std::shared_ptr<ProposeMsgWrapper> &)>;
using DirectlyStepFn = std::function<Status(std::shared_ptr<ProposeMsgWrapper> &, const std::string&)>;
using ConditionFn = std::function<bool(std::shared_ptr<ProposeMsgWrapper>&)>;

struct CompareViewBlock {
    bool operator()(const std::shared_ptr<ViewBlock>& lhs, const std::shared_ptr<ViewBlock>& rhs) const {
        return lhs->qc().view() > rhs->qc().view();
    }
};

using ViewBlockMinHeap =
    std::priority_queue<std::shared_ptr<ViewBlock>,
                        std::vector<std::shared_ptr<ViewBlock>>,
                        CompareViewBlock>;

static const int MaxBlockNumForView = 7;
enum class ViewBlockStatus : int {
    Unknown = 0,
    Proposed = 1,
    Locked = 2,
    Committed = 3,
};

class ViewBlockInfo {
public:
    std::shared_ptr<ViewBlock> view_block;
    ViewBlockStatus status;
    std::shared_ptr<QC> qc;
    std::unordered_set<std::string> added_txs;
    BalanceMapPtr acc_balance_map_ptr;
    std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr;
    bool valid;

    ViewBlockInfo() : 
        view_block(nullptr), 
        status(ViewBlockStatus::Unknown), 
        qc(nullptr),
        valid(false) {
            common::GlobalInfo::Instance()->AddSharedObj(3);
    }

    ~ViewBlockInfo() {
        common::GlobalInfo::Instance()->DecSharedObj(3);
    }
};

struct ViewBlockInfoCmp {
    bool operator()(std::shared_ptr<ViewBlockInfo>& a, std::shared_ptr<ViewBlockInfo>& b) {
        return a->view_block->qc().view() > b->view_block->qc().view(); 
    }
};

} // namespace consensus

} // namespace shardora

