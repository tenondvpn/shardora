#pragma once
#include <common/flow_control.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h> 
#include <consensus/hotstuff/view_block_chain.h>
#include <protos/hotstuff.pb.h>
#include <protos/view_block.pb.h>
#include <security/security.h>

namespace shardora {

namespace vss {
class VssManager;
}

namespace contract {
class ContractManager;
};

namespace hotstuff {

enum MsgType {
  PROPOSE,
  VOTE,
  NEWVIEW,
  PRE_RESET_TIMER,
  RESET_TIMER,
};

typedef hotstuff::protobuf::ProposeMsg  pb_ProposeMsg;
typedef hotstuff::protobuf::HotstuffMessage  pb_HotstuffMessage;
typedef hotstuff::protobuf::VoteMsg pb_VoteMsg;
typedef hotstuff::protobuf::NewViewMsg pb_NewViewMsg;

using SyncViewBlockFn = std::function<void(const uint32_t&, const HashStr&)>;

static const uint64_t STUCK_PACEMAKER_DURATION_MIN_US = 2000000lu; // the min duration that hotstuff can be considered stucking
static const bool WITH_CONSENSUS_STATISTIC = true; // 是否开启 leader 的共识数据统计

class Hotstuff {
public:
    Hotstuff() = default;
    Hotstuff(
            const uint32_t& pool_idx,
            const std::shared_ptr<LeaderRotation>& lr,
            const std::shared_ptr<ViewBlockChain>& chain,
            const std::shared_ptr<IBlockAcceptor>& acceptor,
            const std::shared_ptr<IBlockWrapper>& wrapper,
            const std::shared_ptr<Pacemaker>& pm,
            const std::shared_ptr<Crypto>& crypto,
            const std::shared_ptr<ElectInfo>& elect_info,
            std::shared_ptr<db::Db>& db) :
        pool_idx_(pool_idx),
        crypto_(crypto),
        pacemaker_(pm),
        block_acceptor_(acceptor),
        block_wrapper_(wrapper),
        view_block_chain_(chain),
        leader_rotation_(lr),
        elect_info_(elect_info),
        db_(db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        pacemaker_->SetNewProposalFn(std::bind(&Hotstuff::Propose, this, std::placeholders::_1));
        pacemaker_->SetNewViewFn(std::bind(&Hotstuff::NewView, this, std::placeholders::_1));
        pacemaker_->SetStopVotingFn(std::bind(&Hotstuff::StopVoting, this, std::placeholders::_1));
    }
    ~Hotstuff() {};

    void Init();

    void SetSyncPoolFn(SyncPoolFn sync_fn) {
        sync_pool_fn_ = sync_fn;
    }    
    
    Status Start();
    
    void HandleProposeMsg(const transport::protobuf::Header& header);
    void HandleNewViewMsg(const transport::protobuf::Header& header);
    void HandlePreResetTimerMsg(const transport::protobuf::Header& header);
    void HandleResetTimerMsg(const transport::protobuf::Header& header);
    void HandleVoteMsg(const transport::protobuf::Header& header);
    void NewView(const std::shared_ptr<SyncInfo>& sync_info);
    Status Propose(const std::shared_ptr<SyncInfo>& sync_info);
    Status ResetReplicaTimers();
    Status TryCommit(const std::shared_ptr<QC> commit_qc);    

    void StopVoting(const View& view) {
        if (last_vote_view_ < view) {
            last_vote_view_ = view;
        }
    }

    void HandleSyncedViewBlock(
            const std::shared_ptr<ViewBlock>& vblock,
            const std::shared_ptr<QC>& self_commit_qc) {
        ZJC_DEBUG("now handle synced view block %u_%u_%lu",
            vblock->block->network_id(),
            vblock->block->pool_index(),
            vblock->block->height());
        acceptor()->CommitSynced(vblock->block);
        auto elect_item = elect_info()->GetElectItem(
                vblock->block->network_id(),
                vblock->ElectHeight());
        if (elect_item && elect_item->IsValid()) {
            elect_item->consensus_stat(pool_idx_)->Commit(vblock);
        }
        
        auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
        if (!latest_committed_block || latest_committed_block->view < vblock->view) {
            view_block_chain()->SetLatestCommittedBlock(vblock);        
        }
        
        view_block_chain()->StoreToDb(vblock, self_commit_qc);
    }

    // 已经投票
    inline bool HasVoted(const View& view) {
        return last_vote_view_ >= view;
    }    

    inline std::shared_ptr<IBlockWrapper> wrapper() const {
        return block_wrapper_;
    }

    inline std::shared_ptr<Crypto> crypto() const {
        return crypto_;
    }

