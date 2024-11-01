#include <common/encode.h>
#include <common/log.h>
#include <common/defer.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/types.h>
#include <protos/hotstuff.pb.h>
#include <protos/pools.pb.h>
#include <protos/view_block.pb.h>

namespace shardora {

namespace hotstuff {

void Hotstuff::Init() {
    // set pacemaker timeout callback function
    last_vote_view_ = 0lu;
    auto latest_view_block = std::make_shared<ViewBlock>();
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block);
    if (s == Status::kSuccess) {
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        view_block_chain_->SetLatestCommittedBlock(latest_view_block);
        InitAddNewViewBlock(latest_view_block);
        LoadAllViewBlockWithLatestCommitedBlock(latest_view_block);
    } else {
        ZJC_DEBUG("no genesis, waiting for syncing, pool_idx: %d", pool_idx_);
    }

    InitHandleProposeMsgPipeline();
    LoadLatestProposeMessage();
}

void Hotstuff::LoadAllViewBlockWithLatestCommitedBlock(
        std::shared_ptr<ViewBlock>& view_block) {
    std::vector<std::shared_ptr<ViewBlock>> children_view_blocks;
    prefix_db_->GetChildrenViewBlock(
        view_block->qc().view_block_hash(), 
        children_view_blocks);
    ZJC_DEBUG("init load view block %u_%u_%lu, %lu, hash: %s, phash: %s, size: %u",
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), view_block->block_info().height(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str(),
        children_view_blocks.size());
    for (auto iter = children_view_blocks.begin(); iter != children_view_blocks.end(); ++iter) {
        assert(!view_block_chain_->Has((*iter)->qc().view_block_hash()));
        InitAddNewViewBlock(*iter);
        LoadAllViewBlockWithLatestCommitedBlock(*iter);
    }
}
    
void Hotstuff::InitAddNewViewBlock(std::shared_ptr<ViewBlock>& latest_view_block) {
    ZJC_DEBUG("pool: %d, latest vb from db, vb view: %lu",
            pool_idx_, 
            latest_view_block->qc().view());
        // 初始状态，使用 db 中最后一个 view_block 初始化视图链
        // TODO: check valid
        view_block_chain_->Store(latest_view_block, true, nullptr, nullptr);
        view_block_chain_->UpdateHighViewBlock(latest_view_block->qc());
        StopVoting(latest_view_block->qc().view());
        // 开启第一个视图
        ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu, hash: %s",
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
        Propose(nullptr, nullptr);
    }
    return Status::kSuccess;
}

Status Hotstuff::Propose(
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc) {

    // TODO(HT): 打包的交易，超时后如何释放？
    // 打包参与共识中的交易，如何保证幂等
    auto pre_v_block = view_block_chain()->HighViewBlock();
    if (!pre_v_block) {
        ZJC_DEBUG("pool %u not has prev view block.", pool_idx_);
        return Status::kError;
    }

    auto dht_ptr = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht_ptr) {
        ZJC_DEBUG("pool %u not has dht ptr.", pool_idx_);
        return Status::kError;
    }

    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    if (readobly_dht->size() < 2) {
        ZJC_DEBUG("pool %u not has dreadobly_dht->size() < 2", pool_idx_);
        return Status::kError;
    }

    latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    if (tc != nullptr) {
        if (latest_qc_item_ptr_ == nullptr || tc->view() >= latest_qc_item_ptr_->view()) {
            assert(tc->pool_index() == pool_idx_);
            assert(tc->network_id() == common::GlobalInfo::Instance()->network_id());
            assert(tc->has_sign_x() && !tc->sign_x().empty());
            latest_qc_item_ptr_ = tc;
        }

        if (latest_leader_propose_message_ && 
                latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() <= tc->view()) {
            latest_leader_propose_message_ = nullptr;
        }
    }

    if (latest_leader_propose_message_ && 
            latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view() >= pacemaker_->CurView()) {
        latest_leader_propose_message_->header.release_broadcast();
        auto broadcast = latest_leader_propose_message_->header.mutable_broadcast();
        auto* hotstuff_msg = latest_leader_propose_message_->header.mutable_hotstuff();
        if (tc != nullptr) {
            auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
            *pb_pro_msg->mutable_tc() = *tc;
        }

        transport::TcpTransport::Instance()->SetMessageHash(latest_leader_propose_message_->header);
        auto s = crypto()->SignMessage(latest_leader_propose_message_);
        auto& header = latest_leader_propose_message_->header;
        if (s != Status::kSuccess) {
            ZJC_ERROR("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
                pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
            return s;
        }

        latest_leader_propose_message_ = latest_leader_propose_message_;
        network::Route::Instance()->Send(latest_leader_propose_message_);
        ZJC_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
            "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s, msg view: %lu, cur view: %lu",
            pool_idx_,
            header.hotstuff().pool_index(),
            hotstuff_msg->pro_msg().tx_propose().txs_size(),
            hotstuff_msg->pro_msg().view_item().qc().view(),
            common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            header.hash64(),
            header.debug().c_str(),
            latest_leader_propose_message_->header.hotstuff().pro_msg().view_item().qc().view(),
            pacemaker_->CurView());
        HandleProposeMsg(latest_leader_propose_message_);
        return s;
    }

    ZJC_DEBUG("1 now ontime called propose: %d", pool_idx_);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
    Status s = ConstructProposeMsg(pb_pro_msg);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("pool: %d construct propose msg failed, %d",
            pool_idx_, s);
        return s;
    }

    s = ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
        return s;
    }

    if (tc != nullptr) {
        *pb_pro_msg->mutable_tc() = *tc;
    }

    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

#ifndef NDEBUG
    std::string propose_debug_str = common::StringUtil::Format(
        "%u-%u-%lu", 
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        propose_debug_index_++);
    propose_debug_str += ", tx gids: ";
    for (uint32_t tx_idx = 0; tx_idx < pb_pro_msg->tx_propose().txs_size(); ++tx_idx) {
        propose_debug_str += common::Encode::HexEncode(pb_pro_msg->tx_propose().txs(tx_idx).gid()) + " ";
    }

    header.set_debug(propose_debug_str);
    ZJC_DEBUG("leader begin propose_debug: %s", header.debug().c_str());
#endif
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    s = crypto()->SignMessage(msg_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
        return s;
    }

    latest_leader_propose_message_ = msg_ptr;
    SaveLatestProposeMessage();
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        header.hotstuff().pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().qc().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        header.hash64(),
        header.debug().c_str());
    if (tc != nullptr && tc->has_sign_x()) {
        ZJC_DEBUG("new prev qc coming: %s, %u_%u_%lu, parent hash: %s, tx size: %u, view: %lu",
            common::Encode::HexEncode(tc->view_block_hash()).c_str(), 
            tc->network_id(), 
            tc->pool_index(), 
            pb_pro_msg->view_item().block_info().height(), 
            "", 
            pb_pro_msg->tx_propose().txs_size(),
            tc->view());
    }

    msg_ptr->is_leader = true;
    HandleProposeMsg(msg_ptr);
    return Status::kSuccess;
}

