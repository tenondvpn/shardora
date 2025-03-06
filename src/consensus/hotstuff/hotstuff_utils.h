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

struct ProposeMsgWrapper {
    // Context
    transport::MessagePtr msg_ptr;
    std::shared_ptr<ViewBlock> view_block_ptr;
    BalanceMapPtr acc_balance_map_ptr;
    std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr;
    Breakpoint breakpoint; // 断点位置
    int tried_times;
    common::BftMemberPtr leader;

    ProposeMsgWrapper(const transport::MessagePtr& mptr) 
        : msg_ptr(mptr), breakpoint(0), tried_times(0) {}
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

struct ViewBlockInfo {
    std::shared_ptr<ViewBlock> view_block;
    ViewBlockStatus status;
    // std::vector<std::shared_ptr<ViewBlock>> children;
    std::shared_ptr<QC> qc;
    std::unordered_set<std::string> added_txs;
    BalanceMapPtr acc_balance_map_ptr;
    std::shared_ptr<zjcvm::ZjchainHost> zjc_host_ptr;
    bool block_chain_choosed;

    ViewBlockInfo() : 
        view_block(nullptr), 
        status(ViewBlockStatus::Unknown), 
        qc(nullptr),
        block_chain_choosed(false) {}
};

struct ViewBlockInfoCmp {
    bool operator<(std::shared_ptr<ViewBlockInfo>& a, std::shared_ptr<ViewBlockInfo>& b) const {
        return a->view_block->qc().view() > b->view_block->qc().view(); 
    }
};

class Pipeline {
public:
    static const int MAX_MSG_NUM = 2;
    
    Pipeline() {};
    ~Pipeline() {};

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;


    void AddStep(StepFn pipeline_fn) {
        pipeline_fns_.push_back(pipeline_fn);
    }

    void SetCondition(ConditionFn condition_fn) {
        condition_fn_ = condition_fn;
    }

    void UseRetry(bool on) {
        with_retry_ = on;
    }

    Status Call(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        pro_msg_wrap->tried_times++;

        if (IsQcTcValid(pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc())) {            
            std::string key = std::to_string(
                pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().leader_idx()) + "_" + 
                std::to_string(pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view());
            auto iter = leader_view_with_propose_msgs_.find(key);
            if (iter != leader_view_with_propose_msgs_.end()) {
                if (pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view() > 
                        iter->second->view_block_ptr->qc().view()) {
                    assert(derectly_call_accept_and_store_fn_ != nullptr);
                    Status prev_s = derectly_call_accept_and_store_fn_(
                        iter->second, 
                        pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view_block_hash());
                    if (prev_s != Status::kSuccess) {
                        ZJC_WARN("failed handle prev message chain store: %s, hash64: %lu, block timestamp: %lu", 
                            key.c_str(),
                            iter->second->msg_ptr->header.hash64(),
                            iter->second->view_block_ptr->block_info().timestamp());
                        assert(false);
                    }
                }                                                                                                                                                                                                                                              
                
                leader_view_with_propose_msgs_.erase(iter);
                CHECK_MEMORY_SIZE(leader_view_with_propose_msgs_);
            }
        }

        for (Breakpoint bp = pro_msg_wrap->breakpoint; bp < pipeline_fns_.size(); bp++) {
            auto fn = pipeline_fns_[bp];
            Status s = fn(pro_msg_wrap);
            if (s != Status::kSuccess) {
                pro_msg_wrap->breakpoint = bp; // 记录失败断点
                auto& view_block = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().view_item();
                auto& qc = view_block.qc();
                std::string key = std::to_string(qc.leader_idx()) + "_" + 
                    std::to_string(qc.view());
                leader_view_with_propose_msgs_[key] = pro_msg_wrap;
                CHECK_MEMORY_SIZE(leader_view_with_propose_msgs_);
                ZJC_DEBUG("success store invalid message: %u_%u_%lu, hash: %s, phash: %s, "
                    "hash64: %lu, block timestamp: %lu",
                    qc.network_id(), qc.pool_index(), qc.view(),
                    common::Encode::HexEncode(qc.view_block_hash()).c_str(),
                    common::Encode::HexEncode(view_block.parent_hash()).c_str(),
                    pro_msg_wrap->msg_ptr->header.hash64(),
                    pro_msg_wrap->view_block_ptr->block_info().timestamp());
                return Status::kError;
            }
        }
        
        return Status::kSuccess;
    }

    int CallWaitingProposeMsgs() {
        int succ_num = 0;
        
        std::vector<std::shared_ptr<ProposeMsgWrapper>> ordered_msg;
        while (!min_heap_.empty()) {
            auto pro_msg_wrap = min_heap_.top();
            min_heap_.pop();
            ordered_msg.push_back(pro_msg_wrap);
        }

        for (auto pro_msg_wrap : ordered_msg) {
            if (condition_fn_ && !condition_fn_(pro_msg_wrap)) {
                continue;
            }
            if (Call(pro_msg_wrap) == Status::kSuccess) {
                succ_num++;
            }            
        }
        
        return succ_num;
    }

    int Size() {
        return min_heap_.size();
    }

    void PushMsg(std::shared_ptr<ProposeMsgWrapper> pro_msg_wrap) {
        while (Size() >= MAX_MSG_NUM) {
            min_heap_.pop();
        }
        min_heap_.push(pro_msg_wrap);
    }

    void set_derectly_call_accept_and_store_fn(DirectlyStepFn fn) {
        derectly_call_accept_and_store_fn_ = fn;
    }
    
private:
    DirectlyStepFn derectly_call_accept_and_store_fn_ = nullptr;

    std::vector<StepFn> pipeline_fns_;
    ConditionFn condition_fn_;
    ProposeMsgMinHeap min_heap_;
    bool with_retry_ = false;
    std::unordered_map<std::string, std::shared_ptr<ProposeMsgWrapper>> leader_view_with_propose_msgs_;
};

} // namespace consensus

} // namespace shardora

