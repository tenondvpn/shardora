#include <common/encode.h>
#include <common/log.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/types.h>
#include <protos/hotstuff.pb.h>
#include <protos/pools.pb.h>
#include <protos/view_block.pb.h>

namespace shardora {

namespace hotstuff {

void Hotstuff::Init(std::shared_ptr<db::Db>& db_) {
    // set pacemaker timeout callback function
    last_vote_view_ = GenesisView;
    
    auto latest_view_block = std::make_shared<ViewBlock>();
    auto latest_view_block_commit_qc = std::make_shared<QC>();
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block, latest_view_block_commit_qc);
    if (s == Status::kSuccess) {
        // 初始状态，使用 db 中最后一个 view_block 初始化视图链
        view_block_chain_->Store(latest_view_block);
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        view_block_chain_->SetLatestCommittedBlock(latest_view_block);
        // 开启第一个视图

        pacemaker_->AdvanceView(new_sync_info()->WithQC(latest_view_block_commit_qc));
        ZJC_DEBUG("has latest block, pool_idx: %d, latest block height: %lu, commit_qc_hash: %s, latest_view_block: %s, high_qc_hash: %s",
            pool_idx_, latest_view_block->block->height(),
            common::Encode::HexEncode(latest_view_block_commit_qc->view_block_hash).c_str(),
            common::Encode::HexEncode(view_block_chain_->LatestLockedBlock()->hash).c_str(),
            common::Encode::HexEncode(pacemaker_->HighQC()->view_block_hash).c_str());
    } else {
        ZJC_DEBUG("no genesis, waiting for syncing, pool_idx: %d", pool_idx_);
    }            
}


Status Hotstuff::Start() {
    auto leader = leader_rotation()->GetLeader();
    auto elect_item = elect_info_->GetElectItem();
    if (!elect_item) {
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
        Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    }
    return Status::kSuccess;
}

Status Hotstuff::Propose(const std::shared_ptr<SyncInfo>& sync_info) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
    Status s = ConstructProposeMsg(sync_info, pb_pro_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d construct propose msg failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }
    s = ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().view());
        return s;
    }
    header.mutable_hotstuff()->CopyFrom(*hotstuff_msg);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }    
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    s = crypto()->SignMessage(msg_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().view());
        return s;
    }

    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("pool: %d, propose, txs size: %lu, view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
        pool_idx_,
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view,
        header.hash64());
    HandleProposeMsg(header);
    return Status::kSuccess;
}

void Hotstuff::NewView(const std::shared_ptr<SyncInfo>& sync_info) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_newview_msg = hotstuff_msg->mutable_newview_msg();
    pb_newview_msg->set_elect_height(elect_info_->GetElectItem()->ElectHeight());
    if (!sync_info->tc && !sync_info->qc) {
        return;
    }

    if (sync_info->tc) {
        pb_newview_msg->set_tc_str(sync_info->tc->Serialize());
    }

    if (sync_info->qc) {
        pb_newview_msg->set_qc_str(sync_info->qc->Serialize());
    }
    
    Status s = ConstructHotstuffMsg(NEWVIEW, nullptr, nullptr, pb_newview_msg, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().view());
        return;
    }
    
    header.mutable_hotstuff()->CopyFrom(*hotstuff_msg);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }
    
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("pool: %d, newview, txs size: %lu, view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
        pool_idx_,
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view,
        header.hash64());
    HandleNewViewMsg(header);
    return;    
}