void Hotstuff::SaveLatestProposeMessage() {
    prefix_db_->SaveLatestLeaderProposeMessage(latest_leader_propose_message_->header);
}

void Hotstuff::LoadLatestProposeMessage() {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    if (prefix_db_->GetLatestLeaderProposeMessage(
            common::GlobalInfo::Instance()->network_id(), 
            pool_idx_, 
            &msg_ptr->header)) {
        msg_ptr->is_leader = true;
        latest_leader_propose_message_ = msg_ptr;
    }
}

void Hotstuff::NewView(
        std::shared_ptr<tnet::TcpInterface> conn,
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> qc) {
    if (latest_qc_item_ptr_ == nullptr || tc->view() >= latest_qc_item_ptr_->view()) {
        assert(tc->pool_index() == pool_idx_);
        assert(tc->network_id() == common::GlobalInfo::Instance()->network_id());
        if (tc->has_sign_x() && !tc->sign_x().empty()) {
            latest_qc_item_ptr_ = tc;
        }
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_newview_msg = hotstuff_msg->mutable_newview_msg();
    auto elect_item = elect_info_->GetElectItemWithShardingId(
            common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        return;
    }

    pb_newview_msg->set_elect_height(elect_item->ElectHeight());
    if (tc == nullptr) {
        return;
    }

    if (tc != nullptr) {
        *pb_newview_msg->mutable_tc() = *tc;
    }

    Status s = ConstructHotstuffMsg(NEWVIEW, nullptr, nullptr, pb_newview_msg, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().qc().view());
        return;
    }
    
    header.mutable_hotstuff()->CopyFrom(*hotstuff_msg);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }
    
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    if (conn) {
        header.release_broadcast();
        transport::TcpTransport::Instance()->Send(conn.get(), msg_ptr->header);
    } else {
        network::Route::Instance()->Send(msg_ptr);
    }

    ZJC_DEBUG("pool: %d, msg pool: %d, newview, txs size: %lu, view: %lu, "
        "hash: %s, qc_view: %lu, tc_view: %lu hash64: %lu",
        pool_idx_,
        hotstuff_msg->pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().qc().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        pacemaker()->HighTC()->view(),
        header.hash64());
    HandleNewViewMsg(msg_ptr);
    return;    
}

void Hotstuff::HandleProposeMsg(const transport::MessagePtr& msg_ptr) {
    latest_propose_msg_tm_ms_ = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("handle propose called hash: %lu, %u_%u_%lu, "
        "view block hash: %s, sign x: %s, propose_debug: %s", 
        msg_ptr->header.hash64(), 
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().network_id(), 
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().pool_index(),
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().view(),
        common::Encode::HexEncode(
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(
        msg_ptr->header.hotstuff().pro_msg().view_item().qc().sign_x()).c_str(),
        msg_ptr->header.debug().c_str());
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        ZJC_DEBUG("pool: %d handle propose duration: %lu ms", pool_idx_, e-b);
    });

    auto pro_msg_wrap = std::make_shared<ProposeMsgWrapper>(msg_ptr);
    pro_msg_wrap->view_block_ptr = std::make_shared<ViewBlock>(
        msg_ptr->header.hotstuff().pro_msg().view_item());
    pro_msg_wrap->view_block_ptr->set_debug(msg_ptr->header.debug());
    ZJC_DEBUG("handle new propose message parent hash: %s, %u_%u_%lu, "
        "hash64: %lu, block timestamp: %lu, propose_debug: %s",
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(), 
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        msg_ptr->header.hash64(),
        pro_msg_wrap->view_block_ptr->block_info().timestamp(),
        msg_ptr->header.debug().c_str());
    assert(pro_msg_wrap->view_block_ptr->block_info().tx_list_size() == 0);
    if (msg_ptr->header.hotstuff().pro_msg().has_tc()) {
        HandleTC(pro_msg_wrap);
    }

    auto& view_item = *pro_msg_wrap->view_block_ptr;
    ZJC_DEBUG("HandleProposeMessageByStep called hash: %lu, "
        "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, view_item.qc().view(),
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    auto st = HandleProposeMsgStep_HasVote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
        return;
    }

    auto propose_view = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc().view();
    View handled_view = 0;
    for (auto iter = leader_view_with_propose_msgs_.begin();
            iter != leader_view_with_propose_msgs_.end();) {
        if (iter->first > propose_view) {
            assert(false);
            break;
        }

        auto& rehandle_view_item = *pro_msg_wrap->view_block_ptr;
        ZJC_DEBUG("rehandle propose message begin HandleProposeMessageByStep called hash: %lu, "
            "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
            iter->second->msg_ptr->header.hash64(), last_vote_view_, rehandle_view_item.qc().view(),
            iter->second->msg_ptr->header.debug().c_str());
        auto st = HandleProposeMessageByStep(iter->second);
        if (st != Status::kSuccess) {
            ZJC_ERROR("handle propose message failed hash: %lu, propose_debug: %s",
                msg_ptr->header.hash64(),
                msg_ptr->header.debug().c_str());
            break;
        }

        ZJC_DEBUG("rehandle propose message success HandleProposeMessageByStep called hash: %lu, "
            "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
            iter->second->msg_ptr->header.hash64(), last_vote_view_, rehandle_view_item.qc().view(),
            iter->second->msg_ptr->header.debug().c_str());
        iter = leader_view_with_propose_msgs_.erase(iter);
    }
    
    HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
    st = HandleProposeMessageByStep(pro_msg_wrap);
    if (st != Status::kSuccess) {
        ZJC_ERROR("handle propose message failed hash: %lu, propose_debug: %s",
            msg_ptr->header.hash64(),
            msg_ptr->header.debug().c_str());
        leader_view_with_propose_msgs_[propose_view] = pro_msg_wrap;
    } else {
        for (auto iter = leader_view_with_propose_msgs_.begin();
                iter != leader_view_with_propose_msgs_.end();) {
            if (iter->first > propose_view) {
                break;
            }

            iter = leader_view_with_propose_msgs_.erase(iter);
        }
    }
}

