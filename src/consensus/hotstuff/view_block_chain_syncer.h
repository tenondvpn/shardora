#pragma once
#include <consensus/hotstuff/types.h>
#include <common/thread_safe_queue.h>
#include <common/utils.h>
#include <memory>
#include <queue>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "protos/view_block.pb.h"
#include <queue>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace consensus {

typedef std::function<void(const std::shared_ptr<ViewBlock> &)> FetchCallbackFn;

const int kEachRequestMaxViewBlocksCount = 8; 

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;
};

class ViewBlockChainSyncer {
public:
    ViewBlockChainSyncer(FetchCallbackFn*);
    ViewBlockChainSyncer(const ViewBlockChainSyncer&) = delete;
    ViewBlockChainSyncer& operator=(const ViewBlockChainSyncer&) = delete;

    ~ViewBlockChainSyncer();
    
    Status AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx); // 目前没用
    void HandleMessage(const transport::MessagePtr& msg_ptr);
private:
    Status sendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg);
    void ConsensusTimerMessage();
    void produceMessages();
    void consumeMessages();
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    Status GetViewBlockChain(uint32_t pool_idx, std::shared_ptr<ViewBlockChain>& view_block_chain);
    
    uint64_t timeout_ms_;
    FetchCallbackFn* fetch_callback_fn_; // fetch 回调函数
    std::unordered_set<HashStr> pending_fetch_;
    transport::TcpTransport* trans_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockItem>> input_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<transport::MessagePtr> consume_queue_;
    common::Tick tick_;
};

} // namespace consensus

} // namespace shardora

