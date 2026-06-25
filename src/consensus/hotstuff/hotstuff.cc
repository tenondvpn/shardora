#include <bls/agg_bls.h>
#include <bls/bls_dkg.h>
#include "broadcast/broadcast_utils.h"
#include <common/encode.h>
#include <common/log.h>
#include <common/defer.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/hotstuff.h>
#include "consensus/hotstuff/hotstuff_manager.h"
#include <consensus/hotstuff/types.h>
#include <protos/hotstuff.pb.h>
#include <protos/pools.pb.h>
#include <protos/view_block.pb.h>
#include "security/ecdsa/ecdsa.h"
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

// #ifndef NDEBUG
std::atomic<uint32_t> Hotstuff::sendout_bft_message_count_ = 0;
// #endif

void Hotstuff::StartInit() {
    // set pacemaker timeout callback function
    last_vote_view_ = 0lu;
    InitLoadLatestBlock(
        view_block_chain_,
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_);
    if (root_view_block_chain_ == nullptr) {
        root_view_block_chain_ = std::make_shared<ViewBlockChain>();
        root_view_block_chain_->Init(
            kCrossRootChian,
            pool_idx_,
            db_,
            block_mgr_,
            nullptr,
            kv_sync_,
            nullptr,
            nullptr,
            new_block_cache_callback_);
    }

    InitLoadLatestBlock(
        root_view_block_chain_,
        network::kRootCongressNetworkId, 
        pool_idx_);
    for (uint32_t network_id = network::kConsensusShardBeginNetworkId;
            network_id < network::kConsensusShardEndNetworkId; ++network_id) {
        if (network_id % common::kImmutablePoolSize != pool_idx_) {
            continue;
        }

        if (network::IsSameShardOrSameWaitingPool(
                common::GlobalInfo::Instance()->network_id(), 
                network_id)) {
            continue;
        }

        SHARDORA_DEBUG("now init cross consensus shard: %u begin.", network_id);
        auto chain = std::make_shared<ViewBlockChain>();
        chain->Init(
            kCrossShardingChain,
            common::kGlobalPoolIndex,
            db_,
            block_mgr_,
            nullptr,
            kv_sync_,
            nullptr,
            nullptr,
            new_block_cache_callback_);
        cross_shard_view_block_chain_[network_id] = chain;
        if (!InitLoadLatestBlock(
                chain,
                network_id, 
                common::kImmutablePoolSize)) {
            break;
        }
        SHARDORA_DEBUG("now init cross consensus shard: %u end.", network_id);
    }

    auto high_view_block = view_block_chain_->HighViewBlock();
    if (high_view_block) {
        // Only update pacemaker's cur_view_ from HighViewBlock, do NOT update
        // latest_qc_item_ptr_ here. InitLoadLatestBlock already set
        // latest_qc_item_ptr_ to the latest COMMITTED block's QC.
        // If we overwrite it with HighViewBlock's QC (which may be an
        // uncommitted view, e.g. view 176 while committed is 175), then
        // the "locked view" check in HandleProposeMsgStep_HasVote will
        // reject the resent propose for the same view (176 >= 176 → reject).
        // The pacemaker still needs the high view to advance cur_view_.
        pacemaker_->NewQcView(high_view_block->qc().view());
        SHARDORA_DEBUG("init load pool: %d, high view block view: %lu, high view block hash: %s, "
            "pacemaker updated to view: %lu, latest_qc_item_ptr_ view: %lu",
            pool_idx_,
            high_view_block->qc().view(),
            common::Encode::HexEncode(high_view_block->qc().view_block_hash()).c_str(),
            high_view_block->qc().view(),
            latest_qc_item_ptr_ ? latest_qc_item_ptr_->view() : 0);
    }

    auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
    tmp_msg_ptr->is_leader = true;
    auto& header = tmp_msg_ptr->header;
    if (prefix_db_->GetLatestLeaderProposeMessage(
            common::GlobalInfo::Instance()->network_id(), 
            pool_idx_, 
            &header, 
            &tmp_msg_ptr->latest_qc_view)) {
        latest_leader_propose_message_ = tmp_msg_ptr;
        // Restore leader_view_block_hash_ from the saved message so that
        // ResendLeaderLatestProposeMessage sends the correct hash.
        auto& saved_vb_hash = header.hotstuff().pro_msg().view_item().qc().view_block_hash();
        if (!saved_vb_hash.empty()) {
            leader_view_block_hash_ = saved_vb_hash;
        }
        // Restore last_leader_propose_view_ so that Propose() won't construct
        // new proposes for views <= the saved view. Without this, after restart
        // the node starts proposing from view 1 upward, each new propose
        // overwriting the saved message in DB before reaching the saved view.
        auto saved_view = header.hotstuff().pro_msg().view_item().qc().view();
        last_leader_propose_view_ = saved_view;
        SHARDORA_DEBUG("init load pool: %d, set latest_leader_propose_message_ = value, view: %lu, "
            "view_block_hash: %s, last_leader_propose_view_: %lu", 
            pool_idx_, saved_view,
            common::Encode::HexEncode(saved_vb_hash).c_str(),
            last_leader_propose_view_);
    }

    SHARDORA_DEBUG("success start init network: %d, pool index: %d, root_view_block_chain_: %d", 
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        (root_view_block_chain_ != nullptr));
}

bool Hotstuff::InitLoadLatestBlock(
        std::shared_ptr<ViewBlockChain> view_block_chain, 
        uint32_t network_id, uint32_t pool_index) {
    auto latest_view_block = std::make_shared<ViewBlock>(); // Get the last ViewBlock with QC from the db
    Status s = GetLatestViewBlockFromDb(network_id, db_, pool_index, latest_view_block);
    if (s == Status::kSuccess) {
        auto balane_map_ptr = std::make_shared<BalanceAndNonceMap>();
        view_block_chain->Store(latest_view_block, false, balane_map_ptr, nullptr, true);
        auto temp_ptr = view_block_chain->Get(latest_view_block->qc().view_block_hash());
        //assert(temp_ptr);
        if (network::IsSameToLocalShard(latest_view_block->qc().network_id())) {
            //assert(!latest_view_block->qc().sign_x().empty());
            UpdateLatestQcItemPtr(std::make_shared<view_block::protobuf::QcItem>(latest_view_block->qc()));
        }

        view_block_chain->SetLatestCommittedBlock(temp_ptr);
        InitAddNewViewBlock(view_block_chain, latest_view_block);
        auto parent_hash = latest_view_block->parent_hash();
        while (!parent_hash.empty()) {
            ViewBlock view_block;
            if (!prefix_db_->GetBlock(parent_hash, &view_block)) {
                SHARDORA_ERROR("failed get parent hash: %s", 
                    common::Encode::HexEncode(parent_hash).c_str());
                break;
            }

            SHARDORA_DEBUG("success get parent hash: %s", common::Encode::HexEncode(parent_hash).c_str());
            if (view_block.qc().view() <= 0 || latest_view_block->qc().view() >= view_block.qc().view() + 2) {
                break;
            }

            parent_hash = view_block.parent_hash();
        }

        return true;
    } else {
        SHARDORA_DEBUG("no genesis, waiting for syncing, network: %lu, pool_idx: %d", network_id, pool_index);
    }

    return false;
}

void Hotstuff::InitAddNewViewBlock(
        std::shared_ptr<ViewBlockChain> view_block_chain, 
        std::shared_ptr<ViewBlock>& latest_view_block) {
    SHARDORA_DEBUG("%u_%u_%llu, now pool: %u latest vb from db, vb view: %lu",
        latest_view_block->qc().network_id(),
        latest_view_block->qc().pool_index(),
        latest_view_block->qc().view(),
        pool_idx_, 
        latest_view_block->qc().view());
    auto balane_map_ptr = std::make_shared<BalanceAndNonceMap>();
    view_block_chain->Store(latest_view_block, true, balane_map_ptr, nullptr, true);
    view_block_chain->UpdateHighViewBlock(latest_view_block->qc());
    SHARDORA_DEBUG("success new set qc view: %lu, %u_%u_%lu, hash: %s",
        latest_view_block->qc().view(),
        latest_view_block->qc().network_id(),
        latest_view_block->qc().pool_index(),
        latest_view_block->qc().view(),
        common::Encode::HexEncode(latest_view_block->qc().view_block_hash()).c_str());
    if (network::IsSameToLocalShard(latest_view_block->qc().network_id())) {
        StopVoting(latest_view_block->qc().view());
        pacemaker_->NewQcView(latest_view_block->qc().view());
    }
}

Status Hotstuff::Start() {
    StartInit();
    return Status::kSuccess;
}

