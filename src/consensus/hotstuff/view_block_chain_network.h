#pragma once
#include "consensus/hotstuff/types.h"
#include <common/thread_safe_queue.h>
#include <common/utils.h>
#include <memory>
#include <queue>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "protos/view_block.pb.h"

namespace shardora {
namespace consensus {

typedef std::function<void(const std::shared_ptr<ViewBlock> &)> FetchCallbackFn;

const int kEachRequestMaxViewBlocksCount = 8; 

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;
};

class ViewBlockChainNetwork {
public:
    ViewBlockChainNetwork(FetchCallbackFn*);
    ViewBlockChainNetwork(const ViewBlockChainNetwork&) = delete;
    ViewBlockChainNetwork& operator=(const ViewBlockChainNetwork&) = delete;

    ~ViewBlockChainNetwork();

    Status Fetch(const HashStr& view_block_hash, uint32_t pool_idx);
    Status AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx); // 目前没用
    void HandleMessage(const transport::MessagePtr& msg_ptr);
private:
    Status sendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg);
    void ConsensusTimerMessage();
    Status processRequest();
    Status processResponse();
    
    uint64_t timeout_ms_;
    FetchCallbackFn* fetch_callback_fn_; // fetch 回调函数
    std::unordered_set<HashStr> pending_fetch_;
    transport::TcpTransport* trans_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockItem>> input_queues_[common::kMaxThreadCount];
    common::Tick tick_;
};

}
}
