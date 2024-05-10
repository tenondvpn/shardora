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
    auto latest_view_block_self_qc = std::make_shared<QC>();
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block, latest_view_block_self_qc);
    if (s == Status::kSuccess) {
        // 初始状态，使用 db 中最后一个 view_block 初始化视图链
        view_block_chain_->Store(latest_view_block);
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        view_block_chain_->SetLatestCommittedBlock(latest_view_block);
        // 开启第一个视图

        pacemaker_->AdvanceView(new_sync_info()->WithQC(latest_view_block_self_qc));
        ZJC_DEBUG("has latest block, pool_idx: %d, latest block height: %lu", pool_idx_, latest_view_block->block->height());
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
    if (!sync_info->tc) {
        return;
    }

    pb_newview_msg->set_tc_str(sync_info->tc->Serialize());
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
    // 3 Verify TC
    std::shared_ptr<TC> tc = nullptr;
    if (!pro_msg.tc_str().empty()) {
        tc = std::make_shared<TC>();
        if (!tc->Unserialize(pro_msg.tc_str())) {
            ZJC_ERROR("tc Unserialize is error.");
            return;
        }
        if (tc->view > pacemaker()->HighTC()->view) {
            if (crypto()->VerifyTC(tc, pro_msg.elect_height()) != Status::kSuccess) {
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
    ZJC_DEBUG("====1.0 pool: %d, onPropose, view: %lu, hash: %s, qc_view: %lu",
        pool_idx_,
        pro_msg.view_item().view(),
        common::Encode::HexEncode(pro_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view);
    
    // view 必须最新
    // TODO 超时情况可能相同，严格限制并不影响共识，但会减少共识参与节点数
    if (HasVoted(v_block->view)) {
        ZJC_ERROR("pool: %d has voted: %lu", pool_idx_, v_block->view);
        return;
    }    
    
    // 2 Veriyfy Leader
    if (VerifyLeader(v_block) != Status::kSuccess) {
        ZJC_ERROR("verify leader failed, pool: %d has voted: %lu, hash64: %lu", 
            pool_idx_, v_block->view, header.hash64());
        return;
    }
    
    // 4 Verify ViewBlock    
    if (VerifyViewBlock(v_block, view_block_chain(), tc, pro_msg.elect_height()) != Status::kSuccess) {
        ZJC_ERROR("Verify ViewBlock is error. hash: %s",
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
    
    if (acceptor()->Accept(block_info, header, true) != Status::kSuccess) {
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
    
    if (SendVoteMsg(trans_msg) != Status::kSuccess) {
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
    // std::vector<std::shared_ptr<pools::protobuf::TxMessage>> tx_msgs;
    // for (const auto& tx : vote_msg.txs()) {
    //     tx_msgs.push_back(std::make_shared<pools::protobuf::TxMessage>(tx));
    // }
    acceptor()->AddTxs(header.hotstuff());
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

    auto qc = std::make_shared<QC>();
    Status s = crypto()->CreateQC(vote_msg.view_block_hash(), vote_msg.view(), reconstructed_sign, qc);
    if (s != Status::kSuccess) {
        return;
    }    
    // 切换视图
    pacemaker()->AdvanceView(new_sync_info()->WithQC(qc));
    Propose(new_sync_info()->WithQC(pacemaker()->HighQC()));
    return;
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
            if (crypto()->VerifyTC(tc, newview_msg.elect_height()) != Status::kSuccess) {
                ZJC_ERROR("VerifyTC error.");
                return;
            }

            ZJC_DEBUG("====3.2 pool: %d, tc: %lu, onNewview", pool_idx_, tc->view);
            pacemaker()->AdvanceView(new_sync_info()->WithTC(tc));
        }
    }
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
        if (crypto()->VerifyQC(qc, elect_height) != Status::kSuccess) {
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
        ZJC_ERROR("block view message is error.");
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

Status Hotstuff::VerifyLeader(const std::shared_ptr<ViewBlock>& view_block) {
    uint32_t leader_idx = view_block->leader_idx;
    auto leader = leader_rotation()->GetLeader(); // 判断是否为空
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
        return  Status::kError;
    }

    if (leader_idx != leader->index) {
        ZJC_ERROR("leader_idx message is error, %d, %d", leader_idx, leader->index);
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
    vote_msg->set_view(v_block->view);
    vote_msg->set_elect_height(elect_height);
    
    std::string sign_x, sign_y;
    if (crypto()->PartialSign(
                elect_height,
                GetQCMsgHash(v_block->view, v_block->hash),
                &sign_x,
                &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }

    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_y(sign_y);

    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    wrapper()->GetTxsIdempotently(txs);
    ZJC_DEBUG("get tx size: %u", txs.size());
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
    view_block->parent_hash = (pacemaker()->HighQC()->view_block_hash);
    auto leader_idx = elect_info_->GetElectItem()->LocalMember()->index;
    view_block->leader_idx = leader_idx;

    auto pre_v_block = std::make_shared<ViewBlock>();
    Status s = view_block_chain()->Get(view_block->parent_hash, pre_v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("parent view block has not found, pool: %d, view: %lu, parent_view: %lu", pool_idx_, pacemaker()->CurView(), pacemaker()->HighQC()->view);
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

Status Hotstuff::SendVoteMsg(std::shared_ptr<transport::TransportMessage>& trans_msg) {
    Status ret = Status::kSuccess;
    auto& header_msg = trans_msg->header;
    auto leader = leader_rotation()->GetLeader();
    if (!leader) {
        ZJC_ERROR("Get Leader failed.");
        return Status::kError;
    }

    header_msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id, leader->id);
    header_msg.set_des_dht_key(dht_key.StrKey());
    header_msg.set_type(common::kHotstuffMessage);
    transport::TcpTransport::Instance()->SetMessageHash(header_msg);    
    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        if (leader->public_ip == 0 || leader->public_port == 0) {
            network::Route::Instance()->Send(trans_msg);
        } else {
            transport::TcpTransport::Instance()->Send(
                common::Uint32ToIp(leader->public_ip), 
                leader->public_port, 
                header_msg);
        }
    } else {
        HandleVoteMsg(header_msg);
    }
 
    ZJC_DEBUG("send to leader vote message to leader net: %u, %s, hash64: %lu", 
        leader->net_id, 
        common::Encode::HexEncode(leader->id).c_str(), 
        header_msg.hash64());
    return ret;
}

} // namespace consensus

} // namespace shardora

