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
    last_vote_view_ = GenesisView;
    
    auto latest_view_block = std::make_shared<ViewBlock>();
    std::string qc_str;
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block, &qc_str);
    if (s == Status::kSuccess) {
        auto latest_view_block_commit_qc = std::make_shared<QC>(qc_str);
        if (!latest_view_block_commit_qc->valid() || latest_view_block_commit_qc->view() < GenesisView) {
            latest_view_block_commit_qc = GetGenesisQC(pool_idx_, latest_view_block->hash);
        }

        ZJC_DEBUG("pool: %d, latest vb from db, vb view: %lu, self_commit_qc view: %lu",
            pool_idx_, latest_view_block->view, latest_view_block_commit_qc->view());

        // 初始状态，使用 db 中最后一个 view_block 初始化视图链
        view_block_chain_->Store(latest_view_block);
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        view_block_chain_->SetLatestCommittedBlock(latest_view_block);
        // 开启第一个视图

        ZJC_DEBUG("init changed latest commited block %u_%u_%lu, new view: %lu",
            view_block_chain_->LatestCommittedBlock()->block->network_id(), 
            view_block_chain_->LatestCommittedBlock()->block->pool_index(), 
            view_block_chain_->LatestCommittedBlock()->block->height(),
            view_block_chain_->LatestCommittedBlock()->view);
        pacemaker_->AdvanceView(new_sync_info()->WithQC(latest_view_block_commit_qc));
        ZJC_DEBUG("has latest block, pool_idx: %d, latest block height: %lu, commit_qc_hash: %s, latest_view_block: %s, high_qc_hash: %s",
            pool_idx_, latest_view_block->block->height(),
            common::Encode::HexEncode(latest_view_block_commit_qc->view_block_hash()).c_str(),
            common::Encode::HexEncode(view_block_chain_->LatestLockedBlock()->hash).c_str(),
            common::Encode::HexEncode(pacemaker_->HighQC()->view_block_hash()).c_str());
    } else {
        ZJC_DEBUG("no genesis, waiting for syncing, pool_idx: %d", pool_idx_);
    }

    InitHandleProposeMsgPipeline();
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
        Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    }
    return Status::kSuccess;
}

Status Hotstuff::Propose(const std::shared_ptr<SyncInfo>& sync_info) {
    // TODO(HT): 打包的交易，超时后如何释放？
    // 打包参与共识中的交易，如何保证幂等
    ZJC_DEBUG("1 now ontime called propose: %d", pool_idx_);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();
    Status s = ConstructProposeMsg(sync_info, pb_pro_msg);
    if (s != Status::kSuccess) {
        ZJC_WARN("pool: %d construct propose msg failed, %d, member_index: %d",
            pool_idx_, s, 
            elect_info_->GetElectItemWithShardingId(
                common::GlobalInfo::Instance()->network_id())->LocalMember()->index);
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
    ZJC_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, gid: %s",
        pool_idx_,
        header.hotstuff().pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view(),
        header.hash64(),
        (hotstuff_msg->pro_msg().tx_propose().txs_size() > 0 ? 
        common::Encode::HexEncode(hotstuff_msg->pro_msg().tx_propose().txs(0).gid()).c_str() :
        ""));
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
    pb_newview_msg->set_elect_height(
        elect_info_->GetElectItemWithShardingId(
            common::GlobalInfo::Instance()->network_id())->ElectHeight());
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
    ZJC_DEBUG("pool: %d, msg pool: %d, newview, txs size: %lu, view: %lu, "
        "hash: %s, qc_view: %lu, tc_view: %lu hash64: %lu",
        pool_idx_,
        hotstuff_msg->pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view(),
        pacemaker()->HighTC()->view(),
        header.hash64());
    HandleNewViewMsg(header);
    return;    
}

