#include <common/encode.h>
#include <common/log.h>
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

void Hotstuff::Init(std::shared_ptr<db::Db>& db_) {
    // set pacemaker timeout callback function
    pacemaker_->SetNewProposalFn(std::bind(&Hotstuff::Propose, this, std::placeholders::_1));
    pacemaker_->SetStopVotingFn(std::bind(&Hotstuff::StopVoting, this, std::placeholders::_1));

    ZJC_DEBUG("====9 pool: %d, pm: %p, init", pool_idx_, pacemaker_.get());
    last_vote_view_ = GenesisView;
    
    auto genesis = GetGenesisViewBlock(db_, pool_idx_);
    if (genesis) {
        // 初始状态，将创世块放入链中
        view_block_chain_->Store(genesis);
        view_block_chain_->SetLatestLockedBlock(genesis);
        view_block_chain_->SetLatestCommittedBlock(genesis);
        auto sync_info = std::make_shared<SyncInfo>();

        // 使用 genesis qc 进行视图切换
        auto genesis_qc = GetGenesisQC(genesis->hash);
        // 开启第一个视图
        pacemaker_->AdvanceView(sync_info->WithQC(genesis_qc));
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

void Hotstuff::Propose(const std::shared_ptr<SyncInfo>& sync_info) {
    ZJC_DEBUG("====9 pool: %d, pm: %p, propose", pool_idx_, pacemaker_.get());
    auto pb_pro_msg = std::make_shared<hotstuff::protobuf::ProposeMsg>();
    ConstructProposeMsg(sync_info, pb_pro_msg);

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto hotstuff_msg = std::make_shared<pb_HotstuffMessage>();
    Status s = ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("====0.3 pool: %d construct hotstuff msg failed", pool_idx_);
        return;
    }
    header.mutable_hotstuff()->CopyFrom(*hotstuff_msg);

    ZJC_DEBUG("====0.1 pool: %d, propose, txs size: %lu, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);

    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }    
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    s = crypto()->SignMessage(msg_ptr);
    if (s != Status::kSuccess) {
        return;
    }

    ZJC_DEBUG("====0.2 pool: %d, propose, txs size: %lu, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);

    HandleProposeMsg(hotstuff_msg->pro_msg());
    network::Route::Instance()->Send(msg_ptr);
    return;
}

void Hotstuff::HandleProposeMsg(const hotstuff::protobuf::ProposeMsg& pro_msg) {
    ZJC_DEBUG("====1.0 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);    
    // 1 校验pb view block格式
    view_block::protobuf::ViewBlockItem pb_view_block = pro_msg.view_item();
    auto v_block = std::make_shared<ViewBlock>();
    Status s = Proto2ViewBlock(pb_view_block, v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pb_view_block to ViewBlock is error.");
        return;
    }
    ZJC_DEBUG("====1.1 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);    

    // 已经投过票
    if (HasVoted(v_block->view)) {
        return;
    }
    ZJC_DEBUG("====1.2 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);    
    // 2 Veriyfy Leader
    if (VerifyLeader(v_block) != Status::kSuccess) {
        return;
    }
    ZJC_DEBUG("====1.3 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);
    // 3 Verify TC
    if (!pro_msg.tc_str().empty()) {
        auto tc = std::make_shared<TC>();
        if (!tc->Unserialize(pro_msg.tc_str())) {
            ZJC_ERROR("tc Unserialize is error.");
            return;
        }
        if (crypto()->VerifyTC(tc, pro_msg.elect_height()) != Status::kSuccess) {
            return;
        }
        pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));
    }
    
    ZJC_DEBUG("====1.4 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);
    
    // 4 Verify ViewBlock    
    if (VerifyViewBlock(v_block, view_block_chain(), pro_msg.elect_height()) != Status::kSuccess) {
        ZJC_ERROR("Verify ViewBlock is error. hash: %s",
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }
    ZJC_DEBUG("====1.5 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);
    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(v_block->qc));
    
    // 5 Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    auto block = v_block->block;
    block_info->block = block;
    block_info->tx_type = pro_msg.tx_propose().tx_type();
    for (int i = 0; i < pro_msg.tx_propose().txs_size(); i++)
    {
        auto tx = std::make_shared<pools::protobuf::TxMessage>(pro_msg.tx_propose().txs(i));
        block_info->txs.push_back(tx);
    }
    block_info->view = v_block->view;
    
    if (acceptor()->Accept(block_info) != Status::kSuccess) {
        // 归还交易
        acceptor()->Return(block_info->block);
        ZJC_ERROR("Accept tx is error");
        return;
    }
    // 更新哈希值
    v_block->UpdateHash();
    
    // 6 add view block
    if (view_block_chain()->Store(v_block) != Status::kSuccess) {
        ZJC_ERROR("add view block error. hash: %s",
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }

    // 1、验证是否存在3个连续qc，设置commit，lock qc状态；2、提交commit块之间的交易信息；3、减枝保留最新commit块，回退分支的交易信息
    auto v_block_to_commit = CheckCommit(v_block);
    if (v_block_to_commit) {
        Status s = Commit(v_block_to_commit);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->view,
                common::Encode::HexEncode(v_block_to_commit->hash).c_str());
            return;
        }
    }
    
    // Construct VoteMsg
    auto vote_msg = std::make_shared<hotstuff::protobuf::VoteMsg>();
    s = ConstructVoteMsg(vote_msg, pro_msg.elect_height(), v_block);
    if (s != Status::kSuccess) {
        return;
    }
    // Construct HotstuffMessage and send
    auto pb_hotstuff_msg = std::make_shared<pb_HotstuffMessage>();
    s = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, pb_hotstuff_msg);
    if (s != Status::kSuccess) {
        return;
    }
    if (SendVoteMsg(pb_hotstuff_msg) != Status::kSuccess) {
        ZJC_ERROR("Send Propose message is error.");
    }
    
    return;
}

void Hotstuff::HandleVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    ZJC_DEBUG("====2.0 pool: %d, onVote, hash: %s",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str());
    
    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        ZJC_ERROR("vote message is error.");
        return;
    }
    ZJC_DEBUG("====2.1 pool: %d, onVote, hash: %s",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str());    
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    
    Status ret = crypto()->ReconstructAndVerifyThresSign(
            elect_height,
            vote_msg.view(),
            GetQCMsgHash(vote_msg.view(), vote_msg.view_block_hash()),
            replica_idx, 
            vote_msg.sign_x(),
            vote_msg.sign_y(),
            reconstructed_sign);
    
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_INFO("kBlsVerifyWaiting");
            return;
        }
        ZJC_ERROR("ReconstructAndVerify error");
        return;
    }
    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        reconstructed_sign == nullptr);

    auto qc = std::make_shared<QC>();
    Status s = crypto()->CreateQC(vote_msg.view_block_hash(), vote_msg.view(), reconstructed_sign, qc);
    if (s != Status::kSuccess) {
        return;
    }

    ZJC_DEBUG("====2.3 pool: %d, onVote, hash: %s, qc: %lu, %s, cur: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        qc->view,
        common::Encode::HexEncode(qc->view_block_hash).c_str(),
        pacemaker()->CurView());    
    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(qc));

    // std::this_thread::sleep_for(std::chrono::microseconds(1000ull));
    
    Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    ZJC_DEBUG("====2.4 pool: %d, onVote, hash: %s, qc: %lu, %s, cur: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        qc->view,
        common::Encode::HexEncode(qc->view_block_hash).c_str(),
        pacemaker()->CurView());    
    return;
}

