#pragma once
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/view_block_chain_manager.h>
#include <consensus/hotstuff/view_duration.h>
#include <db/db.h>
#include <transport/transport_utils.h>

// 临时文件，用于测试同步，最后替换为 hotstuff_manager

namespace shardora {

namespace hotstuff {

// one for pool
class Consensus {
public:
    Consensus(
            const std::shared_ptr<ViewBlockChain> chain,
            const std::shared_ptr<Pacemaker> pacemaker,
            const std::shared_ptr<LeaderRotation> leader_rotation,
            const std::shared_ptr<db::Db> db,
            const uint32_t& pool_idx) :
        view_block_chain_(chain),
        pacemaker_(pacemaker),
        leader_rotation_(leader_rotation),
        db_(db),
        pool_idx_(pool_idx) {
        // TODO 从 db 加载最近的 vb，验证后执行 OnPropose 逻辑
        auto genesis = GetGenesisViewBlock(db_, pool_idx_);
        if (genesis) {
            view_block_chain_->Store(genesis);
            view_block_chain_->SetLatestCommittedBlock(genesis);
            auto sync_info = std::make_shared<SyncInfo>();
            sync_info->qc = genesis->qc;
            pacemaker_->AdvanceView(sync_info);
        } else {
            ZJC_DEBUG("no genesis, pool_idx: %d", pool_idx_);
        }
    }
    ~Consensus() {};

    Consensus(const Consensus&) = delete;
    Consensus& operator=(const Consensus&) = delete;

    void Propose() {};
    void OnPropose(const transport::MessagePtr& msg_ptr) {};
    void OnVote() {};
    void StopVoting() {};

    inline std::shared_ptr<Pacemaker> pacemaker() const {
        return pacemaker_;
    }

    inline std::shared_ptr<ViewBlockChain> chain() const {
        return view_block_chain_;
    }
private:
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::shared_ptr<Pacemaker> pacemaker_;
    std::shared_ptr<LeaderRotation> leader_rotation_;
    std::shared_ptr<db::Db> db_;
    uint32_t pool_idx_;
};

class ConsensusManager {
public:
    ConsensusManager(
            const std::shared_ptr<ViewBlockChainManager> vbc_mgr,
            const std::shared_ptr<ElectInfo> elect_info,
            const std::shared_ptr<Crypto> crypto,
            const std::shared_ptr<db::Db> db) :
        view_block_chain_mgr_(vbc_mgr),
        elect_info_(elect_info),
        crypto_(crypto),
        db_(db) {}
    ~ConsensusManager() {};
    ConsensusManager(const ConsensusManager&) = delete;
    ConsensusManager& operator=(const ConsensusManager&) = delete;

    Status Init() {
        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
            auto chain = view_block_chain_mgr_->Chain(pool_idx);
            auto leader_rotation = std::make_shared<LeaderRotation>(chain, elect_info_);
            auto view_duration = std::make_shared<ViewDuration>();
            auto pacemaker = std::make_shared<Pacemaker>(pool_idx, crypto_, leader_rotation, view_duration);
            pool_consensus_map_[pool_idx] = std::make_shared<Consensus>(
                    chain, pacemaker, leader_rotation, db_, pool_idx);
        }

        return Status::kSuccess;
    }

    inline std::shared_ptr<Consensus> consensus(const uint32_t& pool_idx) const {
        return pool_consensus_map_.at(pool_idx);
    }
    
private:
    std::shared_ptr<ViewBlockChainManager> view_block_chain_mgr_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<Crypto> crypto_;
    std::unordered_map<uint32_t, std::shared_ptr<Consensus>> pool_consensus_map_;
    std::shared_ptr<db::Db> db_;
};



} // namespace hotstuff

} // namespace shardora