void Hotstuff::HandleProposeMsg(const transport::protobuf::Header& header) {
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    auto b = common::TimeUtils::TimestampMs();
    defer({
        auto e = common::TimeUtils::TimestampMs();
        ZJC_DEBUG("pool: %d handle propose duration: %lu ms", pool_idx_, e-b);
    });

    auto pro_msg_wrap = std::make_shared<ProposeMsgWrapper>(header);
    view_block::protobuf::ViewBlockItem pb_view_block = pro_msg_wrap->header.hotstuff().pro_msg().view_item();
    auto v_block = std::make_shared<ViewBlock>();
    Status s = Proto2ViewBlock(pb_view_block, v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pb_view_block to ViewBlock is error.");
        return;
    }
    pro_msg_wrap->v_block = v_block;

    // 执行预设好的 steps
    // 一般来说，一旦某个节点状态落后，而新提案由于还没有生成 QC 无法通过同步获得，
    // 因此它将再也无法参与投票（由于父块缺失，chain->Store 会失败），直到一次超时发生
    // 为了避免这种情况，pipeline 会自动将失败的提案消息放入等待队列，在父块同步过来后立刻执行之前失败的提案，追上进度
    handle_propose_pipeline_.Call(pro_msg_wrap);
}

Status Hotstuff::HandleProposeMsgStep_HasVote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    if (HasVoted(pro_msg_wrap->v_block->view)) {
        ZJC_DEBUG("pool: %d has voted: %lu, last_vote_view_: %u, hash64: %lu",
            pool_idx_, pro_msg_wrap->v_block->view, last_vote_view_, pro_msg_wrap->header.hash64());
        return Status::kError;
    }
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    if (VerifyLeader(pro_msg_wrap->v_block->leader_idx) != Status::kSuccess) {
        // TODO 一旦某个节点状态滞后，那么 Leader 就与其他 replica 不同，导致无法处理新提案
        // 只能依赖同步，但由于同步慢于新的 Propose 消息
        // 即是这里再加一次同步，也很难追上 Propose 的速度，导致该节点掉队，因此还是需要一个队列缓存一下
        // 暂时无法处理的 Propose 消息
        if (sync_pool_fn_) { // leader 不一致触发同步
            sync_pool_fn_(pool_idx_, 1);
        }
        ZJC_ERROR("verify leader failed, pool: %d view: %lu, hash64: %lu", 
            pool_idx_, pro_msg_wrap->v_block->view, pro_msg_wrap->header.hash64());

        return Status::kError;
    }        
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 3 Verify TC
    std::shared_ptr<TC> tc = nullptr;
    if (!pro_msg_wrap->pro_msg->tc_str().empty()) {
        tc = std::make_shared<TC>(pro_msg_wrap->pro_msg->tc_str());
        if (!tc->valid()) {
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

Status Hotstuff::HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    if (VerifyQC(pro_msg_wrap->v_block->qc) != Status::kSuccess) {
        ZJC_ERROR("pool: %d verify qc failed: %lu", pool_idx_, pro_msg_wrap->v_block->view);
        return Status::kError;
    }

    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(pro_msg_wrap->v_block->qc));

    // Commit 一定要在 Txs Accept 之前，因为一旦 v_block->qc 合法就已经可以 Commit 了，不需要 Txs 合法
    // Commit 不能在 VerifyViewBlock 之后
    TryCommit(pro_msg_wrap->v_block->qc, 99999999lu);    
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
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
        pacemaker()->HighQC()->view(),
        pro_msg_wrap->header.hash64());        
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
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
                pacemaker()->HighQC()->view(),
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
            pacemaker()->HighQC()->view(),
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

