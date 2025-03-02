#pragma once
#ifdef USE_AGG_BLS
#include <consensus/hotstuff/agg_crypto.h>
#else
#include <consensus/hotstuff/crypto.h>
#endif
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
        const ViewBlock& view_block)>;

// Sync ViewBlocks and HighQC and HighTC
class HotstuffSyncer {
public:
    // TODO 将 ViewBlockChainManager 换成 HotstuffManager
    HotstuffSyncer(
            const std::shared_ptr<consensus::HotstuffManager>&,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<sync::KeyValueSync>& kv_sync,
            std::shared_ptr<block::AccountManager> account_mgr);
    HotstuffSyncer(const HotstuffSyncer&) = delete;
    HotstuffSyncer& operator=(const HotstuffSyncer&) = delete;

    ~HotstuffSyncer();

    void Start();
    void Stop();
    void SyncPool(const uint32_t& pool_idx, const int32_t& node_num);
    void SyncViewBlock(const uint32_t& pool_idx, const HashStr& hash);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    void ConsumeMessages();
    Status MergeChain(
            const uint32_t& pool_idx,
            std::shared_ptr<ViewBlockChain>& ori_chain,
            const std::shared_ptr<ViewBlockChain>& sync_chain,
            const ViewBlock& high_commit_qc);

    // 修改处理 view_block 的函数
    inline void SetOnRecvViewBlockFn(const OnRecvViewBlockFn& fn) {
        on_recv_vb_fn_ = fn;
    }
    
private:
    inline std::shared_ptr<Hotstuff> hotstuff(uint32_t pool_idx) const {
        return hotstuff_mgr_->hotstuff(pool_idx);
    }

    inline std::shared_ptr<ViewBlockChain> view_block_chain(uint32_t pool_idx) const {
        return hotstuff_mgr_->chain(pool_idx);
    }

    inline std::shared_ptr<Pacemaker> pacemaker(uint32_t pool_idx) const {
        return hotstuff_mgr_->pacemaker(pool_idx);
    }

#ifdef USE_AGG_BLS
    inline std::shared_ptr<AggCrypto> crypto(uint32_t pool_idx) const {
        return hotstuff_mgr_->crypto(pool_idx);
    }    
#else
    inline std::shared_ptr<Crypto> crypto(uint32_t pool_idx) const {
        return hotstuff_mgr_->crypto(pool_idx);
    }
#endif
    
    inline uint64_t SyncTimerCycleUs(uint32_t pool_idx) const {
        // return pacemaker(pool_idx)->DurationUs();
        return kSyncTimerCycleUs;
    }

    void HandleSyncedBlocks(const transport::MessagePtr& msg_ptr);
    void SyncAllPools();
    Status Broadcast(const view_block::protobuf::ViewBlockSyncMessage& view_block_msg);
    Status SendRequest(
            uint32_t network_id,
            view_block::protobuf::ViewBlockSyncMessage& view_block_msg,
            int32_t node_num);
    
    Status ReplyMsg(
            uint32_t network_id,
            const view_block::protobuf::ViewBlockSyncMessage& view_block_msg,
            const transport::MessagePtr& msg_ptr);

    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    // void SyncChains();
    Status processRequest(const transport::MessagePtr&);
    Status processResponse(const transport::MessagePtr&);
    Status processRequestSingle(const transport::MessagePtr&);

    Status processResponseQcTc(
            const uint32_t& pool_idx,
            const view_block::protobuf::ViewBlockSyncResponse& view_block_res);
    Status processResponseLatestCommittedBlock(
            const uint32_t& pool_idx,
            const view_block::protobuf::ViewBlockSyncResponse& view_block_res);    
    Status processResponseChain(
            const uint32_t& pool_idx,
            const view_block::protobuf::ViewBlockSyncResponse& view_block_res);
    Status onRecViewBlock(
            const uint32_t& pool_idx,
            const std::shared_ptr<ViewBlockChain>& ori_chain,
            const ViewBlock& view_block);
    
    uint64_t timeout_ms_;
    common::ThreadSafeQueue<transport::MessagePtr> consume_queues_[common::kMaxThreadCount];
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    OnRecvViewBlockFn on_recv_vb_fn_;
    std::shared_ptr<db::Db> db_ = nullptr;
    uint64_t last_timers_us_[common::kInvalidPoolIndex];
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    bool running_ = false;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
};

} // namespace consensus

} // namespace shardora