Status Hotstuff::HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> pro_msg_wrap) {
    auto st = HandleProposeMsgStep_VerifyLeader(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    st = HandleProposeMsgStep_VerifyViewBlock(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    st = HandleProposeMsgStep_TxAccept(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    st = HandleProposeMsgStep_ChainStore(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    st = HandleProposeMsgStep_Vote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    return Status::kSuccess;
}


Status Hotstuff::HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    ZJC_DEBUG("HandleProposeMsgStep_HasVote called hash: %lu, "
        "last_vote_view_: %lu, view_item.qc().view(): %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), last_vote_view_, view_item.qc().view(),
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    if (last_vote_view_ >= view_item.qc().view()) {
        ZJC_DEBUG("pool: %d has voted view: %lu, last_vote_view_: %u, "
            "hash64: %lu, pacemaker()->CurView(): %lu",
            pool_idx_, view_item.qc().view(),
            last_vote_view_, pro_msg_wrap->msg_ptr->header.hash64(),
            pacemaker()->CurView());
        if (last_vote_view_ == view_item.qc().view()) {
            auto iter = voted_msgs_.find(view_item.qc().view());
            if (iter != voted_msgs_.end()) {
                ZJC_DEBUG("pool: %d has voted: %lu, last_vote_view_: %u, "
                    "hash64: %lu and resend vote: hash: %s",
                    pool_idx_, view_item.qc().view(),
                    last_vote_view_, pro_msg_wrap->msg_ptr->header.hash64(),
                    common::Encode::HexEncode(iter->second->header.hotstuff().vote_msg().view_block_hash()).c_str());
                if (SendMsgToLeader(iter->second, VOTE) != Status::kSuccess) {
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
    ZJC_DEBUG("HandleProposeMsgStep_VerifyLeader called hash: %lu, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    if (VerifyLeader(view_item.qc().leader_idx()) != Status::kSuccess) {
        // TODO 一旦某个节点状态滞后，那么 Leader 就与其他 replica 不同，导致无法处理新提案
        // 只能依赖同步，但由于同步慢于新的 Propose 消息
        // 即是这里再加一次同步，也很难追上 Propose 的速度，导致该节点掉队，因此还是需要一个队列缓存一下
        // 暂时无法处理的 Propose 消息
        if (sync_pool_fn_) { // leader 不一致触发同步
            sync_pool_fn_(pool_idx_, 1);
        }

        ZJC_ERROR("verify leader failed, pool: %d view: %lu, hash64: %lu", 
            pool_idx_, view_item.qc().view(), pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }        
    return Status::kSuccess;
}

Status Hotstuff::HandleTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 3 Verify TC
    ZJC_DEBUG("HandleTC called hash: %lu, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    std::shared_ptr<TC> tc = nullptr;
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    if (pro_msg.has_tc() && !pro_msg.tc().has_view_block_hash()) {
        if (VerifyTC(pro_msg.tc()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify tc failed: %lu", pool_idx_, pro_msg.view_item().qc().view());
            assert(false);
            return Status::kError;
        }

        auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(pro_msg.tc());
        pacemaker()->NewTc(tc_ptr);
        if (latest_qc_item_ptr_ == nullptr ||
                tc_ptr->view() >= latest_qc_item_ptr_->view()) {
            assert(tc_ptr->has_sign_x() && !tc_ptr->sign_x().empty());
            latest_qc_item_ptr_ = tc_ptr;
        }
#ifndef NDEBUG
        auto msg_hash = GetTCMsgHash(pro_msg.tc());
        ZJC_DEBUG("HandleTC success verify tc %u_%u_%lu, hash: %s called hash: %lu, propose_debug: %s",
            tc_ptr->network_id(), 
            tc_ptr->pool_index(), 
            tc_ptr->view(), 
            common::Encode::HexEncode(msg_hash).c_str(), 
            pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
#endif
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    ZJC_DEBUG("HandleProposeMsgStep_VerifyQC called hash: %lu, view_block_hash: %s, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), 
        common::Encode::HexEncode(pro_msg.tc().view_block_hash()).c_str(),
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    if (pro_msg.has_tc() && pro_msg.tc().has_view_block_hash() && pro_msg.tc().has_sign_x()) {
        if (VerifyQC(pro_msg.tc()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify qc failed: %lu", pool_idx_, pro_msg.tc().view());
            assert(false);
            return Status::kError;
        }

        ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu",
            pro_msg.tc().view(),
            pro_msg.tc().network_id(),
            pro_msg.tc().pool_index(),
            pro_msg.tc().view());
        pacemaker()->NewQcView(pro_msg.tc().view());
        view_block_chain()->UpdateHighViewBlock(pro_msg.tc());
        TryCommit(pro_msg.tc(), 99999999lu);
        if (latest_qc_item_ptr_ == nullptr ||
                pro_msg.tc().view() >= latest_qc_item_ptr_->view()) {
            assert(pro_msg.tc().has_sign_x() && !pro_msg.tc().sign_x().empty());
            latest_qc_item_ptr_ = std::make_shared<view_block::protobuf::QcItem>(pro_msg.tc());
        }
#ifndef NDEBUG
        auto msg_hash = GetQCMsgHash(pro_msg.tc());
        auto* tc_ptr = &pro_msg.tc();
        ZJC_DEBUG("HandleProposeMsgStep_VerifyQC success verify qc %u_%u_%lu, hash: %s, "
            "view block hash: %s, sign x: %s called hash: %lu, propose_debug: %s",
            tc_ptr->network_id(), 
            tc_ptr->pool_index(), 
            tc_ptr->view(), 
            common::Encode::HexEncode(msg_hash).c_str(), 
            common::Encode::HexEncode(tc_ptr->view_block_hash()).c_str(), 
            common::Encode::HexEncode(tc_ptr->sign_x()).c_str(), 
            pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->msg_ptr->header.debug().c_str());
#endif
    }
    
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 4 Verify ViewBlock
    ZJC_DEBUG("HandleProposeMsgStep_VerifyViewBlock called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
    auto* tc = &pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc();
    if (VerifyViewBlock(
            *pro_msg_wrap->view_block_ptr,
            view_block_chain(),
            tc,
            pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().elect_height()) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Verify ViewBlock is error. hash: %s, hash64: %lu", pool_idx_,
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    
    ZJC_DEBUG("====1.1 pool: %d, verify view block success, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        pro_msg_wrap->view_block_ptr->qc().view(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        view_block_chain()->HighViewBlock()->qc().view(),
        pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());        
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_Directly(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        const std::string& expect_view_block_hash) {
    ZJC_DEBUG("HandleProposeMsgStep_Directly called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), 
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->view_block_ptr->mutable_block_info()->clear_tx_list();
    auto balance_map_ptr = std::make_shared<BalanceMap>();
    auto& balance_map = *balance_map_ptr;
    auto zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    zjcvm::ZjchainHost prev_zjc_host;
    zjcvm::ZjchainHost& zjc_host = *zjc_host_ptr;
    zjc_host.prev_zjc_host_ = &prev_zjc_host;
    if (acceptor()->Accept(
            view_block_chain_, 
            pro_msg_wrap, 
            true, 
            true, 
            balance_map,
            zjc_host) != Status::kSuccess) {
        ZJC_WARN("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }

    ZJC_DEBUG("====1.1.2 success Accept pool: %d, verify view block, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64());
    ZJC_DEBUG("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() || 
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()));
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }
    
    // 6 add view block
    ZJC_DEBUG("store v block pool: %u, hash: %s, prehash: %s, %u_%u_%lu, propose_debug: %s",
        pool_idx_,
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(),
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(), pro_msg_wrap->msg_ptr->header.debug().c_str());
    if (expect_view_block_hash != pro_msg_wrap->view_block_ptr->qc().view_block_hash()) {
        ZJC_DEBUG("invalid parent hash: %s, %s",
            common::Encode::HexEncode(expect_view_block_hash).c_str(),
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str());
        return Status::kNotExpectHash;
    }

    Status s = view_block_chain()->Store(pro_msg_wrap->view_block_ptr, true, balance_map_ptr, zjc_host_ptr);
    ZJC_DEBUG("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u",
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

    // 成功接入链中，标记交易占用
    acceptor()->MarkBlockTxsAsUsed(pro_msg_wrap->view_block_ptr->block_info());
    return Status::kSuccess;    
}

Status Hotstuff::HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    ZJC_DEBUG("HandleProposeMsgStep_TxAccept called hash: %lu, propose_debug: %s", pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
    // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto& proto_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    pro_msg_wrap->acc_balance_map_ptr = std::make_shared<BalanceMap>();
    auto& balance_map = *pro_msg_wrap->acc_balance_map_ptr;
    pro_msg_wrap->zjc_host_ptr = std::make_shared<zjcvm::ZjchainHost>();
    zjcvm::ZjchainHost prev_zjc_host;
    zjcvm::ZjchainHost& zjc_host = *pro_msg_wrap->zjc_host_ptr;
    if (acceptor()->Accept(
            view_block_chain_, 
            pro_msg_wrap, 
            true, 
            false, 
            balance_map,
            zjc_host) != Status::kSuccess) {
        ZJC_WARN("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->msg_ptr->header.debug().c_str());
        return Status::kError;
    }

    ZJC_DEBUG("====1.1.2 success Accept pool: %d, verify view block, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
            pool_idx_,
            proto_msg.view_item().qc().view(),
            common::Encode::HexEncode(proto_msg.view_item().qc().view_block_hash()).c_str(),
            view_block_chain()->HighViewBlock()->qc().view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->msg_ptr->header.debug().c_str());
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    ZJC_DEBUG("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() || 
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()), pro_msg_wrap->msg_ptr->header.debug().c_str());
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }
    
    // 6 add view block
    Status s = view_block_chain()->Store(
        pro_msg_wrap->view_block_ptr, 
        false, 
        pro_msg_wrap->acc_balance_map_ptr,
        pro_msg_wrap->zjc_host_ptr);
    ZJC_DEBUG("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u, propose_debug: %s",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
        s,
        pro_msg_wrap->view_block_ptr->qc().network_id(),
        pro_msg_wrap->view_block_ptr->qc().pool_index(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu, propose_debug: %s",
            pool_idx_, 
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->qc().view_block_hash()).c_str(),
            pro_msg_wrap->view_block_ptr->qc().network_id(),
            pro_msg_wrap->view_block_ptr->qc().pool_index(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            pro_msg_wrap->msg_ptr->header.debug().c_str());
        // 父块不存在，则加入等待队列，后续处理
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { // 父块缺失触发同步
            sync_pool_fn_(pool_idx_, 1);
        }

        return Status::kError;
    }

    // 成功接入链中，标记交易占用
    acceptor()->MarkBlockTxsAsUsed(pro_msg_wrap->view_block_ptr->block_info());
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    ZJC_DEBUG("HandleProposeMsgStep_Vote called hash: %lu", pro_msg_wrap->msg_ptr->header.hash64());
    // NOTICE: pipeline 重试时，protobuf 结构体被析构，因此 pro_msg_wrap->header.hash64() 是 0
    ZJC_DEBUG("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, "
        "curView: %lu, vblock: %lu, txs: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        view_block_chain()->HighViewBlock()->qc().view(),
        pacemaker()->HighTC()->view(),
        view_block_chain()->Size(),
        pacemaker()->CurView(),
        pro_msg_wrap->view_block_ptr->qc().view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& trans_header = trans_msg->header;
    trans_header.set_debug(pro_msg_wrap->msg_ptr->header.debug());
    auto* hotstuff_msg = trans_header.mutable_hotstuff();
    auto* vote_msg = hotstuff_msg->mutable_vote_msg();
    assert(pro_msg_wrap->view_block_ptr->qc().elect_height() > 0);
    // Construct VoteMsg
    Status s = ConstructVoteMsg(
        vote_msg, 
        pro_msg_wrap->view_block_ptr->qc().elect_height(), 
        pro_msg_wrap->view_block_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, ConstructVoteMsg error %d, hash64: %lu",
            pool_idx_, s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    // Construct HotstuffMessage and send
    s = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, ConstructHotstuffMsg error %d, hash64: %lu",
            pool_idx_, s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    
    if (SendMsgToLeader(trans_msg, VOTE) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Send vote message is error.",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
    }

    if (!pro_msg_wrap->msg_ptr->is_leader) {
        // 避免对 view 重复投票
        voted_msgs_[pro_msg_wrap->view_block_ptr->qc().view()] = trans_msg;
        ZJC_DEBUG("pool: %d, Send vote message is success., hash64: %lu, "
            "last_vote_view_: %lu, send to leader tx size: %u, last_vote_view_: %lu",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->view_block_ptr->qc().view(),
            vote_msg->txs_size(),
            last_vote_view_);
        StopVoting(pro_msg_wrap->view_block_ptr->qc().view());
    }
    
    has_user_tx_tag_ = false;
    return Status::kSuccess;
}

void Hotstuff::HandleVoteMsg(const transport::MessagePtr& msg_ptr) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d handle vote duration: %lu ms", pool_idx_, e-b);
        });
    
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    std::string followers_gids;

#ifndef NDEBUG
    for (uint32_t i = 0; i < vote_msg.txs_size(); ++i) {
        followers_gids += common::Encode::HexEncode(vote_msg.txs(i).gid()) + " ";
    }
#endif

    ZJC_DEBUG("====2.0 pool: %d, onVote, hash: %s, view: %lu, "
        "local high view: %lu, replica: %lu, hash64: %lu, propose_debug: %s, followers_gids: %s",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        view_block_chain()->HighViewBlock()->qc().view(),
        vote_msg.replica_idx(),
        msg_ptr->header.hash64(),
        msg_ptr->header.debug().c_str(),
        followers_gids.c_str());
    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        ZJC_WARN("vote message is error: hash64: %lu", msg_ptr->header.hash64());
        return;
    }

    ZJC_DEBUG("====2.1 pool: %d, onVote, hash: %s, view: %lu, hash64: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        msg_ptr->header.hash64());

    // 同步 replica 的 txs
    acceptor()->AddTxs(vote_msg.txs());
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();

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
            ZJC_DEBUG("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        return;
    }

    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, ",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        agg_sig->IsValid(),
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        msg_ptr->header.debug().c_str(),
        vote_msg.replica_idx());
    qc_item.mutable_agg_sig()->CopyFrom(agg_sig->DumpToProto());
    // 切换视图
    ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu",
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
    Status ret = crypto()->ReconstructAndVerifyThresSign(
            elect_height,
            vote_msg.view(),
            qc_hash,
            replica_idx, 
            vote_msg.sign_x(),
            vote_msg.sign_y(),
            reconstructed_sign);
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_DEBUG("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        return;
    }

    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, ",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        reconstructed_sign == nullptr,
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        msg_ptr->header.debug().c_str(),
        vote_msg.replica_idx());
    qc_item.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->X));
    qc_item.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->Y));
    // 切换视图
    ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu",
        qc_item.view(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());
#endif
    
    view_block_chain()->UpdateHighViewBlock(qc_item);
    pacemaker()->NewQcView(qc_item.view());
    // 先单独广播新 qc，即是 leader 出不了块也不用额外同步 HighQC，这比 Gossip 的效率:q高很多
    ZJC_DEBUG("NewView propose newview called pool: %u, qc_view: %lu, tc_view: %lu, propose_debug: %s",
        pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), pacemaker()->HighTC()->view(), msg_ptr->header.debug().c_str());
    auto s = Propose(qc_item_ptr, nullptr);
    if (s != Status::kSuccess) {
        NewView(nullptr, qc_item_ptr, nullptr);
    }
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

    TryCommit(v_block->qc(), 99999999lu);
    ZJC_DEBUG("success store v block pool: %u, hash: %s, prehash: %s",
        pool_idx_,
        common::Encode::HexEncode(v_block->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block->parent_hash()).c_str());
    // TODO: check valid
    return view_block_chain()->Store(v_block, true, nullptr, nullptr);
}

void Hotstuff::HandleNewViewMsg(const transport::MessagePtr& msg_ptr) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d HandleNewViewMsg duration: %lu ms, hash64: %lu",
                pool_idx_, e-b, msg_ptr->header.hash64());
        });

    static uint64_t test_index = 0;
    ++test_index;
    ZJC_DEBUG("====3.1 pool: %d, newview, message pool: %d, hash64: %lu, test_index: %lu",
        pool_idx_, msg_ptr->header.hotstuff().pool_index(), msg_ptr->header.hash64(), test_index);
    assert(msg_ptr->header.hotstuff().pool_index() == pool_idx_);
    auto& newview_msg = msg_ptr->header.hotstuff().newview_msg();
    if (newview_msg.has_tc()) {
        auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(newview_msg.tc());
        auto& tc = *tc_ptr;
        ZJC_DEBUG("pool index: %u,  0 test_index: %lu, %lu, %lu",
            pool_idx_, test_index, tc.view() > pacemaker()->HighTC()->view());
        if (tc.view() > pacemaker()->HighTC()->view()) {
            if (!tc.has_view_block_hash()) {
                auto tc_msg_hash = GetTCMsgHash(tc);
                ZJC_DEBUG("newview now verify tc hash: %s, pool index: %u", 
                    common::Encode::HexEncode(tc_msg_hash).c_str(), pool_idx_);                
                if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
                    ZJC_ERROR("VerifyTC error.");
                    return;
                }

                ZJC_DEBUG("====3.2 pool: %d, tc: %lu, onNewview", pool_idx_, tc.view());
                ZJC_DEBUG("pool index: %u,  1 test_index: %lu", pool_idx_, test_index);
                pacemaker()->NewTc(tc_ptr);
                ZJC_DEBUG("pool index: %u,  2 test_index: %lu", pool_idx_, test_index);
            } else {
                auto& qc = tc;
                if (qc.view() > view_block_chain()->HighViewBlock()->qc().view()) {
                    ZJC_DEBUG("pool index: %u,  4 test_index: %lu", pool_idx_, test_index);
                    if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
                        ZJC_ERROR("VerifyQC error.");
                        return;
                    }

                    ZJC_DEBUG("====3.3 pool: %d, qc: %lu, onNewview, test_index: %lu",
                        pool_idx_, qc.view(), test_index);
                    ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu",
                        qc.view(),
                        qc.network_id(),
                        qc.pool_index(),
                        qc.view());
                    pacemaker()->NewQcView(qc.view());
                    ZJC_DEBUG("pool index: %u,  5 test_index: %lu", pool_idx_, test_index);
                    ZJC_DEBUG("pool index: %u,  6 test_index: %lu", pool_idx_, test_index);
                }
            }
        }
            
        if (tc.has_view_block_hash()) {
            auto& qc = tc;
            pacemaker()->NewQcView(qc.view());
            view_block_chain()->UpdateHighViewBlock(qc);
            TryCommit(qc, 99999999lu);
            if (latest_qc_item_ptr_ == nullptr ||
                    qc.view() >= latest_qc_item_ptr_->view()) {
                if (qc.has_sign_x() && !qc.sign_x().empty()) {
                    latest_qc_item_ptr_ = std::make_shared<view_block::protobuf::QcItem>(qc);
                }
            }

            #ifndef NDEBUG
            auto msg_hash = GetQCMsgHash(qc);
            auto* tc_ptr = &qc;
            ZJC_DEBUG("HandleProposeMsgStep_VerifyQC success verify qc %u_%u_%lu, hash: %s, "
                "view block hash: %s, sign x: %s called hash: %lu, propose_debug: %s",
                tc_ptr->network_id(), 
                tc_ptr->pool_index(), 
                tc_ptr->view(), 
                common::Encode::HexEncode(msg_hash).c_str(), 
                common::Encode::HexEncode(tc_ptr->view_block_hash()).c_str(), 
                common::Encode::HexEncode(tc_ptr->sign_x()).c_str(), 
                msg_ptr->header.hash64(),
                msg_ptr->header.debug().c_str());
    #endif
        }
    }
}

void Hotstuff::HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr) {
    auto& pre_rst_timer_msg = msg_ptr->header.hotstuff().pre_reset_timer_msg();
    if (pre_rst_timer_msg.txs_size() == 0 && !pre_rst_timer_msg.has_single_tx()) {
        ZJC_DEBUG("pool: %d has proposed!", pool_idx_);
        return;
    }

#ifndef NDEBUG
    std::string gids;
    for (uint32_t i = 0; i < pre_rst_timer_msg.txs_size(); ++i) {
        gids += common::Encode::HexEncode(pre_rst_timer_msg.txs(i).gid()) + " ";
    }

    ZJC_DEBUG("pool: %u, reset timer get follower tx gids: %s", pool_idx_, gids.c_str());
#endif

    if (pre_rst_timer_msg.txs_size() > 0) {
        Status s = acceptor()->AddTxs(pre_rst_timer_msg.txs());
        if (s != Status::kSuccess) {
            ZJC_DEBUG("reset timer failed, add txs failed");
            return;
        }
    }

    // TODO: Flow Control
    if (latest_qc_item_ptr_ != nullptr) {
        ZJC_DEBUG("reset timer propose message called view: %lu",
            latest_qc_item_ptr_->view());
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        ZJC_DEBUG("reset timer failed, now_tm_ms < latest_propose_msg_tm_ms_ + "
            "kLatestPoposeSendTxToLeaderPeriodMs: %lu",
            (latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs - now_tm_ms));
        return;
    }

    Propose(latest_qc_item_ptr_, nullptr);
    ZJC_DEBUG("reset timer success!");
}

Status Hotstuff::TryCommit(const QC& commit_qc, uint64_t test_index) {
    assert(commit_qc.has_view_block_hash());
    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        ZJC_DEBUG("pool: %d TryCommit duration: %lu ms", pool_idx_, e-b);
    });

    auto v_block_to_commit = CheckCommit(commit_qc);
    if (v_block_to_commit) {
        ZJC_DEBUG("commit tx size: %u, propose_debug: %s", 
            v_block_to_commit->block_info().tx_list_size(), 
            v_block_to_commit->debug().c_str());
        Status s = Commit(v_block_to_commit, commit_qc, test_index);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s, propose_debug: %s",
                v_block_to_commit->qc().view(),
                common::Encode::HexEncode(v_block_to_commit->qc().view_block_hash()).c_str(), 
                v_block_to_commit->debug().c_str());
            return s;
        }
    }
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> Hotstuff::CheckCommit(const QC& qc) {
    auto v_block1 = view_block_chain()->Get(qc.view_block_hash());
    if (!v_block1) {
        ZJC_WARN("Failed get v block 1: %s, %u_%u_%lu",
            common::Encode::HexEncode(qc.view_block_hash()).c_str(),
            qc.network_id(), qc.pool_index(), qc.view());
        // assert(false);
        return nullptr;
    }

    ZJC_DEBUG("success get v block 1 propose_debug: %s", v_block1->debug().c_str());
    auto v_block2 = view_block_chain()->Get(v_block1->parent_hash());
    if (!v_block2) {
        ZJC_DEBUG("Failed get v block 2 ref: %s", common::Encode::HexEncode(v_block1->parent_hash()).c_str());
        return nullptr;
    }

    ZJC_DEBUG("success get v block 2 propose_debug: %s", v_block2->debug().c_str());
    if (!view_block_chain()->LatestLockedBlock() ||
            v_block2->qc().view() > view_block_chain()->LatestLockedBlock()->qc().view()) {
        view_block_chain()->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = view_block_chain()->Get(v_block2->parent_hash());
    if (!v_block3) {
        ZJC_DEBUG("Failed get v block 3 block hash: %s, %u_%u_%lu", 
            common::Encode::HexEncode(v_block2->parent_hash()).c_str(), 
            qc.network_id(), 
            qc.pool_index(), 
            v_block1->block_info().height());
        return nullptr;
    }

    ZJC_DEBUG("success get v block 3 propose_debug: %s", v_block3->debug().c_str());
    if (v_block1->parent_hash() == v_block2->qc().view_block_hash() &&
            v_block2->parent_hash() == v_block3->qc().view_block_hash()) {
        return v_block3;
    }
    
    ZJC_DEBUG("Failed get v block hash invalid: %s, %s, %s, %s",
        common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block2->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(v_block2->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block3->qc().view_block_hash()).c_str());
    return nullptr;
}

Status Hotstuff::Commit(
        const std::shared_ptr<ViewBlock>& v_block,
        const QC& commit_qc,
        uint64_t test_index) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d Commit duration: %lu ms, test_index: %lu, "
                "propose_debug: %s", pool_idx_, e-b, test_index, v_block->debug().c_str());
        });

    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->qc().view() >= v_block->qc().view()) {
        return Status::kSuccess;
    }
    
    auto tmp_block = v_block;
    while (tmp_block != nullptr) {
        auto db_batch = std::make_shared<db::DbWriteBatch>();
        auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(tmp_block, db_batch);
        view_block_chain()->StoreToDb(tmp_block, test_index, db_batch);
        CommitInner(tmp_block, test_index, queue_item_ptr);
        std::shared_ptr<ViewBlock> parent_block = nullptr;
        parent_block = view_block_chain()->Get(tmp_block->parent_hash());
        if (parent_block == nullptr) {
            break;
        }

        tmp_block = parent_block;
    }
    
    view_block_chain()->SetLatestCommittedBlock(v_block);
    // 剪枝
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    auto s = view_block_chain()->PruneTo(v_block->qc().view_block_hash(), forked_blockes, true);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, prune failed s: %d, vb view: &lu", pool_idx_, s, v_block->qc().view());
        return s;
    }

    // 归还分支交易
    for (const auto& forked_block : forked_blockes) {
        s = acceptor()->Return(forked_block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    return Status::kSuccess;
}

Status Hotstuff::VerifyQC(const QC& qc) {
    // 验证 qc
#ifdef USE_AGG_BLS
    if (!qc.has_agg_sig()) {
        assert(false);
        return Status::kError;
    }
#else
    if (!qc.has_sign_x() || !qc.has_sign_y()) {
        assert(false);
        return Status::kError;
    }
#endif
    if (qc.view() > view_block_chain()->HighViewBlock()->qc().view()) {        
        if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
            return Status::kError; 
        }
    }

    return Status::kSuccess;
}