Status Hotstuff::Propose(
        View leader_view,
        common::BftMemberPtr leader,
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc,
        const transport::MessagePtr& msg_ptr,
        uint64_t leader_tm_ms) {
    auto propose_begin_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("pool: %d, called propose!", pool_idx_);
#ifndef NDEBUG
    auto btime = common::TimeUtils::TimestampMs();
#endif
    auto pre_v_block = view_block_chain()->HighViewBlock();
    if (!pre_v_block) {
        SHARDORA_DEBUG("pool %u not has prev view block.", pool_idx_);
        return Status::kError;
    }

    uint64_t view_prev_vote_tm = 0;
    if (pre_v_block->qc().leader_idx() != last_stable_leader_member_index_ && laste_vote_prev_view_tm_.Get(
            pre_v_block->qc().view(), view_prev_vote_tm)) {
        auto now_tm = common::TimeUtils::TimestampMs();
        if (view_prev_vote_tm + 300lu >= now_tm) {
            SHARDORA_DEBUG("view: %lu, view_prev_vote_tm: %lu, now_tm: %lu, not timeout, "
                "pre_v_block->qc().leader_idx(): %u, last_stable_leader_member_index_: %u ", 
                pre_v_block->qc().view(), 
                view_prev_vote_tm,
                now_tm,
                pre_v_block->qc().leader_idx(),
                last_stable_leader_member_index_.load());
            return Status::kError;
        }
    }
    
    auto dht_ptr = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht_ptr) {
        SHARDORA_WARN("pool %u not has dht ptr.", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    if (readobly_dht->size() < 2) {
        SHARDORA_WARN("pool %u not has readobly_dht->size() < 2", pool_idx_);
        return Status::kError;
    }

    if (!view_block_chain_->ChainIsFull()) {
        SHARDORA_DEBUG("pool %u chain is not full, waiting for syncing.", pool_idx_);
        if (latest_leader_propose_message_) {
            SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = nullptr", pool_idx_);
            latest_leader_propose_message_ = nullptr;
        }

        return Status::kError;
    }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (latest_leader_propose_message_ &&
    //         latest_leader_propose_message_->latest_qc_view < latest_qc_item_ptr_->view()) {
    //     SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = nullptr, "
    //         "latest_leader_propose_message_->latest_qc_view: %lu, latest_qc_item_ptr_->view: %lu", 
    //         pool_idx_,
    //         latest_leader_propose_message_->latest_qc_view,
    //         latest_qc_item_ptr_->view());
    //     latest_leader_propose_message_ = nullptr;
    //     last_leader_propose_view_ = 0llu;
    // }

#ifndef NDEBUG
    auto t1 = common::TimeUtils::TimestampMs();
#endif
    if (!leader) {
        SHARDORA_DEBUG("no leader");
        return Status::kError;
    }

    ResendLeaderLatestProposeMessage();
    if (max_view() != 0 && 
            max_view() <= last_leader_propose_view_ && 
            last_leader_propose_view_ >= leader_view) {
        SHARDORA_DEBUG("pool: %d construct propose msg failed, %d, "
            "max_view(): %lu last_leader_propose_view_: %lu, leader_view: %lu",
            pool_idx_, (int32_t)Status::kError,
            max_view(), last_leader_propose_view_, leader_view);
        return Status::kError;
    }

    // After restart, latest_leader_propose_message_ may hold a propose for
    // the same or higher view that was already sent before the crash.
    // Other nodes have already voted on it, so we must NOT construct a new
    // propose for the same view (which would have different content and hash).
    // Instead, just resend the saved message and return.
    if (latest_leader_propose_message_) {
        auto saved_view = latest_leader_propose_message_->header
            .hotstuff().pro_msg().view_item().qc().view();
        if (saved_view >= leader_view) {
            SHARDORA_DEBUG("pool: %d, skip new propose — saved propose view %lu >= leader_view %lu, "
                "resending saved message instead",
                pool_idx_, saved_view, leader_view);
            last_leader_propose_view_ = saved_view;
            return Status::kSuccess;
        }
    }

#ifndef NDEBUG
    auto t2 = common::TimeUtils::TimestampMs();
#endif
    leader_view_block_hash_ = "";
    auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
    tmp_msg_ptr->is_leader = true;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = tmp_msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
    SHARDORA_DEBUG("pool: %d, leader begin construct propose msg, pre_vb: %u_%u_%lu, timeblock_height: %lu",
        pool_idx_,
        pre_v_block->qc().network_id(),
        pre_v_block->qc().pool_index(),
        pre_v_block->qc().view(),
        tm_block_mgr_->LatestTimestampHeight());
    pb_pro_msg->mutable_view_item()->mutable_block_info()->set_timestamp(leader_tm_ms);
    auto construct_begin_ms = common::TimeUtils::TimestampMs();
    Status s = ConstructProposeMsg(leader_view, leader, msg_ptr, pb_pro_msg);
    auto construct_end_ms = common::TimeUtils::TimestampMs();
    if (s != Status::kSuccess) {
        if (!tc) {
            SHARDORA_DEBUG("pool: %d construct propose msg failed, %d",
                pool_idx_, (int32_t)s);
            return s;
        }

        pb_pro_msg->release_view_item();
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
#ifndef NDEBUG
    auto t3 = common::TimeUtils::TimestampMs();
#endif
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, nullptr, hotstuff_msg);
    if (latest_qc_item_ptr_) {
        *pb_pro_msg->mutable_tc() = *latest_qc_item_ptr_;
    }

    if (!header.has_broadcast()) {
        auto brd_param = header.mutable_broadcast();
        broadcast::SetDefaultBroadcastParam(brd_param);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
#ifndef NDEBUG
    auto t4 = common::TimeUtils::TimestampMs();
#endif
    ADD_DEBUG_PROCESS_TIMESTAMP();
    dht::DhtKeyManager dht_key(tmp_msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
#ifndef NDEBUG
    std::string propose_debug_str = common::StringUtil::Format(
        "%lu, %u_%u_%lu, hash64: %lu, %lu, tx size: %u", 
        common::TimeUtils::TimestampMs(),
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        hotstuff_msg->pro_msg().view_item().qc().view(),
        header.hash64(),
        propose_debug_index_++,
        pb_pro_msg->tx_propose().txs_size());
    // propose_debug_str += ", tx gids: ";
    // security::Ecdsa ecdsa;
    // for (int32_t tx_idx = 0; tx_idx < pb_pro_msg->tx_propose().txs_size(); ++tx_idx) {
    //     if (!pb_pro_msg->tx_propose().txs(tx_idx).pubkey().empty()) {
    //         propose_debug_str += common::Encode::HexEncode(ecdsa.GetAddressWithPublicKey(pb_pro_msg->tx_propose().txs(tx_idx).pubkey())) + "_" +
    //             common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).to())  + "_" +
    //             common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).key())  + "_" +
    //             std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).step()) + "_" +
    //             std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).nonce()) + " ";
    //     } else {
    //         propose_debug_str += common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).to())  + "_" +
    //             common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).key())  + "_" +
    //             std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).step()) + "_" +
    //             std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).nonce()) + " ";
    //     }
    // }

    transport::protobuf::ConsensusDebug consensus_debug;
    consensus_debug.add_messages(propose_debug_str);
    consensus_debug.set_begin_timestamp(common::TimeUtils::TimestampMs());
    header.set_debug(consensus_debug.SerializeAsString());
    SHARDORA_DEBUG("leader begin propose_debug: %s", ProtobufToJson(consensus_debug).c_str());
    auto t5 = common::TimeUtils::TimestampMs();
#endif
    auto sign_begin_ms = common::TimeUtils::TimestampMs();
    s = crypto()->SignMessage(tmp_msg_ptr);
    auto sign_end_ms = common::TimeUtils::TimestampMs();
    if (s != Status::kSuccess) {
        SHARDORA_WARN("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
        return s;
    }
    
    latest_leader_propose_message_ = tmp_msg_ptr;
    latest_leader_propose_message_->latest_qc_view = latest_qc_item_ptr_->view();
    uint64_t tm = 0;
    if (view_with_block_tm_map_.Get(pb_pro_msg->view_item().qc().view(), tm)) {
        pb_pro_msg->mutable_view_item()->mutable_block_info()->set_timestamp(tm);
    } else {
        view_with_block_tm_map_.Put(
            pb_pro_msg->view_item().qc().view(), 
            pb_pro_msg->view_item().block_info().timestamp());
    }

    SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = value", pool_idx_);
    SHARDORA_DEBUG("set latest_leader_propose_message_, view: %lu, block tm: %lu", 
        pb_pro_msg->view_item().qc().view(), 
        pb_pro_msg->view_item().block_info().timestamp());
    

    if (hotstuff_msg->pro_msg().tx_propose().txs_size() == 0 && 
            latest_qc_item_ptr_ && latest_qc_item_ptr_->view() > 0) {
        auto latest_view_block_ptr = view_block_chain()->Get(latest_qc_item_ptr_->view_block_hash());
        if (latest_view_block_ptr && latest_view_block_ptr->view_block &&
                latest_view_block_ptr->view_block->block_info().tx_list_size() == 0) {
            SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = nullptr, "
                "latest_view_block_ptr->view_block->block_info().tx_list_size() == 0, "
                "msg view: %lu, latest view: %lu", pool_idx_, 
                pb_pro_msg->view_item().qc().view(), 
                latest_view_block_ptr->view_block->qc().view());
            latest_leader_propose_message_ = nullptr;
        }
    }

    if (latest_leader_propose_message_) {
        prefix_db_->SaveLatestLeaderProposeMessage(
            latest_leader_propose_message_->header, 
            latest_leader_propose_message_->latest_qc_view);
    }

#ifndef NDEBUG
    auto t6 = common::TimeUtils::TimestampMs();
    tmp_msg_ptr->header.set_debug(std::to_string(tmp_msg_ptr->header.hash64()));
#endif
    // TODO: test
    tmp_msg_ptr->header.set_debug(std::to_string(tmp_msg_ptr->header.hash64()));
    transport::TcpTransport::Instance()->AddLocalMessage(tmp_msg_ptr);
    // SHARDORA_DEBUG("1 success add local message: %lu", tmp_msg_ptr->header.hash64());
    {
        // Check propose message size before broadcasting.
        // If oversized, trim transactions and rebuild to stay within limit.
        int msg_size = tmp_msg_ptr->header.ByteSizeLong();
        if (msg_size > common::kMaxProposeMsgBytes) {
            int tx_count = hotstuff_msg->pro_msg().tx_propose().txs_size();
            // Estimate safe tx count: (limit / current_size) * current_tx_count * 0.9 safety margin
            int safe_count = (int)((double)common::kMaxProposeMsgBytes / msg_size * tx_count * 0.9);
            if (safe_count < 1) safe_count = 1;
            SHARDORA_WARN("pool: %d, propose msg OVERSIZED: %d bytes (limit %d), "
                "txs=%d, trimming to %d txs",
                pool_idx_, msg_size, common::kMaxProposeMsgBytes, tx_count, safe_count);
            // Remove excess transactions from the propose
            auto* tx_propose = hotstuff_msg->mutable_pro_msg()->mutable_tx_propose();
            while (tx_propose->txs_size() > safe_count) {
                tx_propose->mutable_txs()->RemoveLast();
            }
            // Also trim the view block's tx_list to match
            auto* block_info = hotstuff_msg->mutable_pro_msg()->mutable_view_item()->mutable_block_info();
            while (block_info->tx_list_size() > safe_count) {
                block_info->mutable_tx_list()->RemoveLast();
            }
        }
    }
    auto send_begin_ms = common::TimeUtils::TimestampMs();
    network::Route::Instance()->Send(tmp_msg_ptr);
    auto send_end_ms = common::TimeUtils::TimestampMs();
    if (hotstuff_msg->pro_msg().tx_propose().txs_size() > 0) {
        latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
#ifndef NDEBUG
    auto t7 = common::TimeUtils::TimestampMs();
#endif
    auto old_last_leader_propose_view_ = last_leader_propose_view_;
    last_leader_propose_view_ = std::max<uint64_t>(
        hotstuff_msg->pro_msg().view_item().qc().view(), 
        hotstuff_msg->pro_msg().tc().view());

    SHARDORA_DEBUG("new propose message hash: %lu, tx size: %u, %u_%u_%lu", 
        tmp_msg_ptr->header.hash64(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        hotstuff_msg->pro_msg().view_item().qc().view());
    ADD_DEBUG_PROCESS_TIMESTAMP();

#ifndef NDEBUG
    auto t8 = common::TimeUtils::TimestampMs();
    ++sendout_bft_message_count_;
    SHARDORA_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
        "old_last_leader_propose_view_: %lu, block tm: %lu, "
        "last_leader_propose_view_: %lu, tc view: %lu, hash: %s, "
        "qc_view: %lu, hash64: %lu, propose_debug: %s, t1: %lu, t2: %lu, "
        "t3: %u, t4: %lu, t5: %lu, t6: %lu, t7: %lu, t8: %lu, sendout_bft_message_count_: %u",
        pool_idx_,
        header.hotstuff().pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().qc().view(),
        old_last_leader_propose_view_,
        hotstuff_msg->pro_msg().view_item().block_info().timestamp(),
        last_leader_propose_view_,
        hotstuff_msg->pro_msg().tc().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        header.hash64(),
        ProtobufToJson(*hotstuff_msg).c_str(),
        (t1 - btime),
        (t2 - btime),
        (t3 - btime),
        (t4 - btime),
        (t5 - btime),
        (t6 - btime),
        (t7 - btime),
        (t8 - btime),
        sendout_bft_message_count_.fetch_add(0));
    if (tc != nullptr && IsQcTcValid(*tc)) {
        SHARDORA_DEBUG("new prev qc coming: %s, %u_%u_%lu, parent hash: %s, tx size: %u, "
            "view: %lu, max_view(): %lu, last_leader_propose_view_: %lu",
            common::Encode::HexEncode(tc->view_block_hash()).c_str(), 
            tc->network_id(), 
            tc->pool_index(), 
            pb_pro_msg->view_item().block_info().height(), 
            "", 
            pb_pro_msg->tx_propose().txs_size(),
            tc->view(),
            max_view(), 
            last_leader_propose_view_);
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("propose use time: %lu", (common::TimeUtils::TimestampMs() - btime));

#endif
    return Status::kSuccess;
}

void Hotstuff::ResendLeaderLatestProposeMessage() {
    SHARDORA_DEBUG("pool: %d, call ResendLeaderLatestProposeMessage, latest_leader_propose_message_ %s, "
        "latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() %lu, "
        "pacemaker_->CurView() %lu",
        pool_idx_,
        latest_leader_propose_message_ ? "is not null" : "is null",
        latest_leader_propose_message_ ? latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() : 0,
        pacemaker_->CurView());
    if (latest_leader_propose_message_ &&
            latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() >= 
            pacemaker_->CurView()) {
        auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
        tmp_msg_ptr->header.CopyFrom(latest_leader_propose_message_->header);
        tmp_msg_ptr->is_leader = true;
        tmp_msg_ptr->header.release_broadcast();
        auto* leader_qc = tmp_msg_ptr->header.mutable_hotstuff()->mutable_pro_msg()->mutable_view_item()->mutable_qc();
        if (!leader_view_block_hash_.empty()) {
            leader_qc->set_view_block_hash(leader_view_block_hash_);
        }

        auto broadcast = tmp_msg_ptr->header.mutable_broadcast();
        broadcast::SetDefaultBroadcastParam(broadcast);       
        auto* hotstuff_msg = tmp_msg_ptr->header.mutable_hotstuff();
        transport::TcpTransport::Instance()->SetMessageHash(tmp_msg_ptr->header);
        auto s = crypto()->SignMessage(tmp_msg_ptr);
        auto& header = tmp_msg_ptr->header;
        if (s != Status::kSuccess) {
            SHARDORA_WARN("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
                pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
            return;
        }

        transport::TcpTransport::Instance()->AddLocalMessage(tmp_msg_ptr);
        {
            // Check propose message size before sending.
            int msg_size = tmp_msg_ptr->header.ByteSizeLong();
            if (msg_size > common::kMaxProposeMsgBytes) {
                SHARDORA_WARN("pool: %d, propose msg OVERSIZED: %d bytes (limit %d), "
                    "txs=%d, view=%lu — message will be dropped by receivers",
                    pool_idx_, msg_size, common::kMaxProposeMsgBytes,
                    hotstuff_msg->pro_msg().tx_propose().txs_size(),
                    hotstuff_msg->pro_msg().view_item().qc().view());
            }
        }
        network::Route::Instance()->Send(tmp_msg_ptr);
#ifndef NDEBUG
        ++sendout_bft_message_count_;
        transport::protobuf::ConsensusDebug cons_debug;
        cons_debug.ParseFromString(header.debug());
        SHARDORA_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
            "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s, "
            "msg view: %lu, cur view: %lu, propose msg: %s, sendout_bft_message_count_: %u, "
            "latest_leader_propose_message_->latest_qc_view: %lu, latest_qc_item_ptr_->view(): %lu",
            pool_idx_,
            header.hotstuff().pool_index(),
            hotstuff_msg->pro_msg().tx_propose().txs_size(),
            hotstuff_msg->pro_msg().view_item().qc().view(),
            common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            header.hash64(),
            ProtobufToJson(cons_debug).c_str(),
            tmp_msg_ptr->header.hotstuff().pro_msg().view_item().qc().view(),
            pacemaker_->CurView(),
            ProtobufToJson(header.hotstuff().pro_msg()).c_str(),
            sendout_bft_message_count_.fetch_add(0),
            latest_leader_propose_message_->latest_qc_view,
            latest_qc_item_ptr_->view());
#endif
        latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    } else {
        SHARDORA_DEBUG("pool: %d, no need resend leader latest propose message, "
            "latest_leader_propose_message_ view: %lu, pacemaker cur view: %lu",
            pool_idx_,
            latest_leader_propose_message_ ? latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() : 0,
            pacemaker_->CurView());
    }
}


void Hotstuff::BroadcastGlobalPoolBlock(const std::shared_ptr<ViewBlock>& v_block) {
    if (!network::IsSameToLocalShard(network::kRootCongressNetworkId) &&
            v_block->qc().pool_index() != common::kGlobalPoolIndex) {
        return;
    }

    kv_sync_->AddBroadcastGlobalBlock(v_block);
}

void Hotstuff::HandleProposeMsg(const transport::MessagePtr& msg_ptr) {
    int res = HandleProposeMsgImpl(msg_ptr);
    if (res != Status::kSuccess) {
        if (res == Status::kLeaderInvalid) {
            if (msg_ptr->is_leader) {
                // SHARDORA_DEBUG("pool: %d, set latest_leader_propose_message_ = nullptr", pool_idx_);
                // latest_leader_propose_message_ = nullptr;
                // last_leader_propose_view_ = 0llu;
            }
        }

        SHARDORA_DEBUG("handle propose failed hash: %lu, propose_debug: %s", 
            msg_ptr->header.hash64(), ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    }
}

int Hotstuff::HandleProposeMsgImpl(const transport::MessagePtr& msg_ptr) {
    if (!view_block_chain()->HighViewBlock()) {
        return Status::kLeaderInvalid;
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("handle propose called hash: %lu, propose_debug: %s", 
        msg_ptr->header.hash64(), 
        ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    auto pro_msg_wrap = std::make_shared<ProposeMsgWrapper>(msg_ptr);
    if (!msg_ptr->header.hotstuff().pro_msg().has_tc()) {
        SHARDORA_DEBUG("not has tc handle propose called hash: %lu, propose_debug: %s", 
            msg_ptr->header.hash64(), 
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        //assert(false);
        return Status::kLeaderInvalid;
    }

    auto st = HandleTC(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("invalid tc handle propose called hash: %lu, propose_debug: %s", 
            msg_ptr->header.hash64(), 
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        // //assert(false);
        return Status::kLeaderInvalid;
    }

    // if (msg_ptr->header.hotstuff().pro_msg().view_item().qc().tm_height() != tm_block_mgr_->LatestTimestampHeight()) {
    //     SHARDORA_DEBUG("timestamp height not match handle propose called hash: %lu, propose_debug: %s", 
    //         msg_ptr->header.hash64(), 
    //         ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    //     return;
    // }
    
    // uint64_t view_prev_vote_tm = 0;
    // if (last_stable_leader_member_index_ != msg_ptr->header.hotstuff().pro_msg().view_item().qc().leader_idx()) {
    //     if (laste_vote_prev_view_tm_.Get(
    //             msg_ptr->header.hotstuff().pro_msg().tc().view(), view_prev_vote_tm)) {
    //         auto now_tm = common::TimeUtils::TimestampMs();
    //         if (view_prev_vote_tm + 25000lu >= now_tm) {
    //             SHARDORA_DEBUG("view: %lu, view_prev_vote_tm: %lu, now_tm: %lu, not timeout, ignore propose msg hash: %lu, propose_debug: %s", 
    //                 msg_ptr->header.hotstuff().pro_msg().tc().view(), 
    //                 view_prev_vote_tm, now_tm, msg_ptr->header.hash64(),
    //                 ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    //             return Status::kLeaderInvalid;
    //         }
    //     }
    // }

    if (!msg_ptr->header.hotstuff().pro_msg().has_view_item()) {
        SHARDORA_DEBUG("handle propose called hash: %lu, %u_%u_%lu, "
            "view block hash: %s, sign x: %s, propose_debug: %s", 
            msg_ptr->header.hash64(), 
            msg_ptr->header.hotstuff().pro_msg().tc().network_id(), 
            msg_ptr->header.hotstuff().pro_msg().tc().pool_index(),
            msg_ptr->header.hotstuff().pro_msg().tc().view(),
            common::Encode::HexEncode(
            msg_ptr->header.hotstuff().pro_msg().tc().view_block_hash()).c_str(),
            common::Encode::HexEncode(
            msg_ptr->header.hotstuff().pro_msg().tc().sign_x()).c_str(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        ADD_DEBUG_PROCESS_TIMESTAMP();
        return Status::kLeaderInvalid;
    }

    auto latest_view_block_ptr = view_block_chain()->HighViewBlock();
    if (msg_ptr->header.hotstuff().pro_msg().tx_propose().txs_size() == 0) {
        if (latest_view_block_ptr->block_info().tx_list_size() == 0 && 
                latest_view_block_ptr->qc().view() == pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view()) {
            ADD_DEBUG_PROCESS_TIMESTAMP();
            SHARDORA_DEBUG("pool: %d, high view block tx size is 0, and not timeout "
                "and propose tx size is 0, ignore.", pool_idx_);
            return Status::kLeaderInvalid;
        }
    }
    
    SHARDORA_DEBUG("handle propose called hash: %lu, propose_debug: %s", msg_ptr->header.hash64(), 
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    // //assert(msg_ptr->header.hotstuff().pro_msg().view_item().qc().view_block_hash().empty());
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(msg_ptr->header.debug());
    SHARDORA_DEBUG("handle propose called hash: %lu, %u_%u_%lu, "
        "view block hash: %s, sign x: %s, propose_debug: %s", 
        msg_ptr->header.hash64(), 
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().network_id(), 
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().pool_index(),
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().view(),
        common::Encode::HexEncode(
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().sign_x()).c_str(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (GetLocalMemberIdx() == common::kInvalidUint32) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        return Status::kLeaderInvalid;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        SHARDORA_DEBUG("pool: %d handle propose duration: %lu ms, hash64: %lu",
            pool_idx_, e-b, msg_ptr->header.hash64());
    });

    pro_msg_wrap->view_block_ptr = std::make_shared<ViewBlock>(
        msg_ptr->header.hotstuff().pro_msg().view_item());
#ifndef NDEBUG
    pro_msg_wrap->view_block_ptr->set_debug(cons_debug.SerializeAsString());
    SHARDORA_DEBUG("handle new propose message parent hash: %s, %u_%u_%lu, view hash: %s, "
        "hash64: %lu, block timestamp: %lu, propose_debug: %s",
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(), 
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        msg_ptr->header.hash64(),
        pro_msg_wrap->view_block_ptr->block_info().timestamp(),
        ProtobufToJson(cons_debug).c_str());
#endif
    //assert(pro_msg_wrap->view_block_ptr->block_info().tx_list_size() == 0);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    View out_view = 0;
    auto leader = GetLeader(
        view_item.qc().leader_idx(), 
        msg_ptr->header.hotstuff().pro_msg().tc(), 
        &out_view,
        pro_msg_wrap->view_block_ptr->block_info().timestamp(),
        true);
    if (!leader) {
        SHARDORA_DEBUG("pool: %d, propose message no leader info, leader idx: %u, tc view: %lu, "
            "propose_debug: %s",
            pool_idx_, view_item.qc().leader_idx(), 
            msg_ptr->header.hotstuff().pro_msg().tc().view(),
            "");
        return Status::kLeaderInvalid;
    }

    if (view_item.qc().view() != out_view) {
        // Fix: When the backup node is behind (out_view < propose_view), try to
        // catch up by advancing the local high_view_block_view_ using the proposal's
        // QC. This happens when the node restarts or sync is slow — the leader is
        // far ahead and the backup keeps rejecting proposals, unable to participate.
        // 
        // We only advance if the propose view is AHEAD of our local view (not behind).
        // If propose view < out_view, the proposal is stale and should be rejected.
        if (view_item.qc().view() > out_view && view_item.qc().view() > 0) {
            kv_sync_->AddSyncView(
                view_item.qc().network_id(), 
                view_item.qc().pool_index(), 
                view_item.qc().view(), 
                sync::kSyncHigh);
            // auto& propose_qc = msg_ptr->header.hotstuff().pro_msg().view_item().qc();
            // // Advance the view block chain to accept the higher view.
            // // This triggers sync for the missing block if needed.
            // view_block_chain_->UpdateHighViewBlock(propose_qc);
            // pacemaker()->NewQcView(propose_qc.view());
            // SHARDORA_DEBUG("pool: %d, catching up: advanced local view from %lu to %lu via proposal QC, "
            //     "hash: %lu",
            //     pool_idx_, out_view, view_item.qc().view(), 
            //     pro_msg_wrap->msg_ptr->header.hash64());
            // // Recompute out_view after advancing
            // View new_out_view = 0;
            // auto new_leader = GetLeader(
            //     view_item.qc().leader_idx(),
            //     msg_ptr->header.hotstuff().pro_msg().tc(),
            //     &new_out_view,
            //     pro_msg_wrap->view_block_ptr->block_info().timestamp(),
            //     true);
            // if (new_leader && view_item.qc().view() == new_out_view) {
            //     // Successfully caught up, continue processing
            //     pro_msg_wrap->leader = new_leader;
            //     out_view = new_out_view;
            //     leader = new_leader;
            //     goto view_matched;
            // }
        }

        SHARDORA_DEBUG("pool: %d, propose message view not match leader view, "
            "leader view: %lu, propose view: %lu, hash: %lu, propose_debug: %s",
            pool_idx_, out_view, view_item.qc().view(), pro_msg_wrap->msg_ptr->header.hash64(),
            "");
        return Status::kLeaderInvalid;
    }

    pro_msg_wrap->leader = leader;
#ifndef NDEBUG
    SHARDORA_DEBUG("HandleProposeMessageByStep called hash: %lu, "
        "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, view_item.qc().view(),
        ProtobufToJson(cons_debug).c_str());
#endif
    st = HandleProposeMsgStep_HasVote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        // HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
        ADD_DEBUG_PROCESS_TIMESTAMP();
        return Status::kLeaderInvalid;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto propose_view = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view();
    View handled_view = 0;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMessageByStep(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_ERROR("handle propose message failed: status=%d, hash=%lu, view=%lu, height=%lu, "
            "pool_idx=%u, txs_count=%d, propose_debug=%s",
            (int)st,
            msg_ptr->header.hash64(),
            msg_ptr->header.hotstuff().pro_msg().view_item().qc().view(),
            msg_ptr->header.hotstuff().pro_msg().view_item().block_info().height(),
            msg_ptr->header.hotstuff().pool_index(),
            msg_ptr->header.hotstuff().pro_msg().tx_propose().txs_size(),
            ProtobufToJson(msg_ptr->header).c_str());
    }

    SHARDORA_DEBUG("handle propose message success hash: %lu, propose_debug: %s",
        msg_ptr->header.hash64(),
        msg_ptr->header.debug().c_str());
    if (msg_ptr->header.hotstuff().pro_msg().tx_propose().txs_size() > 0) {
        latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    }

    leader_view_block_hash_ = "";
    if (msg_ptr->is_leader) {
        leader_view_block_hash_ = pro_msg_wrap->view_block_ptr->qc().view_block_hash();
        // Re-save the propose message with the actual view_block_hash.
        // When the propose was first saved (in Propose()), view_block_hash
        // was empty because tx execution hadn't happened yet. Now that we've
        // executed txs and generated the hash, update the persisted message
        // so that after a restart, ResendLeaderLatestProposeMessage sends
        // the correct view_block_hash that matches other nodes' votes.
        if (latest_leader_propose_message_ && !leader_view_block_hash_.empty()) {
            auto* saved_qc = latest_leader_propose_message_->header
                .mutable_hotstuff()->mutable_pro_msg()
                ->mutable_view_item()->mutable_qc();
            saved_qc->set_view_block_hash(leader_view_block_hash_);
            prefix_db_->SaveLatestLeaderProposeMessage(
                latest_leader_propose_message_->header,
                latest_leader_propose_message_->latest_qc_view);
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return st;
}

Status Hotstuff::HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> pro_msg_wrap) {
    auto msg_ptr = pro_msg_wrap->msg_ptr;
    auto step_begin_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto st = HandleProposeMsgStep_VerifyLeader(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("HandleProposeMsgStep_VerifyLeader failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        return Status::kLeaderInvalid;
    }

    auto verify_leader_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_VerifyViewBlock(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("HandleProposeMsgStep_VerifyViewBlock failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        return Status::kLeaderInvalid;
    }

    auto verify_vb_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_TxAccept(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("HandleProposeMsgStep_TxAccept failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        return Status::kLeaderInvalid;
    }

    auto tx_accept_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_ChainStore(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("HandleProposeMsgStep_ChainStore failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        return Status::kLeaderInvalid;
    }

    auto chain_store_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_Vote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        SHARDORA_DEBUG("HandleProposeMsgStep_Vote failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
        return Status::kLeaderInvalid;
    }

    auto vote_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("HandleProposeMessageByStep success hash: %lu, propose_debug: %s",
        msg_ptr->header.hash64(),
        ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto& view_item = *pro_msg_wrap->view_block_ptr;
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    SHARDORA_DEBUG("HandleProposeMsgStep_HasVote called hash: %lu, "
        "last_vote_view_: %lu, last qc view: %lu, "
        "view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, 
        latest_qc_item_ptr_->view(), view_item.qc().view(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (latest_qc_item_ptr_->view() >= view_item.qc().view()) {
        // locked view
        return Status::kError;
    }

    if (last_vote_view_ >= view_item.qc().view()) {
        SHARDORA_DEBUG("pool: %d has voted view: %lu, last_locked_view_: %u, "
            "last_vote_view_: %lu, hash64: %lu, pacemaker()->CurView(): %lu",
            pool_idx_, view_item.qc().view(),
            latest_qc_item_ptr_->view(), last_vote_view_,
            pro_msg_wrap->msg_ptr->header.hash64(),
            pacemaker()->CurView());
        if (last_vote_view_ == view_item.qc().view()) {
            auto leader_iter = voted_msgs_.find(view_item.qc().leader_idx());
            if (leader_iter == voted_msgs_.end()) {
                SHARDORA_DEBUG("not find leader: %d, pool: %d has voted view: %lu, last_locked_view_: %u, "
                    "last_vote_view_: %lu, hash64: %lu, pacemaker()->CurView(): %lu",
                    view_item.qc().leader_idx(),
                    pool_idx_, view_item.qc().view(),
                    latest_qc_item_ptr_->view(), last_vote_view_,
                    pro_msg_wrap->msg_ptr->header.hash64(),
                    pacemaker()->CurView());
                return Status::kSuccess;
            }

            auto iter = leader_iter->second.find(view_item.qc().view());
            if (iter != leader_iter->second.end()) {
#ifndef NDEBUG
                auto block_hash = GetBlockHash(*latest_voted_view_block_);
                SHARDORA_DEBUG("pool: %d has voted: %lu, last_vote_view_: %u, "
                    "hash64: %lu and resend vote: hash: %s, local block hash: %s",
                    pool_idx_, view_item.qc().view(),
                    last_vote_view_, pro_msg_wrap->msg_ptr->header.hash64(),
                    common::Encode::HexEncode(iter->second->header.hotstuff().vote_msg().view_block_hash()).c_str(),
                    common::Encode::HexEncode(block_hash).c_str());
#endif
                auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
                tmp_msg_ptr->header.CopyFrom(iter->second->header);
                auto leader = pro_msg_wrap->leader;
                if (!leader || SendMsgToLeader(leader, tmp_msg_ptr, VOTE) != Status::kSuccess) {
                    SHARDORA_ERROR("pool: %d, Send vote message is error.",
                        pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
                }
            } else {
                SHARDORA_DEBUG("not find view leader: %d, pool: %d has voted view: %lu, last_locked_view_: %u, "
                    "last_vote_view_: %lu, hash64: %lu, pacemaker()->CurView(): %lu",
                    view_item.qc().leader_idx(),
                    pool_idx_, view_item.qc().view(),
                    latest_qc_item_ptr_->view(), last_vote_view_,
                    pro_msg_wrap->msg_ptr->header.hash64(),
                    pacemaker()->CurView());
            }
        }

        return Status::kError;
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    SHARDORA_DEBUG("HandleProposeMsgStep_VerifyLeader called hash: %lu, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    auto local_idx = GetLocalMemberIdx();
    if (VerifyLeader(pro_msg_wrap) != Status::kSuccess) {
        if (sync_pool_fn_) {
            sync_pool_fn_(pool_idx_, 1);
        }

        SHARDORA_ERROR("verify leader failed, pool: %d view: %lu, hash64: %lu", 
            pool_idx_, view_item.qc().view(), pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }        

    return Status::kSuccess;
}

Status Hotstuff::HandleTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 3 Verify TC
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    SHARDORA_DEBUG("HandleTC called hash: %lu, propose_debug: %s, pro_msg.tc().has_view_block_hash(): %d", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        ProtobufToJson(pro_msg).c_str(),
        pro_msg.tc().has_view_block_hash());
#endif
    if (pro_msg.has_tc() && pro_msg.tc().has_view_block_hash()) {
        if (pro_msg.tc().view() < latest_qc_item_ptr_->view()) {
            SHARDORA_WARN("pool: %d verify tc old view: %lu, latest qc view: %lu, hash: %lu, propose_debug: %s",
                pool_idx_, pro_msg.tc().view(), latest_qc_item_ptr_->view(), pro_msg_wrap->msg_ptr->header.hash64(),
                ProtobufToJson(pro_msg).c_str());
            return Status::kError;
        }

        auto btime = common::TimeUtils::TimestampMs();
        if (VerifyQC(pro_msg.tc()) != Status::kSuccess) {
            SHARDORA_ERROR("pool: %d verify tc failed: %lu", pool_idx_, pro_msg.tc().view());
            // //assert(false);
            return Status::kError;
        }
        auto verify_qc_end_ms = common::TimeUtils::TimestampMs();

        auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(pro_msg.tc());
        pacemaker()->NewTc(tc_ptr);
        auto& qc = pro_msg.tc();
        pacemaker()->NewQcView(qc.view());
        view_block_chain()->UpdateHighViewBlock(qc);
        auto msg_ptr = pro_msg_wrap->msg_ptr;
        ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(view_block_chain(), pro_msg_wrap->msg_ptr, qc);
        if (latest_qc_item_ptr_ == nullptr ||
                tc_ptr->view() >= latest_qc_item_ptr_->view()) {
            //assert(IsQcTcValid(*tc_ptr));
            UpdateLatestQcItemPtr(tc_ptr);
        }

        SHARDORA_DEBUG("commit use time: %lu, verify_qc_use_ms: %lu, src hash64: %lu, "
            "leader propose hash64: %lu, propose_debug: %s",
            (common::TimeUtils::TimestampMs() - btime), 
            (verify_qc_end_ms - btime), 
            pro_msg_wrap->msg_ptr->header.hash64(),
            latest_leader_propose_message_ ? latest_leader_propose_message_->header.hash64() : 0,
            pro_msg_wrap->msg_ptr->header.debug().c_str());
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    SHARDORA_DEBUG("HandleProposeMsgStep_VerifyViewBlock called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto* tc = &pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc();
    if (VerifyViewBlock(
            *pro_msg_wrap->view_block_ptr,
            view_block_chain(),
            tc,
            pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().elect_height()) != Status::kSuccess) {
        SHARDORA_DEBUG("pool: %d, Verify ViewBlock is error. hash: %s, hash64: %lu, pool now: %s", pool_idx_,
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            view_block_chain_->String().c_str());
        return Status::kError;
    }
    
#ifndef NDEBUG
    SHARDORA_DEBUG("====1.1 pool: %d, verify view block success, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        pro_msg_wrap->view_block_ptr->qc().view(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());        
#endif
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_Directly(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        const std::string& expect_view_block_hash) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    SHARDORA_DEBUG("HandleProposeMsgStep_Directly called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), 
        ProtobufToJson(cons_debug).c_str());
#endif
    // Verify ViewBlock.block and tx_propose, verify tx_propose, fill in Block tx related fields
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->view_block_ptr->mutable_block_info()->clear_tx_list();
    auto balance_map_ptr = std::make_shared<BalanceAndNonceMap>();
    auto& balance_map = *balance_map_ptr;
    auto shardora_host_ptr = std::make_shared<shardoravm::ShardorahainHost>();
    auto btime = common::TimeUtils::TimestampMs();
    shardoravm::ShardorahainHost& shardora_host = *shardora_host_ptr;
    if (acceptor()->Accept(
            pro_msg_wrap, 
            true, 
            true, 
            balance_map,
            shardora_host) != Status::kSuccess) {
        SHARDORA_DEBUG("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }

    auto etime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("====1.1.2 success Accept pool: %d, verify view block, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, use time: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            (etime - btime));
    SHARDORA_DEBUG("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() || 
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()));
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }
    
    // 6 add view block
#ifndef NDEBUG
    SHARDORA_DEBUG("store v block pool: %u, hash: %s, prehash: %s, %u_%u_%lu, propose_debug: %s",
        pool_idx_,
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(),
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(), ProtobufToJson(cons_debug).c_str());
#endif
    if (expect_view_block_hash != pro_msg_wrap->view_block_ptr->qc().view_block_hash()) {
        SHARDORA_DEBUG("invalid parent hash: %s, %s",
            common::Encode::HexEncode(expect_view_block_hash).c_str(),
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str());
        return Status::kNotExpectHash;
    }

    Status s = view_block_chain()->Store(pro_msg_wrap->view_block_ptr, true, balance_map_ptr, shardora_host_ptr, false);
    SHARDORA_DEBUG("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        (int32_t)s,
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size());
    if (s != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu",
            pool_idx_, 
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->view_block_ptr->qc().network_id(),
            pro_msg_wrap->view_block_ptr->qc().pool_index(),
            pro_msg_wrap->view_block_ptr->qc().view());
        // If the parent block does not exist, add it to the waiting queue for subsequent processing
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { // The lack of a parent block triggers synchronization
            sync_pool_fn_(pool_idx_, 1);
        }

        return Status::kError;
    }

    return Status::kSuccess;    
}

Status Hotstuff::HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    SHARDORA_DEBUG("HandleProposeMsgStep_TxAccept called hash: %lu, view hash: %s, "
        "propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        ProtobufToJson(cons_debug).c_str());
#endif
    // Verify ViewBlock.block and tx_propose, verify tx_propose, fill in Block tx related fields
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->acc_balance_and_nonce_map_ptr = std::make_shared<BalanceAndNonceMap>();
    auto& balance_and_nonce_map = *pro_msg_wrap->acc_balance_and_nonce_map_ptr;
    pro_msg_wrap->shardora_host_ptr = std::make_shared<shardoravm::ShardorahainHost>();
    auto btime = common::TimeUtils::TimestampMs();
    shardoravm::ShardorahainHost& shardora_host = *pro_msg_wrap->shardora_host_ptr;
    pro_msg_wrap->leader_nonce_map = std::make_shared<std::unordered_map<std::string, uint64_t>>();
    Status s = acceptor()->Accept(
        pro_msg_wrap, 
        true, 
        false, 
        balance_and_nonce_map,
        shardora_host,
        pro_msg_wrap->leader_nonce_map.get());
    if (s != Status::kSuccess) {
#ifndef NDEBUG
        SHARDORA_DEBUG("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s, status: %d",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            ProtobufToJson(cons_debug).c_str(),
            (int32_t)s);
#endif
        return Status::kError;
    }

#ifndef NDEBUG
    auto etime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("====1.1.2 success Accept pool: %d, verify view block, "
        "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, "
        "propose_debug: %s, size: %u, use time: %lu",
        pool_idx_,
        proto_msg.view_item().qc().view(),
        common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        pro_msg_wrap->msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tx_propose().txs_size(),
        (etime - btime));
#endif
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    SHARDORA_DEBUG("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() || 
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()), 
        ProtobufToJson(cons_debug).c_str());
#endif
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }
    
    // 6 add view block
    Status s = view_block_chain()->Store(
        pro_msg_wrap->view_block_ptr, 
        false, 
        pro_msg_wrap->acc_balance_and_nonce_map_ptr,
        pro_msg_wrap->shardora_host_ptr,
        false);
#ifndef NDEBUG
    SHARDORA_DEBUG("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u, propose_debug: %s",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        (int32_t)s,
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (s != Status::kSuccess) {
#ifndef NDEBUG
        SHARDORA_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu, propose_debug: %s",
            pool_idx_, 
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->view_block_ptr->qc().network_id(),
            pro_msg_wrap->view_block_ptr->qc().pool_index(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            ProtobufToJson(cons_debug).c_str());
#endif
        // If the parent block does not exist, add it to the waiting queue for subsequent processing. The lack of a parent block triggers synchronization.
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { 
            sync_pool_fn_(pool_idx_, 1);
        }

        return Status::kError;
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    // NOTICE: When the pipeline is retried, the protobuf structure is destructed, so pro_msg_wrap->header.hash64() is 0
    SHARDORA_DEBUG("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, "
        "curView: %lu, vblock: %lu, txs: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        view_block_chain()->HighViewBlock()->qc().view(),
        pacemaker()->HighTC()->view(),
        view_block_chain()->Size(),
        pacemaker()->CurView(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        pro_msg_wrap->msg_ptr->header.hash64(), 
        ProtobufToJson(cons_debug).c_str());
#endif
    auto msg_ptr = pro_msg_wrap->msg_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();

    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& trans_header = trans_msg->header;
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto* hotstuff_msg = trans_header.mutable_hotstuff();
    auto* vote_msg = hotstuff_msg->mutable_vote_msg();
    //assert(pro_msg_wrap->view_block_ptr->qc().elect_height() > 0);
    trans_header.set_debug(pro_msg_wrap->msg_ptr->header.debug());
    // Construct VoteMsg
    Status s = ConstructVoteMsg(
        msg_ptr,
        vote_msg, 
        pro_msg_wrap->view_block_ptr->qc().elect_height(), 
        pro_msg_wrap->view_block_ptr->qc().tm_height(), 
        pro_msg_wrap->view_block_ptr,
        pro_msg_wrap->leader_nonce_map.get());
    if (s != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d, ConstructVoteMsg error %d, hash64: %lu",
            pool_idx_, (int32_t)s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    // Construct HotstuffMessage and send
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (SendMsgToLeader(pro_msg_wrap->leader, trans_msg, VOTE) != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d, Send vote message is error.",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
    }

    latest_voted_view_block_ = pro_msg_wrap->view_block_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!pro_msg_wrap->msg_ptr->is_leader) {
        // Avoid repeated voting on the view
        uint32_t leader_idx = pro_msg_wrap->view_block_ptr->qc().leader_idx();
        View current_view = pro_msg_wrap->view_block_ptr->qc().view();
        voted_msgs_[leader_idx][current_view] = trans_msg;
        auto iter = voted_msgs_[leader_idx].begin();
        auto riter = voted_msgs_[leader_idx].rbegin();
        if (iter->first + 16 < riter->first) {
            voted_msgs_[leader_idx].erase(iter);
        }
        
        SHARDORA_DEBUG("pool: %d, Send vote message is success., hash64: %lu, "
            "last_vote_view_: %lu, send to leader tx size: %u, last_vote_view_: %lu",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            vote_msg->txs_size(),
            last_vote_view_);
        StopVoting(pro_msg_wrap->view_block_ptr->qc().view());
    }
    
    laste_vote_prev_view_tm_.Put(msg_ptr->header.hotstuff().pro_msg().tc().view(), now_tm_ms);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    has_user_tx_tag_ = false;
    return Status::kSuccess;
}

Status Hotstuff::VerifyFollower(const transport::MessagePtr& msg_ptr) {
#ifdef USE_SERVER_TEST_TRANSACTION
    return Status::kSuccess;
#endif
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    auto member = GetMember(vote_msg.replica_idx());
    if (!member) {
        return Status::kError;
    }

    if (member->backup_ecdh_key.empty()) {
        if (crypto_->security()->GetEcdhKey(
                member->pubkey,
                &member->backup_ecdh_key) != security::kSecuritySuccess) {
            SHARDORA_DEBUG("verify follower get ecdh key failed: %s", 
                common::Encode::HexEncode(member->id).c_str());
            return Status::kError;
        }
    }
    
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        msg_ptr->header);
    std::string decrypt_msg;
    security::RawPrivateKey ecdh_key = std::make_pair(
        member->backup_ecdh_key.c_str(), 
        member->backup_ecdh_key.size());
    if (crypto_->security()->Decrypt(
            msg_ptr->header.ecdh_encrypt(), 
            ecdh_key, 
            &decrypt_msg) != security::kSecuritySuccess) {
        SHARDORA_DEBUG("verify follower encrypt failed: %s", 
            common::Encode::HexEncode(member->id).c_str());
        return Status::kError;
    }

    if (memcmp(decrypt_msg.c_str(), msg_hash.c_str(), msg_hash.size()) != 0) {
        SHARDORA_DEBUG("verify follower encrypt failed: %s", 
            common::Encode::HexEncode(member->id).c_str());
        return Status::kError;
    }

    return Status::kSuccess;
}

void Hotstuff::HandleVoteMsg(const transport::MessagePtr& msg_ptr) {
    if (VerifyFollower(msg_ptr) != Status::kSuccess) {
        return;
    }
    
    auto res = HandleVoteMsgImpl(msg_ptr);
    // if (res != Status::kSuccess) {
    //     auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    //     if (vote_msg.leader_idx() == GetLocalMemberIdx()) {
    //     }
    // }
}

Status Hotstuff::HandleVoteMsgImpl(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto b = common::TimeUtils::TimestampMs();
   
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    // acceptor()->AddTxs(msg_ptr, vote_msg.txs());
    if (vote_msg.txs_size() > 0) {
        hotstuff_mgr_.ConsensusAddTxsMessage(msg_ptr);
        SHARDORA_DEBUG("tps vote from follower tx size: %u", vote_msg.txs_size());
    }

    if (prefix_db_->BlockExists(vote_msg.view_block_hash())) {
        return Status::kError;
    }

    std::string followers_gids;
    auto view_block_info_ptr = view_block_chain_->Get(vote_msg.view_block_hash());
    if (!view_block_info_ptr) {
        SHARDORA_DEBUG("follower view block hash not equal to leader pool: %d, onVote, hash: %s, view: %lu, "
            "local high view: %lu, replica: %lu, hash64: %lu, propose_debug: %s, followers_gids: %s",
            pool_idx_,
            common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
            vote_msg.view(),
            view_block_chain()->HighViewBlock()->qc().view(),
            vote_msg.replica_idx(),
            msg_ptr->header.hash64(),
            "",
            followers_gids.c_str());
        return Status::kError;
    }

// #ifndef NDEBUG
//     for (uint32_t i = 0; i < vote_msg.txs_size(); ++i) {
//         followers_gids += common::Encode::HexEncode(vote_msg.txs(i).gid()) + " ";
//     }
// #endif
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(msg_ptr->header.debug());
    // cons_debug.add_timestamps(
    //     b - cons_debug.timestamps(0));
    SHARDORA_DEBUG("====2.0 pool: %d, onVote, hash: %s, view: %lu, "
        "local high view: %lu, replica: %lu, hash64: %lu, propose_debug: %s, followers_gids: %s",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        view_block_chain()->HighViewBlock()->qc().view(),
        vote_msg.replica_idx(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        followers_gids.c_str());
#endif
    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        SHARDORA_DEBUG("vote message is error: hash64: %lu", msg_ptr->header.hash64());
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("%u_%u_%lu, ====2.1 pool: %d, onVote, hash: %s, "
        "src debug: %s, hash64: %lu, replica: %d",
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        vote_msg.view(),
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        msg_ptr->header.debug().c_str(),
        msg_ptr->header.hash64(),
        vote_msg.replica_idx());

    // Sync replica's txs
    // Generate aggregate signature, create qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    auto tm_height = vote_msg.tm_height();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    auto qc_item_ptr = std::make_shared<QC>();
    QC& qc_item = *qc_item_ptr;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(vote_msg.view());
    qc_item.set_view_block_hash(vote_msg.view_block_hash());
    //assert(!prefix_db_->BlockExists(qc_item.view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(vote_msg.leader_idx());
    qc_item.set_tm_height(tm_height);
    auto qc_hash = GetQCMsgHash(qc_item);
    if (latest_leader_propose_message_)
    SHARDORA_INFO("success set view block hash: %s, qc_hash: %s, "
        "sign x: %s, replica: %d, elect_height: %lu, %u_%u_%lu, "
        "vote_msg.leader_idx: %d, use time: %lu, hash64: %lu",
        common::Encode::HexEncode(qc_item.view_block_hash()).c_str(),
        common::Encode::HexEncode(qc_hash).c_str(),
        vote_msg.sign_x().c_str(),
        replica_idx,
        elect_height,
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view(),
        vote_msg.leader_idx(),
        (common::TimeUtils::TimestampMs() - view_block_info_ptr->b_tm_ms),
        latest_leader_propose_message_->header.hash64());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (latest_leader_propose_message_ && 
            latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().leader_idx() != vote_msg.leader_idx()) {
        // //assert(false);
        return Status::kError;
    }

    auto bls_begin_ms = common::TimeUtils::TimestampMs();
    Status ret = crypto()->ReconstructAndVerifyThresSign(
        msg_ptr,
        elect_height,
        vote_msg.view(),
        qc_hash,
        replica_idx, 
        vote_msg.sign_x(),
        vote_msg.sign_y(),
        reconstructed_sign);
    if (ret == Status::kInvalidOpposedCount) {
        SHARDORA_WARN("invalid opposed count: %u_%u_%lu", qc_item.network_id(), qc_item.pool_index(), qc_item.view());
    }

    SHARDORA_DEBUG("ReconstructAndVerifyThresSign success set view block hash: %s, qc_hash: %s, %u_%u_%lu, ret: %d",
        common::Encode::HexEncode(qc_item.view_block_hash()).c_str(),
        common::Encode::HexEncode(qc_hash).c_str(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view(),
        (int32_t)ret);
    auto bls_end_ms = common::TimeUtils::TimestampMs();
    if (ret == Status::kSuccess) {
    }
    // //assert(ret != Status::kInvalidOpposedCount); It may occur temporarily due to inconsistent status
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            SHARDORA_DEBUG("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return Status::kSuccess;
        }

        SHARDORA_DEBUG("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
            pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
        return Status::kError;
    }

#ifndef NDEBUG
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, ",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        reconstructed_sign == nullptr,
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        vote_msg.replica_idx());
#endif
    qc_item.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->X));
    qc_item.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->Y));
    // switch view
    SHARDORA_DEBUG("success new set qc view: %lu, %u_%u_%lu",
        qc_item.view(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());

    // store to ck
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (ck_client_) {
    //     auto elect_item = elect_info()->GetElectItemWithShardingId(
    //             common::GlobalInfo::Instance()->network_id());
    //     if (elect_item) {
    //         ck::BlsBlockInfo info;
    //         info.elect_height = elect_height;
    //         info.view = vote_msg.view();
    //         info.shard_id = common::GlobalInfo::Instance()->network_id();
    //         info.pool_idx = pool_idx_;
    //         info.leader_idx = elect_item->LocalMember()->index;
    //         info.msg_hash = common::Encode::HexEncode(qc_hash);
    //         info.partial_sign_map = crypto()->serializedPartialSigns(elect_height, qc_hash);
    //         info.reconstructed_sign = crypto()->serializedSign(*reconstructed_sign);
    //         info.common_pk = bls::BlsDkg::serializeCommonPk(elect_item->common_pk());
    //         ck_client_->InsertBlsBlockInfo(info);
    //     }        
    // }    
    
    ADD_DEBUG_PROCESS_TIMESTAMP();

    view_block_chain()->UpdateHighViewBlock(qc_item);
    BroadcastGlobalPoolBlock(view_block_info_ptr->view_block);
    pacemaker()->NewQcView(qc_item.view());
    SHARDORA_INFO("NewView propose newview called %u_%u_%lu, tc_view: %lu, "
        "propose_debug: %s, use time: %lu, latest_leader_propose_message_ = nullptr, "
        "hash64: %lu, tx size: %lu",
        qc_item.network_id(),
        pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), 
        pacemaker()->HighTC()->view(),
        msg_ptr->header.debug().c_str(),
        (common::TimeUtils::TimestampMs() - view_block_info_ptr->b_tm_ms),
        latest_leader_propose_message_->header.hash64(),
        latest_leader_propose_message_->header.hotstuff().pro_msg().tx_propose().txs_size());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    latest_leader_propose_message_ = nullptr;
    last_leader_propose_view_ = 0llu;
    latest_propose_msg_tm_ms_ = 0;
    UpdateLatestQcItemPtr(qc_item_ptr);
    auto leader = LocalMember();
    auto leader_tm = GetLeaderBlockTimestamp();
    if (leader) {
        Propose(qc_item_ptr->view() + 1, leader, qc_item_ptr, nullptr, msg_ptr, leader_tm);
    } else {
        SHARDORA_DEBUG("pool index: %d, no leader", pool_idx_);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // prev_recover_check_tm_ms_ = 0;
    return Status::kSuccess;
}

void Hotstuff::HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& pre_rst_timer_msg = msg_ptr->header.hotstuff().pre_reset_timer_msg();
    if (pre_rst_timer_msg.txs_size() == 0 && !pre_rst_timer_msg.has_single_tx()) {
        SHARDORA_DEBUG("pool: %d has proposed!", pool_idx_);
        return;
    }

#ifndef NDEBUG
    std::string gids;
    for (int32_t i = 0; i < pre_rst_timer_msg.txs_size(); ++i) {
        gids += std::to_string(pre_rst_timer_msg.txs(i).nonce()) + " ";
    }

    SHARDORA_WARN("pool: %u, reset timer get follower tx gids: %s", pool_idx_, gids.c_str());
#endif

    if (pre_rst_timer_msg.txs_size() > 0) {
        SHARDORA_DEBUG("tps reset from follower tx size: %u", pre_rst_timer_msg.txs_size());
        hotstuff_mgr_.ConsensusAddTxsMessage(msg_ptr);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (latest_qc_item_ptr_ != nullptr) {
        SHARDORA_DEBUG("reset timer propose message called view: %lu",
            latest_qc_item_ptr_->view());
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        SHARDORA_DEBUG("reset timer failed, now_tm_ms < latest_propose_msg_tm_ms_ + "
            "kLatestPoposeSendTxToLeaderPeriodMs: %lu",
            (latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs - now_tm_ms));
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    View out_view = 0;
    auto local_idx = GetLocalMemberIdx();
    auto leader_block_tm = GetLeaderBlockTimestamp();
    auto leader = GetLeader(local_idx, *latest_qc_item_ptr_, &out_view, leader_block_tm, false);
    if (!leader) {
        SHARDORA_DEBUG("pool index: %d, no leader", pool_idx_);
        return;
    }

    if (last_vote_view_ < out_view) {
        Propose(out_view, leader, nullptr, nullptr, msg_ptr, leader_block_tm);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("reset timer success!");
}

Status Hotstuff::TryCommit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const transport::MessagePtr& msg_ptr, 
        const QC& commit_qc) {
    //assert(commit_qc.has_view_block_hash());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto v_block_to_commit_info = CheckCommit(view_block_chain, commit_qc);
    if (v_block_to_commit_info) {
        auto v_block_to_commit = v_block_to_commit_info->view_block;
// #ifndef NDEBUG
//         transport::protobuf::ConsensusDebug cons_debug;
//         cons_debug.ParseFromString(v_block_to_commit->debug());
//         SHARDORA_DEBUG("commit tx size: %u, propose_debug: %s", 
//             v_block_to_commit->block_info().tx_list_size(), 
//             ProtobufToJson(cons_debug).c_str());
// #endif
        ADD_DEBUG_PROCESS_TIMESTAMP();
        auto commit_begin_ms = common::TimeUtils::TimestampMs();
        Status s = Commit(view_block_chain, msg_ptr, v_block_to_commit_info, commit_qc);
        auto commit_end_ms = common::TimeUtils::TimestampMs();
        if (s != Status::kSuccess) {
            SHARDORA_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->qc().view(),
                common::Encode::HexEncode(v_block_to_commit->qc().view_block_hash()).c_str());
            return s;
        }
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    return Status::kSuccess;
}

std::shared_ptr<ViewBlockInfo> Hotstuff::CheckCommit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const QC& qc) {
    return view_block_chain->CheckCommit(qc);
}

Status Hotstuff::Commit(
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlockInfo>& v_block_info,
        const QC& commit_qc) {
    view_block_chain->Commit(v_block_info);
    return Status::kSuccess;
}

void Hotstuff::HandleSyncedViewBlock(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock) {
    if (BlockHeightCommited(
            prefix_db_, 
            vblock->qc().network_id(), 
            vblock->qc().pool_index(), 
            vblock->block_info().height())) {
        SHARDORA_DEBUG("block height committed, no need to store block db, %u_%u_%lu, height: %lu",
            vblock->qc().network_id(), 
            vblock->qc().pool_index(), 
            vblock->qc().view(), 
            vblock->block_info().height());
        return;
    }
    
    if (prefix_db_->BlockExists(vblock->qc().view_block_hash())) {
        SHARDORA_DEBUG("block db exists %u_%u_%lu, height: %lu",
            vblock->qc().network_id(), 
            vblock->qc().pool_index(), 
            vblock->qc().view(), 
            vblock->block_info().height());
        return;
    }
    
    SHARDORA_DEBUG("now handle synced view block %u_%u_%lu, height: %lu",
        vblock->qc().network_id(),
        vblock->qc().pool_index(),
        vblock->qc().view(),
        vblock->block_info().height());
    transport::MessagePtr msg_ptr;
    if (network::IsSameToLocalShard(vblock->qc().network_id())) {
        if (!view_block_chain()->ReplaceWithSyncedBlock(vblock)) {
            SHARDORA_DEBUG("block hash exists %u_%u_%lu, height: %lu",
                vblock->qc().network_id(), 
                vblock->qc().pool_index(), 
                vblock->qc().view(), 
                vblock->block_info().height());
            // return;
        }
        
        auto elect_item = elect_info()->GetElectItem(
                vblock->qc().network_id(),
                vblock->qc().elect_height());
        if (elect_item && elect_item->IsValid()) {
            elect_item->consensus_stat(pool_idx_)->Commit(vblock);
        }

        pacemaker_->NewQcView(vblock->qc().view());
        view_block_chain()->Store(vblock, true, nullptr, nullptr, false);
        view_block_chain()->UpdateHighViewBlock(vblock->qc());
        if (latest_qc_item_ptr_ == nullptr ||
                vblock->qc().view() >= latest_qc_item_ptr_->view()) {
            if (IsQcTcValid(vblock->qc())) {
                UpdateLatestQcItemPtr(std::make_shared<view_block::protobuf::QcItem>(vblock->qc()));
            }
        }
    ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(view_block_chain(), msg_ptr, *latest_qc_item_ptr_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(view_block_chain(), msg_ptr, vblock->qc());
        if (vblock->block_info().tx_list_size() > 0) {
            SyncLaterBlocks(
                view_block_chain(), 
                vblock->qc().network_id(), 
                vblock->qc().pool_index(), 
                vblock->qc().view());
        }
    } else if (network::IsSameShardOrSameWaitingPool(
            vblock->qc().network_id(), network::kRootCongressNetworkId)) {
        if (vblock->qc().pool_index() != pool_idx_) {
            SHARDORA_ERROR("invalid shard id: %u, pool_idx: %u, src pool: %d",
                vblock->qc().network_id(), pool_idx_, vblock->qc().pool_index());
            return;
        }

        root_view_block_chain_->Store(vblock, true, nullptr, nullptr, false);
        root_view_block_chain_->UpdateHighViewBlock(vblock->qc());
    ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(root_view_block_chain_, msg_ptr, vblock->qc());
        // root_view_block_chain_->CommitSynced(vblock);
        if (vblock->block_info().tx_list_size() > 0) {
            SyncLaterBlocks(
                root_view_block_chain_, 
                vblock->qc().network_id(), 
                vblock->qc().pool_index(), 
                vblock->qc().view());
        }
    } else {
        if (vblock->qc().network_id() % common::kImmutablePoolSize != pool_idx_) {
            SHARDORA_ERROR("invalid shard id: %u, pool_idx: %u",
                vblock->qc().network_id(), pool_idx_);
            //assert(false);
            return;
        }

        auto cross_view_block_chain = cross_shard_view_block_chain_[vblock->qc().network_id()];
        cross_view_block_chain->Store(vblock, true, nullptr, nullptr, false);
        cross_view_block_chain->UpdateHighViewBlock(vblock->qc());
    ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(cross_view_block_chain, msg_ptr, vblock->qc());
        if (vblock->block_info().tx_list_size() > 0) {
            SyncLaterBlocks(
                cross_view_block_chain, 
                vblock->qc().network_id(), 
                vblock->qc().pool_index(), 
                vblock->qc().view());
        }
    }
}

void Hotstuff::SyncLaterBlocks(
        std::shared_ptr<ViewBlockChain> view_block_chain, 
        uint32_t network_id, 
        uint32_t pool_index, 
        View view) {
    auto call_sync_later_func = [this, view_block_chain, network_id, pool_index, view]() {
        static const uint32_t kSyncLaterViewCount = 16u;
        for (View i = view + 1; i <= view + kSyncLaterViewCount; ++i) {
            SHARDORA_DEBUG("now sync later block %u_%u_%lu",
                network_id, pool_index, i);
            if (view_block_chain->HighView() >= i) {
                continue;
            }
            
            SHARDORA_DEBUG("real now sync later block %u_%u_%lu",
                network_id, pool_index, i);
            kv_sync_->AddSyncView(
                network_id,
                pool_index,
                i,
                sync::kSyncHighest);
        }
    };

    layter_sync_tick_.CutOff(200000llu, call_sync_later_func);
}

Status Hotstuff::VerifyQC(const QC& qc) {
    // verify qc
    if (!IsQcTcValid(qc)) {
        //assert(false);
        return Status::kError;
    }

    if (qc.view() < view_block_chain()->HighViewBlock()->qc().view()) {        
        SHARDORA_DEBUG("qc view is smaller than high qc view, pool: %d, qc view: %lu, high qc view: %lu",
            pool_idx_, qc.view(), view_block_chain()->HighViewBlock()->qc().view());
        return Status::kError;
    }

    if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d verify qc failed: %lu", pool_idx_, qc.view());
        // //assert(false);
        return Status::kError; 
    }

    return Status::kSuccess;
}

Status Hotstuff::VerifyViewBlock(
        const ViewBlock& v_block, 
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const TC* tc,
        const uint32_t& elect_height) {
    Status ret = Status::kSuccess;
    if (!v_block.has_qc()) {
        SHARDORA_ERROR("qc not exist.");
        return Status::kError;
    }

    if (v_block.block_info().height() <= view_block_chain->LatestCommittedBlock()->block_info().height()) {
        SHARDORA_ERROR("new view block height error: %lu, last commited block height: %lu", 
            v_block.block_info().height(),
            view_block_chain->LatestCommittedBlock()->block_info().height());
        return Status::kError;
    }

    if (v_block.block_info().timestamp() < view_block_chain->HighViewBlock()->block_info().timestamp()) {
        SHARDORA_ERROR("%u_%u_%lu_%lu, new view block timestamp error: %lu, last view block timestamp: %lu", 
            common::GlobalInfo::Instance()->network_id(),
            pool_idx_,
            v_block.qc().view(),
            v_block.block_info().height(),
            v_block.block_info().timestamp(),
            view_block_chain->HighViewBlock()->block_info().timestamp());
        return Status::kError;
    }

    // Get the effective height for comparison. If HighViewBlock has no
    // block_info (TC timeout placeholder after restart), fall back to
    // LatestCommittedBlock's height to avoid comparing against 0.
    uint64_t local_high_height = view_block_chain->HighViewBlock()->block_info().height();
    if (local_high_height == 0 && !view_block_chain->HighViewBlock()->has_block_info()) {
        auto committed = view_block_chain->LatestCommittedBlock();
        if (committed && committed->has_block_info()) {
            local_high_height = committed->block_info().height();
        }
    }

    if (v_block.block_info().height() != local_high_height + 1) {
        auto gap = v_block.block_info().height() - local_high_height;
        SHARDORA_WARN("%u_%u_%lu_%lu, new view block height gap: %lu (local: %lu, propose: %lu)", 
            common::GlobalInfo::Instance()->network_id(),
            pool_idx_,
            v_block.qc().view(),
            v_block.block_info().height(),
            gap,
            local_high_height,
            v_block.block_info().height());
        // Don't add individual sync items per height here — that floods the sync
        // queue with thousands of items (33 pools × hundreds of heights each).
        // Instead, just request the next missing height. SyncAllLatestBlocks
        // handles bulk range sync every 1s via the latest_sync_item mechanism,
        // which is far more efficient (one request covers up to 256 heights per pool).
        if (gap > 0 && !BlockHeightCommited(
                prefix_db_,
                v_block.qc().network_id(),
                v_block.qc().pool_index(),
                local_high_height + 1)) {
            kv_sync_->AddSyncHeight(
                v_block.qc().network_id(),
                v_block.qc().pool_index(),
                local_high_height + 1,
                0);
        }
        return Status::kError;
    }

    // fast hotstuff condition
    auto qc_view_block_info = view_block_chain->Get(v_block.parent_hash());
    if (!qc_view_block_info) {
        SHARDORA_DEBUG("get qc prev view block message is error: %s, sync parent view: %u_%u_%lu",
            common::Encode::HexEncode(v_block.parent_hash()).c_str(),
            v_block.qc().network_id(), 
            v_block.qc().pool_index(), 
            v_block.qc().view() - 1);
        // Sync the parent hash
        kv_sync_->AddSyncViewHash(
            v_block.qc().network_id(), 
            v_block.qc().pool_index(), 
            v_block.parent_hash(),
            0);
        // Also sync by height for the immediate predecessor — hash-based sync
        // can be slow if the block isn't in the peer's view_blocks_info_ map yet.
        if (v_block.block_info().height() > 1) {
            kv_sync_->AddSyncHeight(
                v_block.qc().network_id(), 
                v_block.qc().pool_index(), 
                v_block.block_info().height() - 1,
                0);
        }
        
        return Status::kError;
    }

    SHARDORA_DEBUG("pool: %d, block view message is success. %lu, %lu, %s, %s, "
        "v_block.qc().view(): %lu, pacemaker()->CurView(): %lu, "
        "v_block.qc().view(): %lu",
        pool_idx_, v_block.qc().view(), view_block_chain->LatestCommittedBlock()->qc().view(),
        common::Encode::HexEncode(view_block_chain->LatestCommittedBlock()->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block.parent_hash()).c_str(),
        v_block.qc().view(), pacemaker()->CurView(), 
        v_block.qc().view());

    return Status::kSuccess;
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    if (vote_msg.view() < view_block_chain()->HighViewBlock()->qc().view()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status Hotstuff::VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto leader = pro_msg_wrap->leader;
    if (!leader) {
        SHARDORA_ERROR("Get Leader is error.");
        return Status::kError;
    }

    auto& qc = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().view_item().qc();
    auto& block_info = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().view_item().block_info();
    if (leader->index != qc.leader_idx()) {
        SHARDORA_ERROR("%u_%u_%lu_%lu, leader->index: %d != qc.leader_idx(): %d", 
            common::GlobalInfo::Instance()->network_id(),
            pool_idx_,
            qc.view(),
            block_info.height(),
            leader->index,
            qc.leader_idx());
        return Status::kError;
    }
   
    if (last_vote_view_ >= qc.view()) {
        SHARDORA_ERROR("%u_%u_%lu_%lu, last_vote_view_: %lu >= out_view: %lu", 
            common::GlobalInfo::Instance()->network_id(),
            pool_idx_,
            qc.view(),
            block_info.height(),
            last_vote_view_, 
            qc.view());
        return Status::kError;
    }

    if (view_block_chain_->HighViewBlock()->qc().view() >= qc.view()) {
        SHARDORA_ERROR("%u_%u_%lu_%lu, view_block_chain_->HighViewBlock()->qc().view(): %lu >= out_view: %lu", 
            common::GlobalInfo::Instance()->network_id(),
            pool_idx_,
            qc.view(),
            block_info.height(),
            view_block_chain_->HighViewBlock()->qc().view(), 
            qc.view());
        return Status::kError;
    }

    auto local_index = GetLocalMemberIdx();
    if (local_index == leader->index) {
        pro_msg_wrap->leader = leader;
        pro_msg_wrap->msg_ptr->is_leader = true;
        return Status::kSuccess;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        pro_msg_wrap->msg_ptr->header);
    if (crypto_->security()->Verify(
            msg_hash,
            leader->pubkey,
            pro_msg_wrap->msg_ptr->header.sign()) != security::kSecuritySuccess) {
        SHARDORA_DEBUG("pool index: %d, verify leader sign failed: %s, index: %d, pk: %s, "
            "consecutive_failures_: %d, last_stable_leader_member_index_: %d, out_view: %lu", 
            pool_idx_,
            common::Encode::HexEncode(leader->id).c_str(),
            leader->index,
            common::Encode::HexEncode(leader->pubkey).c_str(),
            consecutive_failures_,
            last_stable_leader_member_index_.load(),
            qc.view());
        return Status::kError;
    }
    
    pro_msg_wrap->leader = leader;
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(
        View leader_view,
        common::BftMemberPtr leader,
        const transport::MessagePtr& msg_ptr, 
        hotstuff::protobuf::ProposeMsg* pro_msg) {
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kElectItemNotFound;
    }

    auto new_view_block = pro_msg->mutable_view_item();
    auto* tx_propose = pro_msg->mutable_tx_propose();
    Status s = ConstructViewBlock(leader_view, leader, msg_ptr, new_view_block, tx_propose);
    if (s != Status::kSuccess) {
        SHARDORA_DEBUG("pool: %d construct view block failed, view: %lu, %d, member_index: %d",
            pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), (int32_t)s, 
            elect_item->LocalMember()->index);        
        return s;
    }

    pro_msg->set_elect_height(elect_item->ElectHeight());
    return Status::kSuccess;
}

Status Hotstuff::ConstructVoteMsg(
        const transport::MessagePtr& msg_ptr,
        hotstuff::protobuf::VoteMsg* vote_msg,
        uint64_t elect_height, 
        uint64_t tm_height, 
        const std::shared_ptr<ViewBlock>& v_block,
        const LeaderNonceMap* leader_nonce_map) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = elect_info_->GetElectItem(
        common::GlobalInfo::Instance()->network_id(), 
        elect_height);
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }

    uint32_t replica_idx = elect_item->LocalMember()->index;
    vote_msg->set_replica_idx(replica_idx);
    vote_msg->set_view_block_hash(v_block->qc().view_block_hash());
    SHARDORA_DEBUG("success set view block hash: %s, %u_%u_%lu",
        common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str(),
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        v_block->qc().view());
    //assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    vote_msg->set_view(v_block->qc().view());
    vote_msg->set_elect_height(elect_height);
    vote_msg->set_leader_idx(v_block->qc().leader_idx());
    vote_msg->set_tm_height(tm_height);
    QC qc_item;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(v_block->qc().view());
    qc_item.set_view_block_hash(v_block->qc().view_block_hash());
    SHARDORA_DEBUG("success set view block hash: %s, %u_%u_%lu, vote_msg leader idx: %d",
        common::Encode::HexEncode(qc_item.view_block_hash()).c_str(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view(),
        v_block->qc().leader_idx());
    //assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_tm_height(tm_height);
    qc_item.set_leader_idx(v_block->qc().leader_idx());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto qc_hash = GetQCMsgHash(qc_item);
    std::string sign_x, sign_y;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (crypto()->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_height,
            qc_hash,
            &sign_x,
            &sign_y) != Status::kSuccess) {
        SHARDORA_ERROR("Sign message is error.");
        return Status::kError;
    }

    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_y(sign_y);
    if (!msg_ptr->is_leader) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        // Use the leader_nonce_map built during addTxsToPool (single traversal).
        // Fall back to an empty map if not provided.
        static const LeaderNonceMap kEmptyMap;
        const LeaderNonceMap& nonce_map = leader_nonce_map ? *leader_nonce_map : kEmptyMap;

        auto* txs = vote_msg->mutable_txs();
        wrapper()->GetTxSyncToLeader(
            v_block->qc().leader_idx(), 
            consensus::kSyncToLeaderTxCount,
            view_block_chain_, 
            view_block_chain_->HighQC().view_block_hash(), 
            txs,
            nonce_map);
        if (txs->size() > 0)
        SHARDORA_DEBUG("tps now vote message get tx sync to leader: %d", txs->size());
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    
    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock(
        View leader_view,
        common::BftMemberPtr leader,
        const transport::MessagePtr& msg_ptr, 
        ViewBlock* view_block,
        hotstuff::protobuf::TxPropose* tx_propose) {
    auto local_elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (local_elect_item == nullptr) {
        SHARDORA_DEBUG("pool index: %d, local_elect_item == nullptr", pool_idx_);
        return Status::kError;
    }

    auto local_member = local_elect_item->LocalMember();
    if (local_member == nullptr) {
        SHARDORA_DEBUG("pool index: %d, local_member == nullptr", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto pre_v_block = view_block_chain()->HighViewBlock();
#ifdef TEST_FORKING_ATTACK
    auto new_prev_view = pre_v_block->qc().view();
    auto rand_count = rand() % 100;
    if (rand_count <= 30 && new_prev_view > 4) {
        if (rand_count <= 15) {
            new_prev_view -= 1;
        }

        auto tmp_block = view_block_chain()->GetViewBlockVithView(new_prev_view);
        if (tmp_block != nullptr) {
            SHARDORA_WARN("pool: %d, success change parent view from: %lu, to: %lu, count: %u",
                pool_idx_, pre_v_block->qc().view(), new_prev_view, (gTestChangeViewCount++));
            pre_v_block = tmp_block->view_block;
        }
    }
#endif
    if (!leader || leader->index != local_member->index) {
        SHARDORA_DEBUG("pool index: %d, leader->index: %d != local_member->index: %d",
            pool_idx_, leader->index, local_member->index);
        return Status::kError;
    }

    auto* qc = view_block->mutable_qc();
    qc->set_leader_idx(leader->index);
    qc->set_tm_height(0);
    qc->set_view(leader_view);
    qc->set_network_id(common::GlobalInfo::Instance()->network_id());
    qc->set_pool_index(pool_idx_);
    view_block->set_parent_hash(pre_v_block->qc().view_block_hash());
    // If HighViewBlock has no block_info (e.g., TC timeout placeholder after
    // restart), fall back to LatestCommittedBlock as the parent for height
    // calculation. Otherwise Wrap() would set height = 0+1 = 1, which is
    // rejected by all voters that have already committed higher blocks.
    if (!pre_v_block->has_block_info() || pre_v_block->block_info().height() == 0) {
        auto committed = view_block_chain_->LatestCommittedBlock();
        if (committed && committed->has_block_info() && 
                committed->block_info().height() > 0) {
            SHARDORA_WARN("pool: %d, HighViewBlock has no valid block_info (view: %lu), "
                "falling back to LatestCommittedBlock (height: %lu) for propose",
                pool_idx_, pre_v_block->qc().view(), 
                committed->block_info().height());
            pre_v_block = committed;
            view_block->set_parent_hash(pre_v_block->qc().view_block_hash());
        }
    }
    SHARDORA_DEBUG("get prev block hash: %s, height: %lu, leader->index: %d", 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        pre_v_block->block_info().height(), leader->index);
    auto s = wrapper()->Wrap(
        msg_ptr,
        pre_v_block, 
        view_block, 
        tx_propose, 
        IsEmptyBlockAllowed(*pre_v_block), 
        view_block_chain_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        SHARDORA_DEBUG("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        view_block->release_qc();
        return s;
    }

    SHARDORA_DEBUG("success check is empty block allowd: %d, %u_%u_%lu, "
        "tx size: %u, cur view: %lu, pre view: %lu, last_vote_view_: %lu, "
        "pacemaker()->CurView(): %lu",
        pool_idx_, view_block->qc().network_id(), 
        view_block->qc().pool_index(), view_block->qc().view(),
        tx_propose->txs_size(),
        qc->view(),
        pre_v_block->qc().view(),
        last_vote_view_,
        pacemaker()->CurView());
    auto elect_item = elect_info_->GetElectItem(
        common::GlobalInfo::Instance()->network_id(),
        view_block->qc().elect_height());
    if (!elect_item || !elect_item->IsValid()) {
        view_block->release_qc();
        return Status::kError;
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

bool Hotstuff::IsEmptyBlockAllowed(const ViewBlock& v_block) {
    if (v_block.qc().view() <= 0llu) {
        return false;
    }

    auto* v_block1 = &v_block;
    if (!v_block1 || v_block1->block_info().tx_list_size() > 0) {
        SHARDORA_DEBUG("!v_block1 || v_block1->block_info().tx_list_size() > 0");
        return true;
    }

    return false;
}

void Hotstuff::ConstructHotstuffMsg(
        const MsgType msg_type, 
        pb_ProposeMsg* pb_pro_msg, 
        pb_VoteMsg* pb_vote_msg,
        pb_NewViewMsg* pb_nv_msg,
        pb_HotstuffMessage* pb_hotstuff_msg) {
    pb_hotstuff_msg->set_type(msg_type);
    pb_hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    pb_hotstuff_msg->set_pool_index(pool_idx_);
}

Status Hotstuff::SendMsgToLeader(
        common::BftMemberPtr leader,
        std::shared_ptr<transport::TransportMessage>& trans_msg, 
        const MsgType msg_type) {
    Status ret = Status::kSuccess;
    auto& header_msg = trans_msg->header;
    header_msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id, leader->id);
    header_msg.set_des_dht_key(dht_key.StrKey());
    header_msg.set_type(common::kHotstuffMessage);
    transport::TcpTransport::Instance()->SetMessageHash(header_msg);
    if (leader->leader_ecdh_key.empty()) {
        if (crypto_->security()->GetEcdhKey(
                leader->pubkey,
                &leader->leader_ecdh_key) != security::kSecuritySuccess) {
            SHARDORA_DEBUG("verify leader sign failed: %s", 
                common::Encode::HexEncode(leader->id).c_str());
            return Status::kError;
        }
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        header_msg);
    std::string crypt_msg;
    security::RawPrivateKey ecdh_key = std::make_pair(
        leader->leader_ecdh_key.c_str(), 
        leader->leader_ecdh_key.size());
    if (crypto_->security()->Encrypt(
            msg_hash, 
            ecdh_key, 
            &crypt_msg)!= security::kSecuritySuccess) {
        SHARDORA_DEBUG("send to leader encrypt failed: %s", 
            common::Encode::HexEncode(leader->id).c_str());
        return Status::kError;
    }

    trans_msg->header.set_ecdh_encrypt(crypt_msg);
    auto local_idx = GetLocalMemberIdx();
    if (leader->index != local_idx) {
        // if (leader->public_ip == 0 || leader->public_port == 0) {
            network::Route::Instance()->Send(trans_msg);
        // } else {
        //     transport::TcpTransport::Instance()->Send(
        //         common::Uint32ToIp(leader->public_ip), 
        //         leader->public_port, 
        //         header_msg);
        // }
    } else {
        transport::TcpTransport::Instance()->AddLocalMessage(trans_msg);
        SHARDORA_DEBUG("2 success add local message: %lu", trans_msg->header.hash64());
        // if (msg_type == VOTE) {
        //     HandleVoteMsg(trans_msg);
        // } else if (msg_type == PRE_RESET_TIMER) {
        //     HandlePreResetTimerMsg(trans_msg);
        // }
    }

// #ifndef NDEBUG
//     if (msg_type == PRE_RESET_TIMER) {
//         for (uint32_t i = 0; i < header_msg.hotstuff().pre_reset_timer_msg().txs_size(); ++i) {
//             auto& tx = header_msg.hotstuff().pre_reset_timer_msg().txs(i);
//             SHARDORA_WARN("pool index: %u, send to leader %d message to leader net: %u, %s, "
//                 "hash64: %lu, %s:%d, leader->index: %d, local_idx: %d, nonce: %lu, to: %s",
//                 pool_idx_,
//                 msg_type,
//                 leader->net_id, 
//                 common::Encode::HexEncode(leader->id).c_str(), 
//                 header_msg.hash64(),
//                 common::Uint32ToIp(leader->public_ip).c_str(),
//                 leader->public_port,
//                 leader->index,
//                 local_idx,
//                 tx.nonce(),
//                 common::Encode::HexEncode(tx.to()).c_str());
//         }
//     }
// #endif

    SHARDORA_DEBUG("pool index: %u, send to leader %d message to leader net: %u, %s, "
        "hash64: %lu, %s:%d, leader->index: %d, local_idx: %d",
        pool_idx_,
        (int32_t)msg_type,
        leader->net_id, 
        common::Encode::HexEncode(leader->id).c_str(), 
        header_msg.hash64(),
        common::Uint32ToIp(leader->public_ip).c_str(),
        leader->public_port,
        leader->index,
        local_idx);
    return ret;
}

void Hotstuff::TryRecoverFromStuck(
        const transport::MessagePtr& msg_ptr, 
        bool has_user_tx, 
        bool has_system_tx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (latest_qc_item_ptr_ && update_latest_view_tm_) {
        laste_vote_prev_view_tm_.Put(latest_qc_item_ptr_->view(), now_tm_ms);
        update_latest_view_tm_ = false;
    }

    view_block_chain()->HandleTimerMessage();
    root_view_block_chain_->HandleTimerMessage();
    for (auto& cross_view_block_chain : cross_shard_view_block_chain_) {
        cross_view_block_chain.second->HandleTimerMessage();
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (has_user_tx || has_system_tx) {
        has_user_tx_tag_ = true;
        // New txs arrived, reset empty propose backoff so we try immediately
        if (empty_propose_count_ > 0) {
            empty_propose_count_ = 0;
            empty_propose_backoff_until_ms_ = 0;
            // SHARDORA_DEBUG("pool: %u, backoff reset by %s tx",
            //     pool_idx_, has_user_tx ? "user" : "system");
        }
    }

    // if (GetLocalMemberIdx() == common::kInvalidUint32) {
    //     // SHARDORA_DEBUG("GetLocalMemberIdx() == common::kInvalidUint32, pool: %u", pool_idx_);
    //     return;
    // }

    if (now_tm_ms >= prev_sync_latest_view_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        prev_sync_latest_view_tm_ms_ = now_tm_ms;
        auto hight_view_block = view_block_chain_->HighViewBlock();
        if (hight_view_block) {
            // Only skip sync if this node is an active consensus member AND
            // consensus is progressing normally. If the node is not a committee
            // member (GetLocalMemberIdx() == kInvalidUint32), it cannot vote
            // and must rely on sync to get new blocks.
            auto committed_block = view_block_chain_->LatestCommittedBlock();
            bool consensus_active = false;
            auto local_member_idx = GetLocalMemberIdx();
            if (local_member_idx != common::kInvalidUint32 &&
                    committed_block && committed_block->has_block_info() &&
                    hight_view_block->has_block_info()) {
                auto gap = hight_view_block->block_info().height() - 
                           committed_block->block_info().height();
                // gap <= 3 means consensus pipeline is healthy (propose/prevote/commit)
                if (gap <= 3) {
                    consensus_active = true;
                }
            }
            if (!consensus_active) {
                kv_sync_->AddSyncView(
                    hight_view_block->qc().network_id(), 
                    hight_view_block->qc().pool_index(), 
                    hight_view_block->qc().view() + 1,
                    sync::kSyncHighest);
            }
        }
    // } else {
        // if (!has_user_tx_tag_ && !has_system_tx) {
        //     return;
        // }
    }

    auto local_idx = GetLocalMemberIdx();
    View out_view = 0;
    auto leader_block_tm = GetLeaderBlockTimestamp();
    if (!latest_qc_item_ptr_) {
        // if (pool_idx_ == common::kImmutablePoolSize) {
        //     SHARDORA_DEBUG("pool %u: latest_qc_item_ptr_ is null, cannot get leader", pool_idx_);
        // }
        return;
    }
    auto leader = GetLeader(local_idx, *latest_qc_item_ptr_, &out_view, leader_block_tm, false);
    if (!leader) {
        // SHARDORA_DEBUG("pool index: %d, no leader", pool_idx_);
        return;
    }

    // SHARDORA_DEBUG("pool: %u, get leader index: %u, local index: %u", pool_idx_, leader->index, local_idx);
    if (leader->index != local_idx) {
        SyncLocalTxToLeader(msg_ptr, leader, has_system_tx);
        if (latest_leader_propose_message_) {
            if (latest_leader_propose_message_->prev_timestamp + 3000lu > now_tm_ms) {
                return;
            }

            ResendLeaderLatestProposeMessage();
            latest_leader_propose_message_->prev_timestamp = now_tm_ms;
        }
        return;
    }

    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        return;
    }

    // SHARDORA_DEBUG("pool index: %d, GetLeader return leader: %d, out_view: %lu, local_idx: %d",
    //     pool_idx_, leader ? leader->index : -1, out_view, local_idx);
    // if (prev_recover_check_tm_ms_ + 3000lu > now_tm_ms) {
    //     return;
    // }

    // prev_recover_check_tm_ms_ = now_tm_ms;
    // auto stuck_st = IsStuck();
    // if (stuck_st != 0) {
    //     if (stuck_st != 1) {
    //         SHARDORA_DEBUG("pool: %u stuck_st != 0: %d", pool_idx_, stuck_st);
    //     }
    //     return;
    // }
    // SHARDORA_DEBUG("pool index: %d, found leader: %d, local_index: %d",
    //     pool_idx_, leader->index, local_idx);
    if (leader && leader->index == local_idx) {
        if (leader->pubkey != crypto_->security()->GetPublicKey()) {
            SHARDORA_ERROR("leader pubkey: %s != local pubkey: %s, pool index: %d",
                common::Encode::HexEncode(leader->pubkey).c_str(),
                common::Encode::HexEncode(crypto_->security()->GetPublicKey()).c_str(),
                pool_idx_);
            return;
        }

        ADD_DEBUG_PROCESS_TIMESTAMP();
        // SHARDORA_DEBUG("leader try recover from stuck, pool: %u, out_view: %lu, last_vote_view_: %lu",
        //     pool_idx_, out_view, last_vote_view_);
        if (last_vote_view_ < out_view) {
            // Backoff: if previous proposes yielded 0 txs, delay retries
            if (empty_propose_count_ > 0 && now_tm_ms < empty_propose_backoff_until_ms_) {
                // SHARDORA_DEBUG("pool: %u, empty propose backoff: count=%u, wait until %lu, now %lu",
                //     pool_idx_, empty_propose_count_, empty_propose_backoff_until_ms_, now_tm_ms);
                return;
            }

            auto propose_status = Propose(out_view, leader, nullptr, nullptr, msg_ptr, leader_block_tm);
            if (propose_status != Status::kSuccess) {
                // Propose failed (likely 0 txs), increase backoff
                ++empty_propose_count_;
                uint32_t backoff_ms = std::min(
                    kEmptyProposeBackoffBaseMs * (1u << std::min(empty_propose_count_, 7u)),
                    kEmptyProposeBackoffMaxMs);
                empty_propose_backoff_until_ms_ = now_tm_ms + backoff_ms;
                // SHARDORA_DEBUG("pool: %u, propose failed, empty_propose_count: %u, backoff: %u ms",
                //     pool_idx_, empty_propose_count_, backoff_ms);
            } else {
                // Propose succeeded, reset backoff
                empty_propose_count_ = 0;
                empty_propose_backoff_until_ms_ = 0;
            }
        }
        ADD_DEBUG_PROCESS_TIMESTAMP();
        // if (latest_qc_item_ptr_) {
        //     SHARDORA_DEBUG("leader do propose message: %d, pool index: %u, %u_%u_%lu, "
        //         "sec pk: %s, leader pk: %s", 
        //         local_idx,
        //         pool_idx_,
        //         latest_qc_item_ptr_->network_id(), 
        //         latest_qc_item_ptr_->pool_index(), 
        //         latest_qc_item_ptr_->view(),
        //         common::Encode::HexEncode(crypto_->security()->GetPublicKey()).c_str(),
        //         common::Encode::HexEncode(leader->pubkey).c_str());
        // }

        if (latest_propose_msg_tm_ms_ > prev_sync_latest_view_tm_ms_) {
            prev_sync_latest_view_tm_ms_ = latest_propose_msg_tm_ms_;
        }

        return;
    }
}

void Hotstuff::SyncLocalTxToLeader(
        const transport::MessagePtr& msg_ptr, 
        common::BftMemberPtr leader, 
        bool has_system_tx) {
    if (!has_user_tx_tag_ && !has_system_tx) {
        // SHARDORA_DEBUG("pool: %u not has_user_tx_tag_ and no system tx.", pool_idx_);
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& header = trans_msg->header;
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pre_rst_timer_msg = hotstuff_msg->mutable_pre_reset_timer_msg();
    auto* txs = pre_rst_timer_msg->mutable_txs();
    wrapper()->GetTxSyncToLeader(
        leader->index, 
        1,
        view_block_chain_, 
        view_block_chain_->HighQC().view_block_hash(), 
        txs,
        std::unordered_map<std::string, uint64_t>());  // empty map — no leader block context here
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs->empty()) {
        SHARDORA_DEBUG("pool: %u txs.empty().", pool_idx_);
        return;
    }
    
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        SHARDORA_ERROR("pool: %d no elect item found", pool_idx_);
        return;
    }
    
    pre_rst_timer_msg->set_replica_idx(elect_item->LocalMember()->index);
    pre_rst_timer_msg->set_has_single_tx(has_system_tx);
    hotstuff_msg->set_type(PRE_RESET_TIMER);
    hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg->set_pool_index(pool_idx_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SendMsgToLeader(leader, trans_msg, PRE_RESET_TIMER);
    SHARDORA_DEBUG("pool: %d, send prereset msg from: %lu to: %lu, "
        "has_single_tx: %d, tx size: %u, hash: %lu",
        pool_idx_, pre_rst_timer_msg->replica_idx(), 
        leader->index, has_system_tx, txs->size(),
        trans_msg->header.hash64());
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

uint32_t Hotstuff::GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block) {
    uint32_t ret = 1;
    auto current = v_block;
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (!latest_committed_block) {
        return ret;
    }

    while (current->qc().view() > latest_committed_block->qc().view()) {
        current = view_block_chain()->ParentBlock(*current);
        if (!current) {
            return ret;
        }
        if (current->qc().leader_idx() == v_block->qc().leader_idx()) {
            ret++;
        }
    }

    SHARDORA_DEBUG("pool: %d add succ num: %lu, leader: %lu", pool_idx_, ret, v_block->qc().leader_idx());
    return ret;
}

} // namespace consensus

} // namespace shardora