void Hotstuff::HandleProposeMsg(const transport::protobuf::Header& header) {
    auto& pro_msg = header.hotstuff().pro_msg();
    ZJC_DEBUG("====1.0 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu, hash64: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view,
        header.hash64());

    // 3 Verify TC
    std::shared_ptr<TC> tc = nullptr;
    if (!pro_msg.tc_str().empty()) {
        tc = std::make_shared<TC>();
        if (!tc->Unserialize(pro_msg.tc_str())) {
            ZJC_ERROR("tc Unserialize is error.");
            return;
        }
        if (tc->view > pacemaker()->HighTC()->view) {
            if (crypto()->VerifyTC(tc) != Status::kSuccess) {
                ZJC_ERROR("VerifyTC error.");
                return;
            }
            pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));            
        }
    }
    
    // 1 校验pb view block格式
    view_block::protobuf::ViewBlockItem pb_view_block = pro_msg.view_item();
    auto v_block = std::make_shared<ViewBlock>();
    Status s = Proto2ViewBlock(pb_view_block, v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pb_view_block to ViewBlock is error.");
        return;
    }

    // view 必须最新
    // TODO 超时情况可能相同，严格限制并不影响共识，但会减少共识参与节点数
    if (HasVoted(v_block->view)) {
        ZJC_ERROR("pool: %d has voted: %lu", pool_idx_, v_block->view);
        return;
    }    
    
    // 2 Veriyfy Leader
    if (VerifyLeader(v_block->leader_idx) != Status::kSuccess) {
        ZJC_ERROR("verify leader failed, pool: %d has voted: %lu, hash64: %lu", 
            pool_idx_, v_block->view, header.hash64());
        return;
    }
    
    // 4 Verify ViewBlock    
    if (VerifyViewBlock(v_block, view_block_chain(), tc, pro_msg.elect_height()) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Verify ViewBlock is error. hash: %s", pool_idx_,
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }
    
    ZJC_DEBUG("====1.1 pool: %d, verify view block success, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);
    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(v_block->qc));

    // Commit 一定要在 Txs Accept 之前，因为一旦 v_block->qc 合法就已经可以 Commit 了，不需要 Txs 合法
    auto v_block_to_commit = CheckCommit(v_block);
    if (v_block_to_commit) {
        Status s = Commit(v_block_to_commit);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->view,
                common::Encode::HexEncode(v_block_to_commit->hash).c_str());
            return;
        }
        
        if (!view_block_chain()->HasInDb(
                    v_block_to_commit->block->network_id(),
                    v_block_to_commit->block->pool_index(),
                    v_block_to_commit->block->height())) {
            // 保存 commit vblock 及其 commitQC 用于 kv 同步
            view_block_chain()->StoreToDb(v_block_to_commit, v_block->qc);            
        }
    }    
    
    // Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    auto block = v_block->block;
    block_info->block = block;
    block_info->tx_type = pro_msg.tx_propose().tx_type();
    for (const auto& tx : pro_msg.tx_propose().txs()) {
        block_info->txs.push_back(&tx);
    }
    block_info->view = v_block->view;
    
    if (acceptor()->Accept(block_info, true) != Status::kSuccess) {
        // 归还交易
        acceptor()->Return(block_info->block);
        ZJC_ERROR("Accept tx is error");
        return;
    }
    
    // 更新哈希值
    v_block->UpdateHash();
#ifndef NDEBUG
    for (int32_t i = 0; i < v_block->block->tx_list_size(); ++i) {
        ZJC_DEBUG("block net: %u, pool: %u, height: %lu, prehash: %s, hash: %s, step: %d, "
            "pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, curView: %lu, vblock: %lu, txs: %lu",
            block_info->block->network_id(),
            block_info->block->pool_index(),
            block_info->block->height(),
            common::Encode::HexEncode(block_info->block->prehash()).c_str(),
            common::Encode::HexEncode(block_info->block->hash()).c_str(),
            block_info->block->tx_list(i).step(),
            pool_idx_,
            pacemaker()->HighQC()->view,
            pacemaker()->HighTC()->view,
            view_block_chain()->Size(),
            pacemaker()->CurView(),
            v_block->view,
            v_block->block->tx_list_size());

    }