Status Hotstuff::VerifyTC(const TC& tc) {
#ifdef USE_AGG_BLS    
    if (!tc.has_agg_sig()) {
        assert(false);
        return Status::kError;
    }
#else
    if (!tc.has_sign_x() || !tc.has_sign_y()) {
        assert(false);
        return Status::kError;
    }    
#endif

    if (tc.view() > pacemaker()->HighTC()->view()) {
        if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
            ZJC_ERROR("VerifyTC error.");
            return Status::kError;
        }
        auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(tc);
        pacemaker()->NewTc(tc_ptr);
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

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block = view_block_chain->Get(v_block.parent_hash());
    if (!qc_view_block) {
        ZJC_ERROR("get qc prev view block message is error: %s, sync parent view: %u_%u_%lu",
            common::Encode::HexEncode(v_block.parent_hash()).c_str(),
            v_block.qc().network_id(), 
            v_block.qc().pool_index(), 
            v_block.qc().view() - 1);
        kv_sync_->AddSyncViewHeight(
            v_block.qc().network_id(), 
            v_block.qc().pool_index(), 
            v_block.qc().view() - 1,
            0);
        return Status::kError;
    }

    if (!view_block_chain->Extends(v_block, *qc_view_block)) {
        ZJC_ERROR("extents qc view block message is error.");
        return Status::kError;
    }

    if (view_block_chain->LatestLockedBlock() &&
        !view_block_chain->Extends(v_block, *view_block_chain->LatestLockedBlock()) && 
            v_block.qc().view() <= view_block_chain->LatestLockedBlock()->qc().view()) {
        ZJC_ERROR("pool: %d, block view message is error. %lu, %lu, %s, %s",
            pool_idx_, v_block.qc().view(), view_block_chain->LatestLockedBlock()->qc().view(),
            common::Encode::HexEncode(view_block_chain->LatestLockedBlock()->qc().view_block_hash()).c_str(),
            common::Encode::HexEncode(v_block.parent_hash()).c_str());
        return Status::kError;
    }   

    return ret;
}