Status Hotstuff::HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // 6 add view block
    ZJC_DEBUG("success store v block pool: %u, hash: %s, prehash: %s",
        pool_idx_,
        common::Encode::HexEncode(pro_msg_wrap->v_block->hash).c_str(),
        common::Encode::HexEncode(pro_msg_wrap->v_block->parent_hash).c_str());
    Status s = view_block_chain()->Store(pro_msg_wrap->v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, add view block error. hash: %s",
            pool_idx_, common::Encode::HexEncode(pro_msg_wrap->v_block->hash).c_str());
        // 父块不存在，则加入等待队列，后续处理
        if (s == Status::kLackOfParentBlock && sync_pool_fn_) { // 父块缺失触发同步
            sync_pool_fn_(pool_idx_, 1);
        }
        return Status::kError;
    }
    // 成功接入链中，标记交易占用
    acceptor()->MarkBlockTxsAsUsed(pro_msg_wrap->v_block->block);        
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_Vote(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    // NOTICE: pipeline 重试时，protobuf 结构体被析构，因此 pro_msg_wrap->header.hash64() 是 0
    ZJC_INFO("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, curView: %lu, vblock: %lu, txs: %lu, hash64: %lu",
        pool_idx_,
        pacemaker()->HighQC()->view(),
        pacemaker()->HighTC()->view(),
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

void Hotstuff::HandleVoteMsg(const transport::protobuf::Header& header) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d handle vote duration: %lu ms", pool_idx_, e-b);
        });
    
    auto& vote_msg = header.hotstuff().vote_msg();
    ZJC_DEBUG("====2.0 pool: %d, onVote, hash: %s, view: %lu, replica: %lu, hash64: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        vote_msg.replica_idx(),
        header.hash64());
    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        ZJC_WARN("vote message is error.");
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
        ZJC_DEBUG("pool : %u, handle backup vote message get tx type: %d, to: %s, gid: %s", 
            pool_idx_,
            tx.step(), 
            common::Encode::HexEncode(tx.to()).c_str(), 
            common::Encode::HexEncode(tx.gid()).c_str());
    }
    acceptor()->AddTxs(tx_msgs);
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    auto qc_ptr = std::make_shared<QC>(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        nullptr,
        vote_msg.view(),
        vote_msg.view_block_hash(),
        vote_msg.commit_view_block_hash(),
        elect_height,
        vote_msg.leader_idx());
    Status ret = crypto()->ReconstructAndVerifyThresSign(
            elect_height,
            vote_msg.view(),
            qc_ptr->msg_hash(),
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
    auto new_qc = std::make_shared<QC>(
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        reconstructed_sign, 
        vote_msg.view(), 
        vote_msg.view_block_hash(),
        vote_msg.commit_view_block_hash(),
        elect_height,
        vote_msg.leader_idx());
#ifndef NDEBUG
    block::protobuf::Block block;
    if (!prefix_db_->GetBlockWithHeight(
            network::kRootCongressNetworkId,
            common::GlobalInfo::Instance()->network_id() % common::kImmutablePoolSize,
            elect_height,
            &block)) {
        ZJC_INFO("failed get block with height net: %u, pool: %u, height: %lu",
            network::kRootCongressNetworkId, common::GlobalInfo::Instance()->network_id(), elect_height);
        // assert(false);
        // return;
    }

    // assert(block.tx_list_size() > 0);
#endif

    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(new_qc));
    // 先单独广播新 qc，即是 leader 出不了块也不用额外同步 HighQC，这比 Gossip 的效率:q高很多
    ZJC_DEBUG("NewView propose newview called pool: %u, qc_view: %lu, tc_view: %lu",
        pool_idx_, pacemaker()->HighQC()->view(), pacemaker()->HighTC()->view());
    auto s = Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    if (s != Status::kSuccess) {
        NewView(new_sync_info()->WithQC(pacemaker()->HighQC())->WithTC(pacemaker()->HighTC()));
    }
    return;
}

Status Hotstuff::StoreVerifiedViewBlock(const std::shared_ptr<ViewBlock>& v_block, const std::shared_ptr<QC>& qc) {
    if (view_block_chain()->Has(qc->view_block_hash())) {
        return Status::kSuccess;    
    }
    if (v_block->hash != qc->view_block_hash() || v_block->view != qc->view()) {
        return Status::kError;
    }

    Status s = acceptor()->AcceptSync(v_block->block);
    if (s != Status::kSuccess) {
        return s;
    }

    TryCommit(v_block->qc, 99999999lu);
    ZJC_DEBUG("success store v block pool: %u, hash: %s, prehash: %s",
        pool_idx_,
        common::Encode::HexEncode(v_block->hash).c_str(),
        common::Encode::HexEncode(v_block->parent_hash).c_str());
    return view_block_chain()->Store(v_block);
}

