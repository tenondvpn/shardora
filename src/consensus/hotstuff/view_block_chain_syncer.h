#pragma once
#include <consensus/hotstuff/types.h>
#include <common/thread_safe_queue.h>
#include <common/utils.h>
#include <consensus/hotstuff/view_block_chain_manager.h>
#include <memory>
#include <queue>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "protos/view_block.pb.h"
#include <queue>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/rule.h>

namespace shardora {

namespace consensus {

const int kEachRequestMaxViewBlocksCount = 8; 

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;

    ViewBlockItem(const HashStr& h, const uint32_t& p) : hash(h), pool_idx(p) {}
    ~ViewBlockItem() {};
};

class ViewBlockChainSyncer {
public:
    explicit ViewBlockChainSyncer(const std::shared_ptr<ViewBlockChainManager>&);
    ViewBlockChainSyncer(const ViewBlockChainSyncer&) = delete;
    ViewBlockChainSyncer& operator=(const ViewBlockChainSyncer&) = delete;

    ~ViewBlockChainSyncer();
    
    // Status AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsumeMessages();
    Status MergeChain(std::shared_ptr<ViewBlockChain>& ori_chain, const std::shared_ptr<ViewBlockChain>& sync_chain);
private:
    Status SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg);
    void ConsensusTimerMessage();
    void SyncChains();
    
    Status StoreViewBlock(std::shared_ptr<ViewBlockChain>&, const std::shared_ptr<ViewBlock>&);
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    
    uint64_t timeout_ms_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<std::shared_ptr<ViewBlockItem>> input_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<transport::MessagePtr> consume_queue_;
    common::Tick tick_;
    std::shared_ptr<ViewBlockChainManager> view_block_chain_mgr_;
    std::shared_ptr<Rule> rule_; // rule of view block receiving
};

} // namespace consensus

} // namespace shardora

