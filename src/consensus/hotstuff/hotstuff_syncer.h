#pragma once
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/pacemaker.h>
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
#include <consensus/hotstuff/rule.h>

namespace shardora {

namespace hotstuff {

const int kPopCountMax = 128;
const uint64_t kSyncTimerCycleUs = 500000lu;
const int kMaxSyncBlockNum = 100;

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

// Sync ViewBlocks and HighQC and HighTC
class HotstuffSyncer {
public:
    // TODO 将 ViewBlockChainManager 换成 HotstuffManager
    HotstuffSyncer(const std::shared_ptr<consensus::HotstuffManager>&, std::shared_ptr<db::Db>& db);
    HotstuffSyncer(const HotstuffSyncer&) = delete;
    HotstuffSyncer& operator=(const HotstuffSyncer&) = delete;

    ~HotstuffSyncer();

    void Start();
    void Stop();
    void SyncPool(const uint32_t& pool_idx);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    void ConsumeMessages();
    Status MergeChain(
            const uint32_t& pool_idx,
            std::shared_ptr<ViewBlockChain>& ori_chain,
            const std::shared_ptr<ViewBlockChain>& sync_chain);

    // 修改处理 view_block 的函数
    inline void SetOnRecvViewBlockFn(const OnRecvViewBlockFn& fn) {
        on_recv_vb_fn_ = fn;
    }
    
private:
    inline std::shared_ptr<ViewBlockChain> view_block_chain(uint32_t pool_idx) const {
        return hotstuff_mgr_->chain(pool_idx);
    }

    inline std::shared_ptr<Pacemaker> pacemaker(uint32_t pool_idx) const {
        return hotstuff_mgr_->pacemaker(pool_idx);
    }
    
    inline std::shared_ptr<Crypto> crypto(uint32_t pool_idx) const {
        return hotstuff_mgr_->crypto(pool_idx);
    }

    void SyncAllPools();
    Status SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg);
    
    Status SendMsg(
            uint32_t network_id,
            const view_block::protobuf::ViewBlockSyncMessage& view_block_msg);

    void ConsensusTimerMessage();
    // void SyncChains();
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);

    Status processResponseQcTc(
            const uint32_t& pool_idx,
            const view_block::protobuf::ViewBlockSyncResponse& view_block_res);
    Status processResponseChain(
            const uint32_t& pool_idx,
            const view_block::protobuf::ViewBlockSyncResponse& view_block_res);

    Status onRecViewBlock(
            const uint32_t& pool_idx,
            const std::shared_ptr<ViewBlockChain>& ori_chain,
            const std::shared_ptr<ViewBlock>& view_block);
    
    uint64_t timeout_ms_;
    std::queue<std::shared_ptr<ViewBlockItem>> item_queue_;
    common::ThreadSafeQueue<transport::MessagePtr> consume_queues_[common::kMaxThreadCount];
    common::Tick tick_;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    OnRecvViewBlockFn on_recv_vb_fn_;
    std::shared_ptr<db::Db> db_ = nullptr;
};

} // namespace consensus

} // namespace shardora