#endif
    // 6 add view block
    if (view_block_chain()->Store(v_block) != Status::kSuccess) {
        ZJC_ERROR("add view block error. hash: %s",
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }
        
    ZJC_DEBUG("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, curView: %lu, vblock: %lu, txs: %lu",
        pool_idx_,
        pacemaker()->HighQC()->view,
        pacemaker()->HighTC()->view,
        view_block_chain()->Size(),
        pacemaker()->CurView(),
        v_block->view,
        v_block->block->tx_list_size());
    // TODO 曾经遇到 CommittedBlock 为空的情况，等复现
    assert(view_block_chain()->LatestCommittedBlock() != nullptr);
    // view_block_chain()->Print();    

    
    
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& trans_header = trans_msg->header;
    auto* hotstuff_msg = trans_header.mutable_hotstuff();
    auto* vote_msg = hotstuff_msg->mutable_vote_msg();
    // Construct VoteMsg
    s = ConstructVoteMsg(vote_msg, pro_msg.elect_height(), v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("ConstructVoteMsg error %d", s);
        return;
    }
    // Construct HotstuffMessage and send
    s = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("ConstructHotstuffMsg error %d", s);
        return;
    }
    
    if (SendMsgToLeader(trans_msg, VOTE) != Status::kSuccess) {
        ZJC_ERROR("Send vote message is error.");
    }
    
    return;
}

void Hotstuff::HandleVoteMsg(const transport::protobuf::Header& header) {
    auto& vote_msg = header.hotstuff().vote_msg();
    ZJC_DEBUG("====2.0 pool: %d, onVote, hash: %s, view: %lu, replica: %lu, hash64: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        vote_msg.replica_idx(),
        header.hash64());
    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        ZJC_ERROR("vote message is error.");
        return;
    }
    ZJC_DEBUG("====2.1 pool: %d, onVote, hash: %s, view: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view());

    // 同步 replica 的 txs
    std::vector<const pools::protobuf::TxMessage*> tx_msgs;
    for (const auto& tx : vote_msg.txs()) {
        tx_msgs.push_back(&tx);
        ZJC_DEBUG("handle backup vote message get tx type: %d, to: %s, gid: %s", 
            tx.step(), 
            common::Encode::HexEncode(tx.to()).c_str(), 
            common::Encode::HexEncode(tx.gid()).c_str());
    }
    acceptor()->AddTxs(tx_msgs);
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    
    Status ret = crypto()->ReconstructAndVerifyThresSign(
            elect_height,
            vote_msg.view(),
            GetQCMsgHash(vote_msg.view(),
                vote_msg.view_block_hash(),
                vote_msg.commit_view_block_hash(),
                elect_height,
                vote_msg.leader_idx()),
            replica_idx, 
            vote_msg.sign_x(),
            vote_msg.sign_y(),
            reconstructed_sign);
    
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_DEBUG("kBlsWaiting pool: %d, view: %lu", pool_idx_, vote_msg.view());
            return;
        }
        return;
    }
    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        reconstructed_sign == nullptr,
        vote_msg.view());

#ifndef NDEBUG
        std::shared_ptr<ViewBlock> block_info = nullptr;
        Status st = view_block_chain()->Get(vote_msg.view_block_hash(), block_info);
        if (st == Status::kSuccess && block_info != nullptr) {
            for (int32_t i = 0; i < block_info->block->tx_list_size(); ++i) {
                ZJC_DEBUG("block net: %u, pool: %u, height: %lu, prehash: %s, hash: %s, step: %d, "
                    "pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, curView: %lu, vblock: %lu, txs: %lu",
                    block_info->block->network_id(),
                    block_info->block->pool_index(),
                    block_info->block->height(),
                    common::Encode::HexEncode(block_info->block->prehash()).c_str(),
                    common::Encode::HexEncode(block_info->block->hash()).c_str(),
                    block_info->block->tx_list(i).step(),
                    pool_idx_,
                    pacemaker()->HighQC()->view,
                    pacemaker()->HighTC()->view,
                    view_block_chain()->Size(),
                    pacemaker()->CurView(),
                    block_info->view,
                    block_info->block->tx_list_size());
            }
        }
