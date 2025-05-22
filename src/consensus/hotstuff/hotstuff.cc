#include <bls/agg_bls.h>
#include <bls/bls_dkg.h>
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

void Hotstuff::Init() {
    // set pacemaker timeout callback function
    last_vote_view_ = 0lu;
    auto latest_view_block = std::make_shared<ViewBlock>();
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block);
    if (s == Status::kSuccess) {
        view_block_chain_->Store(latest_view_block, false, nullptr, nullptr, true);
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        auto temp_ptr = view_block_chain_->Get(latest_view_block->qc().view_block_hash());
        view_block_chain_->SetLatestCommittedBlock(temp_ptr);
        InitAddNewViewBlock(latest_view_block);
        auto parent_hash = latest_view_block->parent_hash();
        while (!parent_hash.empty()) {
            ViewBlock view_block;
            if (!prefix_db_->GetBlock(parent_hash, &view_block)) {
                ZJC_ERROR("failed get parent hash: %s", 
                    common::Encode::HexEncode(parent_hash).c_str());
                break;
            }

            ZJC_INFO("success get parent hash: %s", common::Encode::HexEncode(parent_hash).c_str());
            if (view_block.qc().view() <= 0 || latest_view_block->qc().view() >= view_block.qc().view() + 2) {
                break;
            }

            parent_hash = view_block.parent_hash();
        }
    } else {
        ZJC_INFO("no genesis, waiting for syncing, pool_idx: %d", pool_idx_);
    }
}
    
void Hotstuff::InitAddNewViewBlock(std::shared_ptr<ViewBlock>& latest_view_block) {
    ZJC_INFO("pool: %d, latest vb from db, vb view: %lu",
        pool_idx_, 
        latest_view_block->qc().view());
    // 初始状态，使用 db 中最后一个 view_block 初始化视图链
    // TODO: check valid
    view_block_chain_->Store(latest_view_block, true, nullptr, nullptr, true);
    view_block_chain_->UpdateHighViewBlock(latest_view_block->qc());
    StopVoting(latest_view_block->qc().view());
    // 开启第一个视图
    ZJC_INFO("success new set qc view: %lu, %u_%u_%lu, hash: %s",
        latest_view_block->qc().view(),
        latest_view_block->qc().network_id(),
        latest_view_block->qc().pool_index(),
        latest_view_block->qc().view(),
        common::Encode::HexEncode(latest_view_block->qc().view_block_hash()).c_str());
    pacemaker_->NewQcView(latest_view_block->qc().view());
}

Status Hotstuff::Start() {
    auto leader = leader_rotation()->GetLeader();
    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kElectItemNotFound;
    }
    auto local_member = elect_item->LocalMember();
    if (!local_member) {
        return Status::kError;
    }
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
    } else if (leader->index == local_member->index) {
        ZJC_INFO("ViewBlock start propose");
        Propose(nullptr, nullptr, nullptr);
    }
    return Status::kSuccess;
}

Status Hotstuff::Propose(
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc,
        const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // TODO(HT): 打包的交易，超时后如何释放？
    // 打包参与共识中的交易，如何保证幂等
    auto btime = common::TimeUtils::TimestampMs();
    auto pre_v_block = view_block_chain()->HighViewBlock();
    if (!pre_v_block) {
        ZJC_INFO("pool %u not has prev view block.", pool_idx_);
        return Status::kError;
    }

    auto dht_ptr = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht_ptr) {
        ZJC_WARN("pool %u not has dht ptr.", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    if (readobly_dht->size() < 2) {
        ZJC_WARN("pool %u not has readobly_dht->size() < 2", pool_idx_);
        return Status::kError;
    }

    ZJC_INFO("net: %d, pool %u has dht ptr size: %d.", 
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        readobly_dht->size());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    if (tc != nullptr) {
        if (latest_qc_item_ptr_ == nullptr || tc->view() >= latest_qc_item_ptr_->view()) {
            assert(tc->pool_index() == pool_idx_);
            assert(tc->network_id() == common::GlobalInfo::Instance()->network_id());
            assert(IsQcTcValid(*tc));
            latest_qc_item_ptr_ = tc;
        }

        if (latest_leader_propose_message_ && 
                latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() <= tc->view()) {
            latest_leader_propose_message_ = nullptr;
        }
    }

    auto t1 = common::TimeUtils::TimestampMs();
    if (latest_leader_propose_message_ &&
            latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() >= pacemaker_->CurView()) {
        auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
        tmp_msg_ptr->header.CopyFrom(latest_leader_propose_message_->header);
        tmp_msg_ptr->is_leader = true;
        tmp_msg_ptr->header.release_broadcast();
        auto broadcast = tmp_msg_ptr->header.mutable_broadcast();
        auto* hotstuff_msg = tmp_msg_ptr->header.mutable_hotstuff();
        if (tc != nullptr) {
            auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
            *pb_pro_msg->mutable_tc() = *tc;
        }

        transport::TcpTransport::Instance()->SetMessageHash(tmp_msg_ptr->header);
        auto s = crypto()->SignMessage(tmp_msg_ptr);
        auto& header = tmp_msg_ptr->header;
        if (s != Status::kSuccess) {
            ZJC_WARN("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
                pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
            return s;
        }

        transport::TcpTransport::Instance()->AddLocalMessage(tmp_msg_ptr);
        ZJC_INFO("0 success add local message: %lu", tmp_msg_ptr->header.hash64());
        network::Route::Instance()->Send(tmp_msg_ptr);
#ifndef NDEBUG
        transport::protobuf::ConsensusDebug cons_debug;
        cons_debug.ParseFromString(header.debug());
        ZJC_INFO("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
            "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s, "
            "msg view: %lu, cur view: %lu, propose msg: %s",
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
            ProtobufToJson(header.hotstuff().pro_msg()).c_str());
#endif
        // HandleProposeMsg(latest_leader_propose_message_);
        return Status::kSuccess;
    }

    if (max_view() != 0 && max_view() <= last_leader_propose_view_) {
        ZJC_INFO("pool: %d construct propose msg failed, %d, "
            "max_view(): %lu last_leader_propose_view_: %lu",
            pool_idx_, Status::kError,
            max_view(), last_leader_propose_view_);
        return Status::kError;
    }

    auto t2 = common::TimeUtils::TimestampMs();
    ZJC_INFO("1 now ontime called propose: %d", pool_idx_);
    auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
    tmp_msg_ptr->is_leader = true;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = tmp_msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
    Status s = ConstructProposeMsg(msg_ptr, pb_pro_msg);
    if (s != Status::kSuccess) {
        if (!tc) {
            ZJC_INFO("pool: %d construct propose msg failed, %d",
                pool_idx_, s);
            return s;
        }


        pb_pro_msg->release_view_item();
    }
    
    auto t3 = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, nullptr, hotstuff_msg);
    if (tc != nullptr) {
        *pb_pro_msg->mutable_tc() = *tc;
    }

    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

    auto t4 = common::TimeUtils::TimestampMs();
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
    propose_debug_str += ", tx gids: ";
    security::Ecdsa ecdsa;
    for (uint32_t tx_idx = 0; tx_idx < pb_pro_msg->tx_propose().txs_size(); ++tx_idx) {
        if (!pb_pro_msg->tx_propose().txs(tx_idx).pubkey().empty()) {
            propose_debug_str += common::Encode::HexEncode(ecdsa.GetAddress(pb_pro_msg->tx_propose().txs(tx_idx).pubkey())) + "_" +
                common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).to())  + "_" +
                common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).key())  + "_" +
                std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).step()) + "_" +
                std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).nonce()) + " ";
        } else {
            propose_debug_str += common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).to())  + "_" +
                common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).key())  + "_" +
                std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).step()) + "_" +
                std::to_string(pb_pro_msg->tx_propose().txs(tx_idx).nonce()) + " ";
        }
    }

    transport::protobuf::ConsensusDebug consensus_debug;
    consensus_debug.add_messages(propose_debug_str);
    // for (uint32_t i = 0; i < common::kEachShardMaxNodeCount; ++i) {
    //     consensus_debug.add_vote_timestamps(0);
    // }

    consensus_debug.set_begin_timestamp(common::TimeUtils::TimestampMs());
    header.set_debug(consensus_debug.SerializeAsString());
    ZJC_INFO("leader begin propose_debug: %s", ProtobufToJson(consensus_debug).c_str());