void Hotstuff::HandleNewViewMsg(const transport::protobuf::Header& header) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d HandleNewViewMsg duration: %lu ms, hash64: %lu", pool_idx_, e-b, header.hash64());
        });

    static uint64_t test_index = 0;
    ++test_index;
    ZJC_DEBUG("====3.1 pool: %d, newview, message pool: %d, hash64: %lu, test_index: %lu",
        pool_idx_, header.hotstuff().pool_index(), header.hash64(), test_index);
    assert(header.hotstuff().pool_index() == pool_idx_);
    auto& newview_msg = header.hotstuff().newview_msg();
    std::shared_ptr<TC> tc = nullptr;
    if (!newview_msg.tc_str().empty()) {
        ZJC_DEBUG("pool index: %u,  0 test_index: %lu", pool_idx_, test_index);
        tc = std::make_shared<TC>(newview_msg.tc_str());
        if (!tc->valid()) {
            ZJC_ERROR("tc Unserialize is error.");
            return;
        }

        if (tc->view() > pacemaker()->HighTC()->view()) {
            ZJC_DEBUG("newview now verify tc hash: %s, pool index: %u", 
                common::Encode::HexEncode(tc->msg_hash()).c_str(), pool_idx_);
            if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
                ZJC_ERROR("VerifyTC error.");
                return;
            }

            ZJC_DEBUG("====3.2 pool: %d, tc: %lu, onNewview", pool_idx_, tc->view());
            ZJC_DEBUG("pool index: %u,  1 test_index: %lu", pool_idx_, test_index);
            pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));
            ZJC_DEBUG("pool index: %u,  2 test_index: %lu", pool_idx_, test_index);
        }
    }

    std::shared_ptr<QC> qc = nullptr;
    if (!newview_msg.qc_str().empty()) {
            ZJC_DEBUG("pool index: %u,  3 test_index: %lu", pool_idx_, test_index);
        qc = std::make_shared<QC>(newview_msg.qc_str());
        if (!qc->valid()) {
            ZJC_ERROR("qc Unserialize is error.");
            return;
        }
        if (qc->view() > pacemaker()->HighQC()->view()) {
            ZJC_DEBUG("pool index: %u,  4 test_index: %lu", pool_idx_, test_index);
            if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
                ZJC_ERROR("VerifyQC error.");
                return;
            }

            ZJC_DEBUG("====3.3 pool: %d, qc: %lu, onNewview, test_index: %lu",
                pool_idx_, qc->view(), test_index);
            pacemaker()->AdvanceView(new_sync_info()->WithQC(qc));
            ZJC_DEBUG("pool index: %u,  5 test_index: %lu", pool_idx_, test_index);
            
            TryCommit(qc, test_index);
            ZJC_DEBUG("pool index: %u,  6 test_index: %lu", pool_idx_, test_index);
        }
    }    
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

    // Propose(new_sync_info()->WithQC(pacemaker()->HighQC())->WithTC(pacemaker()->HighTC()));
    ResetReplicaTimers();
    ZJC_DEBUG("reset timer success!");
}

Status Hotstuff::ResetReplicaTimers() {
    // Reset timer msg broadcast
    auto rst_timer_msg = std::make_shared<hotstuff::protobuf::ResetTimerMsg>();
    auto local_index = leader_rotation()->GetLocalMemberIdx();
    if (local_index == common::kInvalidUint32) {
        return Status::kError;
    }

    rst_timer_msg->set_leader_idx(local_index);
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
    auto elect_item = elect_info_->GetElectItemWithShardingId(
            common::GlobalInfo::Instance()->network_id());
    if (elect_item == nullptr) {
        assert(false);
        return;
    }

    if (elect_item->LocalMember() == nullptr) {
        // assert(false);
        return;
    }

    ZJC_DEBUG("====5.1 pool: %d, onResetTimer leader_idx: %u, local_idx: %u, hash64: %lu",
        pool_idx_, rst_timer_msg.leader_idx(),
        elect_item->LocalMember()->index,
        header.hash64());
    // TODO(有逻辑安全性问题)，必须是验证聚合签名才能改变本地状态
    // leader 必须不需要保证正确
    // if (VerifyLeader(rst_timer_msg.leader_idx()) != Status::kSuccess) {
    //     if (sync_pool_fn_) { // leader 不一致触发同步
    //         sync_pool_fn_(pool_idx_, 1);
    //     }
    //     return;
    // }
    // 必须处于 stuck 状态
    auto stuck_st = IsStuck();
    if (stuck_st != 0) {
        ZJC_DEBUG("reset timer failed: %u, hash64: %lu, status: %d",
            pool_idx_, header.hash64(), stuck_st);
        return;
    }
    // ZJC_DEBUG("====5.2 pool: %d, ResetTimer", pool_idx_);
    // reset pacemaker view duration
    pacemaker()->ResetViewDuration(std::make_shared<ViewDuration>(
                pool_idx_,
                ViewDurationSampleSize,
                ViewDurationStartTimeoutMs,
                ViewDurationMaxTimeoutMs,
                ViewDurationMultiplier));
    ZJC_DEBUG("reset timer success: %u, hash64: %lu", pool_idx_, header.hash64());
    return;
}