#endif

    auto qc = std::make_shared<QC>();
    Status s = crypto()->CreateQC(
            vote_msg.view_block_hash(),
            vote_msg.commit_view_block_hash(),
            vote_msg.view(),
            elect_height,
            vote_msg.leader_idx(),
            reconstructed_sign,
            qc);
    if (s != Status::kSuccess) {
        return;
    }
    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(qc));
    // 先单独广播新 qc，即是 leader 出不了块也不用额外同步 HighQC，这比 Gossip 的效率高很多
    NewView(new_sync_info()->WithQC(qc));
    
    // 一旦生成新 QC，且本地还没有该 view_block，就直接从 VoteMsg 中获取并添加
    // 没有这个逻辑也不影响共识，只是需要同步而导致 tps 降低
    if (vote_msg.has_view_block_item()) {
        auto pb_v_block = vote_msg.view_block_item();
        auto v_block = std::make_shared<ViewBlock>();
        s = Proto2ViewBlock(pb_v_block, v_block);
        if (s == Status::kSuccess) {
            s = StoreVerifiedViewBlock(v_block, qc);
            if (s != Status::kSuccess) {
                ZJC_ERROR("pool: %d store verified view block failed, ret: %d", pool_idx_, s);
            } else {
                ZJC_DEBUG("pool: %d store verified view block success, view: %lu", pool_idx_, v_block->view);
            }
        }        
    }

    Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    return;
}

Status Hotstuff::StoreVerifiedViewBlock(const std::shared_ptr<ViewBlock>& v_block, const std::shared_ptr<QC>& qc) {
    if (view_block_chain()->Has(qc->view_block_hash)) {
        return Status::kSuccess;    
    }
    if (v_block->hash != qc->view_block_hash || v_block->view != qc->view) {
        return Status::kError;
    }

    Status s = acceptor()->AcceptSync(v_block->block);
    if (s != Status::kSuccess) {
        return s;
    }

    auto view_block_to_commit = CheckCommit(v_block);
    if (view_block_to_commit) {
        s = Commit(view_block_to_commit);
        if (s != Status::kSuccess) {
            return s;
        }
    }    
    return view_block_chain()->Store(v_block);
}

void Hotstuff::HandleNewViewMsg(const transport::protobuf::Header& header) {
    ZJC_DEBUG("====3.1 pool: %d, onNewview", pool_idx_);    
    auto& newview_msg = header.hotstuff().newview_msg();
    std::shared_ptr<TC> tc = nullptr;
    if (!newview_msg.tc_str().empty()) {
        tc = std::make_shared<TC>();
        if (!tc->Unserialize(newview_msg.tc_str())) {
            ZJC_ERROR("tc Unserialize is error.");
            return;
        }
        if (tc->view > pacemaker()->HighTC()->view) {
            if (crypto()->VerifyTC(tc) != Status::kSuccess) {
                ZJC_ERROR("VerifyTC error.");
                return;
            }

            ZJC_DEBUG("====3.2 pool: %d, tc: %lu, onNewview", pool_idx_, tc->view);
            pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));
        }
    }

    std::shared_ptr<QC> qc = nullptr;
    if (!newview_msg.qc_str().empty()) {
        qc = std::make_shared<QC>();
        if (!qc->Unserialize(newview_msg.qc_str())) {
            ZJC_ERROR("qc Unserialize is error.");
            return;
        }
        if (qc->view > pacemaker()->HighQC()->view) {
            if (crypto()->VerifyQC(qc) != Status::kSuccess) {
                ZJC_ERROR("VerifyQC error.");
                return;
            }

            ZJC_DEBUG("====3.3 pool: %d, qc: %lu, onNewview", pool_idx_, qc->view);
            pacemaker()->AdvanceView(new_sync_info()->WithQC(qc));
        }
    }    
    return;
}

