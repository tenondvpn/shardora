#pragma once

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <consensus/hotstuff/crypto.h>
#include "consensus/consensus_utils.h"
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <protos/hotstuff.pb.h>
#include <protos/transport.pb.h>
#include <protos/view_block.pb.h>
#include <security/security.h>
#include "network/dht_manager.h"
#include "common/global_info.h"
#include "common/encode.h"

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
    true; // Whether to enable leader consensus data statistics

class Hotstuff {
public:
    Hotstuff() = default;
    Hotstuff(
            std::shared_ptr<block::BlockManager>& block_mgr,
            consensus::HotstuffManager& hotstuff_mgr,
            std::shared_ptr<sync::KeyValueSync>& kv_sync,
            const uint32_t& pool_idx,
            const std::shared_ptr<ViewBlockChain>& chain,
            const std::shared_ptr<IBlockAcceptor>& acceptor,
            const std::shared_ptr<IBlockWrapper>& wrapper,
            const std::shared_ptr<Pacemaker>& pm,
            const std::shared_ptr<Crypto>& crypto,
            const std::shared_ptr<ElectInfo>& elect_info,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr,
            consensus::BlockCacheCallback new_block_cache_callback) :
        block_mgr_(block_mgr),
        hotstuff_mgr_(hotstuff_mgr),
        kv_sync_(kv_sync),
        pool_idx_(pool_idx),
        crypto_(crypto),
        pacemaker_(pm),
        block_acceptor_(acceptor),
        block_wrapper_(wrapper),
        view_block_chain_(chain),
        elect_info_(elect_info),
        db_(db),
        tm_block_mgr_(tm_block_mgr),
        new_block_cache_callback_(new_block_cache_callback) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }
    ~Hotstuff() {};

    void Init() {}

    // std::shared_ptr<ViewBlock> GetViewBlock(uint64_t view) {
    //     return view_block_chain_->Get(view);
    // }

    void SetSyncPoolFn(SyncPoolFn sync_fn) {
        sync_pool_fn_ = sync_fn;
    }

    void OnNewElectBlock(
            uint32_t sharding_id,
            uint64_t elect_height,
            common::MembersPtr& members,
            const libff::alt_bn128_G2& common_pk,
            const libff::alt_bn128_Fr& sec_key) {
        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            return;
        }

        if (latest_elect_height_ >= elect_height) {
            return;
        }

        latest_elect_height_ = elect_height;
        consecutive_failures_ = 0;
        update_latest_view_tm_ = true;
        if (latest_qc_item_ptr_ == nullptr || latest_elect_height_ > latest_qc_item_ptr_->elect_height()) {
            last_stable_leader_member_index_ = GetEpochLeaderIndex();
            SHARDORA_DEBUG("pool: %u, new elect block, elect height: %lu, last_stable_leader_member_index_: %u",
                pool_idx_, latest_elect_height_, last_stable_leader_member_index_.load());
        }

        if (latest_qc_item_ptr_ == nullptr) {
            SHARDORA_WARN("pool: %u, OnNewElectBlock skipping GetLeader: latest_qc_item_ptr_ is null, "
                "elect_height: %lu", pool_idx_, elect_height);
            return;
        }

        View out_view = 0;
        auto leader_block_tm = GetLeaderBlockTimestamp();
        GetLeader(
            last_stable_leader_member_index_, 
            *latest_qc_item_ptr_, 
            &out_view,
            leader_block_tm,
            true);
    }

    void OnTimeBlock() {
        update_latest_view_tm_ = true;
    }

    Status Start();
    void HandleProposeMsg(const transport::MessagePtr& msg_ptr);
    int HandleProposeMsgImpl(const transport::MessagePtr& msg_ptr);
    void HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr);
    void HandleVoteMsg(const transport::MessagePtr& msg_ptr);
    Status Propose(
        View leader_view,
        common::BftMemberPtr leader,
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc,
        const transport::MessagePtr& msg_ptr,
        uint64_t leader_tm_ms);
    Status TryCommit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const transport::MessagePtr& msg_ptr,
        const QC& commit_qc);
    Status HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> propose_msg_wrap);

    void StopVoting(const View& view) {
        if (last_vote_view_ < view) {
            last_vote_view_ = view;
            SHARDORA_DEBUG("pool: %u, set last vote view: %lu", pool_idx_, view);
        }
    }

    void HandleSyncedViewBlock(
            std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock);

    // Voted
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

    inline std::shared_ptr<ElectInfo> elect_info() const {
        return elect_info_;
    }

    int IsStuck() {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        // Timeout must be greater than threshold
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

    common::BftMemberPtr is_other_leader() {
        auto local_idx = GetLocalMemberIdx();
        View out_view = 0;
        auto leader = LoadPoolTxLeader();
        if (leader && leader->index != local_idx) {
            return leader;
        }
        
        return nullptr;
    }

    common::BftMemberPtr GetLeader() {
        auto leader = LoadPoolTxLeader();
        if (leader ) {
            auto dht_ptr = network::DhtManager::Instance()->GetDht(
                common::GlobalInfo::Instance()->network_id());
            if (dht_ptr != nullptr) {
                auto nodes = dht_ptr->readonly_hash_sort_dht();
                for (auto iter = nodes->begin(); iter != nodes->end(); ++iter) {
                    if ((*iter)->id == leader->id) {
                        leader->public_ip = common::IpToUint32((*iter)->public_ip.c_str());
                        leader->public_port = (*iter)->public_port;
                        SHARDORA_DEBUG("succes query GetLeader set member %s ip port %s:%d, pool: %d",
                            common::Encode::HexEncode((*iter)->id).c_str(),
                            (*iter)->public_ip.c_str(),
                            (*iter)->public_port,
                            pool_idx_);
                        break;
                    }
                }
            }
        }
        return leader;
    }

    // Check if the local node is an active committee member for this pool.
    // Used by sync to decide whether consensus can produce blocks locally.
    inline bool IsLocalMember() const {
        return GetLocalMemberIdx() != common::kInvalidUint32;
    }

private:
    void InitAddNewViewBlock(
        std::shared_ptr<ViewBlockChain> view_block_chain,
        std::shared_ptr<ViewBlock>& view_block);
    Status HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    // Status HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status HandleProposeMsgStep_Directly(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap,
        const std::string& expect_view_block_hash);
    void StartInit();
    Status HandleVoteMsgImpl(const transport::MessagePtr& msg_ptr);
    void UpdateLatestQcItemPtr(std::shared_ptr<view_block::protobuf::QcItem> qc_ptr) {
        if (qc_ptr->elect_height() >= latest_elect_height_ && qc_ptr->leader_idx() != common::kInvalidUint32) {
            last_stable_leader_member_index_ = qc_ptr->leader_idx();
            SHARDORA_DEBUG("pool: %u, update latest qc item ptr, elect height: %lu, last_stable_leader_member_index_: %u, "
                "old last_vote_view_: %lu,  new last_vote_view_: %lu",
                pool_idx_, qc_ptr->elect_height(), last_stable_leader_member_index_.load(),
                last_vote_view_,
                qc_ptr->view());
            laste_vote_prev_view_tm_.Put(qc_ptr->view(), common::TimeUtils::TimestampUs());
        }

        latest_qc_item_ptr_ = qc_ptr;
        auto high_view_block = view_block_chain_->HighViewBlock();
        if (latest_leader_propose_message_ && high_view_block != nullptr && 
                high_view_block->qc().view() < qc_ptr->view() && 
                latest_leader_propose_message_->header.hotstuff().pro_msg().tc().view() < qc_ptr->view()) {
            SHARDORA_DEBUG("pool: %u, update latest qc item ptr view: %lu is higher than high view block view: %lu",
                pool_idx_, qc_ptr->view(), high_view_block->qc().view());
            SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = nullptr", pool_idx_);
            latest_leader_propose_message_ = nullptr;
        }
    }

    bool HandleProposeMsgCondition(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
        // Only new v_block is allowed to execute
        return false;// pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().view_item().qc().view() > view_block_chain()->GetMaxHeight();
    }

    Status HandleTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap);
    Status Commit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlockInfo>& v_block,
        const QC& commit_qc);
    std::shared_ptr<ViewBlockInfo> CheckCommit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const QC& qc);
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
    Status ConstructProposeMsg(
        View leader_view,
        common::BftMemberPtr leader,
        const transport::MessagePtr& msg_ptr, 
        hotstuff::protobuf::ProposeMsg* pro_msg);
    Status ConstructVoteMsg(
        const transport::MessagePtr& msg_ptr,
        hotstuff::protobuf::VoteMsg* vote_msg,
        uint64_t elect_height,
        uint64_t tm_height,
        const std::shared_ptr<ViewBlock>& v_block,
        const LeaderNonceMap* leader_nonce_map = nullptr);
    Status ConstructViewBlock(
        View leader_view,
        common::BftMemberPtr leader,
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
    bool InitLoadLatestBlock(
        std::shared_ptr<ViewBlockChain> view_block_chain,
        uint32_t network_id,
        uint32_t pool_index);
    // Is empty transaction allowed
    bool IsEmptyBlockAllowed(const ViewBlock& v_block);
    // Get the consensus stat succ num to be increased by the Leader
    uint32_t GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block);
    void BroadcastGlobalPoolBlock(const std::shared_ptr<ViewBlock>& v_block);
    void HandleTimerMessage();
    void SyncLaterBlocks(
        std::shared_ptr<ViewBlockChain> view_block_chain,
        uint32_t network_id,
        uint32_t pool_index,
        View view);

    uint64_t GetLeaderBlockTimestamp() {
        auto prev_block = view_block_chain()->HighViewBlock();
        uint64_t cur_time = common::TimeUtils::TimestampMs();
        if (prev_block == nullptr) {
            return cur_time;
        }

        auto tm = prev_block->block_info().timestamp() > cur_time ? prev_block->block_info().timestamp() + 1 : cur_time;
        return tm;
    }

    common::BftMemberPtr GetLeader(
            uint32_t new_leader_idx, 
            const view_block::protobuf::QcItem& leader_latest_qc, 
            View* out_view,
            int64_t leader_tm_ms,
            bool debug) {
        // debug = false;
        // auto members = elect_item->valid_leaders();
        auto members = Members(common::GlobalInfo::Instance()->network_id());
        if (members == nullptr || members->empty()) {
            SHARDORA_WARN("pool: %u, get leader failed, members is null or empty, sharding_id: %u", 
                pool_idx_, common::GlobalInfo::Instance()->network_id());
            return nullptr;
        }

        auto high_view_block = view_block_chain_->HighViewBlock();
        if (!high_view_block) {
            SHARDORA_WARN("pool: %u, get leader failed, high_view_block is null", pool_idx_);
            return nullptr;
        }

        // *out_view = high_view_block->qc().view() + 1;
        // return (*members)[pool_idx_ % members->size()];
        auto now_tm = common::TimeUtils::TimestampSeconds();
        if (now_tm <= common::GlobalInfo::Instance()->leader_change_init_tm()) {
            if (high_view_block->qc().elect_height() < latest_elect_height_) {
                *out_view = high_view_block->qc().view() + latest_elect_height_ + 1;
            } else {
                *out_view = high_view_block->qc().view() + 1;
            }

            // if (high_view_block->qc().tm_height() < tm_block_mgr_->LatestTimestampHeight()) {
            //     *out_view = high_view_block->qc().view() + tm_block_mgr_->LatestTimestampHeight();
            // }
            
            // *out_view += 1;
            
            if (debug)
            SHARDORA_DEBUG("pool: %u, leader_latest_qc view: %lu is equal with high view block qc view: %lu, "
                "high_view_block->qc().elect_height(): %lu, latest_elect_height_: %lu, out view: %lu, "
                "last_stable_leader_member_index_: %u, new_leader_idx: %u, leader_latest_qc.leader_idx(): %u",
                pool_idx_, leader_latest_qc.view(), high_view_block->qc().view(),
                high_view_block->qc().elect_height(),
                latest_elect_height_, *out_view,
                last_stable_leader_member_index_.load(),
                new_leader_idx,
                leader_latest_qc.leader_idx());
            StorePoolTxLeader((*members)[last_stable_leader_member_index_ % members->size()]);
            return (*members)[last_stable_leader_member_index_ % members->size()];
        }

        if (last_stable_leader_member_index_ == new_leader_idx) {
            do {
                if (leader_latest_qc.view() != high_view_block->qc().view()) {
                    if (debug)
                    SHARDORA_DEBUG("pool: %u, leader_latest_qc view: %lu is not equal with high view block qc view: %lu",
                        pool_idx_, leader_latest_qc.view(), high_view_block->qc().view());
                    break;
                }

                if (high_view_block->qc().elect_height() < latest_elect_height_) {
                    *out_view = high_view_block->qc().view() + latest_elect_height_ + 1;
                } else {
                    *out_view = high_view_block->qc().view() + 1;
                }

                // if (high_view_block->qc().tm_height() < tm_block_mgr_->LatestTimestampHeight()) {
                //     *out_view = high_view_block->qc().view() + tm_block_mgr_->LatestTimestampHeight();
                // }

                // *out_view += 1;
                if (debug)
                SHARDORA_DEBUG("pool: %u, leader_latest_qc view: %lu is equal with high view block qc view: %lu, "
                    "high_view_block->qc().elect_height(): %lu, latest_elect_height_: %lu, out view: %lu, "
                    "last_stable_leader_member_index_: %u, new_leader_idx: %u, leader_latest_qc.leader_idx(): %u",
                    pool_idx_, leader_latest_qc.view(), high_view_block->qc().view(),
                    high_view_block->qc().elect_height(),
                    latest_elect_height_, *out_view,
                    last_stable_leader_member_index_.load(),
                    new_leader_idx,
                    leader_latest_qc.leader_idx());
                StorePoolTxLeader((*members)[new_leader_idx % members->size()]);
                return (*members)[new_leader_idx % members->size()];
            } while (0);
        }

        auto high_view_block_info = view_block_chain_->Get(leader_latest_qc.view_block_hash());
        if (high_view_block_info == nullptr || high_view_block_info->view_block == nullptr) {
            if (debug)
            SHARDORA_DEBUG("pool: %u, leader_latest_qc view: %lu, view_block_hash: %s "
                "not found in view block chain", 
                pool_idx_, leader_latest_qc.view(), leader_latest_qc.view_block_hash().c_str());
            return nullptr;
        }

        high_view_block = high_view_block_info->view_block;
        if (high_view_block->qc().elect_height() < latest_elect_height_) {
            *out_view = high_view_block->qc().view() + latest_elect_height_ + 1;
        } else {
            *out_view = high_view_block->qc().view() + 1;
        }
        
        int64_t prev_qc_timestamp_sec = (high_view_block->block_info().timestamp() / 1000lu);
        int64_t now = get_consensus_timestamp(30);
        if (leader_tm_ms != 0) {
            // if (std::abs((leader_tm_ms / 1000lu) - common::TimeUtils::TimestampSeconds()) < 15) {
                now = leader_tm_ms / 1000lu;
            // }
        }

        int64_t timeout = static_cast<int64_t>(
            common::kLeaderRoatationBaseTimeoutSec * std::pow(2, std::min(consecutive_failures_, 6u)));
        int64_t elapsed = now - prev_qc_timestamp_sec;
        if (elapsed < timeout) {
            if (debug)
            SHARDORA_DEBUG("pool: %u, high_view: %lu, elapsed: %lu, timeout: %lu, consecutive_failures: %d, now: %u, block tm: %lu, "
                "last_stable_leader_member_index: %d, get leader index: %u, latest_elect_height: %lu, out view: %lu", 
                pool_idx_, high_view_block->qc().view(), elapsed, timeout, consecutive_failures_,
                now, high_view_block->block_info().timestamp(),
                last_stable_leader_member_index_.load(),
                last_stable_leader_member_index_ % members->size(),
                latest_elect_height_,
                *out_view);
            return (*members)[last_stable_leader_member_index_ % members->size()];
        }

        auto k = (elapsed / common::kLeaderRoatationBaseTimeoutSec) + 7;

        auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
        if (elect_item == nullptr) {
            // //assert(false);
            SHARDORA_WARN("pool: %u, get leader failed, elect item is null, sharding_id: %u", 
                pool_idx_, common::GlobalInfo::Instance()->network_id());
            return nullptr;
        }

        auto index = (
            last_stable_leader_member_index_ + 
            static_cast<int>(k) + 
            pool_idx_) % elect_item->valid_leaders()->size();
        auto leader_idx = elect_item->valid_leaders()->at(index)->index;
        // ++consecutive_failures_;
       
        // switch mode: force skip a view number (V + k + 1)
        // when timeout just occurred (k=1), out_view = last_qc.view + 2
        if (high_view_block->qc().elect_height() < latest_elect_height_) {
            *out_view = high_view_block->qc().view() + latest_elect_height_ + k + 1;
        } else {
            *out_view = high_view_block->qc().view() + k + 1;
        }

        if (debug)
        SHARDORA_DEBUG("pool: %u, high_view: %lu, elapsed: %lu, timeout: %lu, k: %lu, "
            "consecutive_failures: %d, now: %u, block tm: %lu, "
            "last_stable_leader_member_index: %d, get leader index: %u, "
            "latest_elect_height: %lu, out view: %lu, "
            "prev_qc_timestamp_sec: %lu, block_info timestamp: %lu, outview: %lu", 
            pool_idx_, 
            high_view_block->qc().view(), 
            elapsed, 
            timeout, 
            k, 
            consecutive_failures_,
            now, 
            high_view_block->block_info().timestamp(),
            last_stable_leader_member_index_.load(),
            leader_idx,
            latest_elect_height_,
            (high_view_block->qc().view() + latest_elect_height_ + 1),
            prev_qc_timestamp_sec,
            high_view_block->block_info().timestamp(),
            *out_view);
        StorePoolTxLeader((*members)[leader_idx % members->size()]);
        return (*members)[leader_idx % members->size()];
    }

    inline uint64_t get_consensus_timestamp(uint64_t window_size) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        uint64_t current_utc = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        uint64_t consensus_time = (current_utc / window_size) * window_size;

        return consensus_time;
    }

    inline common::BftMemberPtr GetMember(uint32_t member_index) const {
        auto members = Members(common::GlobalInfo::Instance()->network_id());
        if (member_index >= members->size()) {
            return nullptr;
        }

        return (*members)[member_index];
    }

    inline uint32_t GetEpochLeaderIndex() const {
        auto sharding_id = common::GlobalInfo::Instance()->network_id();
        //assert(elect_info_ != nullptr);
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (elect_item == nullptr) {
            // //assert(false);
            return common::kInvalidUint32;
        }

        auto index = (
            elect_item->ElectHeight() + 
            pool_idx_) % elect_item->valid_leaders()->size();
        return elect_item->valid_leaders()->at(index)->index;
    }

    inline common::BftMemberPtr LocalMember() const {
        auto sharding_id = common::GlobalInfo::Instance()->network_id();
        //assert(elect_info_ != nullptr);
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (elect_item == nullptr) {
            // //assert(false);
            return nullptr;
        }

        return elect_item->LocalMember();
    }

    inline uint32_t GetLocalMemberIdx() const {
        auto sharding_id = common::GlobalInfo::Instance()->network_id();
        //assert(elect_info_ != nullptr);
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (elect_item == nullptr) {
            // //assert(false);
            SHARDORA_DEBUG("get local member index failed, elect item is null, sharding_id: %u", sharding_id);
            return common::kInvalidUint32;
        }

        auto local_mem_ptr = elect_item->LocalMember();
        if (local_mem_ptr == nullptr) {
            // //assert(false);
            SHARDORA_DEBUG("get local member index failed, local member is null, sharding_id: %u", sharding_id);
            return common::kInvalidUint32;
        }

        return local_mem_ptr->index;
    }

    inline common::MembersPtr Members(uint32_t sharding_id) const {
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (!elect_item) {
            return std::make_shared<common::Members>();
        }
        return elect_item->Members();
    }

    void SyncLocalTxToLeader(
        const transport::MessagePtr& msg_ptr, 
        common::BftMemberPtr leader, 
        bool has_system_tx);
    void ResendLeaderLatestProposeMessage();
    common::BftMemberPtr LoadPoolTxLeader() const {
        std::lock_guard<std::mutex> lock(pool_tx_leader_mutex_);
        return pool_tx_leader_;
    }

    void StorePoolTxLeader(const common::BftMemberPtr& leader) {
        std::lock_guard<std::mutex> lock(pool_tx_leader_mutex_);
        pool_tx_leader_ = leader;
    }

    static const uint64_t kLatestPoposeSendTxToLeaderPeriodMs = 10000lu;

    std::shared_ptr<block::BlockManager> block_mgr_;
    uint32_t pool_idx_;
    std::shared_ptr<Crypto> crypto_;
    std::shared_ptr<Pacemaker> pacemaker_;
    std::shared_ptr<IBlockAcceptor> block_acceptor_;
    std::shared_ptr<IBlockWrapper> block_wrapper_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::shared_ptr<ViewBlockChain> root_view_block_chain_;
    std::unordered_map<uint32_t, std::shared_ptr<ViewBlockChain>> cross_shard_view_block_chain_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    View last_vote_view_ = 0;
    View last_leader_propose_view_ = 0;
    SyncPoolFn sync_pool_fn_ = nullptr;
    std::unordered_map<uint32_t, std::map<View, transport::MessagePtr>> voted_msgs_;
    uint64_t latest_propose_msg_tm_ms_ = 0;
    std::shared_ptr<view_block::protobuf::QcItem> latest_qc_item_ptr_;
    uint64_t propose_debug_index_ = 0;
    uint64_t recover_from_stuck_timeout_ = 0;
    bool has_user_tx_tag_ = false;
    std::shared_ptr<transport::TransportMessage> latest_leader_propose_message_;
    std::shared_ptr<sync::KeyValueSync> kv_sync_;
    consensus::HotstuffManager& hotstuff_mgr_;
    std::atomic<View> db_stored_view_ = 0llu;
    uint64_t prev_sync_latest_view_tm_ms_ = 0;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    consensus::BlockCacheCallback new_block_cache_callback_ = nullptr;
    common::Tick layter_sync_tick_;
    std::string leader_view_block_hash_;
    std::shared_ptr<ViewBlock> latest_voted_view_block_ = nullptr;

    uint32_t consecutive_failures_ = 0u;
    std::atomic<uint32_t> last_stable_leader_member_index_ = 0u;
    uint64_t latest_elect_height_ = 0llu;
    common::LRUMap<uint64_t, uint64_t> view_with_block_tm_map_{16};
    common::LRUMap<uint64_t, uint64_t> laste_vote_prev_view_tm_{16};
    common::BftMemberPtr pool_tx_leader_ = nullptr;
    mutable std::mutex pool_tx_leader_mutex_;
    std::atomic<bool> update_latest_view_tm_ = false;
    uint64_t prev_recover_check_tm_ms_ = 0;
    // Backoff for empty propose cycles: when consecutive proposes yield 0 txs,
    // delay retries with exponential backoff to avoid CPU-burning tight loops.
    uint32_t empty_propose_count_ = 0;
    uint64_t empty_propose_backoff_until_ms_ = 0;
    uint64_t prev_pool32_debug_tm_ = 0;
    static constexpr uint32_t kEmptyProposeBackoffBaseMs = 50;    // 50ms base
    static constexpr uint32_t kEmptyProposeBackoffMaxMs = 5000;   // 5s cap

// #ifndef NDEBUG
    static std::atomic<uint32_t> sendout_bft_message_count_;
    uint32_t gTestChangeViewCount = 0;
// #endif
};

} // namespace consensus

} // namespace shardora