Status Hotstuff::TryCommit(const std::shared_ptr<QC> commit_qc, uint64_t test_index) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d TryCommit duration: %lu ms", pool_idx_, e-b);
        });
    
    if (!commit_qc) {
        return Status::kInvalidArgument;
    }

    auto v_block_to_commit = CheckCommit(commit_qc);
    if (v_block_to_commit) {
        Status s = Commit(v_block_to_commit, commit_qc, test_index);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->view,
                common::Encode::HexEncode(v_block_to_commit->hash).c_str());
            return s;
        }
    }
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> Hotstuff::CheckCommit(const std::shared_ptr<QC>& qc) {
    std::shared_ptr<ViewBlock> v_block1 = nullptr; 
    view_block_chain()->Get(qc->view_block_hash(), v_block1);
    if (!v_block1) {
        return nullptr;
    }    
    auto v_block2 = view_block_chain()->QCRef(v_block1);
    if (!v_block2) {
        return nullptr;
    }

    if (!view_block_chain()->LatestLockedBlock() || v_block2->view > view_block_chain()->LatestLockedBlock()->view) {
        view_block_chain()->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = view_block_chain()->QCRef(v_block2);
    if (!v_block3) {
        return nullptr;
    }

    if (v_block1->parent_hash == v_block2->hash && v_block2->parent_hash == v_block3->hash) {
        return v_block3;
    }
    
    return nullptr;
}

Status Hotstuff::Commit(
        const std::shared_ptr<ViewBlock>& v_block,
        const std::shared_ptr<QC> commit_qc,
        uint64_t test_index) {
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d Commit duration: %lu ms, test_index: %lu", pool_idx_, e-b, test_index);
        });

    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view >= v_block->view) {
        return Status::kSuccess;
    }
    
    auto tmp_block = v_block;
    while (tmp_block != nullptr) {
        auto db_batch = std::make_shared<db::DbWriteBatch>();
        auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(tmp_block->block, db_batch);
        view_block_chain()->StoreToDb(tmp_block, commit_qc, test_index, db_batch);
        CommitInner(tmp_block, test_index, queue_item_ptr, commit_qc);
        std::shared_ptr<ViewBlock> parent_block = nullptr;
        Status s = view_block_chain()->Get(tmp_block->parent_hash, parent_block);
        if (s != Status::kSuccess || parent_block == nullptr) {
            break;
        }

        tmp_block = parent_block;
    }
    
    // 剪枝
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    auto s = view_block_chain()->PruneTo(v_block->hash, forked_blockes, true);
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

Status Hotstuff::VerifyQC(const std::shared_ptr<QC>& qc) {
    // 验证 qc
    if (!qc) {
        return Status::kError;
    }
    if (qc->view() > pacemaker()->HighQC()->view()) {
        if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
            return Status::kError; 
        }
    }    
    return Status::kSuccess;
}