void Hotstuff::HandlePreResetTimerMsg(const transport::protobuf::Header& header) {
    auto& pre_rst_timer_msg = header.hotstuff().pre_reset_timer_msg();
    if (pre_rst_timer_msg.txs_size() == 0 && !pre_rst_timer_msg.has_single_tx()) {
        return;
    }

    if (pre_rst_timer_msg.txs_size() > 0) {
        std::vector<const pools::protobuf::TxMessage*> tx_msgs;
        for (const auto& tx : pre_rst_timer_msg.txs()) {
            tx_msgs.push_back(&tx);
        }
        Status s = acceptor()->AddTxs(tx_msgs);
        if (s != Status::kSuccess) {
            return;
        }        
    }

    // Flow Control
    if (!reset_timer_fc_.Permitted()) {
        return;
    }

    ResetReplicaTimers();
    return;
}

Status Hotstuff::ResetReplicaTimers() {
    // Reset timer msg broadcast
    auto rst_timer_msg = std::make_shared<hotstuff::protobuf::ResetTimerMsg>();
    rst_timer_msg->set_leader_idx(elect_info_->GetElectItem()->LocalMember()->index);

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    
    auto* hotstuff_msg = header.mutable_hotstuff();
    hotstuff_msg->set_type(RESET_TIMER);
    hotstuff_msg->mutable_reset_timer_msg()->CopyFrom(*rst_timer_msg);
    hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg->set_pool_index(pool_idx_);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);

    network::Route::Instance()->Send(msg_ptr);
    HandleResetTimerMsg(header);
    return Status::kSuccess;
}

void Hotstuff::HandleResetTimerMsg(const transport::protobuf::Header& header) {
    auto& rst_timer_msg = header.hotstuff().reset_timer_msg();
    ZJC_DEBUG("====5.1 pool: %d, onResetTimer leader_idx: %lu, local_idx: %lu",
        pool_idx_, rst_timer_msg.leader_idx(), elect_info_->GetElectItem()->LocalMember()->index);
    // leader 必须正确
    if (VerifyLeader(rst_timer_msg.leader_idx()) != Status::kSuccess) {
        return;
    }
    // 必须处于 stuck 状态
    if (!IsStuck()) {
        return;
    }
    ZJC_DEBUG("====5.2 pool: %d, ResetTimer", pool_idx_);
    // reset pacemaker view duration
    pacemaker()->ResetViewDuration(std::make_shared<ViewDuration>(
                pool_idx_,
                ViewDurationSampleSize,
                ViewDurationStartTimeoutMs,
                ViewDurationMaxTimeoutMs,
                ViewDurationMultiplier));
    return;
}

