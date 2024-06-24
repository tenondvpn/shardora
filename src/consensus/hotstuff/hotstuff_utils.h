#pragma once

#include <common/hash.h>
#include <common/log.h>
#include <protos/block.pb.h>
#include <protos/hotstuff.pb.h>
#include <protos/prefix_db.h>
#include <consensus/hotstuff/types.h>
#include <protos/transport.pb.h>
#include <queue>
#include <functional>

namespace shardora {

namespace hotstuff {

using Breakpoint = int;

struct ProposeMsgWrapper {
    // Context
    const transport::protobuf::Header& header;
    std::shared_ptr<hotstuff::protobuf::ProposeMsg> pro_msg;
    std::shared_ptr<ViewBlock> v_block;
    std::shared_ptr<TC> tc;
    
    Breakpoint breakpoint; // 断点位置
    int tried_times;


    ProposeMsgWrapper(const transport::protobuf::Header& h) 
        : header(h), pro_msg(std::make_shared<hotstuff::protobuf::ProposeMsg>(h.hotstuff().pro_msg())),
          v_block(nullptr), tc(nullptr), breakpoint(0), tried_times(0) {}
};

struct CompareProposeMsg {
    bool operator()(const std::shared_ptr<ProposeMsgWrapper>& lhs, const std::shared_ptr<ProposeMsgWrapper>& rhs) const {
        return lhs->v_block->view > rhs->v_block->view;
    }
};

// 等待队列，用于存放暂时处理不了的 Propose 消息
using ProposeMsgMinHeap =
    std::priority_queue<std::shared_ptr<ProposeMsgWrapper>,
                        std::vector<std::shared_ptr<ProposeMsgWrapper>>,
                        CompareProposeMsg>;

using StepFn = std::function<Status(std::shared_ptr<ProposeMsgWrapper>&)>;

class Pipeline {
public:
    Pipeline(int max_try) : max_try_(max_try) {};
    ~Pipeline() {};

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void AddStepFn(StepFn pipeline_fn) {
        pipeline_fns_.push_back(pipeline_fn);
    }

    Status Call(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        pro_msg_wrap->tried_times++;
        
        for (Breakpoint bp = pro_msg_wrap->breakpoint; bp < pipeline_fns_.size(); bp++) {
            auto fn = pipeline_fns_[bp];
            Status s = fn(pro_msg_wrap);
            if (s != Status::kSuccess) {
                pro_msg_wrap->breakpoint = bp; // 记录失败断点
                if (pro_msg_wrap->tried_times < max_try_) {
                    // 不知为何，protobuf 的 ProposeMsg 会被析构，这里将 pro_msg 的拷贝放入队列
                    pro_msg_wrap->pro_msg = std::make_shared<hotstuff::protobuf::ProposeMsg>(*pro_msg_wrap->pro_msg);
                    min_heap_.push(pro_msg_wrap);
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
            if (Call(pro_msg_wrap) == Status::kSuccess) {
                succ_num++;
            }            
        }

        return succ_num;
    }

    int Size() {
        return min_heap_.size();
    }
    
private:
    std::vector<StepFn> pipeline_fns_;
    ProposeMsgMinHeap min_heap_;
    int max_try_;
};

} // namespace consensus

} // namespace shardora

