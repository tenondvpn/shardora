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
#include <consensus/hotstuff/hotstuff_utils.h>
#include <protos/hotstuff.pb.h>
#include <protos/transport.pb.h>
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
static const bool WITH_CONSENSUS_STATISTIC =
    true; // 是否开启 leader 的共识数据统计

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
    Status TryCommit(const std::shared_ptr<QC> commit_qc, uint64_t t_idx = 9999999lu);
    // 消费等待队列中的 ProposeMsg
    int TryWaitingProposeMsgs() {
        int succ = handle_propose_pipeline_.CallWaitingProposeMsgs();
        ZJC_DEBUG("pool: %d, handle waiting propose, %d/%d", pool_idx_, succ, handle_propose_pipeline_.Size());
        return succ;
    }

    void StopVoting(const View& view) {
        if (last_vote_view_ < view) {
            last_vote_view_ = view;
        }
    }

    void HandleSyncedViewBlock(
            const std::shared_ptr<ViewBlock>& vblock,
            const std::shared_ptr<QC>& self_commit_qc) {
        auto db_batch = std::make_shared<db::DbWriteBatch>();
        auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(vblock->block, db_batch);
        ZJC_DEBUG("now handle synced view block %u_%u_%lu",
            vblock->block->network_id(),
            vblock->block->pool_index(),
            vblock->block->height());
        view_block_chain()->StoreToDb(vblock, self_commit_qc, 99999999lu, db_batch);
        acceptor()->CommitSynced(queue_item_ptr);
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

    int IsStuck() const {
        // 超时时间必须大于阈值
        if (pacemaker()->DurationUs() < STUCK_PACEMAKER_DURATION_MIN_US) {
            return 1;
        }
        // highqc 之前连续三个块都是空交易，则认为 stuck
        auto v_block1 = std::make_shared<ViewBlock>();
        Status s = view_block_chain()->Get(pacemaker()->HighQC()->view_block_hash(), v_block1);
        if (s != Status::kSuccess || v_block1->block->tx_list_size() > 0) {
            return 2;
        }
        auto v_block2 = view_block_chain()->QCRef(v_block1);
        if (!v_block2 || v_block2->block->tx_list_size() > 0) {
            return 3;
        }
        auto v_block3 = view_block_chain()->QCRef(v_block2);
        if (!v_block3 || v_block3->block->tx_list_size() > 0) {
            return 4;
        }
        return 0;   
    }

    void TryRecoverFromStuck();

    std::shared_ptr<QC> GetQcOf(const std::shared_ptr<ViewBlock>& v_block) {
        auto qc = view_block_chain()->GetQcOf(v_block);
        if (!qc) {
            if (pacemaker()->HighQC()->view_block_hash() == v_block->hash) {
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
    uint64_t timer_delay_us_ = common::TimeUtils::TimestampUs() + 10000000lu;
    Pipeline handle_propose_pipeline_;

    void InitHandleProposeMsgPipeline() {
        // 仅 VerifyLeader 和 ChainStore 出错后允许重试
        // 因为一旦节点状态落后，父块缺失，ChainStore 会一直失败，导致无法追上进度
        // 而对于 Leader，理论上是可以通过 QC 同步追上进度的，但 Propose&Vote 要比同步 QC 快很多，因此也会一直失败
        // 因此，要在同步完成之后，给新提案重新 VerifyLeader 和 ChainStore 的机会 
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_HasVote, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyLeader, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyTC, this, std::placeholders::_1));                        
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyQC, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyViewBlock, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_TxAccept, this, std::placeholders::_1));
        handle_propose_pipeline_.AddRetryStep(std::bind(&Hotstuff::HandleProposeMsgStep_ChainStore, this, std::placeholders::_1), 2);
        handle_propose_pipeline_.AddStep(std::bind(&Hotstuff::HandleProposeMsgStep_Vote, this, std::placeholders::_1));
        handle_propose_pipeline_.SetCondition(std::bind(&Hotstuff::HandleProposeMsgCondition, this, std::placeholders::_1));
        handle_propose_pipeline_.UseRetry(true); // 开启断点重试
    }

    Status HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    bool HandleProposeMsgCondition(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // 仅新 v_block 才能允许执行
        return pro_msg_wrap->v_block->view > view_block_chain()->GetMaxHeight();
    }

    Status Commit(
            const std::shared_ptr<ViewBlock>& v_block,
            const std::shared_ptr<QC> commit_qc,
            uint64_t test_index);
    std::shared_ptr<ViewBlock> CheckCommit(const std::shared_ptr<QC>& qc);    
    void CommitInner(
        const std::shared_ptr<ViewBlock>& v_block,
        uint64_t test_index,
        std::shared_ptr<block::BlockToDbItem>&);
    Status VerifyVoteMsg(
            const hotstuff::protobuf::VoteMsg& vote_msg);
    Status VerifyLeader(const uint32_t& leader_idx);
    Status VerifyQC(const std::shared_ptr<QC>& qc);
    Status VerifyTC(const std::shared_ptr<TC>& tc);
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