void Hotstuff::CommitInner(
        const std::shared_ptr<ViewBlock>& v_block, 
        uint64_t test_index, 
        std::shared_ptr<block::BlockToDbItem>& queue_block_item) {
    auto& block_info = v_block->block_info();
    ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu, propose_debug: %s",
        pool_idx_, 0, v_block->qc().view(),
        v_block->qc().network_id(), v_block->qc().pool_index(), block_info.height(),
        view_block_chain()->String().c_str(),
        test_index,
        v_block->debug().c_str());
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->qc().view() >= v_block->qc().view()) {
        ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit failed s: %d, "
            "vb view: %lu, %u_%u_%lu, latest_committed_block: %d, "
            "latest_committed_block->view: %lu, v_block->view: %lu, propose_debug: %s",
            pool_idx_, 0, v_block->qc().view(),
            v_block->qc().network_id(), v_block->qc().pool_index(), block_info.height(),
            (latest_committed_block != nullptr),
            latest_committed_block->qc().view(),
            v_block->qc().view(),
            v_block->debug().c_str());
        return;
    }

    if (!latest_committed_block && v_block->qc().view() == GenesisView) {
        ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit failed s: %d, "
            "vb view: %lu, %u_%u_%lu, propose_debug: %s",
            pool_idx_, 0, v_block->qc().view(),
            v_block->qc().network_id(), v_block->qc().pool_index(), 
            block_info.height(), v_block->debug().c_str());
        return;
    }

    ZJC_DEBUG("1 NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu, propose_debug: %s",
        pool_idx_, 0, v_block->qc().view(),
        v_block->qc().network_id(), v_block->qc().pool_index(), block_info.height(),
        view_block_chain()->String().c_str(),
        test_index, v_block->debug().c_str());
    acceptor()->Commit(queue_block_item);
    ZJC_DEBUG("2 NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu",
        pool_idx_, 0, v_block->qc().view(),
        v_block->qc().network_id(), v_block->qc().pool_index(), block_info.height(),
        view_block_chain()->String().c_str(),
        test_index);

    // 提交 v_block->consensus_stat 共识数据
    auto elect_item = elect_info()->GetElectItem(
            v_block->qc().network_id(),
            v_block->qc().elect_height());
    if (elect_item && elect_item->IsValid()) {
        elect_item->consensus_stat(pool_idx_)->Commit(v_block);
    }    
    
    ZJC_DEBUG("pool: %d consensus stat, leader: %lu, succ: %lu, test_index: %lu",
        pool_idx_, v_block->qc().leader_idx(),
        0,
        test_index);
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    uint32_t replica_idx = vote_msg.replica_idx();

    if (vote_msg.view() < view_block_chain()->HighViewBlock()->qc().view()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status Hotstuff::VerifyLeader(const uint32_t& leader_idx) {
    auto leader = leader_rotation()->GetLeader(); // 判断是否为空
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
        return  Status::kError;
    }

    if (leader_idx != leader->index) {
        auto eleader = leader_rotation()->GetExpectedLeader();
        if (!eleader || leader_idx != eleader->index) {
            ZJC_ERROR("pool: %d, leader_idx message is error, %d, %d", pool_idx_, leader_idx, leader->index);
            // assert(false);
            return Status::kError;
        }

        ZJC_DEBUG("use expected leader index: %u, %u", leader_idx, leader->index);
    }
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(hotstuff::protobuf::ProposeMsg* pro_msg) {
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kElectItemNotFound;
    }

    auto new_view_block = pro_msg->mutable_view_item();
    auto* tx_propose = pro_msg->mutable_tx_propose();
    Status s = ConstructViewBlock(new_view_block, tx_propose);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("pool: %d construct view block failed, view: %lu, %d, member_index: %d",
            pool_idx_, view_block_chain()->HighViewBlock()->qc().view(), s, 
            elect_item->LocalMember()->index);        
        return s;
    }

    pro_msg->set_elect_height(elect_item->ElectHeight());
    return Status::kSuccess;
}