std::shared_ptr<ViewBlock> Hotstuff::CheckCommit(const std::shared_ptr<ViewBlock>& v_block) {
    auto v_block1 = view_block_chain()->QCRef(v_block);
    if (!v_block1) {
        return nullptr;
    }    
    auto v_block2 = view_block_chain()->QCRef(v_block1);
    if (!v_block2) {
        return nullptr;
    }

    if (!view_block_chain()->LatestLockedBlock() || v_block2->view > view_block_chain()->LatestLockedBlock()->view) {
        ZJC_DEBUG("locked block, view: %lu", v_block2->view);
        view_block_chain()->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = view_block_chain()->QCRef(v_block2);
    if (!v_block3) {
        return nullptr;
    }

    if (v_block1->parent_hash == v_block2->hash && v_block2->parent_hash == v_block3->hash) {
        ZJC_DEBUG("decide block, view: %lu", v_block3->view);
        return v_block3;
    }
    
    return nullptr;
}

Status Hotstuff::Commit(const std::shared_ptr<ViewBlock>& v_block) {
    // 递归提交
    ZJC_DEBUG("pool: %d, commit block view: %lu", pool_idx_, v_block->view);
    Status s = CommitInner(v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, commit inner failed s: %d, vb view: &lu", pool_idx_, s, v_block->view);
        return s;
    }
    // 剪枝
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    s = view_block_chain()->PruneTo(v_block->hash, forked_blockes, true);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, prune failed s: %d, vb view: &lu", pool_idx_, s, v_block->view);
        return s;
    }

    // 归还分支交易
    for (const auto& forked_block : forked_blockes) {
        s = acceptor()->Return(forked_block->block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    return Status::kSuccess;
}

Status Hotstuff::VerifyViewBlock(
        const std::shared_ptr<ViewBlock>& v_block, 
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const std::shared_ptr<TC>& tc,
        const uint32_t& elect_height) {
    Status ret = Status::kSuccess;
    auto block_view = v_block->view;
    if (!v_block->Valid()) {
        ZJC_ERROR("view block hash is error.");
        return Status::kError;
    }

    auto qc = v_block->qc;
    if (!qc) {
        ZJC_ERROR("qc 必须存在.");
        return Status::kError;
    }

    // 验证 view 编号
    if (qc->view + 1 != v_block->view && tc && tc->view + 1 != v_block->view) {
        ZJC_ERROR("block view is error.");
        return Status::kError;
    }
    
    // qc 指针和哈希指针一致
    if (qc->view_block_hash != v_block->parent_hash) {
        ZJC_ERROR("qc ref is different from hash ref");
        return Status::kError;        
    }

    // 验证 qc
    if (qc->view > pacemaker()->HighQC()->view) {
        if (crypto()->VerifyQC(qc) != Status::kSuccess) {
            ZJC_ERROR("Verify qc is error. elect_height: %llu, qc: %llu", elect_height, qc->view);
            return Status::kError; 
        }
    }

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block;
    if (view_block_chain->Get(qc->view_block_hash, qc_view_block) != Status::kSuccess 
        && !view_block_chain->Extends(v_block, qc_view_block)) {
        ZJC_ERROR("qc view block message is error.");
        return Status::kError;
    }

    if (view_block_chain->LatestLockedBlock() &&
        !view_block_chain->Extends(v_block, view_block_chain->LatestLockedBlock()) && 
        v_block->qc->view <= view_block_chain->LatestLockedBlock()->view) {
        ZJC_ERROR("pool: %d, block view message is error. %lu, %lu, %s, %s",
            pool_idx_, v_block->qc->view, view_block_chain->LatestLockedBlock()->view,
            common::Encode::HexEncode(view_block_chain->LatestLockedBlock()->hash).c_str(),
            common::Encode::HexEncode(v_block->parent_hash).c_str());
        return Status::kError;
    }   

    return ret;
}

Status Hotstuff::CommitInner(const std::shared_ptr<ViewBlock>& v_block) {
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view >= v_block->view) {
        return Status::kSuccess;
    }

    if (!latest_committed_block && v_block->view == GenesisView) {
        return Status::kSuccess;
    }

    std::shared_ptr<ViewBlock> parent_block = nullptr;
    Status s = view_block_chain()->Get(v_block->parent_hash, parent_block);
    if (s == Status::kSuccess && parent_block != nullptr) {
        s = CommitInner(parent_block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    v_block->block->set_is_commited_block(true);
    s = acceptor()->Commit(v_block->block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, commit failed s: %d, vb view: &lu", pool_idx_, s, v_block->view);
        return s;
    }
    
    view_block_chain()->SetLatestCommittedBlock(v_block);
    return Status::kSuccess;
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    uint32_t replica_idx = vote_msg.replica_idx();

    if (vote_msg.view() <= pacemaker()->HighQC()->view) {
        ZJC_ERROR("view message is not exited.");
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
            return Status::kError;
        }
        return Status::kError;
    }
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(
        const std::shared_ptr<SyncInfo>& sync_info,
        hotstuff::protobuf::ProposeMsg* pro_msg) {
    auto new_view_block = std::make_shared<ViewBlock>();
    auto tx_propose = std::make_shared<hotstuff::protobuf::TxPropose>();
    Status s = ConstructViewBlock(new_view_block, tx_propose);
    if (s != Status::kSuccess) {
        return s;
    }

    auto new_pb_view_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
    ViewBlock2Proto(new_view_block, new_pb_view_block.get());
    pro_msg->mutable_view_item()->CopyFrom(*new_pb_view_block);
    pro_msg->set_elect_height(elect_info_->GetElectItem()->ElectHeight());
    if (sync_info->tc) {
        pro_msg->set_tc_str(sync_info->tc->Serialize());
    }
    pro_msg->mutable_tx_propose()->CopyFrom(*tx_propose);
    return Status::kSuccess;
}

Status Hotstuff::ConstructVoteMsg(
        hotstuff::protobuf::VoteMsg* vote_msg,
        const uint32_t& elect_height, 
        const std::shared_ptr<ViewBlock>& v_block) {
    auto elect_item = elect_info_->GetElectItem(elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    uint32_t replica_idx = elect_item->LocalMember()->index;
    vote_msg->set_replica_idx(replica_idx);
    vote_msg->set_view_block_hash(v_block->hash);
    HashStr commit_view_block_hash = "";
    if (view_block_chain()->LatestLockedBlock()) {
        commit_view_block_hash = view_block_chain()->LatestLockedBlock()->hash;
        // 设置下一个 QC 的 commit_view_block_hash
        vote_msg->set_commit_view_block_hash(commit_view_block_hash); 
    }

    vote_msg->set_view(v_block->view);
    vote_msg->set_elect_height(elect_height);
    vote_msg->set_leader_idx(v_block->leader_idx);

    // 将 vblock 发送给新 leader，防止新 leader 还没有收到提案造成延迟
    // Leader 如果生成了 QC，则一定会保存 vblock，防止发起下一轮提案时没有这个块
    // 这可能会使用一些带宽，但会提高 tps
    // 如果不发并不会影响共识，只是有概率需要额外同步而导致延迟
    if (VOTE_MSG_WITH_VBLOCK) {
        view_block::protobuf::ViewBlockItem pb_view_block;
        ViewBlock2Proto(v_block, &pb_view_block);
        vote_msg->mutable_view_block_item()->CopyFrom(pb_view_block);
    }

    
    std::string sign_x, sign_y;
    if (crypto()->PartialSign(
                elect_height,
                GetQCMsgHash(v_block->view,
                    v_block->hash,
                    commit_view_block_hash,
                    elect_height,
                    v_block->leader_idx),
                &sign_x,
                &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }

    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_y(sign_y);

    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    wrapper()->GetTxsIdempotently(txs);
    for (size_t i = 0; i < txs.size(); i++)
    {
        auto* tx_ptr = vote_msg->add_txs();
        *tx_ptr = *(txs[i].get());
        ZJC_DEBUG("vote send tx message type: %d, to: %s, gid: %s", 
            tx_ptr->step(), 
            common::Encode::HexEncode(tx_ptr->to()).c_str(), 
            common::Encode::HexEncode(tx_ptr->gid()).c_str());
    }

    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock( 
        std::shared_ptr<ViewBlock>& view_block,
        std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) {
    view_block->parent_hash = (pacemaker()->HighQC()->view_block_hash);
    auto leader_idx = elect_info_->GetElectItem()->LocalMember()->index;
    view_block->leader_idx = leader_idx;

    auto pre_v_block = std::make_shared<ViewBlock>();
    Status s = view_block_chain()->Get(view_block->parent_hash, pre_v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("parent view block has not found, pool: %d, view: %lu, parent_view: %lu, leader: %lu", pool_idx_, pacemaker()->CurView(), pacemaker()->HighQC()->view, leader_idx);
        return s;
    }
    
    ZJC_DEBUG("get prev block hash: %s, height: %lu", 
        common::Encode::HexEncode(view_block->parent_hash).c_str(), 
        pre_v_block->block->height());
    auto pre_block = pre_v_block->block;
    auto pb_block = std::make_shared<block::protobuf::Block>();

    // 打包 QC 和 View
    view_block->qc = pacemaker()->HighQC();
    view_block->view = pacemaker()->CurView();

    // TODO 如果单分支最多连续打包三个默认交易
    s = wrapper()->Wrap(pre_block, leader_idx, pb_block, tx_propose, IsEmptyBlockAllowed(view_block));
    if (s != Status::kSuccess) {
        ZJC_WARN("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }
    view_block->block = pb_block;
    view_block->hash = view_block->DoHash();
    return Status::kSuccess;
}

bool Hotstuff::IsEmptyBlockAllowed(const std::shared_ptr<ViewBlock>& v_block) {
    auto v_block1 = view_block_chain()->QCRef(v_block);
    if (!v_block1 || v_block1->block->tx_list_size() > 0) {
        return true;
    }
    auto v_block2 = view_block_chain()->QCRef(v_block1);
    if (!v_block2 || v_block2->block->tx_list_size() > 0) {
        return true;
    }
    auto v_block3 = view_block_chain()->QCRef(v_block2);
    if (!v_block3 || v_block3->block->tx_list_size() > 0) {
        return true;
    }
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

Status Hotstuff::SendMsgToLeader(std::shared_ptr<transport::TransportMessage>& trans_msg, const MsgType msg_type) {
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
    auto leader_idx = leader_rotation_->GetLocalMemberIdx();
    if (leader->index != leader_idx) {
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
            HandleVoteMsg(header_msg);
        } else if (msg_type == PRE_RESET_TIMER) {
            HandlePreResetTimerMsg(header_msg);
        }
    }
 
    ZJC_DEBUG("send to leader %d message to leader net: %u, %s, "
        "hash64: %lu, %s:%d, leader->index: %d, leader_idx: %d",
        msg_type,
        leader->net_id, 
        common::Encode::HexEncode(leader->id).c_str(), 
        header_msg.hash64(),
        common::Uint32ToIp(leader->public_ip).c_str(),
        leader->public_port,
        leader->index,
        leader_idx);
    return ret;
}

void Hotstuff::TryRecoverFromStuck() {
    if (IsStuck() && recover_from_struct_fc_.Permitted()) {
        bool has_single_tx = wrapper()->HasSingleTx();
        std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
        if (!has_single_tx) {
            wrapper()->GetTxsIdempotently(txs);
        }

        // 存在内置交易或普通交易时尝试 reset timer
        if (has_single_tx || !txs.empty()) {
            // TODO 发送 PreResetPacemakerTimerMsg To Leader
            auto trans_msg = std::make_shared<transport::TransportMessage>();
            auto& header = trans_msg->header;
            auto* hotstuff_msg = header.mutable_hotstuff();
            auto* pre_rst_timer_msg = hotstuff_msg->mutable_pre_reset_timer_msg();
            
            auto elect_item = elect_info_->GetElectItem();
            if (!elect_item) {
                ZJC_ERROR("pool: %d no elect item found", pool_idx_);
                return;
            }
            
            pre_rst_timer_msg->set_replica_idx(elect_item->LocalMember()->index);
            ZJC_DEBUG("pool: %d, get tx size: %u", pool_idx_, txs.size());            
            for (size_t i = 0; i < txs.size(); i++) {
                auto& tx_ptr = *(pre_rst_timer_msg->add_txs());
                tx_ptr = *(txs[i].get());
            }
            pre_rst_timer_msg->set_has_single_tx(has_single_tx);

            hotstuff_msg->set_type(PRE_RESET_TIMER);
            hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
            hotstuff_msg->set_pool_index(pool_idx_);

            SendMsgToLeader(trans_msg, PRE_RESET_TIMER);
            ZJC_DEBUG("pool: %d, send prereset msg from: %lu to: %lu, has_single_tx: %d",
                pool_idx_, pre_rst_timer_msg->replica_idx(), leader_rotation_->GetLeader()->index, has_single_tx);
        }
    }
    return;
}

} // namespace consensus

} // namespace shardora

