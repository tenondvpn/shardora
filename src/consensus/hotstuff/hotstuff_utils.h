#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>
#include <protos/prefix_db.h>
#include <consensus/hotstuff/types.h>
#include <protos/transport.pb.h>
#include <queue>

namespace shardora {

namespace hotstuff {

using Breakpoint = int;

struct PipelineMsg {
    // Context
    transport::protobuf::Header* header;
    
    Breakpoint breakpoint; // 断点位置
    int tried_times;
};

struct CompareProposeMsg {
    bool operator()(PipelineMsg* lhs, PipelineMsg* rhs) const {
        auto& l_pro_msg = lhs->header->hotstuff().pro_msg();
        auto& r_pro_msg = rhs->header->hotstuff().pro_msg();
        return l_pro_msg.view_item().view() > r_pro_msg.view_item().view();
    }
};

// 等待队列，用于存放暂时处理不了的 Propose 消息
using ProposeMsgMinHeap =
    std::priority_queue<PipelineMsg*,
                        std::vector<PipelineMsg*>,
                        CompareProposeMsg>;

using PipelineFn = std::function<Status(PipelineMsg&)>;

class Pipeline {
public:
    Pipeline(int retry);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void AddPipelineFn(PipelineFn pipeline_fn) {
        pipeline_fns_.push_back(pipeline_fn);
    }

    void PushMsg(PipelineMsg* pipeline_msg) {
        if (pipeline_msg != nullptr) {
            min_heap_.push(pipeline_msg);
        }
    }

    Status Call(PipelineMsg* pipe_msg) {
        pipe_msg->tried_times++;
        
        for (Breakpoint bp = pipe_msg->breakpoint; bp < pipeline_fns_.size(); bp++) {
            auto fn = pipeline_fns_[bp];
            Status s = fn(*pipe_msg);
            if (s != Status::kSuccess) {
                pipe_msg->breakpoint = bp; // 记录失败断点
                if (pipe_msg->tried_times < max_try_) {
                    min_heap_.push(pipe_msg);
                }
                return Status::kError;
            }
        }

        return Status::kSuccess;
    }

    int TryConsume() {
        int succ_num;
        
        std::vector<PipelineMsg*> ordered_msg;
        while (!min_heap_.empty()) {
            auto pipe_msg = min_heap_.top();
            min_heap_.pop();

            ordered_msg.push_back(pipe_msg);
        }

        for (const auto& pipe_msg : ordered_msg) {
            if (Call(pipe_msg) == Status::kSuccess) {
                succ_num++;
            }            
        }

        return succ_num;
    }
    
private:
    std::vector<PipelineFn> pipeline_fns_;
    ProposeMsgMinHeap min_heap_;
    int max_try_;
};

} // namespace consensus

} // namespace shardora

