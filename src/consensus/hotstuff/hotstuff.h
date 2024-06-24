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
    Status TryCommit(const std::shared_ptr<QC> commit_qc);
    // 消费等待队列中的 ProposeMsg
    int TryWaitingProposeMsgs() {
        ZJC_INFO("====8.0");
        int succ = handle_propose_pipeline_.CallWaitingProposeMsgs();
        ZJC_INFO("====8.5");
        ZJC_INFO("pool: %d, handle waiting propose, %d --- %d", pool_idx_, succ, handle_propose_pipeline_.Size());
        ZJC_INFO("====8.6");
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
    uint64_t timer_delay_us_ = common::TimeUtils::TimestampUs() + 10000000lu;
    Pipeline handle_propose_pipeline_;

    void InitPipeline() {
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyLeader, this, std::placeholders::_1), 2);
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyTC, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyQC, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_VerifyViewBlockAndCommit, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_TxAccept, this, std::placeholders::_1));
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_ChainStore, this, std::placeholders::_1), 2);
        handle_propose_pipeline_.AddStepFn(std::bind(&Hotstuff::HandleProposeMsgStep_Vote, this, std::placeholders::_1));        
    }

    Status HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        if (VerifyLeader(pro_msg_wrap->v_block->leader_idx) != Status::kSuccess) {
            // TODO 一旦某个节点状态滞后，那么 Leader 就与其他 replica 不同，导致无法处理新提案
            // 只能依赖同步，但由于同步慢于新的 Propose 消息
            // 即是这里再加一次同步，也很难追上 Propose 的速度，导致该节点掉队，因此还是需要一个队列缓存一下
            // 暂时无法处理的 Propose 消息
            if (sync_pool_fn_) { // leader 不一致触发同步
                sync_pool_fn_(pool_idx_, 1);
            }
            ZJC_WARN("verify leader failed, pool: %d view: %lu, hash64: %lu", 
                pool_idx_, pro_msg_wrap->v_block->view, pro_msg_wrap->header.hash64());

            return Status::kError;
        }        
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_VerifyTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // 3 Verify TC
        std::shared_ptr<TC> tc = nullptr;
        if (!pro_msg_wrap->pro_msg->tc_str().empty()) {
            tc = std::make_shared<TC>();
            if (!tc->Unserialize(pro_msg_wrap->pro_msg->tc_str())) {
                ZJC_ERROR("tc Unserialize is error.");
                return Status::kError;
            }
            if (VerifyTC(tc) != Status::kSuccess) {
                ZJC_ERROR("pool: %d verify tc failed: %lu", pool_idx_, pro_msg_wrap->v_block->view);
                return Status::kError;
            }
        }

        pro_msg_wrap->tc = tc;
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        if (VerifyQC(pro_msg_wrap->v_block->qc) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify qc failed: %lu", pool_idx_, pro_msg_wrap->v_block->view);
            return Status::kError;
        }        
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_VerifyViewBlockAndCommit(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // 4 Verify ViewBlock
        if (VerifyViewBlock(
                    pro_msg_wrap->v_block,
                    view_block_chain(),
                    pro_msg_wrap->tc,
                    pro_msg_wrap->pro_msg->elect_height()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d, Verify ViewBlock is error. hash: %s, hash64: %lu", pool_idx_,
                common::Encode::HexEncode(pro_msg_wrap->v_block->hash).c_str(),
                pro_msg_wrap->header.hash64());
            return Status::kError;
        }    
    
        ZJC_DEBUG("====1.1 pool: %d, verify view block success, view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
            pool_idx_,
            pro_msg_wrap->pro_msg->view_item().view(),
            common::Encode::HexEncode(pro_msg_wrap->pro_msg->view_item().hash()).c_str(),
            pacemaker()->HighQC()->view,
            pro_msg_wrap->header.hash64());
        // 切换视图
        pacemaker()->AdvanceView(new_sync_info()->WithQC(pro_msg_wrap->v_block->qc));

        // Commit 一定要在 Txs Accept 之前，因为一旦 v_block->qc 合法就已经可以 Commit 了，不需要 Txs 合法
        TryCommit(pro_msg_wrap->v_block->qc);        
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
        auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
        auto block = pro_msg_wrap->v_block->block;
        block_info->block = block;
        block_info->tx_type = pro_msg_wrap->pro_msg->tx_propose().tx_type();
        for (const auto& tx : pro_msg_wrap->pro_msg->tx_propose().txs()) {
            if (!view_block_chain_->CheckTxGidValid(tx.gid(), pro_msg_wrap->v_block->parent_hash)) {
                // assert(false);
                ZJC_DEBUG("====1.1.1 check gid failed: %s pool: %d, verify view block success, "
                    "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
                    common::Encode::HexEncode(tx.gid()).c_str(),
                    pool_idx_,
                    pro_msg_wrap->pro_msg->view_item().view(),
                    common::Encode::HexEncode(pro_msg_wrap->pro_msg->view_item().hash()).c_str(),
                    pacemaker()->HighQC()->view,
                    pro_msg_wrap->header.hash64());
                return Status::kError;
            }

            block_info->txs.push_back(&tx);
            pro_msg_wrap->v_block->added_txs.insert(tx.gid());
        }

        block_info->view = pro_msg_wrap->v_block->view;
        if (acceptor()->Accept(block_info, true) != Status::kSuccess) {
            ZJC_DEBUG("====1.1.2 Accept pool: %d, verify view block success, "
                "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
                pool_idx_,
                pro_msg_wrap->pro_msg->view_item().view(),
                common::Encode::HexEncode(pro_msg_wrap->pro_msg->view_item().hash()).c_str(),
                pacemaker()->HighQC()->view,
                pro_msg_wrap->header.hash64());
            return Status::kError;
        }

        // 更新 leader 共识分数
        if (WITH_CONSENSUS_STATISTIC) {
            auto elect_item = elect_info()->GetElectItem(
                    pro_msg_wrap->v_block->block->network_id(),
                    pro_msg_wrap->v_block->ElectHeight());
            if (elect_item && elect_item->IsValid()) {
                elect_item->consensus_stat(pool_idx_)->Accept(pro_msg_wrap->v_block, GetPendingSuccNumOfLeader(pro_msg_wrap->v_block));
            }        
        }
    
        // 更新哈希值
        pro_msg_wrap->v_block->UpdateHash();        
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // 6 add view block
        if (view_block_chain()->Store(pro_msg_wrap->v_block) != Status::kSuccess) {
            ZJC_ERROR("pool: %d, add view block error. hash: %s",
                pool_idx_, common::Encode::HexEncode(pro_msg_wrap->v_block->hash).c_str());
            // TODO 父块不存在，则加入等待队列，后续处理
            return Status::kError;
        }
        // 成功接入链中，标记交易占用
        acceptor()->MarkBlockTxsAsUsed(pro_msg_wrap->v_block->block);        
        return Status::kSuccess;
    }

    Status HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        ZJC_INFO("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, curView: %lu, vblock: %lu, txs: %lu, hash64: %lu",
            pool_idx_,
            pacemaker()->HighQC()->view,
            pacemaker()->HighTC()->view,
            view_block_chain()->Size(),
            pacemaker()->CurView(),
            pro_msg_wrap->v_block->view,
            pro_msg_wrap->v_block->block->tx_list_size(),
            pro_msg_wrap->header.hash64());
        
        auto trans_msg = std::make_shared<transport::TransportMessage>();
        auto& trans_header = trans_msg->header;
        auto* hotstuff_msg = trans_header.mutable_hotstuff();
        auto* vote_msg = hotstuff_msg->mutable_vote_msg();
        // Construct VoteMsg
        Status s = ConstructVoteMsg(vote_msg, pro_msg_wrap->pro_msg->elect_height(), pro_msg_wrap->v_block);
        if (s != Status::kSuccess) {
            ZJC_ERROR("pool: %d, ConstructVoteMsg error %d, hash64: %lu", pool_idx_, s, pro_msg_wrap->header.hash64());
            return Status::kError;
        }
        // Construct HotstuffMessage and send
        s = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
        if (s != Status::kSuccess) {
            ZJC_ERROR("pool: %d, ConstructHotstuffMsg error %d, hash64: %lu", pool_idx_, s, pro_msg_wrap->header.hash64());
            return Status::kError;
        }
    
        if (SendMsgToLeader(trans_msg, VOTE) != Status::kSuccess) {
            ZJC_ERROR("pool: %d, Send vote message is error.", pool_idx_, pro_msg_wrap->header.hash64());
        }

        // 避免对 view 重复投票
        StopVoting(pro_msg_wrap->v_block->view);

        ZJC_DEBUG("pool: %d, Send vote message is success., hash64: %lu", pool_idx_, pro_msg_wrap->header.hash64());        
        return Status::kSuccess;
    }

    Status Commit(
            const std::shared_ptr<ViewBlock>& v_block,
            const std::shared_ptr<QC> commit_qc);
    std::shared_ptr<ViewBlock> CheckCommit(const std::shared_ptr<QC>& qc);    
    Status CommitInner(const std::shared_ptr<ViewBlock>& v_block);
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

