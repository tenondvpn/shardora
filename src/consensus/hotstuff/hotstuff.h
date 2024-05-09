#pragma once
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h> 
#include <consensus/hotstuff/view_block_chain.h>
#include <protos/hotstuff.pb.h>
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
};

typedef hotstuff::protobuf::ProposeMsg  pb_ProposeMsg;
typedef hotstuff::protobuf::HotstuffMessage  pb_HotstuffMessage;
typedef hotstuff::protobuf::VoteMsg pb_VoteMsg;
typedef hotstuff::protobuf::NewViewMsg  pb_NewViewMsg;

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

    void Init(std::shared_ptr<db::Db>& db_);
    Status Start();

    void NewView(const std::shared_ptr<SyncInfo>& sync_info);
    void HandleProposeMsg(const transport::protobuf::Header& header);
    void HandleVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg);
    void HandleNewViewMsg(const transport::protobuf::Header& header);
    Status Propose(const std::shared_ptr<SyncInfo>& sync_info);
    void HandleVoteMsg(const transport::protobuf::Header& header);
    Status Commit(const std::shared_ptr<ViewBlock>& v_block);
    std::shared_ptr<ViewBlock> CheckCommit(const std::shared_ptr<ViewBlock>& v_block);
    Status VerifyViewBlock(
            const std::shared_ptr<ViewBlock>& v_block, 
            const std::shared_ptr<ViewBlockChain>& view_block_chain,
            const std::shared_ptr<TC>& tc,
            const uint32_t& elect_height);    

    void StopVoting(const View& view) {
        if (last_vote_view_ < view) {
            last_vote_view_ = view;
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

    Status CommitInner(const std::shared_ptr<ViewBlock>& v_block);
    Status VerifyVoteMsg(
            const hotstuff::protobuf::VoteMsg& vote_msg);
    Status VerifyLeader(const std::shared_ptr<ViewBlock>& view_block);
    Status ConstructProposeMsg(
            const std::shared_ptr<SyncInfo>& sync_info,
            std::shared_ptr<hotstuff::protobuf::ProposeMsg>& pro_msg);
    Status ConstructVoteMsg(
            std::shared_ptr<hotstuff::protobuf::VoteMsg>& vote_msg,
            const uint32_t& elect_height, 
            const std::shared_ptr<ViewBlock>& v_block);    
    Status ConstructViewBlock( 
            std::shared_ptr<ViewBlock>& view_block,
            std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose);
    Status ConstructHotstuffMsg(
            const MsgType msg_type, 
            const std::shared_ptr<pb_ProposeMsg>& pb_pro_msg, 
            const std::shared_ptr<pb_VoteMsg>& pb_vote_msg,
            const std::shared_ptr<pb_NewViewMsg>& pb_nv_msg,
            std::shared_ptr<pb_HotstuffMessage>& pb_hf_msg);
    Status SendVoteMsg(std::shared_ptr<hotstuff::protobuf::HotstuffMessage>& hotstuff_msg);
    // 是否允许空交易
    bool IsEmptyBlockAllowed(const std::shared_ptr<ViewBlock>& v_block);
};

} // namespace consensus

} // namespace shardora