Status Hotstuff::ConstructVoteMsg(
        hotstuff::protobuf::VoteMsg* vote_msg,
        uint64_t elect_height, 
        const std::shared_ptr<ViewBlock>& v_block) {
    auto elect_item = elect_info_->GetElectItem(
        common::GlobalInfo::Instance()->network_id(), 
        elect_height);
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }
    uint32_t replica_idx = elect_item->LocalMember()->index;
    vote_msg->set_replica_idx(replica_idx);
    vote_msg->set_view_block_hash(v_block->qc().view_block_hash());
    assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    vote_msg->set_view(v_block->qc().view());
    vote_msg->set_elect_height(elect_height);
    vote_msg->set_leader_idx(v_block->qc().leader_idx());
    QC qc_item;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(v_block->qc().view());
    qc_item.set_view_block_hash(v_block->qc().view_block_hash());
    assert(!prefix_db_->BlockExists(v_block->qc().view_block_hash()));
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(v_block->qc().leader_idx());
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
    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    wrapper()->GetTxsIdempotently(txs);
    for (size_t i = 0; i < txs.size(); i++) {
        auto* tx_ptr = vote_msg->add_txs();
        *tx_ptr = *(txs[i].get());
    }

    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock( 
        ViewBlock* view_block,
        hotstuff::protobuf::TxPropose* tx_propose) {
    auto local_elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (local_elect_item == nullptr) {
        return Status::kError;
    }

    auto local_member = local_elect_item->LocalMember();
    if (local_member == nullptr) {
        return Status::kError;
    }

    auto leader_idx = local_member->index;
    auto pre_v_block = view_block_chain()->HighViewBlock();
    view_block->set_parent_hash(pre_v_block->qc().view_block_hash());
    ZJC_DEBUG("get prev block hash: %s, height: %lu", 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        pre_v_block->block_info().height());
    // 打包 QC 和 View
    auto* qc = view_block->mutable_qc();
    qc->set_leader_idx(leader_idx);
    qc->set_view(pacemaker()->CurView());
    qc->set_network_id(common::GlobalInfo::Instance()->network_id());
    qc->set_pool_index(pool_idx_);
    // TODO 如果单分支最多连续打包三个默认交易
    auto s = wrapper()->Wrap(
        pre_v_block, 
        leader_idx, 
        view_block, 
        tx_propose, 
        IsEmptyBlockAllowed(*pre_v_block), 
        view_block_chain_);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }

    ZJC_DEBUG("success failed check is empty block allowd: %d, %u_%u_%lu, "
        "tx size: %u, cur view: %lu, pre view: %lu, last_vote_view_: %lu",
        pool_idx_, view_block->qc().network_id(), 
        view_block->qc().pool_index(), view_block->qc().view(),
        tx_propose->txs_size(),
        qc->view(),
        pre_v_block->qc().view(),
        last_vote_view_);
    if (last_vote_view_ >= pacemaker()->CurView()) {
        assert(last_vote_view_ < pacemaker()->CurView());
        return Status::kError;
    }

    auto elect_item = elect_info_->GetElectItem(
        common::GlobalInfo::Instance()->network_id(),
        view_block->qc().elect_height());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