    inline std::shared_ptr<IBlockAcceptor> acceptor() const {
        return block_acceptor_;
    }

    inline std::shared_ptr<ViewBlockChain> view_block_chain() const {
        return view_block_chain_;
    }

    inline std::shared_ptr<Pacemaker> pacemaker() const {
        return pacemaker_;
    }

    inline std::shared_ptr<LeaderRotation> leader_rotation() const {
        return leader_rotation_;
    }

    inline std::shared_ptr<ElectInfo> elect_info() const {
        return elect_info_;
    }

    bool IsStuck() const {
        // 超时时间必须大于阈值
        if (pacemaker()->DurationUs() < STUCK_PACEMAKER_DURATION_MIN_US) {
            return false;
        }
        // highqc 之前连续三个块都是空交易，则认为 stuck
        auto v_block1 = std::make_shared<ViewBlock>();
        Status s = view_block_chain()->Get(pacemaker()->HighQC()->view_block_hash, v_block1);
        if (s != Status::kSuccess || v_block1->block->tx_list_size() > 0) {
            return false;
        }
        auto v_block2 = view_block_chain()->QCRef(v_block1);
        if (!v_block2 || v_block2->block->tx_list_size() > 0) {
            return false;
        }
        auto v_block3 = view_block_chain()->QCRef(v_block2);
        if (!v_block3 || v_block3->block->tx_list_size() > 0) {
            return false;
        }
        return true;   
    }

    void TryRecoverFromStuck();

    std::shared_ptr<QC> GetQcOf(const std::shared_ptr<ViewBlock>& v_block) {
        auto qc = view_block_chain()->GetQcOf(v_block);
        if (!qc) {
            if (pacemaker()->HighQC()->view_block_hash == v_block->hash) {
                view_block_chain()->SetQcOf(v_block, pacemaker()->HighQC());
                return pacemaker()->HighQC();
            }
        }
        return qc;
    }
private:
    uint32_t pool_idx_;
    std::shared_ptr<Crypto> crypto_;
    std::shared_ptr<Pacemaker> pacemaker_;
    std::shared_ptr<IBlockAcceptor> block_acceptor_;
    std::shared_ptr<IBlockWrapper> block_wrapper_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::shared_ptr<LeaderRotation> leader_rotation_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    View last_vote_view_;
    common::FlowControl recover_from_struct_fc_{1};
    common::FlowControl reset_timer_fc_{1};
    SyncPoolFn sync_pool_fn_ = nullptr;

    Status Commit(
            const std::shared_ptr<ViewBlock>& v_block,
            const std::shared_ptr<QC> commit_qc);
    std::shared_ptr<ViewBlock> CheckCommit(const std::shared_ptr<QC>& qc);    
    Status CommitInner(const std::shared_ptr<ViewBlock>& v_block);
    Status VerifyVoteMsg(
            const hotstuff::protobuf::VoteMsg& vote_msg);
    Status VerifyLeader(const uint32_t& leader_idx);
    Status VerifyQC(const std::shared_ptr<QC>& qc);
    Status VerifyViewBlock(
            const std::shared_ptr<ViewBlock>& v_block, 
            const std::shared_ptr<ViewBlockChain>& view_block_chain,
            const std::shared_ptr<TC>& tc,
            const uint32_t& elect_height);    
    Status ConstructProposeMsg(
            const std::shared_ptr<SyncInfo>& sync_info,
            hotstuff::protobuf::ProposeMsg* pro_msg);
    Status ConstructVoteMsg(
            hotstuff::protobuf::VoteMsg* vote_msg,
            uint64_t elect_height, 
            const std::shared_ptr<ViewBlock>& v_block);    
    Status ConstructViewBlock( 
            std::shared_ptr<ViewBlock>& view_block,
            std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose);
    Status ConstructHotstuffMsg(
            const MsgType msg_type, 
            pb_ProposeMsg* pb_pro_msg, 
            pb_VoteMsg* pb_vote_msg,
            pb_NewViewMsg* pb_nv_msg,
            pb_HotstuffMessage* pb_hf_msg);
    Status SendMsgToLeader(std::shared_ptr<transport::TransportMessage>& hotstuff_msg, const MsgType msg_type);
    // 是否允许空交易
    bool IsEmptyBlockAllowed(const std::shared_ptr<ViewBlock>& v_block);
    Status StoreVerifiedViewBlock(const std::shared_ptr<ViewBlock>& v_block, const std::shared_ptr<QC>& qc);
    // 获取该 Leader 要增加的 consensus stat succ num
    uint32_t GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block);
};

} // namespace consensus

} // namespace shardora