#endif
    auto t5 = common::TimeUtils::TimestampMs();
    s = crypto()->SignMessage(tmp_msg_ptr);
    if (s != Status::kSuccess) {
        ZJC_WARN("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
        return s;
    }

    if (tmp_msg_ptr->header.hotstuff().pro_msg().has_view_item()) {
        latest_leader_propose_message_ = tmp_msg_ptr;
    }

    auto t6 = common::TimeUtils::TimestampMs();
    transport::TcpTransport::Instance()->AddLocalMessage(tmp_msg_ptr);
    ZJC_INFO("1 success add local message: %lu", tmp_msg_ptr->header.hash64());
    network::Route::Instance()->Send(tmp_msg_ptr);
    auto t7 = common::TimeUtils::TimestampMs();
    auto old_last_leader_propose_view_ = last_leader_propose_view_;
    last_leader_propose_view_ = std::max<uint64_t>(
        hotstuff_msg->pro_msg().view_item().qc().view(), 
        hotstuff_msg->pro_msg().tc().view());

    ZJC_INFO("new propose message hash: %lu", tmp_msg_ptr->header.hash64());
    ADD_DEBUG_PROCESS_TIMESTAMP();

    auto t8 = common::TimeUtils::TimestampMs();
    ZJC_INFO("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
        "old_last_leader_propose_view_: %lu, "
        "last_leader_propose_view_: %lu, tc view: %lu, hash: %s, "
        "qc_view: %lu, hash64: %lu, propose_debug: %s, t1: %lu, t2: %lu, t3: %u, t4: %lu, t5: %lu, t6: %lu, t7: %lu, t8: %lu",
        pool_idx_,
        header.hotstuff().pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().qc().view(),
        old_last_leader_propose_view_,
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
        (t8 - btime)
        );

    if (tc != nullptr && IsQcTcValid(*tc)) {
        ZJC_INFO("new prev qc coming: %s, %u_%u_%lu, parent hash: %s, tx size: %u, "
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
    return Status::kSuccess;
}

void Hotstuff::HandleProposeMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_INFO("handle propose called hash: %lu, propose_debug: %s", 
        msg_ptr->header.hash64(), 
        ProtobufToJson(msg_ptr->header.hotstuff()).c_str());
    auto pro_msg_wrap = std::make_shared<ProposeMsgWrapper>(msg_ptr);
    if (msg_ptr->header.hotstuff().pro_msg().has_tc()) {
        HandleTC(pro_msg_wrap);
    }

    if (!msg_ptr->header.hotstuff().pro_msg().has_view_item()) {
        ZJC_INFO("handle propose called hash: %lu, %u_%u_%lu, "
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
        return;
    }

    assert(msg_ptr->header.hotstuff().pro_msg().view_item().qc().view_block_hash().empty());
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    cons_debug.ParseFromString(msg_ptr->header.debug());
    ZJC_INFO("handle propose called hash: %lu, %u_%u_%lu, "
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
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        ZJC_INFO("pool: %d handle propose duration: %lu ms", pool_idx_, e-b);
    });

    pro_msg_wrap->view_block_ptr = std::make_shared<ViewBlock>(
        msg_ptr->header.hotstuff().pro_msg().view_item());
#ifndef NDEBUG
    pro_msg_wrap->view_block_ptr->set_debug(cons_debug.SerializeAsString());
    ZJC_INFO("handle new propose message parent hash: %s, %u_%u_%lu, view hash: %s, "
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
    assert(pro_msg_wrap->view_block_ptr->block_info().tx_list_size() == 0);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& view_item = *pro_msg_wrap->view_block_ptr;
#ifndef NDEBUG
    ZJC_INFO("HandleProposeMessageByStep called hash: %lu, "
        "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, view_item.qc().view(),
        ProtobufToJson(cons_debug).c_str());
#endif
    auto st = HandleProposeMsgStep_HasVote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto propose_view = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view();
    View handled_view = 0;
    // for (auto iter = leader_view_with_propose_msgs_.begin();
    //         iter != leader_view_with_propose_msgs_.end();) {
    //     if (iter->first > propose_view) {
    //         // assert(false);
    //         break;
    //     }

        // auto& rehandle_view_item = *iter->second->view_block_ptr;
        // ZJC_WARN(
        //     "rehandle propose message begin HandleProposeMessageByStep called hash: %lu, "
        //     "last_vote_view_: %lu, view_item.qc().view(): %lu, "
        //     "propose_debug: %s, view_block_hash: %s",
        //     iter->second->msg_ptr->header.hash64(), 
        //     last_vote_view_, rehandle_view_item.qc().view(),
        //     iter->second->msg_ptr->header.debug().c_str(),
        //     common::Encode::HexEncode(rehandle_view_item.qc().view_block_hash()).c_str());
        // rehandle_view_item.mutable_qc()->release_view_block_hash();
        // auto st = HandleProposeMessageByStep(iter->second);
        // if (st != Status::kSuccess) {
        //     ZJC_ERROR("handle propose message failed hash: %lu, propose_debug: %s",
        //         msg_ptr->header.hash64(),
        //         msg_ptr->header.debug().c_str());
        //     break;
        // }

        // ZJC_WARN("rehandle propose message success HandleProposeMessageByStep called hash: %lu, "
        //     "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        //     iter->second->msg_ptr->header.hash64(), last_vote_view_, rehandle_view_item.qc().view(),
        //     iter->second->msg_ptr->header.debug().c_str());
    //     iter = leader_view_with_propose_msgs_.erase(iter);
    //     CHECK_MEMORY_SIZE(leader_view_with_propose_msgs_);
    // }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMessageByStep(pro_msg_wrap);
    if (st != Status::kSuccess) {
#ifndef NDEBUG
        ZJC_ERROR("handle propose message failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            ProtobufToJson(cons_debug).c_str());
#endif
        // leader_view_with_propose_msgs_[propose_view] = pro_msg_wrap;
        // CHECK_MEMORY_SIZE(leader_view_with_propose_msgs_);
    } else {
        // for (auto iter = leader_view_with_propose_msgs_.begin();
        //         iter != leader_view_with_propose_msgs_.end();) {
        //     if (iter->first > propose_view) {
        //         break;
        //     }

        //     iter = leader_view_with_propose_msgs_.erase(iter);
        //     CHECK_MEMORY_SIZE(leader_view_with_propose_msgs_);
        // }
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

Status Hotstuff::HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> pro_msg_wrap) {
    auto msg_ptr = pro_msg_wrap->msg_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto st = HandleProposeMsgStep_VerifyLeader(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_VerifyViewBlock(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_TxAccept(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_ChainStore(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_Vote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}


Status Hotstuff::HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    return Status::kSuccess;
    auto& view_item = *pro_msg_wrap->view_block_ptr;
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    ZJC_INFO("HandleProposeMsgStep_HasVote called hash: %lu, "
        "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, view_item.qc().view(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (last_vote_view_ >= view_item.qc().view()) {
        ZJC_INFO("pool: %d has voted view: %lu, last_vote_view_: %u, "
            "hash64: %lu, pacemaker()->CurView(): %lu",
            pool_idx_, view_item.qc().view(),
            last_vote_view_, pro_msg_wrap->msg_ptr->header.hash64(),
            pacemaker()->CurView());
        if (last_vote_view_ == view_item.qc().view()) {
            // return Status::kSuccess;
            auto iter = voted_msgs_.find(view_item.qc().view());
            if (iter != voted_msgs_.end()) {
                ZJC_INFO("pool: %d has voted: %lu, last_vote_view_: %u, "
                    "hash64: %lu and resend vote: hash: %s",
                    pool_idx_, view_item.qc().view(),
                    last_vote_view_, pro_msg_wrap->msg_ptr->header.hash64(),
                    common::Encode::HexEncode(iter->second->header.hotstuff().vote_msg().view_block_hash()).c_str());
                auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
                tmp_msg_ptr->header.CopyFrom(iter->second->header);
                auto leader = leader_rotation_->GetLeader();
                // TODO: check is same leader
                if (!leader || SendMsgToLeader(leader, tmp_msg_ptr, VOTE) != Status::kSuccess) {
                    ZJC_ERROR("pool: %d, Send vote message is error.",
                        pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
                }
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
    ZJC_INFO("HandleProposeMsgStep_VerifyLeader called hash: %lu, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    auto local_idx = leader_rotation_->GetLocalMemberIdx();
    if (VerifyLeader(pro_msg_wrap) != Status::kSuccess) {
        // TODO 一旦某个节点状态滞后，那么 Leader 就与其他 replica 不同，导致无法处理新提案
        // 只能依赖同步，但由于同步慢于新的 propose 消息
        // 即是这里再加一次同步，也很难追上 propose 的速度，导致该节点掉队，因此还是需要一个队列缓存一下
        // 暂时无法处理的 propose 消息
        if (sync_pool_fn_) { // leader 不一致触发同步
            sync_pool_fn_(pool_idx_, 1);
        }

        ZJC_ERROR("verify leader failed, pool: %d view: %lu, hash64: %lu", 
            pool_idx_, view_item.qc().view(), pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }        

    if (view_item.qc().leader_idx() == local_idx) {
        pro_msg_wrap->msg_ptr->is_leader = true;
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 3 Verify TC
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    ZJC_INFO("HandleTC called hash: %lu, propose_debug: %s, pro_msg.tc().has_view_block_hash(): %d", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        ProtobufToJson(pro_msg).c_str(),
        pro_msg.tc().has_view_block_hash());
#endif
    if (pro_msg.has_tc() && pro_msg.tc().has_view_block_hash()) {
        if (VerifyQC(pro_msg.tc()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify tc failed: %lu", pool_idx_, pro_msg.tc().view());
            // assert(false);
            return Status::kError;
        }

        auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(pro_msg.tc());
        pacemaker()->NewTc(tc_ptr);
        auto& qc = pro_msg.tc();
        pacemaker()->NewQcView(qc.view());
        view_block_chain()->UpdateHighViewBlock(qc);
        TryCommit(pro_msg_wrap->msg_ptr, qc, 99999999lu);

        if (latest_qc_item_ptr_ == nullptr ||
                tc_ptr->view() >= latest_qc_item_ptr_->view()) {
            assert(IsQcTcValid(*tc_ptr));
            latest_qc_item_ptr_ = tc_ptr;
        }
// #ifndef NDEBUG
//         auto msg_hash = GetTCMsgHash(pro_msg.tc());
//         ZJC_WARN("HandleTC success verify tc %u_%u_%lu, hash: %s called hash: %lu, propose_debug: %s",
//             tc_ptr->network_id(), 
//             tc_ptr->pool_index(), 
//             tc_ptr->view(), 
//             common::Encode::HexEncode(msg_hash).c_str(), 
//             pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
// #endif
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto& msg_ptr = pro_msg_wrap->msg_ptr;
    auto& pro_msg = msg_ptr->header.hotstuff().pro_msg();
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    ZJC_INFO("HandleProposeMsgStep_VerifyQC called hash: %lu, "
        "view_block_hash: %s, propose_debug: %s, sign x: %s",
        msg_ptr->header.hash64(), 
        common::Encode::HexEncode(pro_msg.tc().view_block_hash()).c_str(),
        ProtobufToJson(cons_debug).c_str(),
        common::Encode::HexEncode(pro_msg.tc().sign_x()).c_str());
#endif
    if (pro_msg.has_tc() && pro_msg.tc().has_view_block_hash() && IsQcTcValid(pro_msg.tc())) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        if (VerifyQC(pro_msg.tc()) != Status::kSuccess) {
            ZJC_INFO("pool: %d verify qc failed: %lu", pool_idx_, pro_msg.tc().view());
            return Status::kError;
        }

        ADD_DEBUG_PROCESS_TIMESTAMP();
        ZJC_INFO("success new set qc view: %lu, %u_%u_%lu",
            pro_msg.tc().view(),
            pro_msg.tc().network_id(),
            pro_msg.tc().pool_index(),
            pro_msg.tc().view());
        pacemaker()->NewQcView(pro_msg.tc().view());
        ADD_DEBUG_PROCESS_TIMESTAMP();
        view_block_chain()->UpdateHighViewBlock(pro_msg.tc());
        ADD_DEBUG_PROCESS_TIMESTAMP();
        TryCommit(msg_ptr, pro_msg.tc(), 99999999lu);
        if (latest_qc_item_ptr_ == nullptr ||
                pro_msg.tc().view() >= latest_qc_item_ptr_->view()) {
            assert(IsQcTcValid(pro_msg.tc()));
            latest_qc_item_ptr_ = std::make_shared<view_block::protobuf::QcItem>(pro_msg.tc());
        }

        ADD_DEBUG_PROCESS_TIMESTAMP();
// #ifndef NDEBUG
//         auto msg_hash = GetQCMsgHash(pro_msg.tc());
//         auto* tc_ptr = &pro_msg.tc();
//         ZJC_WARN("HandleProposeMsgStep_VerifyQC success verify qc %u_%u_%lu, hash: %s, "
//             "view block hash: %s, sign x: %s called hash: %lu, propose_debug: %s",
//             tc_ptr->network_id(), 
//             tc_ptr->pool_index(), 
//             tc_ptr->view(), 
//             common::Encode::HexEncode(msg_hash).c_str(), 
//             common::Encode::HexEncode(tc_ptr->view_block_hash()).c_str(), 
//             common::Encode::HexEncode(tc_ptr->sign_x()).c_str(), 
//             pro_msg_wrap->msg_ptr->header.hash64(),
//             pro_msg_wrap->msg_ptr->header.debug().c_str());
// #endif
    }
    
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    ZJC_INFO("HandleProposeMsgStep_VerifyViewBlock called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto* tc = &pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc();
    if (VerifyViewBlock(
            *pro_msg_wrap->view_block_ptr,
            view_block_chain(),
            tc,
            pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().elect_height()) != Status::kSuccess) {
        ZJC_INFO("pool: %d, Verify ViewBlock is error. hash: %s, hash64: %lu, pool now: %s", pool_idx_,
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            view_block_chain_->String().c_str());
        return Status::kError;
    }
    
#ifndef NDEBUG
    ZJC_INFO("====1.1 pool: %d, verify view block success, view: %lu, "
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
    ZJC_INFO("HandleProposeMsgStep_Directly called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), 
        ProtobufToJson(cons_debug).c_str());
#endif
    // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->view_block_ptr->mutable_block_info()->clear_tx_list();
    auto balance_map_ptr = std::make_shared<BalanceAndNonceMap>();
    auto& balance_map = *balance_map_ptr;
    auto zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    auto btime = common::TimeUtils::TimestampMs();
    zjcvm::ZjchainHost prev_zjc_host;
    zjcvm::ZjchainHost& zjc_host = *zjc_host_ptr;
    if (acceptor()->Accept(
            pro_msg_wrap, 
            true, 
            true, 
            balance_map,
            zjc_host) != Status::kSuccess) {
        ZJC_INFO("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }

    auto etime = common::TimeUtils::TimestampMs();
    ZJC_INFO("====1.1.2 success Accept pool: %d, verify view block, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, use time: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            (etime - btime));
    ZJC_INFO("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() || 
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()));
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }
    
    // 6 add view block
#ifndef NDEBUG
    ZJC_INFO("store v block pool: %u, hash: %s, prehash: %s, %u_%u_%lu, propose_debug: %s",
        pool_idx_,
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(),
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(), ProtobufToJson(cons_debug).c_str());
#endif
    if (expect_view_block_hash != pro_msg_wrap->view_block_ptr->qc().view_block_hash()) {
        ZJC_INFO("invalid parent hash: %s, %s",
            common::Encode::HexEncode(expect_view_block_hash).c_str(),
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str());
        return Status::kNotExpectHash;
    }

    Status s = view_block_chain()->Store(pro_msg_wrap->view_block_ptr, true, balance_map_ptr, zjc_host_ptr, false);
    ZJC_INFO("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        s,
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size());
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu",
            pool_idx_, 
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->view_block_ptr->qc().network_id(),
            pro_msg_wrap->view_block_ptr->qc().pool_index(),
            pro_msg_wrap->view_block_ptr->qc().view());
        // 父块不存在，则加入等待队列，后续处理
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { // 父块缺失触发同步
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

    ZJC_INFO("HandleProposeMsgStep_TxAccept called hash: %lu, view hash: %s, "
        "propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        ProtobufToJson(cons_debug).c_str());
#endif
    // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->acc_balance_and_nonce_map_ptr = std::make_shared<BalanceAndNonceMap>();
    auto& balance_and_nonce_map = *pro_msg_wrap->acc_balance_and_nonce_map_ptr;
    pro_msg_wrap->zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    auto btime = common::TimeUtils::TimestampMs();
    zjcvm::ZjchainHost prev_zjc_host;
    zjcvm::ZjchainHost& zjc_host = *pro_msg_wrap->zjc_host_ptr;
    Status s = acceptor()->Accept(
        pro_msg_wrap, 
        true, 
        false, 
        balance_and_nonce_map,
        zjc_host);
    if (s != Status::kSuccess) {
#ifndef NDEBUG
        ZJC_INFO("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s, status: %d",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            ProtobufToJson(cons_debug).c_str());
#endif
        return Status::kError;
    }

#ifndef NDEBUG
    auto etime = common::TimeUtils::TimestampMs();
    ZJC_INFO("====1.1.2 success Accept pool: %d, verify view block, "
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

    ZJC_INFO("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d, propose_debug: %s", 
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
        pro_msg_wrap->zjc_host_ptr,
        false);
#ifndef NDEBUG
    ZJC_INFO("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u, propose_debug: %s",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        s,
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (s != Status::kSuccess) {
#ifndef NDEBUG
        ZJC_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu, propose_debug: %s",
            pool_idx_, 
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->view_block_ptr->qc().network_id(),
            pro_msg_wrap->view_block_ptr->qc().pool_index(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            ProtobufToJson(cons_debug).c_str());
#endif
        // 父块不存在，则加入等待队列，后续处理
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { // 父块缺失触发同步
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

    // NOTICE: pipeline 重试时，protobuf 结构体被析构，因此 pro_msg_wrap->header.hash64() 是 0
    ZJC_INFO("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, "
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
    assert(pro_msg_wrap->view_block_ptr->qc().elect_height() > 0);
    trans_header.set_debug(pro_msg_wrap->msg_ptr->header.debug());
    // Construct VoteMsg
    Status s = ConstructVoteMsg(
        msg_ptr,
        vote_msg, 
        pro_msg_wrap->view_block_ptr->qc().elect_height(), 
        pro_msg_wrap->view_block_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, ConstructVoteMsg error %d, hash64: %lu",
            pool_idx_, s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    // Construct HotstuffMessage and send
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (SendMsgToLeader(pro_msg_wrap->leader, trans_msg, VOTE) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Send vote message is error.",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!pro_msg_wrap->msg_ptr->is_leader) {
        // 避免对 view 重复投票
        voted_msgs_[pro_msg_wrap->view_block_ptr->qc().view()] = trans_msg;
        auto iter = voted_msgs_.begin();
        auto riter = voted_msgs_.rbegin();
        if (iter->first + 16 < riter->first) {
            voted_msgs_.erase(iter);
        }
        
        CHECK_MEMORY_SIZE(voted_msgs_);
        ZJC_INFO("pool: %d, Send vote message is success., hash64: %lu, "
            "last_vote_view_: %lu, send to leader tx size: %u, last_vote_view_: %lu",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            vote_msg->txs_size(),
            last_vote_view_);
        StopVoting(pro_msg_wrap->view_block_ptr->qc().view());
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    has_user_tx_tag_ = false;
    return Status::kSuccess;
}

Status Hotstuff::VerifyFollower(const transport::MessagePtr& msg_ptr) {
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    auto member = leader_rotation_->GetMember(vote_msg.replica_idx());
    if (!member) {
        return Status::kError;
    }

    if (member->backup_ecdh_key.empty()) {
        if (crypto_->security()->GetEcdhKey(
                member->pubkey,
                &member->backup_ecdh_key) != security::kSecuritySuccess) {
            ZJC_INFO("verify follower get ecdh key failed: %s", 
                common::Encode::HexEncode(member->id).c_str());
            return Status::kError;
        }
    }
    
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        msg_ptr->header);
    std::string decrypt_msg;
    if (crypto_->security()->Decrypt(
            msg_ptr->header.ecdh_encrypt(), 
            member->backup_ecdh_key, 
            &decrypt_msg) != security::kSecuritySuccess) {
        ZJC_INFO("verify follower encrypt failed: %s", 
            common::Encode::HexEncode(member->id).c_str());
        return Status::kError;
    }

    if (memcmp(decrypt_msg.c_str(), msg_hash.c_str(), msg_hash.size()) != 0) {
        ZJC_INFO("verify follower encrypt failed: %s", 
            common::Encode::HexEncode(member->id).c_str());
        return Status::kError;
    }

    return Status::kSuccess;
}

void Hotstuff::HandleVoteMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto b = common::TimeUtils::TimestampMs();
    if (VerifyFollower(msg_ptr) != Status::kSuccess) {
        return;
    }
    
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    // acceptor()->AddTxs(msg_ptr, vote_msg.txs());
    if (vote_msg.txs_size() > 0) {
        hotstuff_mgr_.ConsensusAddTxsMessage(msg_ptr);
        ZJC_INFO("tps vote from follower tx size: %u", vote_msg.txs_size());
    }

    if (prefix_db_->BlockExists(vote_msg.view_block_hash())) {
        return;
    }

    std::string followers_gids;
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
    ZJC_INFO("====2.0 pool: %d, onVote, hash: %s, view: %lu, "
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
        ZJC_INFO("vote message is error: hash64: %lu", msg_ptr->header.hash64());
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_INFO("====2.1 pool: %d, onVote, hash: %s, view: %lu, hash64: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        msg_ptr->header.hash64());

    // 同步 replica 的 txs
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();

    ADD_DEBUG_PROCESS_TIMESTAMP();
#ifdef USE_AGG_BLS
    auto qc_item_ptr = std::make_shared<QC>();
    QC& qc_item = *qc_item_ptr;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(vote_msg.view());
    qc_item.set_view_block_hash(vote_msg.view_block_hash());
    assert(!prefix_db_->BlockExists(qc_item.view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(vote_msg.leader_idx());
    auto qc_hash = GetQCMsgHash(qc_item);

    AggregateSignature partial_sig, agg_sig;
    if (!partial_sig.LoadFromProto(vote_msg.partial_sig())) {
        return;
    }
    
    Status ret = crypto()->VerifyAndAggregateSig(
            elect_height,
            vote_msg.view(),
            qc_hash,
            partial_sig,
            agg_sig);
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_INFO("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        return;
    }    

    ZJC_INFO("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, ",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        agg_sig.IsValid(),
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        vote_msg.replica_idx());
    qc_item.mutable_agg_sig()->CopyFrom(agg_sig.DumpToProto());
    // 切换视图
    ZJC_INFO("success new set qc view: %lu, %u_%u_%lu",
        qc_item.view(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());    
#else
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    auto qc_item_ptr = std::make_shared<QC>();
    QC& qc_item = *qc_item_ptr;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(vote_msg.view());
    qc_item.set_view_block_hash(vote_msg.view_block_hash());
    assert(!prefix_db_->BlockExists(qc_item.view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(vote_msg.leader_idx());
    auto qc_hash = GetQCMsgHash(qc_item);
    ZJC_INFO("success set view block hash: %s, qc_hash: %s, %u_%u_%lu",
        common::Encode::HexEncode(qc_item.view_block_hash()).c_str(),
        common::Encode::HexEncode(qc_hash).c_str(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());
    ADD_DEBUG_PROCESS_TIMESTAMP();
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
        ZJC_WARN("invalid opposed count: %u_%u_%lu", qc_item.network_id(), qc_item.pool_index(), qc_item.view());
    }
    // assert(ret != Status::kInvalidOpposedCount); 有可能由于状态不一致临时出现
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_INFO("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        ZJC_INFO("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
            pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
        return;
    }

#ifndef NDEBUG
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_INFO("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, ",
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
    // 切换视图
    ZJC_INFO("success new set qc view: %lu, %u_%u_%lu",
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
#endif

    view_block_chain()->UpdateHighViewBlock(qc_item);
    pacemaker()->NewQcView(qc_item.view());
    // 先单独广播新 qc，即是 leader 出不了块也不用额外同步 HighQC，这比 Gossip 的效率:q高很多
    ZJC_INFO("NewView propose newview called pool: %u, qc_view: %lu, tc_view: %lu, propose_debug: %s",
        pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), pacemaker()->HighTC()->view(),
        "ProtobufToJson(cons_debug).c_str()");
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto s = Propose(qc_item_ptr, nullptr, msg_ptr);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

Status Hotstuff::StoreVerifiedViewBlock(
        const std::shared_ptr<ViewBlock>& v_block, 
        const std::shared_ptr<QC>& qc) {
    if (view_block_chain()->Has(qc->view_block_hash())) {
        return Status::kSuccess;    
    }

    if (v_block->qc().view_block_hash() != qc->view_block_hash() || v_block->qc().view() != qc->view()) {
        return Status::kError;
    }

    Status s = acceptor()->AcceptSync(*v_block);
    if (s != Status::kSuccess) {
        return s;
    }

    transport::MessagePtr msg_ptr;
    TryCommit(msg_ptr, *qc, 99999999lu);
    ZJC_INFO("success store v block pool: %u, hash: %s, prehash: %s",
        pool_idx_,
        common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block->parent_hash()).c_str());
    // TODO: check valid
    return view_block_chain()->Store(v_block, true, nullptr, nullptr, false);
}

void Hotstuff::HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& pre_rst_timer_msg = msg_ptr->header.hotstuff().pre_reset_timer_msg();
    if (pre_rst_timer_msg.txs_size() == 0 && !pre_rst_timer_msg.has_single_tx()) {
        ZJC_INFO("pool: %d has proposed!", pool_idx_);
        return;
    }

#ifndef NDEBUG
    std::string gids;
    for (uint32_t i = 0; i < pre_rst_timer_msg.txs_size(); ++i) {
        gids += std::to_string(pre_rst_timer_msg.txs(i).nonce()) + " ";
    }

    ZJC_WARN("pool: %u, reset timer get follower tx gids: %s", pool_idx_, gids.c_str());
#endif

    if (pre_rst_timer_msg.txs_size() > 0) {
        ZJC_INFO("tps reset from follower tx size: %u", pre_rst_timer_msg.txs_size());
        hotstuff_mgr_.ConsensusAddTxsMessage(msg_ptr);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // TODO: Flow Control
    if (latest_qc_item_ptr_ != nullptr) {
        ZJC_INFO("reset timer propose message called view: %lu",
            latest_qc_item_ptr_->view());
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        ZJC_INFO("reset timer failed, now_tm_ms < latest_propose_msg_tm_ms_ + "
            "kLatestPoposeSendTxToLeaderPeriodMs: %lu",
            (latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs - now_tm_ms));
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    Propose(nullptr, nullptr, msg_ptr);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_INFO("reset timer success!");
}

Status Hotstuff::TryCommit(
        const transport::MessagePtr& msg_ptr, 
        const QC& commit_qc, 
        uint64_t test_index) {
    assert(commit_qc.has_view_block_hash());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto v_block_to_commit_info = CheckCommit(commit_qc);
    if (v_block_to_commit_info) {
        auto v_block_to_commit = v_block_to_commit_info->view_block;
// #ifndef NDEBUG
//         transport::protobuf::ConsensusDebug cons_debug;
//         cons_debug.ParseFromString(v_block_to_commit->debug());
//         ZJC_INFO("commit tx size: %u, propose_debug: %s", 
//             v_block_to_commit->block_info().tx_list_size(), 
//             ProtobufToJson(cons_debug).c_str());
// #endif
        ADD_DEBUG_PROCESS_TIMESTAMP();
        Status s = Commit(msg_ptr, v_block_to_commit_info, commit_qc, test_index);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->qc().view(),
                common::Encode::HexEncode(v_block_to_commit->qc().view_block_hash()).c_str());
            return s;
        }
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    return Status::kSuccess;
}

std::shared_ptr<ViewBlockInfo> Hotstuff::CheckCommit(const QC& qc) {
    // fast hotstuff
    assert(!qc.view_block_hash().empty());
    auto v_block1_info = view_block_chain()->Get(qc.view_block_hash());
    if (!v_block1_info) {
        ZJC_INFO("Failed get v block 1: %s, %u_%u_%lu",
            common::Encode::HexEncode(qc.view_block_hash()).c_str(),
            qc.network_id(), qc.pool_index(), qc.view());
        if (!view_block_chain()->view_commited(qc.network_id(), qc.view())) {
            kv_sync_->AddSyncViewHash(qc.network_id(), qc.pool_index(), qc.view_block_hash(), 0);
        }
        // assert(false);
        return nullptr;
    }

    if (view_block_chain_->ViewBlockIsCheckedParentHash(qc.view_block_hash())) {
        return v_block1_info;
    }

    auto v_block1 = v_block1_info->view_block;
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(v_block1->debug());
    ZJC_INFO("success get v block 1: %s, %u_%u_%lu, propose_debug: %s",
        common::Encode::HexEncode(qc.view_block_hash()).c_str(),
        qc.network_id(), qc.pool_index(), qc.view(), ProtobufToJson(cons_debug).c_str());
#endif
    assert(v_block1->parent_hash() != qc.view_block_hash());
    auto v_block2_info = view_block_chain()->Get(v_block1->parent_hash());
    if (!v_block2_info) {
        ZJC_INFO("Failed get v block 2 block hash: %s, %u_%u_%lu, now chain: %s", 
            common::Encode::HexEncode(v_block1->parent_hash()).c_str(), 
            qc.network_id(), 
            qc.pool_index(), 
            v_block1->qc().view() - 1,
            view_block_chain_->String().c_str());
        if (v_block1->qc().view() > 0 && !view_block_chain()->view_commited(
                v_block1->qc().network_id(), v_block1->qc().view() - 1)) {
            kv_sync_->AddSyncViewHash(qc.network_id(), qc.pool_index(), v_block1->parent_hash(), 0);
        }
        return nullptr;
    }

    auto v_block2 = v_block2_info->view_block;
    if (v_block2->qc().view() + 1 != v_block1->qc().view()) {
        ZJC_INFO("Failed get v block 2 ref: %s, "
            "v_block2->qc().view() + 1 != v_block1->qc().view(): %lu, %lu",
            common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
            v_block2->qc().view(), 
            v_block1->qc().view());
        return nullptr;
    }

#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug2;
    cons_debug2.ParseFromString(v_block2->debug());
    ZJC_INFO("success get v block 2: %s, %u_%u_%lu, propose_debug: %s",
        common::Encode::HexEncode(v_block2->qc().view_block_hash()).c_str(),
        v_block2->qc().network_id(), v_block2->qc().pool_index(), 
        v_block2->qc().view(), ProtobufToJson(cons_debug2).c_str());
#endif
    if (!view_block_chain()->LatestLockedBlock() ||
            v_block2->qc().view() > view_block_chain()->LatestLockedBlock()->qc().view()) {
        view_block_chain()->SetLatestLockedBlock(v_block2);
    }

    auto v_block3_info = view_block_chain()->Get(v_block2->parent_hash());
    if (!v_block3_info) {
        ZJC_INFO("Failed get v block 3 block hash: %s, %u_%u_%lu, now chain: %s", 
            common::Encode::HexEncode(v_block2->parent_hash()).c_str(), 
            qc.network_id(), 
            qc.pool_index(), 
            v_block2->qc().view() - 1,
            view_block_chain_->String().c_str());
        if (v_block2->qc().view() > 0 && !view_block_chain()->view_commited(
                v_block2->qc().network_id(), v_block2->qc().view() - 1)) {
            kv_sync_->AddSyncViewHash(qc.network_id(), qc.pool_index(), v_block2->parent_hash(), 0);
        }
        return nullptr;
    }
    
    auto v_block3 = v_block3_info->view_block;
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug3;
    cons_debug3.ParseFromString(v_block2->debug());
    ZJC_INFO("success get v block views: %lu, %lu, %lu, hash: %s, %s, %s, %s, %s, now: %s, propose_debug: %s",
        v_block1->qc().view(),
        v_block2->qc().view(),
        v_block3->qc().view(),
        common::Encode::HexEncode(v_block1->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block2->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block2->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block3->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(qc.view_block_hash()).c_str(),
        ProtobufToJson(cons_debug3).c_str());
#endif
    // fast hotstuff
    if (v_block3->qc().view() + 1 != v_block2->qc().view()) {
        ZJC_INFO("Failed get v block 2 ref: %s, "
            "v_block3->qc().view() + 1 != v_block2->qc().view(): %lu, %lu",
            common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
            v_block3->qc().view(),
            v_block2->qc().view());
        return nullptr;
    }

    return v_block3_info;
}

Status Hotstuff::Commit(
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlockInfo>& v_block_info,
        const QC& commit_qc,
        uint64_t test_index) {
    view_block_chain_->Commit(v_block_info);
    return Status::kSuccess;
}

void Hotstuff::HandleSyncedViewBlock(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& vblock) {
    if (!view_block_chain_->ReplaceWithSyncedBlock(vblock)) {
        ZJC_INFO("block hash exists %u_%u_%lu, height: %lu",
            vblock->qc().network_id(), 
            vblock->qc().pool_index(), 
            vblock->qc().view(), 
            vblock->block_info().height());
        return;
    }

    if (prefix_db_->BlockExists(vblock->qc().view_block_hash())) {
        ZJC_INFO("block db exists %u_%u_%lu, height: %lu",
            vblock->qc().network_id(), 
            vblock->qc().pool_index(), 
            vblock->qc().view(), 
            vblock->block_info().height());
        return;
    }
    
    ZJC_INFO("now handle synced view block %u_%u_%lu, height: %lu",
        vblock->qc().network_id(),
        vblock->qc().pool_index(),
        vblock->qc().view(),
        vblock->block_info().height());
    if (network::IsSameToLocalShard(vblock->qc().network_id())) {
        auto elect_item = elect_info()->GetElectItem(
                vblock->qc().network_id(),
                vblock->qc().elect_height());
        if (elect_item && elect_item->IsValid()) {
            elect_item->consensus_stat(pool_idx_)->Commit(vblock);
        }
        
        pacemaker_->NewQcView(vblock->qc().view());
        // auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
        // if (!latest_committed_block ||
        //         latest_committed_block->qc().view() < vblock->qc().view()) {
        //     view_block_chain()->SetLatestCommittedBlock(vblock);        
        // }

        // TODO: fix balance map and storage map
        view_block_chain()->Store(vblock, true, nullptr, nullptr, false);
        view_block_chain()->UpdateHighViewBlock(vblock->qc());
        transport::MessagePtr msg_ptr;
        if (latest_qc_item_ptr_ == nullptr ||
                vblock->qc().view() >= latest_qc_item_ptr_->view()) {

            if (IsQcTcValid(vblock->qc())) {
                latest_qc_item_ptr_ = std::make_shared<view_block::protobuf::QcItem>(vblock->qc());
            }
        }
        TryCommit(msg_ptr, *latest_qc_item_ptr_, 99999999lu);
        TryCommit(msg_ptr, vblock->qc(), 99999999lu);
    } else {
        view_block_chain()->CommitSynced(vblock);
    }
}

Status Hotstuff::VerifyQC(const QC& qc) {
    // 验证 qc
    if (!IsQcTcValid(qc)) {
        assert(false);
        return Status::kError;
    }

    if (qc.view() < view_block_chain()->HighViewBlock()->qc().view()) {        
        return Status::kError;
    }

    if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
        ZJC_ERROR("pool: %d verify qc failed: %lu", pool_idx_, qc.view());
        // assert(false);
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
        ZJC_ERROR("qc not exist.");
        return Status::kError;
    }

    if (v_block.block_info().height() <= view_block_chain->LatestCommittedBlock()->block_info().height()) {
        ZJC_ERROR("new view block height error: %lu, last commited block height: %lu", 
            v_block.block_info().height(),
            view_block_chain->LatestCommittedBlock()->block_info().height());
        return Status::kError;
    }

    // fast hotstuff condition
    auto qc_view_block_info = view_block_chain->Get(v_block.parent_hash());
    if (!qc_view_block_info) {
        ZJC_ERROR("get qc prev view block message is error: %s, sync parent view: %u_%u_%lu",
            common::Encode::HexEncode(v_block.parent_hash()).c_str(),
            v_block.qc().network_id(), 
            v_block.qc().pool_index(), 
            v_block.qc().view() - 1);
        if (view_block_chain->HighQC().view() < (v_block.qc().view() + db_stored_view_) && 
                v_block.qc().view() > 0 && 
                !view_block_chain->view_commited(
                    v_block.qc().network_id(), v_block.qc().view() - 1)) {
            kv_sync_->AddSyncViewHash(
                v_block.qc().network_id(), 
                v_block.qc().pool_index(), 
                v_block.parent_hash(),
                0);
        } else if (!view_block_chain->view_commited(
                v_block.qc().network_id(), v_block.qc().view() - 1)) {
            ZJC_INFO("now add sync height 0, %u_%u_%lu", 
                v_block.qc().network_id(), 
                v_block.qc().pool_index(), 
                v_block.block_info().height() - 1);
            kv_sync_->AddSyncHeight(
                v_block.qc().network_id(), 
                v_block.qc().pool_index(), 
                v_block.block_info().height() - 1,
                0);
        }
        
        return Status::kError;
    }

    auto qc_view_block = qc_view_block_info->view_block;
    if ((v_block.qc().view() + 1) >= pacemaker()->CurView() && 
            v_block.qc().view() == qc_view_block->qc().view() + 1) {
        return Status::kSuccess;
    }

    ZJC_ERROR("pool: %d, block view message is error. %lu, %lu, %s, %s, "
        "v_block.qc().view(): %lu, pacemaker()->CurView(): %lu, "
        "v_block.qc().view(): %lu, qc_view_block->qc().view(): %lu",
        pool_idx_, v_block.qc().view(), view_block_chain->LatestLockedBlock()->qc().view(),
        common::Encode::HexEncode(view_block_chain->LatestLockedBlock()->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block.parent_hash()).c_str(),
        v_block.qc().view(), pacemaker()->CurView(), 
        v_block.qc().view(), qc_view_block->qc().view());

    return Status::kError;
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    if (vote_msg.view() < view_block_chain()->HighViewBlock()->qc().view()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status Hotstuff::VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    if (pro_msg_wrap->leader) {
        return Status::kSuccess;
    }

    auto leader = leader_rotation()->GetLeader(); // 判断是否为空
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
        return Status::kError;
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        pro_msg_wrap->msg_ptr->header);
    if (crypto_->security()->Verify(
            msg_hash,
            leader->pubkey,
            pro_msg_wrap->msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_INFO("verify leader sign failed: %s", 
            common::Encode::HexEncode(leader->id).c_str());
        return Status::kError;
    }
    
    pro_msg_wrap->leader = leader;
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(
        const transport::MessagePtr& msg_ptr, 
        hotstuff::protobuf::ProposeMsg* pro_msg) {
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kElectItemNotFound;
    }

    auto new_view_block = pro_msg->mutable_view_item();
    auto* tx_propose = pro_msg->mutable_tx_propose();
    Status s = ConstructViewBlock(msg_ptr, new_view_block, tx_propose);
    if (s != Status::kSuccess) {
        ZJC_INFO("pool: %d construct view block failed, view: %lu, %d, member_index: %d",
            pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), s, 
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
        const std::shared_ptr<ViewBlock>& v_block) {
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

    ZJC_INFO("success set view block hash: %s, %u_%u_%lu",
        common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str(),
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        v_block->qc().view());
    assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    vote_msg->set_view(v_block->qc().view());
    vote_msg->set_elect_height(elect_height);
    vote_msg->set_leader_idx(v_block->qc().leader_idx());
    QC qc_item;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(v_block->qc().view());
    qc_item.set_view_block_hash(v_block->qc().view_block_hash());
    ZJC_INFO("success set view block hash: %s, %u_%u_%lu",
        common::Encode::HexEncode(qc_item.view_block_hash()).c_str(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());
    assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(v_block->qc().leader_idx());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto qc_hash = GetQCMsgHash(qc_item);
#ifdef USE_AGG_BLS
    AggregateSignature partial_sig;
    if (crypto()->PartialSign(
                common::GlobalInfo::Instance()->network_id(),
                elect_height,
                qc_hash,
                &partial_sig) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }

    vote_msg->mutable_partial_sig()->CopyFrom(partial_sig.DumpToProto());
#else
    std::string sign_x, sign_y;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (crypto()->PartialSign(
                common::GlobalInfo::Instance()->network_id(),
                elect_height,
                qc_hash,
                &sign_x,
                &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }

    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_y(sign_y);
#endif
    if (!msg_ptr->is_leader) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        auto* txs = vote_msg->mutable_txs();
        wrapper()->GetTxSyncToLeader(
            v_block->qc().leader_idx(), 
            view_block_chain_, 
            view_block_chain_->HighQC().view_block_hash(), 
            txs);
        if (txs->size() > 0)
        ZJC_INFO("tps now vote message get tx sync to leader: %d", txs->size());
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    
    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock(
        const transport::MessagePtr& msg_ptr, 
        ViewBlock* view_block,
        hotstuff::protobuf::TxPropose* tx_propose) {
    auto local_elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (local_elect_item == nullptr) {
        ZJC_INFO("pool index: %d, local_elect_item == nullptr", pool_idx_);
        return Status::kError;
    }

    auto local_member = local_elect_item->LocalMember();
    if (local_member == nullptr) {
        ZJC_INFO("pool index: %d, local_member == nullptr", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto leader_idx = local_member->index;
    auto pre_v_block = view_block_chain()->HighViewBlock();
    auto* qc = view_block->mutable_qc();
    qc->set_leader_idx(leader_idx);
    qc->set_view(pre_v_block->qc().view() + 1);
    qc->set_network_id(common::GlobalInfo::Instance()->network_id());
    qc->set_pool_index(pool_idx_);
    view_block->set_parent_hash(pre_v_block->qc().view_block_hash());
    ZJC_INFO("get prev block hash: %s, height: %lu", 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        pre_v_block->block_info().height());
    // TODO 如果单分支最多连续打包三个默认交易
    auto s = wrapper()->Wrap(
        msg_ptr,
        pre_v_block, 
        leader_idx, 
        view_block, 
        tx_propose, 
        IsEmptyBlockAllowed(*pre_v_block), 
        view_block_chain_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        ZJC_INFO("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        view_block->release_qc();
        return s;
    }

    ZJC_INFO("success check is empty block allowd: %d, %u_%u_%lu, "
        "tx size: %u, cur view: %lu, pre view: %lu, last_vote_view_: %lu",
        pool_idx_, view_block->qc().network_id(), 
        view_block->qc().pool_index(), view_block->qc().view(),
        tx_propose->txs_size(),
        qc->view(),
        pre_v_block->qc().view(),
        last_vote_view_);
    // TODO 有问题，由于 qc.view 的含义变更为本次 view 而非上一个视图的 view
    // 因此 CurView 此时还没有增加，还是上一次投票的 View，正常来说此时 last_vote_view_ == pacemaker()->CurView()
    if (last_vote_view_ > pacemaker()->CurView()) {
        // assert(last_vote_view_ <= pacemaker()->CurView());
        view_block->release_qc();
        return Status::kError;
    }

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
    auto* v_block1 = &v_block;
    if (!v_block1 || v_block1->block_info().tx_list_size() > 0) {
        ZJC_INFO("!v_block1 || v_block1->block_info().tx_list_size() > 0");
        return true;
    }

    auto v_block2_info = view_block_chain()->Get(v_block.parent_hash());
    if (!v_block2_info) {
        ZJC_INFO("!v_block2_info: %s, %u_%u_%lu", 
            common::Encode::HexEncode(v_block.parent_hash()),
            v_block.qc().network_id(), v_block.qc().pool_index(), v_block.qc().view());
        return true;
    }

    auto v_block2 = v_block2_info->view_block;
    if (!v_block2 || v_block2->block_info().tx_list_size() > 0) {
        ZJC_INFO("v_block2 || v_block2->block_info().tx_list_size() > 0 %s, %u_%u_%lu", 
            common::Encode::HexEncode(v_block.parent_hash()),
            v_block.qc().network_id(), v_block.qc().pool_index(), v_block.qc().view());

        return true;
    }

    // fast hotstuff
    auto v_block3_info = view_block_chain()->Get(v_block2->parent_hash());
    if (!v_block3_info) {
        ZJC_INFO("v_block2 || v_block2->block_info().tx_list_size() > 0 %s, %u_%u_%lu", 
            common::Encode::HexEncode(v_block2->parent_hash()),
            v_block2->qc().network_id(), v_block2->qc().pool_index(), v_block2->qc().view());
        return true;
    }

    auto v_block3 = v_block3_info->view_block;
    if (!v_block3 || v_block3->block_info().tx_list_size() > 0) {
        ZJC_INFO("!v_block3 || v_block3->block_info().tx_list_size() > 0 %s, %u_%u_%lu", 
            common::Encode::HexEncode(v_block2->parent_hash()),
            v_block2->qc().network_id(), v_block2->qc().pool_index(), v_block2->qc().view());
        return true;
    }

    // ZJC_INFO("failed check is empty block allowd block1: %u_%u_%lu, %s, block2: %u_%u_%lu, %s, block3: %u_%u_%lu, %s",
    //     v_block1->qc().network_id(),
    //     v_block1->qc().pool_index(),
    //     v_block1->qc().view(),
    //     common::Encode::HexEncode(v_block1->qc().view_block_hash()).c_str(),
    //     v_block2->qc().network_id(),
    //     v_block2->qc().pool_index(),
    //     v_block2->qc().view(),
    //     common::Encode::HexEncode(v_block2->qc().view_block_hash()).c_str(),
    //     v_block3->qc().network_id(),
    //     v_block3->qc().pool_index(),
    //     v_block3->qc().view(),
    //     common::Encode::HexEncode(v_block3->qc().view_block_hash()).c_str());
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
            ZJC_INFO("verify leader sign failed: %s", 
                common::Encode::HexEncode(leader->id).c_str());
            return Status::kError;
        }
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(
        header_msg);
    std::string crypt_msg;
    if (crypto_->security()->Encrypt(
            msg_hash, 
            leader->leader_ecdh_key, 
            &crypt_msg)!= security::kSecuritySuccess) {
        ZJC_INFO("send to leader encrypt failed: %s", 
            common::Encode::HexEncode(leader->id).c_str());
        return Status::kError;
    }

    trans_msg->header.set_ecdh_encrypt(crypt_msg);

    auto local_idx = leader_rotation_->GetLocalMemberIdx();
    if (leader->index != local_idx) {
        if (leader->public_ip == 0 || leader->public_port == 0) {
            network::Route::Instance()->Send(trans_msg);
        } else {
            transport::TcpTransport::Instance()->Send(
                common::Uint32ToIp(leader->public_ip), 
                leader->public_port, 
                header_msg);
        }
    } else {
        transport::TcpTransport::Instance()->AddLocalMessage(trans_msg);
        ZJC_INFO("2 success add local message: %lu", trans_msg->header.hash64());
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
//             ZJC_WARN("pool index: %u, send to leader %d message to leader net: %u, %s, "
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

    ZJC_INFO("pool index: %u, send to leader %d message to leader net: %u, %s, "
        "hash64: %lu, %s:%d, leader->index: %d, local_idx: %d",
        pool_idx_,
        msg_type,
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
    // if (!latest_qc_item_ptr_) {
    //     ZJC_WARN("latest_qc_item_ptr_ null, pool: %u", pool_idx_);
    //     return;
    // }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (has_user_tx) {
        has_user_tx_tag_ = true;
    }

    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        // ZJC_INFO("leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32, pool: %u", pool_idx_);
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (now_tm_ms >= prev_sync_latest_view_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        prev_sync_latest_view_tm_ms_ = now_tm_ms;
    } else {
        if (!has_user_tx_tag_ && !has_system_tx) {
            return;
        }
    }

    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        // ZJC_WARN("pool: %u now_tm_ms < latest_propose_msg_tm_ms_ + "
        //     "kLatestPoposeSendTxToLeaderPeriodMs: %lu, %lu",
        //     pool_idx_, now_tm_ms, 
        //     (latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs));
        return;
    }

    // auto stuck_st = IsStuck();
    // if (stuck_st != 0) {
    //     if (stuck_st != 1) {
    //         ZJC_INFO("pool: %u stuck_st != 0: %d", pool_idx_, stuck_st);
    //     }
    //     return;
    // }

    auto leader = leader_rotation()->GetLeader();
    if (!leader) {
        // ZJC_INFO("no leader");
        return;
    }
    
    auto local_idx = leader_rotation_->GetLocalMemberIdx();
    if (leader && leader->index == local_idx) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        Propose(nullptr, nullptr, msg_ptr);
        ADD_DEBUG_PROCESS_TIMESTAMP();
        if (latest_qc_item_ptr_) {
            ZJC_INFO("leader do propose message: %d, pool index: %u, %u_%u_%lu", 
                local_idx,
                pool_idx_,
                latest_qc_item_ptr_->network_id(), 
                latest_qc_item_ptr_->pool_index(), 
                latest_qc_item_ptr_->view());
        }

        if (latest_propose_msg_tm_ms_ > prev_sync_latest_view_tm_ms_) {
            prev_sync_latest_view_tm_ms_ = latest_propose_msg_tm_ms_;
        }

        return;
    }

    if (!has_user_tx_tag_) {
        // ZJC_INFO("pool: %u not has_user_tx_tag_.", pool_idx_);
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // ZJC_INFO("now timeout reset get tx sync to leader.");
    // 存在内置交易或普通交易时尝试 reset timer
    // TODO 发送 PreResetPacemakerTimerMsg To Leader
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& header = trans_msg->header;
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pre_rst_timer_msg = hotstuff_msg->mutable_pre_reset_timer_msg();
    auto* txs = pre_rst_timer_msg->mutable_txs();
    wrapper()->GetTxSyncToLeader(
        leader->index, 
        view_block_chain_, 
        view_block_chain_->HighQC().view_block_hash(), 
        txs);
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs->empty()) {
        // ZJC_INFO("pool: %u txs.empty().", pool_idx_);
        return;
    }
    
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        ZJC_ERROR("pool: %d no elect item found", pool_idx_);
        return;
    }
    
    pre_rst_timer_msg->set_replica_idx(elect_item->LocalMember()->index);
    pre_rst_timer_msg->set_has_single_tx(has_system_tx);
    hotstuff_msg->set_type(PRE_RESET_TIMER);
    hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg->set_pool_index(pool_idx_);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    SendMsgToLeader(leader, trans_msg, PRE_RESET_TIMER);
    ZJC_INFO("pool: %d, send prereset msg from: %lu to: %lu, has_single_tx: %d, tx size: %u, hash: %lu",
        pool_idx_, pre_rst_timer_msg->replica_idx(), 
        leader_rotation_->GetLeader()->index, has_system_tx, txs->size(),
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

    ZJC_INFO("pool: %d add succ num: %lu, leader: %lu", pool_idx_, ret, v_block->qc().leader_idx());
    return ret;
}

} // namespace consensus

} // namespace shardora