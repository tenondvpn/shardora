#pragma once
#include "consensus/hotstuff/types.h"
#include <common/thread_safe_queue.h>
#include <common/utils.h>
#include <memory>
#include <queue>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "protos/view_block.pb.h"
#include <queue>

namespace shardora {
namespace consensus {

typedef std::function<void(const std::shared_ptr<ViewBlock> &)> FetchCallbackFn;

const int kEachRequestMaxViewBlocksCount = 8; 

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;
};

struct CompareViewBlock {
    bool operator()(const std::shared_ptr<ViewBlock>& lhs, const std::shared_ptr<ViewBlock>& rhs) const {
        return lhs->view > rhs->view;
    }
};

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

using ViewBlockMinHeap = std::priority_queue<std::shared_ptr<ViewBlock>, std::vector<std::shared_ptr<ViewBlock>>, CompareViewBlock>;

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
    void ConsumeOrphanBlocks();
    void produceMessages();
    void consumeMessages();
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    Status GetViewBlock(uint32_t pool_idx, const std::string& hash, ViewBlock* view_block);
    
    uint64_t timeout_ms_;
    FetchCallbackFn* fetch_callback_fn_; // fetch 回调函数
    std::unordered_set<HashStr> pending_fetch_;
    transport::TcpTransport* trans_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockItem>> input_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<transport::MessagePtr> consume_queue_;
    common::Tick tick_;
    std::unordered_map<uint32_t, ViewBlockMinHeap> pool_orphan_blocks_map_; // 已经获得但没有父块, TODO 按照 view 排序
};

}
}