std::shared_ptr<ViewBlock> Hotstuff::CheckCommit(
        const std::shared_ptr<ViewBlock>& v_block) {
    auto c = view_block_chain();
    if (!c) {
        return nullptr;
    }
    auto v_block1 = c->QCRef(v_block);
    if (!v_block1) {
        return nullptr;
    }    
    auto v_block2 = c->QCRef(v_block1);
    if (!v_block2) {
        return nullptr;
    }

    if (!c->LatestLockedBlock() || v_block2->view > c->LatestLockedBlock()->view) {
        ZJC_DEBUG("locked block, view: %lu", v_block2->view);
        c->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = c->QCRef(v_block2);
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
    Status s = CommitInner(view_block_chain(), acceptor(), v_block);
    if (s != Status::kSuccess) {
        return s;
    }
    // 剪枝
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    s = view_block_chain()->PruneTo(v_block->hash, forked_blockes, true);
    if (s != Status::kSuccess) {
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
        const uint32_t& elect_height) {
    Status ret = Status::kSuccess;
    auto block_view = v_block->view;
    if (!v_block->Valid()) {
        ZJC_ERROR("view block hash is error.");
        return Status::kError;
    }
    // view 必须最新
    if (view_block_chain->GetMaxHeight() >= v_block->view) {
        ZJC_ERROR("block view is error.");
        return Status::kError;
    }
    
    auto qc = v_block->qc;
    if (!qc) {
        ZJC_ERROR("qc 必须存在.");
        return Status::kError;
    }
    
    // qc 指针和哈希指针一致
    if (qc->view_block_hash != v_block->parent_hash) {
        ZJC_ERROR("qc ref is different from hash ref");
        return Status::kError;        
    }

    // 验证 qc
    if (crypto()->VerifyQC(qc, elect_height) != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error. elect_height: %llu, qc: %llu", elect_height, qc->view);
        return Status::kError; 
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
        ZJC_ERROR("block view message is error.");
        return Status::kError;
    }   

    return ret;
}


Status Hotstuff::CommitInner(
        const std::shared_ptr<ViewBlockChain>& c,
        const std::shared_ptr<IBlockAcceptor> accp, 
        const std::shared_ptr<ViewBlock>& v_block) {
    if (!c) {
        return Status::kError;
    }

    auto latest_committed_block = c->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view >= v_block->view) {
        return Status::kSuccess;
    }

    if (!latest_committed_block && v_block->view == GenesisView) {
        return Status::kSuccess;
    }

    std::shared_ptr<ViewBlock> parent_block = nullptr;
    Status s = c->Get(v_block->parent_hash, parent_block);
    if (s == Status::kSuccess && parent_block != nullptr) {
        s = CommitInner(c, accp, parent_block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    if (!accp) {
        return Status::kError;
    }

    v_block->block->set_is_commited_block(true);
    s = accp->Commit(v_block->block);
    if (s != Status::kSuccess) {
        return s;
    }
    
    c->SetLatestCommittedBlock(v_block);
    return Status::kSuccess;
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    uint32_t replica_idx = vote_msg.replica_idx();
    // 1、根据hash查找view_block；2、view_block.view <= heighQC.view
    // if (!view_block_chain()->Has(vote_msg.view_block_hash())) {
    //     // 2. TODO 延迟重试
    //     ZJC_ERROR("view_block_hash message is not exited.");
    //     return Status::kError;
    // }
    
    // view_block_chain()->Get(vote_msg.view_block_hash(), view_block);
    if (vote_msg.view() <= pacemaker()->HighQC()->view) {
        ZJC_ERROR("view message is not exited.");
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status Hotstuff::VerifyLeader(const std::shared_ptr<ViewBlock>& view_block) {
    uint32_t leader_idx = view_block->leader_idx;
    auto leader_rotation = std::make_shared<LeaderRotation>(pool_idx_, view_block_chain(), elect_info_);
    auto leader = leader_rotation->GetLeader(); // 判断是否为空
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
        return  Status::kError;
    }
    if (leader_idx != leader->index) {
        ZJC_ERROR("leader_idx message is error.");
        return Status::kError;
    }
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(
        const std::shared_ptr<SyncInfo>& sync_info,
        std::shared_ptr<hotstuff::protobuf::ProposeMsg>& pro_msg) {
    auto new_view_block = std::make_shared<ViewBlock>();
    auto tx_propose = std::make_shared<hotstuff::protobuf::TxPropose>();
    ConstructViewBlock(new_view_block, tx_propose);

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
        std::shared_ptr<hotstuff::protobuf::VoteMsg>& vote_msg,
        const uint32_t& elect_height, 
        const std::shared_ptr<ViewBlock>& v_block) {
    auto elect_item = elect_info_->GetElectItem(elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    uint32_t replica_idx = elect_item->LocalMember()->index;
    vote_msg->set_replica_idx(replica_idx);
    vote_msg->set_view_block_hash(v_block->hash);
    vote_msg->set_view(v_block->view);
    vote_msg->set_elect_height(elect_height);
    
    std::string sign_x, sign_y;
    if (crypto()->PartialSign(elect_height, GetQCMsgHash(v_block->view, v_block->hash), &sign_x, &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }
    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_x(sign_y);

    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    wrapper()->GetTxsIdempotently(txs);
    for (size_t i = 0; i < txs.size(); i++)
    {
        auto& tx_ptr = *(vote_msg->add_txs());
        tx_ptr = *(txs[i].get());
    }

    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock( 
        std::shared_ptr<ViewBlock>& view_block,
        std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) {
    auto high_qc = pacemaker()->HighQC();
    view_block->parent_hash = (high_qc->view_block_hash);

    auto leader_idx = elect_info_->GetElectItem()->LocalMember()->index;
    view_block->leader_idx = (leader_idx);

    auto pre_v_block = std::make_shared<ViewBlock>();
    view_block_chain()->Get(high_qc->view_block_hash, pre_v_block);
    auto pre_block = pre_v_block->block;
    auto pb_block = std::make_shared<block::protobuf::Block>();
    Status s = wrapper()->Wrap(pre_block, leader_idx, pb_block, tx_propose);
    if (s != Status::kSuccess) {
        ZJC_WARN("====0.1 pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }
    view_block->block = (pb_block);
    view_block->qc = high_qc;
    view_block->view = pacemaker()->CurView();
    view_block->hash = view_block->DoHash();
    return Status::kSuccess;
}

Status Hotstuff::ConstructHotstuffMsg(
        const MsgType msg_type, 
        const std::shared_ptr<pb_ProposeMsg>& pb_pro_msg, 
        const std::shared_ptr<pb_VoteMsg>& pb_vote_msg,
        std::shared_ptr<pb_HotstuffMessage>& pb_hotstuff_msg) {
    pb_hotstuff_msg->set_type(msg_type);
    switch (msg_type)
    {
    case PROPOSE:
        pb_hotstuff_msg->mutable_pro_msg()->CopyFrom(*pb_pro_msg);
        break;
    case VOTE:
        pb_hotstuff_msg->mutable_vote_msg()->CopyFrom(*pb_vote_msg);
        break;
    default:
        ZJC_ERROR("MsgType is error");
        break;
    }
    pb_hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    pb_hotstuff_msg->set_pool_index(pool_idx_);
    return Status::kSuccess;
}

Status Hotstuff::SendVoteMsg(std::shared_ptr<hotstuff::protobuf::HotstuffMessage>& hotstuff_msg) {
    Status ret = Status::kSuccess;
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& header_msg = trans_msg->header;
    header_msg.mutable_hotstuff()->CopyFrom(*hotstuff_msg);

    auto leader_rotation = std::make_shared<LeaderRotation>(pool_idx_, view_block_chain(), elect_info_);
    auto leader = leader_rotation->GetLeader();
    if (!leader) {
        ZJC_ERROR("Get Leader failed.");
        return Status::kError;
    }
    header_msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id);
    header_msg.set_des_dht_key(dht_key.StrKey());
    header_msg.set_type(common::kHotstuffMessage);
    transport::TcpTransport::Instance()->SetMessageHash(header_msg);
    transport::TcpTransport::Instance()->Send(common::Uint32ToIp(leader->public_ip), leader->public_port, header_msg);
    return ret;
}

} // namespace consensus

} // namespace shardora