bool Hotstuff::IsEmptyBlockAllowed(const ViewBlock& v_block) {
    auto* v_block1 = &v_block;
    if (!v_block1 || v_block1->block_info().tx_list_size() > 0) {
        return true;
    }

    std::shared_ptr<ViewBlock> v_block2 = view_block_chain()->Get(v_block.parent_hash());
    if (!v_block2 || v_block2->block_info().tx_list_size() > 0) {
        return true;
    }

    std::shared_ptr<ViewBlock> v_block3 = view_block_chain()->Get(v_block2->parent_hash());
    if (!v_block3 || v_block3->block_info().tx_list_size() > 0) {
        return true;
    }

    ZJC_DEBUG("failed check is empty block allowd block1: %u_%u_%lu, %s, block2: %u_%u_%lu, %s, block3: %u_%u_%lu, %s",
        v_block1->qc().network_id(),
        v_block1->qc().pool_index(),
        v_block1->qc().view(),
        common::Encode::HexEncode(v_block1->qc().view_block_hash()).c_str(),
        v_block2->qc().network_id(),
        v_block2->qc().pool_index(),
        v_block2->qc().view(),
        common::Encode::HexEncode(v_block2->qc().view_block_hash()).c_str(),
        v_block3->qc().network_id(),
        v_block3->qc().pool_index(),
        v_block3->qc().view(),
        common::Encode::HexEncode(v_block3->qc().view_block_hash()).c_str());
    return false;
}

