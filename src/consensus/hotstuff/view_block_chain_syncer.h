#pragma once
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/pacemaker.h>
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
const uint64_t kSyncTimerCycleUs = 500000lu;
const int kMaxSyncBlockNum = 64;

struct ViewBlockItem {
    HashStr hash;
    uint32_t pool_idx;

    ViewBlockItem(const HashStr& h, const uint32_t& p) : hash(h), pool_idx(p) {}
    ~ViewBlockItem() {};
};

using OnRecvViewBlockFn = std::function<Status(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlockChain>& chain,
        const std::shared_ptr<ViewBlock>& view_block)>;

class ViewBlockChainSyncer {
public:
    // TODO 将 ViewBlockChainManager 换成 HotstuffManager
    ViewBlockChainSyncer(const std::shared_ptr<consensus::HotstuffManager>&);
    ViewBlockChainSyncer(const ViewBlockChainSyncer&) = delete;
    ViewBlockChainSyncer& operator=(const ViewBlockChainSyncer&) = delete;

    ~ViewBlockChainSyncer();

    void Start();
    void Stop();
    // Status AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    void ConsumeMessages();
    Status MergeChain(
            const uint32_t& pool_idx,
            std::shared_ptr<ViewBlockChain>& ori_chain,
            const std::shared_ptr<ViewBlockChain>& sync_chain);

    inline void SetOnRecvViewBlockFn(const OnRecvViewBlockFn& fn) {
        on_recv_vb_fn_ = fn;
    }
    
private:
    inline std::shared_ptr<ViewBlockChain> Chain(uint32_t pool_idx) const {
        return hotstuff_mgr_->chain(pool_idx);
    }

    inline std::shared_ptr<Pacemaker> Pacemaker(uint32_t pool_idx) const {
        return hotstuff_mgr_->pacemaker(pool_idx);
    }
    
    inline std::shared_ptr<Crypto> crypto() const {
        return hotstuff_mgr_->crypto();
    }
    
    Status SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg);
    void ConsensusTimerMessage();
    void SyncChains();
    
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    
    uint64_t timeout_ms_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<transport::MessagePtr> consume_queues_[common::kMaxThreadCount];
    common::Tick tick_;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    OnRecvViewBlockFn on_recv_vb_fn_;
};

} // namespace consensus

} // namespace shardora