Status Hotstuff::VerifyTC(const std::shared_ptr<TC>& tc) {
    if (!tc) {
        return Status::kError;
    }
    if (tc->view() > pacemaker()->HighTC()->view()) {
        if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
            ZJC_ERROR("VerifyTC error.");
            return Status::kError;
        }
        pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));            
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
    if (qc->view() + 1 != v_block->view && tc && tc->view() + 1 != v_block->view) {
        ZJC_ERROR("block view is error.");
        return Status::kError;
    }
    
    // qc 指针和哈希指针一致
    if (qc->view_block_hash() != v_block->parent_hash) {
        ZJC_ERROR("qc ref is different from hash ref");
        return Status::kError;        
    }

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block;
    if (view_block_chain->Get(qc->view_block_hash(), qc_view_block) != Status::kSuccess 
        && !view_block_chain->Extends(v_block, qc_view_block)) {
        ZJC_ERROR("qc view block message is error.");
        return Status::kError;
    }

    if (view_block_chain->LatestLockedBlock() &&
        !view_block_chain->Extends(v_block, view_block_chain->LatestLockedBlock()) && 
        v_block->qc->view() <= view_block_chain->LatestLockedBlock()->view) {
        ZJC_ERROR("pool: %d, block view message is error. %lu, %lu, %s, %s",
            pool_idx_, v_block->qc->view(), view_block_chain->LatestLockedBlock()->view,
            common::Encode::HexEncode(view_block_chain->LatestLockedBlock()->hash).c_str(),
            common::Encode::HexEncode(v_block->parent_hash).c_str());
        return Status::kError;
    }   

    return ret;
}

