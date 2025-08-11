#pragma once
#ifdef USE_AGG_BLS
#include <consensus/hotstuff/agg_crypto.h>
#else
#include <consensus/hotstuff/crypto.h>
#endif
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
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

namespace consensus {
    class HotstuffManager;
}

namespace hotstuff {

enum MsgType {
  PROPOSE,
  VOTE,
  NEWVIEW,
  PRE_RESET_TIMER,
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
            consensus::HotstuffManager& hotstuff_mgr,
            std::shared_ptr<sync::KeyValueSync>& kv_sync,
            const uint32_t& pool_idx,
            const std::shared_ptr<LeaderRotation>& lr,
            const std::shared_ptr<ViewBlockChain>& chain,
            const std::shared_ptr<IBlockAcceptor>& acceptor,
            const std::shared_ptr<IBlockWrapper>& wrapper,
            const std::shared_ptr<Pacemaker>& pm,
#ifdef USE_AGG_BLS
            const std::shared_ptr<AggCrypto>& crypto,
#else
            const std::shared_ptr<Crypto>& crypto,
#endif
            const std::shared_ptr<ElectInfo>& elect_info,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr) :
        hotstuff_mgr_(hotstuff_mgr),
        kv_sync_(kv_sync),
        pool_idx_(pool_idx),
        crypto_(crypto),
        pacemaker_(pm),
        block_acceptor_(acceptor),
        block_wrapper_(wrapper),
        view_block_chain_(chain),
        leader_rotation_(lr),
        elect_info_(elect_info),
        db_(db),
        tm_block_mgr_(tm_block_mgr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        pacemaker_->SetNewProposalFn(std::bind(&Hotstuff::Propose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pacemaker_->SetStopVotingFn(std::bind(&Hotstuff::StopVoting, this, std::placeholders::_1));        

    }
    ~Hotstuff() {};

    void Init();
    
    // std::shared_ptr<ViewBlock> GetViewBlock(uint64_t view) {
    //     return view_block_chain_->Get(view);
    // }

    void SetSyncPoolFn(SyncPoolFn sync_fn) {
        sync_pool_fn_ = sync_fn;
    }    
    
    Status Start();
    void HandleProposeMsg(const transport::MessagePtr& msg_ptr);
    void HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr);
    void HandleVoteMsg(const transport::MessagePtr& msg_ptr);
    Status Propose(
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc,
        const transport::MessagePtr& msg_ptr);
    Status TryCommit(const transport::MessagePtr& msg_ptr, const QC& commit_qc, uint64_t t_idx = 9999999lu);
    Status HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> propose_msg_wrap);

    void StopVoting(const View& view) {
        if (last_vote_view_ < view) {
            last_vote_view_ = view;
            ZJC_DEBUG("pool: %u, set last vote view: %lu", pool_idx_, view);
        }
    }

    void HandleSyncedViewBlock(
            std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock);

    // 已经投票
    inline bool HasVoted(const View& view) {
        return last_vote_view_ >= view;
    }    

    inline std::shared_ptr<IBlockWrapper> wrapper() const {
        return block_wrapper_;
    }

#ifdef USE_AGG_BLS
    inline std::shared_ptr<AggCrypto> crypto() const {
        return crypto_;
    }    
#else
    inline std::shared_ptr<Crypto> crypto() const {
        return crypto_;
    }
#endif

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

    int IsStuck() {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        // 超时时间必须大于阈值
        if (recover_from_stuck_timeout_ >= now_tm_us) {
            return 1;
        }

        recover_from_stuck_timeout_ = now_tm_us + STUCK_PACEMAKER_DURATION_MIN_US;
        return 0;
    }

    inline uint64_t max_view() {
        if (last_vote_view_ > pacemaker()->CurView()) {
            return last_vote_view_;
        } else if (last_vote_view_ == pacemaker()->CurView()) {
            return last_vote_view_ + 1;
        }

        return pacemaker()->CurView();
    }

