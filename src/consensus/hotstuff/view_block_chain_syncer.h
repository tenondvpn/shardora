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

namespace hotstuff {

const int kPopCountMax = 128;
const uint64_t kSyncTimerCycleUs = 100000lu;

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;

    ViewBlockItem(const HashStr& h, const uint32_t& p) : hash(h), pool_idx(p) {}
    ~ViewBlockItem() {};
};

using OnRecvViewBlockFn = std::function<Status(const std::shared_ptr<ViewBlockChain>&, const std::shared_ptr<ViewBlock>&)>;

class ViewBlockChainSyncer {
public:
    explicit ViewBlockChainSyncer(const std::shared_ptr<ViewBlockChainManager>&);
    ViewBlockChainSyncer(const ViewBlockChainSyncer&) = delete;
    ViewBlockChainSyncer& operator=(const ViewBlockChainSyncer&) = delete;

    ~ViewBlockChainSyncer();

    void Start();
    void Stop();
    // Status AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    void ConsumeMessages();
    Status MergeChain(std::shared_ptr<ViewBlockChain>& ori_chain, const std::shared_ptr<ViewBlockChain>& sync_chain);

    inline void SetOnRecvViewBlockFn(const OnRecvViewBlockFn& fn) {
        on_recv_vb_fn_ = fn;
    }    
    
private:
    Status SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg);
    void ConsensusTimerMessage();
    void SyncChains();
    
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    
    uint64_t timeout_ms_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<transport::MessagePtr> consume_queues_[common::kMaxThreadCount];
    common::Tick tick_;
    std::shared_ptr<ViewBlockChainManager> view_block_chain_mgr_;
    std::shared_ptr<Rule> rule_; // rule of view block receiving
    OnRecvViewBlockFn on_recv_vb_fn_;
};

} // namespace consensus

} // namespace shardora