void Hotstuff::CommitInner(
        const std::shared_ptr<ViewBlock>& v_block, 
        uint64_t test_index, 
        std::shared_ptr<block::BlockToDbItem>& queue_block_item,
        const std::shared_ptr<QC>& commit_qc) {
    ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu",
        pool_idx_, 0, v_block->view,
        v_block->block->network_id(), v_block->block->pool_index(), v_block->block->height(),
        view_block_chain()->String().c_str(),
        test_index);
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view >= v_block->view) {
        ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit failed s: %d, "
            "vb view: %lu, %u_%u_%lu, latest_committed_block: %d, "
            "latest_committed_block->view: %lu, v_block->view: %lu",
            pool_idx_, 0, v_block->view,
            v_block->block->network_id(), v_block->block->pool_index(), v_block->block->height(),
            (latest_committed_block != nullptr),
            latest_committed_block->view,
            v_block->view);
        return;
    }

    if (!latest_committed_block && v_block->view == GenesisView) {
        ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit failed s: %d, vb view: %lu, %u_%u_%lu",
            pool_idx_, 0, v_block->view,
            v_block->block->network_id(), v_block->block->pool_index(), v_block->block->height());
        return;
    }

    v_block->block->set_is_commited_block(true);
    ZJC_DEBUG("1 NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu",
        pool_idx_, 0, v_block->view,
        v_block->block->network_id(), v_block->block->pool_index(), v_block->block->height(),
        view_block_chain()->String().c_str(),
        test_index);

    auto vblock_with_proof = std::make_shared<ViewBlockWithCommitQC>(v_block, commit_qc);
    acceptor()->Commit(queue_block_item, vblock_with_proof);
    ZJC_DEBUG("2 NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
        "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu",
        pool_idx_, 0, v_block->view,
        v_block->block->network_id(), v_block->block->pool_index(), v_block->block->height(),
        view_block_chain()->String().c_str(),
        test_index);

    // 提交 v_block->consensus_stat 共识数据
    auto elect_item = elect_info()->GetElectItem(
            v_block->block->network_id(),
            v_block->ElectHeight());
    if (elect_item && elect_item->IsValid()) {
        elect_item->consensus_stat(pool_idx_)->Commit(v_block);
    }    
    
    view_block_chain()->SetLatestCommittedBlock(v_block);
    ZJC_DEBUG("pool: %d consensus stat, leader: %lu, succ: %lu, test_index: %lu",
        pool_idx_, v_block->leader_idx,
        elect_item->consensus_stat(pool_idx_)->GetMemberConsensusStat(v_block->leader_idx)->succ_num,
        test_index);
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
    uint32_t replica_idx = vote_msg.replica_idx();

    if (vote_msg.view() <= pacemaker()->HighQC()->view()) {
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

Status Hotstuff::ConstructProposeMsg(
        const std::shared_ptr<SyncInfo>& sync_info,
        hotstuff::protobuf::ProposeMsg* pro_msg) {
    auto new_view_block = std::make_shared<ViewBlock>();
    auto tx_propose = std::make_shared<hotstuff::protobuf::TxPropose>();
    Status s = ConstructViewBlock(new_view_block, tx_propose);
    if (s != Status::kSuccess) {
        ZJC_WARN("pool: %d construct view block failed, view: %lu, %d, member_index: %d",
            pool_idx_, pacemaker()->HighQC()->view(), s, 
            elect_info_->GetElectItemWithShardingId(
                common::GlobalInfo::Instance()->network_id())->LocalMember()->index);        
        return s;
    }

    auto new_pb_view_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
    ViewBlock2Proto(new_view_block, new_pb_view_block.get());
    pro_msg->mutable_view_item()->CopyFrom(*new_pb_view_block);
    pro_msg->set_elect_height(
        elect_info_->GetElectItemWithShardingId(
            common::GlobalInfo::Instance()->network_id())->ElectHeight());
    if (sync_info->tc) {
        pro_msg->set_tc_str(sync_info->tc->Serialize());
    }
    pro_msg->mutable_tx_propose()->CopyFrom(*tx_propose);
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
    auto qc_ptr = std::make_shared<QC>(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        nullptr,
        v_block->view,
        v_block->hash,
        commit_view_block_hash,
        elect_height,
        v_block->leader_idx);
    std::string sign_x, sign_y;
    if (crypto()->PartialSign(
                common::GlobalInfo::Instance()->network_id(),
                elect_height,
                qc_ptr->msg_hash(),
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
    }

    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock( 
        std::shared_ptr<ViewBlock>& view_block,
        std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) {
    view_block->parent_hash = (pacemaker()->HighQC()->view_block_hash());
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
    view_block->leader_idx = leader_idx;
    auto pre_v_block = std::make_shared<ViewBlock>();
    Status s = view_block_chain()->Get(view_block->parent_hash, pre_v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("parent view block has not found, pool: %d, view: %lu, "
            "parent_view: %lu, leader: %lu, chain: %s",
            pool_idx_,
            pacemaker()->CurView(),
            pacemaker()->HighQC()->view(),
            leader_idx,
            view_block_chain()->String().c_str());
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
    s = wrapper()->Wrap(
        pre_v_block, 
        leader_idx, 
        pb_block, 
        tx_propose, 
        IsEmptyBlockAllowed(view_block), 
        view_block_chain_);
    if (s != Status::kSuccess) {
        ZJC_WARN("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }

    view_block->block = pb_block;
    auto elect_item = elect_info_->GetElectItem(
            common::GlobalInfo::Instance()->network_id(), view_block->ElectHeight());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }
    
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
            HandleVoteMsg(header_msg);
        } else if (msg_type == PRE_RESET_TIMER) {
            HandlePreResetTimerMsg(header_msg);
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

void Hotstuff::TryRecoverFromStuck() {
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    if (timer_delay_us_ > common::TimeUtils::TimestampUs()) {
        return;
    }

    auto stuck_st = IsStuck();
    if (recover_from_struct_fc_.Permitted() && stuck_st == 0) {
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
            pre_rst_timer_msg->set_has_single_tx(has_single_tx);

            hotstuff_msg->set_type(PRE_RESET_TIMER);
            hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
            hotstuff_msg->set_pool_index(pool_idx_);
            SendMsgToLeader(trans_msg, PRE_RESET_TIMER);
            ZJC_DEBUG("pool: %d, send prereset msg from: %lu to: %lu, has_single_tx: %d",
                pool_idx_, pre_rst_timer_msg->replica_idx(), leader_rotation_->GetLeader()->index, has_single_tx);
        }
    }
}

uint32_t Hotstuff::GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block) {
    uint32_t ret = 1;
    auto current = v_block;
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (!latest_committed_block) {
        return ret;
    }
    while (current->view > latest_committed_block->view) {
        current = view_block_chain()->QCRef(current);
        if (!current) {
            return ret;
        }
        if (current->leader_idx == v_block->leader_idx) {
            ret++;
        }
    }

    ZJC_DEBUG("pool: %d add succ num: %lu, leader: %lu", pool_idx_, ret, v_block->leader_idx);
    return ret;
}

} // namespace consensus

} // namespace shardora

