#pragma once

#include <functional>
#include <queue>

#include <common/hash.h>
#include <common/log.h>
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

    ProposeMsgWrapper(const transport::MessagePtr& mptr) 
        : msg_ptr(mptr), breakpoint(0), tried_times(0) {}
};

struct CompareProposeMsg {
    bool operator()(
            const std::shared_ptr<ProposeMsgWrapper>& lhs, 
            const std::shared_ptr<ProposeMsgWrapper>& rhs) const {
        auto lview = lhs->msg_ptr->header.hotstuff().pro_msg().view_item().view();
        auto rview = rhs->msg_ptr->header.hotstuff().pro_msg().view_item().view();
        return lview > rview;
    }
};

// 等待队列，用于存放暂时处理不了的 Propose 消息
using ProposeMsgMinHeap =
    std::priority_queue<std::shared_ptr<ProposeMsgWrapper>,
                        std::vector<std::shared_ptr<ProposeMsgWrapper>>,
                        CompareProposeMsg>;
using StepFn = std::function<Status(std::shared_ptr<ProposeMsgWrapper> &)>;
using ConditionFn = std::function<bool(std::shared_ptr<ProposeMsgWrapper>&)>;

class Pipeline {
public:
    static const int MAX_MSG_NUM = 2;
    
    Pipeline() {};
    ~Pipeline() {};

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void AddRetryStep(StepFn pipeline_fn, int retry) {
        pipeline_fns_.push_back(pipeline_fn);
        pipeline_fn_max_trys_.push_back(retry+1);
    }
    
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
        
        for (Breakpoint bp = pro_msg_wrap->breakpoint; bp < pipeline_fns_.size(); bp++) {
            auto fn = pipeline_fns_[bp];
            Status s = fn(pro_msg_wrap);
            if (s != Status::kSuccess) {
                pro_msg_wrap->breakpoint = bp; // 记录失败断点
                if (with_retry_ && pro_msg_wrap->tried_times < pipeline_fn_max_trys_[bp]) {
                    PushMsg(pro_msg_wrap);
                }
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

private:
    std::vector<StepFn> pipeline_fns_;
    ConditionFn condition_fn_;
    std::vector<int> pipeline_fn_max_trys_;
    ProposeMsgMinHeap min_heap_;
    bool with_retry_ = false;
};

} // namespace consensus

} // namespace shardora