    void TryRecoverFromStuck(
        const transport::MessagePtr& msg_ptr, 
        bool has_new_tx, 
        bool has_system_tx);

private:
    void InitAddNewViewBlock(std::shared_ptr<ViewBlock>& view_block);
    Status HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_Directly(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        const std::string& expect_view_block_hash);

    bool HandleProposeMsgCondition(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // 仅新 v_block 才能允许执行
        return false;// pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().view_item().qc().view() > view_block_chain()->GetMaxHeight();
    }

    Status HandleTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status Commit(
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlockInfo>& v_block,
        const QC& commit_qc,
        uint64_t test_index);
    std::shared_ptr<ViewBlockInfo> CheckCommit(const QC& qc);
    Status VerifyVoteMsg(
            const hotstuff::protobuf::VoteMsg& vote_msg);
    Status VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status VerifyFollower(const transport::MessagePtr& msg_ptr);
    Status VerifyQC(const QC& qc);
    Status VerifyViewBlock(
            const ViewBlock& v_block, 
            const std::shared_ptr<ViewBlockChain>& view_block_chain,
            const TC* tc,
            const uint32_t& elect_height);    
    Status ConstructProposeMsg(const transport::MessagePtr& msg_ptr, hotstuff::protobuf::ProposeMsg* pro_msg);
    Status ConstructVoteMsg(
        const transport::MessagePtr& msg_ptr,
        hotstuff::protobuf::VoteMsg* vote_msg,
        uint64_t elect_height, 
        const std::shared_ptr<ViewBlock>& v_block);    
    Status ConstructViewBlock( 
        const transport::MessagePtr& msg_ptr, 
        ViewBlock* view_block,
        hotstuff::protobuf::TxPropose* tx_propose);
    void ConstructHotstuffMsg(
            const MsgType msg_type, 
            pb_ProposeMsg* pb_pro_msg, 
            pb_VoteMsg* pb_vote_msg,
            pb_NewViewMsg* pb_nv_msg,
            pb_HotstuffMessage* pb_hf_msg);
    Status SendMsgToLeader(
        common::BftMemberPtr leader, 
        std::shared_ptr<transport::TransportMessage>& hotstuff_msg, 
        const MsgType msg_type);
    // 是否允许空交易
    bool IsEmptyBlockAllowed(const ViewBlock& v_block);
    Status StoreVerifiedViewBlock(const std::shared_ptr<ViewBlock>& v_block, const std::shared_ptr<QC>& qc);
    // 获取该 Leader 要增加的 consensus stat succ num
    uint32_t GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block);

    static const uint64_t kLatestPoposeSendTxToLeaderPeriodMs = 3000lu;

    uint32_t pool_idx_;
#ifdef USE_AGG_BLS
    std::shared_ptr<AggCrypto> crypto_;
#else
    std::shared_ptr<Crypto> crypto_;
#endif
    std::shared_ptr<Pacemaker> pacemaker_;
    std::shared_ptr<IBlockAcceptor> block_acceptor_;
    std::shared_ptr<IBlockWrapper> block_wrapper_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::shared_ptr<LeaderRotation> leader_rotation_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    View last_vote_view_ = 0;
    View last_leader_propose_view_ = 0;
    SyncPoolFn sync_pool_fn_ = nullptr;
    std::map<View, transport::MessagePtr> voted_msgs_;
    uint64_t latest_propose_msg_tm_ms_ = 0;
    std::shared_ptr<view_block::protobuf::QcItem> latest_qc_item_ptr_;
    uint64_t propose_debug_index_ = 0;
    uint64_t recover_from_stuck_timeout_ = 0;
    bool has_user_tx_tag_ = false;
    std::shared_ptr<transport::TransportMessage> latest_leader_propose_message_;
    std::shared_ptr<sync::KeyValueSync> kv_sync_;
    consensus::HotstuffManager& hotstuff_mgr_;
    volatile View db_stored_view_ = 0llu;
    uint64_t prev_sync_latest_view_tm_ms_ = 0;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    
// #ifndef NDEBUG
    static std::atomic<uint32_t> sendout_bft_message_count_;
// #endif
};

} // namespace consensus

} // namespace shardora