Status Hotstuff::ConstructHotstuffMsg(
        const MsgType msg_type, 
        pb_ProposeMsg* pb_pro_msg, 
        pb_VoteMsg* pb_vote_msg,
        pb_NewViewMsg* pb_nv_msg,
        pb_HotstuffMessage* pb_hotstuff_msg) {
    pb_hotstuff_msg->set_type(msg_type);
    pb_hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    pb_hotstuff_msg->set_pool_index(pool_idx_);
    return Status::kSuccess;
}

Status Hotstuff::SendMsgToLeader(
        std::shared_ptr<transport::TransportMessage>& trans_msg, 
        const MsgType msg_type) {
    Status ret = Status::kSuccess;
    auto& header_msg = trans_msg->header;
    auto leader = leader_rotation()->GetLeader();
    if (!leader) {
        ZJC_ERROR("Get Leader failed.");
        return Status::kError;
    }
    
    // 记录收到回执时期望的 leader 用于验证
    leader_rotation_->SetExpectedLeader(leader);
    header_msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id, leader->id);
    header_msg.set_des_dht_key(dht_key.StrKey());
    header_msg.set_type(common::kHotstuffMessage);
    transport::TcpTransport::Instance()->SetMessageHash(header_msg);
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
        if (msg_type == VOTE) {
            HandleVoteMsg(trans_msg);
        } else if (msg_type == PRE_RESET_TIMER) {
            HandlePreResetTimerMsg(trans_msg);
        }
    }

#ifndef NDEBUG
    if (msg_type == PRE_RESET_TIMER) {
        for (uint32_t i = 0; i < header_msg.hotstuff().pre_reset_timer_msg().txs_size(); ++i) {
            auto& tx = header_msg.hotstuff().pre_reset_timer_msg().txs(i);
            ZJC_DEBUG("pool index: %u, send to leader %d message to leader net: %u, %s, "
                "hash64: %lu, %s:%d, leader->index: %d, local_idx: %d, gid: %s, to: %s",
                pool_idx_,
                msg_type,
                leader->net_id, 
                common::Encode::HexEncode(leader->id).c_str(), 
                header_msg.hash64(),
                common::Uint32ToIp(leader->public_ip).c_str(),
                leader->public_port,
                leader->index,
                local_idx,
                common::Encode::HexEncode(tx.gid()).c_str(),
                common::Encode::HexEncode(tx.to()).c_str());
        }
    }
#endif

    ZJC_DEBUG("pool index: %u, send to leader %d message to leader net: %u, %s, "
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

void Hotstuff::TryRecoverFromStuck(bool has_user_tx, bool has_system_tx) {
    // if (!latest_qc_item_ptr_) {
    //     ZJC_DEBUG("latest_qc_item_ptr_ null, pool: %u", pool_idx_);
    //     return;
    // }

    if (has_user_tx) {
        has_user_tx_tag_ = true;
    }

    if (!has_user_tx_tag_ && !has_system_tx) {
        // ZJC_DEBUG("!has_user_tx_tag_ && !has_system_tx, pool: %u", pool_idx_);
        return;
    }

    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        // ZJC_DEBUG("leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32, pool: %u", pool_idx_);
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (now_tm_ms < latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs) {
        // ZJC_DEBUG("pool: %u now_tm_ms < latest_propose_msg_tm_ms_ + "
        //     "kLatestPoposeSendTxToLeaderPeriodMs: %lu, %lu",
        //     pool_idx_, now_tm_ms, 
        //     (latest_propose_msg_tm_ms_ + kLatestPoposeSendTxToLeaderPeriodMs));
        return;
    }

    auto stuck_st = IsStuck();
    if (stuck_st != 0) {
        if (stuck_st != 1) {
            ZJC_DEBUG("pool: %u stuck_st != 0: %d", pool_idx_, stuck_st);
        }
        return;
    }

    if (has_system_tx) {
        auto leader = leader_rotation()->GetLeader();
        if (leader) {
            auto local_idx = leader_rotation_->GetLocalMemberIdx();
            if (leader->index == local_idx) {
                Propose(latest_qc_item_ptr_, nullptr);
                if (latest_qc_item_ptr_) {
                    ZJC_DEBUG("leader do propose message: %d, pool index: %u, %u_%u_%lu", 
                        local_idx,
                        pool_idx_,
                        latest_qc_item_ptr_->network_id(), 
                        latest_qc_item_ptr_->pool_index(), 
                        latest_qc_item_ptr_->view());
                } else {
                    ZJC_DEBUG("normal restart.");
                }
                return;
            }
        }
    }

    if (!has_user_tx_tag_) {
        // ZJC_DEBUG("pool: %u not has_user_tx_tag_.", pool_idx_);
        return;
    }

    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    wrapper()->GetTxsIdempotently(txs);
    if (txs.empty()) {
        // ZJC_DEBUG("pool: %u txs.empty().", pool_idx_);
        return;
    }
    
    // 存在内置交易或普通交易时尝试 reset timer
    // TODO 发送 PreResetPacemakerTimerMsg To Leader
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& header = trans_msg->header;
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pre_rst_timer_msg = hotstuff_msg->mutable_pre_reset_timer_msg();
    auto elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (!elect_item || !elect_item->IsValid()) {
        ZJC_ERROR("pool: %d no elect item found", pool_idx_);
        return;
    }
    
    pre_rst_timer_msg->set_replica_idx(elect_item->LocalMember()->index);
    for (size_t i = 0; i < txs.size(); i++) {
        auto& tx_ptr = *(pre_rst_timer_msg->add_txs());
        tx_ptr = *(txs[i].get());
    }

    pre_rst_timer_msg->set_has_single_tx(has_system_tx);
    hotstuff_msg->set_type(PRE_RESET_TIMER);
    hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg->set_pool_index(pool_idx_);
    SendMsgToLeader(trans_msg, PRE_RESET_TIMER);
    ZJC_DEBUG("pool: %d, send prereset msg from: %lu to: %lu, has_single_tx: %d, tx size: %u",
        pool_idx_, pre_rst_timer_msg->replica_idx(), 
        leader_rotation_->GetLeader()->index, has_system_tx, txs.size());
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

    ZJC_DEBUG("pool: %d add succ num: %lu, leader: %lu", pool_idx_, ret, v_block->qc().leader_idx());
    return ret;
}

} // namespace consensus

} // namespace shardora

