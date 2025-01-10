#include <bls/agg_bls.h>
#include <bls/bls_dkg.h>
#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <common/defer.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/utils.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <protos/hotstuff.pb.h>
#include <protos/pools.pb.h>
#include <protos/view_block.pb.h>
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

void Hotstuff::Init() {
    // set pacemaker timeout callback function
    last_vote_view_ = GenesisView;
    
    auto latest_view_block = std::make_shared<ViewBlock>();
    // 从 db 中获取最后一个有 QC 的 ViewBlock
    Status s = GetLatestViewBlockFromDb(db_, pool_idx_, latest_view_block);
    if (s == Status::kSuccess) {
        if (!latest_view_block->has_self_commit_qc() || latest_view_block->self_commit_qc().view() < GenesisView) {
            // latest_view_block doesn't have a commit qc, it must be a genesis block
            // genesis blocks have a default commit qc
            latest_view_block->mutable_self_commit_qc()->CopyFrom(*GetGenesisQC(pool_idx_, latest_view_block->hash()));
        }
        
        view_block_chain_->SetLatestLockedBlock(latest_view_block);
        view_block_chain_->SetLatestCommittedBlock(latest_view_block);
        InitAddNewViewBlock(latest_view_block);
        // get all children blocks of latest commit block
        // LoadAllViewBlockWithLatestCommitedBlock(latest_view_block);
    } else {
        ZJC_DEBUG("no genesis, waiting for syncing, pool_idx: %d", pool_idx_);
    }

    InitHandleProposeMsgPipeline();
}

// xufeisofly ? 已经是 latest_committed_block 了为什么还有子块，只有 committed
// block 会被保存才对
// Deprecated
// void Hotstuff::LoadAllViewBlockWithLatestCommitedBlock(
//         std::shared_ptr<ViewBlock>& view_block) {
//     std::vector<std::shared_ptr<ViewBlock>> children_view_blocks;
//     prefix_db_->GetChildrenViewBlock(
//             view_block->hash(), 
//             children_view_blocks);
//     ZJC_DEBUG("init load view block %u_%u_%lu, %lu, hash: %s, phash: %s, size: %u",
//         view_block->network_id(), view_block->pool_index(), 
//         view_block->view(), view_block->block_info().height(),
//         common::Encode::HexEncode(view_block->hash()).c_str(),
//         common::Encode::HexEncode(view_block->parent_hash()).c_str(),
//         children_view_blocks.size());
//     for (auto iter = children_view_blocks.begin(); iter != children_view_blocks.end(); ++iter) {
//         assert(!view_block_chain_->Has((*iter)->hash()));
//         InitAddNewViewBlock(*iter);
//         LoadAllViewBlockWithLatestCommitedBlock(*iter);
//     }
// }
    
void Hotstuff::InitAddNewViewBlock(std::shared_ptr<ViewBlock>& latest_view_block) {
    ZJC_WARN("pool: %d, latest vb from db, vb view: %lu",
        pool_idx_, 
        latest_view_block->view());
    // 初始状态，使用 db 中最后一个 view_block 初始化视图链
    // TODO: check valid
    view_block_chain_->Store(latest_view_block, true, nullptr, nullptr);
    pacemaker()->AdvanceView(new_sync_info()->WithQC(std::make_shared<QC>(latest_view_block->self_commit_qc())));
    StopVoting(latest_view_block->view());
    // 开启第一个视图
    ZJC_WARN("success new set qc view: %lu, %u_%u_%lu, hash: %s",
        latest_view_block->view(),
        latest_view_block->network_id(),
        latest_view_block->pool_index(),
        latest_view_block->view(),
        common::Encode::HexEncode(latest_view_block->hash()).c_str());
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
        Propose(new_sync_info()->WithQC(pacemaker()->HighQC()), nullptr);
    }
    return Status::kSuccess;
}

Status Hotstuff::Propose(
        const std::shared_ptr<SyncInfo>& sync_info,
        const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // TODO(HT): 打包的交易，超时后如何释放？
    // 打包参与共识中的交易，如何保证幂等
    auto dht_ptr = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht_ptr) {
        ZJC_DEBUG("pool %u not has dht ptr.", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    if (readobly_dht->size() < 2) {
        ZJC_DEBUG("pool %u not has readobly_dht->size() < 2", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();

    auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = tmp_msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto* hotstuff_msg = header.mutable_hotstuff();
    auto* pb_pro_msg = hotstuff_msg->mutable_pro_msg();

    Status s = ConstructProposeMsg(msg_ptr, sync_info, pb_pro_msg);

    if (s != Status::kSuccess) {
        ZJC_WARN("pool: %d construct propose msg failed, %d",
            pool_idx_, s);
        return s;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    s = ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().view());
        return s;
    }

    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }

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
        hotstuff_msg->pro_msg().view_item().view(),
        header.hash64(),
        propose_debug_index_++,
        pb_pro_msg->tx_propose().txs_size());

    transport::protobuf::ConsensusDebug consensus_debug;
    consensus_debug.add_messages(propose_debug_str);
    for (uint32_t i = 0; i < common::kEachShardMaxNodeCount; ++i) {
        consensus_debug.add_vote_timestamps(0);
    }

    consensus_debug.set_begin_timestamp(common::TimeUtils::TimestampMs());
    header.set_debug(consensus_debug.SerializeAsString());
    ZJC_DEBUG("leader begin propose_debug: %s", ProtobufToJson(consensus_debug).c_str());
#else
    header.set_debug(std::to_string(common::TimeUtils::TimestampMs()));
#endif
    s = crypto()->SignMessage(tmp_msg_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("sign message failed pool: %d, view: %lu, construct hotstuff msg failed",
            pool_idx_, hotstuff_msg->pro_msg().view_item().view());
        return s;
    }

    transport::TcpTransport::Instance()->AddLocalMessage(tmp_msg_ptr);
    network::Route::Instance()->Send(tmp_msg_ptr);

    ADD_DEBUG_PROCESS_TIMESTAMP();

#ifndef NDEBUG
    ZJC_DEBUG("pool: %d, header pool: %d, propose, txs size: %lu, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        header.hotstuff().pool_index(),
        hotstuff_msg->pro_msg().tx_propose().txs_size(),
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view(),
        header.hash64(),
        ProtobufToJson(consensus_debug).c_str());
#endif
    tmp_msg_ptr->is_leader = true;
    // HandleProposeMsg(tmp_msg_ptr);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

void Hotstuff::NewView(
        std::shared_ptr<tnet::TcpInterface> conn,
        const std::shared_ptr<SyncInfo>& sync_info) {
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

    if (sync_info->tc != nullptr) {
        *pb_newview_msg->mutable_tc() = *sync_info->tc;
    }

    if (sync_info->qc != nullptr) {
        *pb_newview_msg->mutable_qc() = *sync_info->qc;
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
        hotstuff_msg->pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg->pro_msg().view_item().hash()).c_str(),
        pacemaker()->HighQC()->view(),
        pacemaker()->HighTC()->view(),
        header.hash64());
    HandleNewViewMsg(msg_ptr);
    return;    
}

void Hotstuff::HandleProposeMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();

    assert(msg_ptr->header.hotstuff().pro_msg().view_item().hash().empty());
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(msg_ptr->header.debug());    

    ZJC_DEBUG("handle propose called hash: %lu, %u_%u_%lu, "
        "view block hash: %s, propose_debug: %s", 
        msg_ptr->header.hash64(),
        msg_ptr->header.hotstuff().pro_msg().view_item().network_id(), 
        msg_ptr->header.hotstuff().pro_msg().view_item().pool_index(),
        msg_ptr->header.hotstuff().pro_msg().view_item().view(),
        common::Encode::HexEncode(msg_ptr->header.hotstuff().pro_msg().view_item().hash()).c_str(),
        "ProtobufToJson(cons_debug).c_str()");

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
#ifndef NDEBUG
    pro_msg_wrap->view_block_ptr->set_debug(cons_debug.SerializeAsString());

    ZJC_DEBUG("handle new propose message parent hash: %s, %u_%u_%lu, view hash: %s, "
        "hash64: %lu, block timestamp: %lu, propose_debug: %s",
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->parent_hash()).c_str(), 
        pro_msg_wrap->view_block_ptr->network_id(),
        pro_msg_wrap->view_block_ptr->pool_index(),
        pro_msg_wrap->view_block_ptr->view(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
        msg_ptr->header.hash64(),
        pro_msg_wrap->view_block_ptr->block_info().timestamp(),
        ProtobufToJson(cons_debug).c_str());
#else
    pro_msg_wrap->view_block_ptr->set_debug(msg_ptr->header.debug());
#endif
    assert(pro_msg_wrap->view_block_ptr->block_info().tx_list_size() == 0);

    ADD_DEBUG_PROCESS_TIMESTAMP();

    // handle_propose_pipeline_.Call(pro_msg_wrap);
    HandleProposeMessageByStep(pro_msg_wrap);
}

Status Hotstuff::HandleProposeMessageByStep(std::shared_ptr<ProposeMsgWrapper> pro_msg_wrap) {
    auto msg_ptr = pro_msg_wrap->msg_ptr;
    auto st = HandleProposeMsgStep_HasVote(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_VerifyLeader(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }    
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_VerifyTC(pro_msg_wrap);
    if (st != Status::kSuccess) {
        return st;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    st = HandleProposeMsgStep_VerifyQC(pro_msg_wrap);
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
    auto& view_item = *pro_msg_wrap->view_block_ptr;
#ifdef USE_TC    
    if (last_vote_view_ >= view_item.view()) {
        return Status::kError;
    }        
#else
    if (last_vote_view_ > view_item.view()) {
        return Status::kError;
    }
#endif


    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyLeader(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    ZJC_DEBUG("HandleProposeMsgStep_VerifyLeader called hash: %lu, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto local_idx = leader_rotation_->GetLocalMemberIdx();
    auto& view_item = *pro_msg_wrap->view_block_ptr;
    if (VerifyLeader(view_item.leader_idx()) != Status::kSuccess) {
        // TODO 一旦某个节点状态滞后，那么 Leader 就与其他 replica 不同，导致无法处理新提案
        // 只能依赖同步，但由于同步慢于新的 Propose 消息
        // 即是这里再加一次同步，也很难追上 Propose 的速度，导致该节点掉队，因此还是需要一个队列缓存一下
        // 暂时无法处理的 Propose 消息
        if (sync_pool_fn_) { // leader 不一致触发同步
            sync_pool_fn_(pool_idx_, 1);
        }

        ZJC_ERROR("verify leader failed, pool: %d view: %lu, hash64: %lu", 
            pool_idx_, view_item.view(), pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }        

    if (view_item.leader_idx() == local_idx) {
        pro_msg_wrap->msg_ptr->is_leader = true;
    }

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyTC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    std::shared_ptr<TC> tc = nullptr;
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    if (pro_msg.has_tc()) {
        if (VerifyTC(pro_msg.tc()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify tc failed: %lu", pool_idx_, pro_msg.view_item().view());
            assert(false);
            return Status::kError;
        }
        
        pacemaker()->AdvanceView(new_sync_info()->WithTC(std::make_shared<TC>(pro_msg.tc())));
    }

    

    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyQC(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
    auto& msg_ptr = pro_msg_wrap->msg_ptr;
    auto& pro_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg();
    if (!pro_msg.has_view_item()) {
        return Status::kError;
    }
    auto& vblock = pro_msg.view_item();
    if (vblock.has_qc() && IsQcTcValid(vblock.qc())) {
        if (VerifyQC(vblock.qc()) != Status::kSuccess) {
            ZJC_ERROR("pool: %d verify qc failed: %lu", pool_idx_, vblock.view());
            assert(false);
            return Status::kError;
        }
        
        pacemaker()->AdvanceView(new_sync_info()->WithQC(std::make_shared<QC>(vblock.qc())));
        TryCommit(msg_ptr, vblock.qc(), 99999999lu);
    }
    
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_VerifyViewBlock(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());
    
    ZJC_DEBUG("HandleProposeMsgStep_VerifyViewBlock called hash: %lu, propose_debug: %s",
        pro_msg_wrap->msg_ptr->header.hash64(), ProtobufToJson(cons_debug).c_str());
#endif
    auto* tc = &pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tc();
    if (VerifyViewBlock(
            *pro_msg_wrap->view_block_ptr,
            view_block_chain(),
            tc,
            pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().elect_height()) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Verify ViewBlock is error. hash: %s, hash64: %lu", pool_idx_,
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
            pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    
#ifndef NDEBUG
    ZJC_DEBUG("====1.1 pool: %d, verify view block success, view: %lu, "
        "hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        pro_msg_wrap->view_block_ptr->view(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
        pacemaker()->HighQC()->view(),
        pro_msg_wrap->msg_ptr->header.hash64(), pro_msg_wrap->msg_ptr->header.debug().c_str());
#endif
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_TxAccept(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    ZJC_DEBUG("HandleProposeMsgStep_TxAccept called hash: %lu, view hash: %s, "
        "propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(),
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
        ProtobufToJson(cons_debug).c_str());
#endif
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
#ifndef NDEBUG
        ZJC_DEBUG("====1.1.2 Accept pool: %d, verify view block failed, "
            "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
            pool_idx_,
            proto_msg.view_item().view(),
            common::Encode::HexEncode(proto_msg.view_item().hash()).c_str(),
            pacemaker()->HighQC()->view(),
            pro_msg_wrap->msg_ptr->header.hash64(),
            ProtobufToJson(cons_debug).c_str());
#endif
        return Status::kError;
    }

    ZJC_DEBUG("====1.1.2 success Accept pool: %d, verify view block, "
        "view: %lu, hash: %s, qc_view: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        proto_msg.view_item().view(),
        common::Encode::HexEncode(proto_msg.view_item().hash()).c_str(),
        pacemaker()->HighQC()->view(),
        pro_msg_wrap->msg_ptr->header.hash64(),
        pro_msg_wrap->msg_ptr->header.debug().c_str());
    return Status::kSuccess;
}

Status Hotstuff::HandleProposeMsgStep_ChainStore(std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    ZJC_DEBUG("HandleProposeMsgStep_ChainStore called hash: %lu, sign empty: %d, propose_debug: %s", 
        pro_msg_wrap->msg_ptr->header.hash64(), 
        (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
        pro_msg_wrap->view_block_ptr->qc().sign_y().empty()), 
        ProtobufToJson(cons_debug).c_str());
#endif
    // if (pro_msg_wrap->view_block_ptr->qc().sign_x().empty() ||
    //         pro_msg_wrap->view_block_ptr->qc().sign_y().empty()) {
    //     return Status::kSuccess;
    // }

    
    // add view block
    Status s = view_block_chain()->Store(
        pro_msg_wrap->view_block_ptr, 
        false, 
        pro_msg_wrap->acc_balance_map_ptr,
        pro_msg_wrap->zjc_host_ptr);
#ifndef NDEBUG
    ZJC_DEBUG("pool: %d, add view block hash: %s, status: %d, view: %u_%u_%lu, tx size: %u, propose_debug: %s",
        pool_idx_, 
        common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
        s,
        pro_msg_wrap->view_block_ptr->network_id(),
        pro_msg_wrap->view_block_ptr->pool_index(),
        pro_msg_wrap->view_block_ptr->view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        ProtobufToJson(cons_debug).c_str());
#endif
    if (s != Status::kSuccess) {
#ifndef NDEBUG
        ZJC_ERROR("pool: %d, add view block error. hash: %s, view: %u_%u_%lu, propose_debug: %s",
            pool_idx_,
            common::Encode::HexEncode(pro_msg_wrap->view_block_ptr->hash()).c_str(),
            pro_msg_wrap->view_block_ptr->network_id(),
            pro_msg_wrap->view_block_ptr->pool_index(),
            pro_msg_wrap->view_block_ptr->view(),
            ProtobufToJson(cons_debug).c_str());
#endif
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
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(pro_msg_wrap->msg_ptr->header.debug());

    // NOTICE: pipeline 重试时，protobuf 结构体被析构，因此 pro_msg_wrap->header.hash64() 是 0
    ZJC_INFO("pacemaker pool: %d, highQC: %lu, highTC: %lu, chainSize: %lu, "
        "curView: %lu, vblock: %lu, txs: %lu, hash64: %lu, propose_debug: %s",
        pool_idx_,
        pacemaker()->HighQC()->view(),
        pacemaker()->HighTC()->view(),
        view_block_chain()->Size(),
        pacemaker()->CurView(),
        pro_msg_wrap->view_block_ptr->view(),
        pro_msg_wrap->view_block_ptr->block_info().tx_list_size(),
        pro_msg_wrap->msg_ptr->header.hash64(),
        "ProtobufToJson(cons_debug).c_str()");

    auto msg_ptr = pro_msg_wrap->msg_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();

    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& trans_header = trans_msg->header;
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto* hotstuff_msg = trans_header.mutable_hotstuff();
    auto* vote_msg = hotstuff_msg->mutable_vote_msg();
    assert(pro_msg_wrap->view_block_ptr->elect_height() > 0);
    // Construct VoteMsg
    Status s = ConstructVoteMsg(
        msg_ptr,
        vote_msg, 
        pro_msg_wrap->view_block_ptr->elect_height(), 
        pro_msg_wrap->view_block_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, ConstructVoteMsg error %d, hash64: %lu",
            pool_idx_, s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    // Construct HotstuffMessage and send
    ADD_DEBUG_PROCESS_TIMESTAMP();
    s = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, nullptr, hotstuff_msg);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d, ConstructHotstuffMsg error %d, hash64: %lu",
            pool_idx_, s, pro_msg_wrap->msg_ptr->header.hash64());
        return Status::kError;
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (SendMsgToLeader(trans_msg, VOTE) != Status::kSuccess) {
        ZJC_ERROR("pool: %d, Send vote message is error.",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64());
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!pro_msg_wrap->msg_ptr->is_leader) {
        // 避免对 view 重复投票
        ZJC_DEBUG("pool: %d, Send vote message is success., hash64: %lu, "
            "last_vote_view_: %lu, send to leader tx size: %u, last_vote_view_: %lu",
            pool_idx_, pro_msg_wrap->msg_ptr->header.hash64(),
            pro_msg_wrap->view_block_ptr->view(),
            vote_msg->txs_size(),
            last_vote_view_);
        StopVoting(pro_msg_wrap->view_block_ptr->view());
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    has_user_tx_tag_ = false;
    return Status::kSuccess;
}

void Hotstuff::HandleVoteMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto b = common::TimeUtils::TimestampMs();
    defer({
            auto e = common::TimeUtils::TimestampMs();
            ZJC_DEBUG("pool: %d handle vote duration: %lu ms", pool_idx_, e-b);
        });
    
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
    if (prefix_db_->BlockExists(vote_msg.view_block_hash())) {
        return;
    }

    std::string followers_gids;
#ifndef NDEBUG
    for (uint32_t i = 0; i < uint32_t(vote_msg.txs_size()); ++i) {
        followers_gids += common::Encode::HexEncode(vote_msg.txs(i).gid()) + " ";
    }
#endif
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(msg_ptr->header.debug());
    // cons_debug.add_timestamps(
    //     b - cons_debug.timestamps(0));

    ZJC_DEBUG("====2.0 pool: %d, onVote, hash: %s, view: %lu, "
        "local high view: %lu, replica: %lu, hash64: %lu, propose_debug: %s, followers_gids: %s, local idx: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        pacemaker()->HighQC()->view(),
        vote_msg.replica_idx(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        followers_gids.c_str(),
        leader_rotation()->GetLocalMemberIdx());

    // 同步 replica 的 txs
    // 无论是否是合法的 vote msg，都要尝试添加交易，否则剩余 f 个节点的交易同步会丢失
    acceptor()->AddTxs(msg_ptr, vote_msg.txs());    

    if (VerifyVoteMsg(vote_msg) != Status::kSuccess) {
        ZJC_DEBUG("vote message is error: hash64: %lu", msg_ptr->header.hash64());
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("====2.1 pool: %d, onVote, hash: %s, view: %lu, hash64: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        vote_msg.view(),
        msg_ptr->header.hash64());

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
            ZJC_WARN("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        return;
    }    

    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, local: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        agg_sig.IsValid(),
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        vote_msg.replica_idx(),
        leader_rotation()->GetLocalMemberIdx());
    qc_item.mutable_agg_sig()->CopyFrom(agg_sig.DumpToProto());
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
    ZJC_DEBUG("success set view block hash: %s, qc_hash: %s, %u_%u_%lu",
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
    assert(ret != Status::kInvalidOpposedCount);
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_DEBUG("kBlsWaiting pool: %d, view: %lu, hash64: %lu",
                pool_idx_, vote_msg.view(), msg_ptr->header.hash64());
            return;
        }

        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("====2.2 pool: %d, onVote, hash: %s, %d, view: %lu, qc_hash: %s, hash64: %lu, propose_debug: %s, replica: %lu, local: %lu",
        pool_idx_,
        common::Encode::HexEncode(vote_msg.view_block_hash()).c_str(),
        reconstructed_sign == nullptr,
        vote_msg.view(),
        common::Encode::HexEncode(qc_hash).c_str(),
        msg_ptr->header.hash64(),
        ProtobufToJson(cons_debug).c_str(),
        vote_msg.replica_idx(),
        leader_rotation()->GetLocalMemberIdx());

    qc_item.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->X));
    qc_item.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->Y));
    // 切换视图
    ZJC_DEBUG("success new set qc view: %lu, %u_%u_%lu",
        qc_item.view(),
        qc_item.network_id(),
        qc_item.pool_index(),
        qc_item.view());
#endif
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
    
    pacemaker()->AdvanceView(new_sync_info()->WithQC(std::make_shared<QC>(qc_item)));
    // 先单独广播新 qc，即是 leader 出不了块也不用额外同步 HighQC，这比 Gossip 的效率:q高很多
    ZJC_DEBUG("NewView propose newview called pool: %u, qc_view: %lu, tc_view: %lu, propose_debug: %s",
        pool_idx_, pacemaker()->HighQC()->view(), pacemaker()->HighTC()->view(), ProtobufToJson(cons_debug).c_str());

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto s = Propose(new_sync_info()->WithQC(pacemaker()->HighQC())->WithTC(pacemaker()->HighTC()), msg_ptr);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        NewView(nullptr, new_sync_info()->WithQC(pacemaker()->HighQC())->WithTC(pacemaker()->HighTC()));
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

// Store view block with its qc
Status Hotstuff::StoreVerifiedViewBlock(
        const std::shared_ptr<ViewBlock>& v_block, 
        const std::shared_ptr<QC>& qc) {
    if (view_block_chain()->Has(qc->view_block_hash())) {
        return Status::kSuccess;    
    }

    if (v_block->hash() != qc->view_block_hash() || v_block->view() != qc->view()) {
        return Status::kError;
    }

    Status s = acceptor()->AcceptSync(*v_block);
    if (s != Status::kSuccess) {
        return s;
    }

    transport::MessagePtr msg_ptr;
    TryCommit(msg_ptr, *qc, 99999999lu);
    ZJC_DEBUG("success store v block pool: %u, hash: %s, prehash: %s",
        pool_idx_,
        common::Encode::HexEncode(v_block->hash()).c_str(),
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
        auto tc_ptr = std::make_shared<TC>(newview_msg.tc());
        auto& tc = *tc_ptr;
        if (tc.view() > pacemaker()->HighTC()->view()) {                
            if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
                ZJC_ERROR("VerifyTC error.");
                return;
            }

            pacemaker()->AdvanceView(new_sync_info()->WithTC(tc_ptr));
        }
    }

    if (newview_msg.has_qc()) {
        auto qc_ptr = std::make_shared<QC>(newview_msg.qc());
        auto& qc = *qc_ptr;
        if (qc.view() > pacemaker()->HighQC()->view()) {
            if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
                ZJC_ERROR("VerifyQC error.");
                return;
            }
            
            pacemaker()->AdvanceView(new_sync_info()->WithQC(qc_ptr));
            TryCommit(msg_ptr, qc, 99999999lu);
        } else if (qc.view() == pacemaker()->HighQC()->view()) {
            TryCommit(msg_ptr, qc, 99999999lu);
        }
    }
}

void Hotstuff::HandlePreResetTimerMsg(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& pre_rst_timer_msg = msg_ptr->header.hotstuff().pre_reset_timer_msg();
    if (pre_rst_timer_msg.txs_size() == 0 && !pre_rst_timer_msg.has_single_tx()) {
        ZJC_DEBUG("pool: %d has proposed!", pool_idx_);
        return;
    }

    if (pre_rst_timer_msg.txs_size() > 0) {
        Status s = acceptor()->AddTxs(msg_ptr, pre_rst_timer_msg.txs());
        if (s != Status::kSuccess) {
            ZJC_WARN("reset timer failed, add txs failed");
            return;
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // TODO: Flow Control

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ResetReplicaTimers();

    ADD_DEBUG_PROCESS_TIMESTAMP();
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

        return;
    }

    ZJC_DEBUG("====5.1 pool: %d, onResetTimer leader_idx: %u, local_idx: %u, hash64: %lu",
        pool_idx_, rst_timer_msg.leader_idx(),
        elect_item->LocalMember()->index,
        header.hash64());

    // 必须处于 stuck 状态
    auto stuck_st = IsStuck();
    if (stuck_st != 0) {
        ZJC_DEBUG("reset timer failed: %u, hash64: %lu, status: %d",
            pool_idx_, header.hash64(), stuck_st);
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
    ZJC_DEBUG("reset timer success: %u, hash64: %lu", pool_idx_, header.hash64());
    return;
}


Status Hotstuff::TryCommit(const transport::MessagePtr& msg_ptr, const QC& commit_qc, uint64_t test_index) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    
    auto v_block_to_commit = CheckCommit(commit_qc);

    if (v_block_to_commit) {
        transport::protobuf::ConsensusDebug cons_debug;
        cons_debug.ParseFromString(v_block_to_commit->debug());
        ZJC_DEBUG("commit tx size: %u, propose_debug: %s", 
            v_block_to_commit->block_info().tx_list_size(), 
            ProtobufToJson(cons_debug).c_str());

        ADD_DEBUG_PROCESS_TIMESTAMP();
        Status s = Commit(msg_ptr, v_block_to_commit, commit_qc, test_index);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s, propose_debug: %s",
                v_block_to_commit->view(),
                common::Encode::HexEncode(v_block_to_commit->hash()).c_str(), 
                ProtobufToJson(cons_debug).c_str());
            return s;
        }
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> Hotstuff::CheckCommit(const QC& qc) {
    auto v_block1 = view_block_chain()->Get(qc.view_block_hash());
    if (!v_block1) {
        // kv_sync_->AddSyncViewHeight(qc.network_id(), qc.pool_index(), qc.view(), 0);
        // assert(false);
        return nullptr;
    }
        
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(v_block1->debug());
    ZJC_DEBUG("success get v block 1: %s, %u_%u_%lu, propose_debug: %s",
        common::Encode::HexEncode(v_block1->hash()).c_str(),
        v_block1->network_id(), v_block1->pool_index(), v_block1->view(), ProtobufToJson(cons_debug).c_str());
#endif
    auto v_block2 = view_block_chain()->QCRef(v_block1);
    if (!v_block2) {
        return nullptr;
    }

    transport::protobuf::ConsensusDebug cons_debug2;
    cons_debug2.ParseFromString(v_block2->debug());
    ZJC_DEBUG("success get v block 2: %s, %u_%u_%lu, propose_debug: %s",
        common::Encode::HexEncode(v_block2->hash()).c_str(),
        v_block2->network_id(), v_block2->pool_index(), 
        v_block2->view(), ProtobufToJson(cons_debug2).c_str());

    if (!view_block_chain()->LatestLockedBlock() || v_block2->view() > view_block_chain()->LatestLockedBlock()->view()) {
        view_block_chain()->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = view_block_chain()->QCRef(v_block2);
    if (!v_block3) {
        return nullptr;
    }
    
    transport::protobuf::ConsensusDebug cons_debug3;
    cons_debug3.ParseFromString(v_block2->debug());
    ZJC_DEBUG("success get v block hash: %s, %s, %s, %s, now: %s, propose_debug: %s",
        common::Encode::HexEncode(v_block1->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block2->hash()).c_str(),
        common::Encode::HexEncode(v_block2->parent_hash()).c_str(),
        common::Encode::HexEncode(v_block3->hash()).c_str(),
        common::Encode::HexEncode(v_block1->hash()).c_str(),
        ProtobufToJson(cons_debug3).c_str());    

    if (v_block1->parent_hash() != v_block2->hash() || v_block2->parent_hash() != v_block3->hash()) {
        assert(false);
        return nullptr;
    }
    
    return v_block3;
}

Status Hotstuff::Commit(
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlock>& v_block,
        const QC& commit_qc,
        uint64_t test_index) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view() >= v_block->view()) {
        ZJC_DEBUG("commit failed latest view: %lu, noew view: %lu_%lu", 
            latest_committed_block->view(), pool_idx_, v_block->view());
        return Status::kSuccess;
    }
    
    auto tmp_block = v_block;
    while (tmp_block != nullptr) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        
        auto db_batch = std::make_shared<db::DbWriteBatch>();

        // set commit_qc to vblock and store to database
        ADD_DEBUG_PROCESS_TIMESTAMP();

        tmp_block->mutable_self_commit_qc()->CopyFrom(commit_qc);
        view_block_chain()->StoreToDb(tmp_block, test_index, db_batch);        

        auto block_copy = std::make_shared<ViewBlock>(*tmp_block);
        auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(block_copy, db_batch);
        if (!CommitInner(msg_ptr, block_copy, test_index, queue_item_ptr)) {
            break;
        }

        ADD_DEBUG_PROCESS_TIMESTAMP();
        
        std::shared_ptr<ViewBlock> parent_block = nullptr;
        parent_block = view_block_chain()->Get(tmp_block->parent_hash());
        if (parent_block == nullptr) {
            break;
        }

        view_block_chain()->SetLatestCommittedBlock(v_block);

        tmp_block = parent_block;
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    // 剪枝
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    ZJC_DEBUG("success commit view block %u_%u_%lu, height: %lu, now chain: %s",
        v_block->network_id(), 
        v_block->pool_index(), 
        v_block->view(), 
        v_block->block_info().height(),
        view_block_chain()->String().c_str());
    
    auto s = view_block_chain()->PruneTo(v_block->hash(), forked_blockes, true);
    if (s != Status::kSuccess) {
        ZJC_WARN("PruneTo failed, success commit view block %u_%u_%lu, height: %lu, now chain: %s",
            v_block->network_id(), 
            v_block->pool_index(), 
            v_block->view(), 
            v_block->block_info().height(),
            view_block_chain()->String().c_str());
        return s;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("PruneTo success, success commit view block %u_%u_%lu, height: %lu, now chain: %s",
        v_block->network_id(), 
        v_block->pool_index(), 
        v_block->view(), 
        v_block->block_info().height(),
        view_block_chain()->String().c_str());
    // 归还分支交易
    ADD_DEBUG_PROCESS_TIMESTAMP();
    for (const auto& forked_block : forked_blockes) {
        s = acceptor()->Return(forked_block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

Status Hotstuff::VerifyQC(const QC& qc) {
    // 验证 qc
    if (!IsQcTcValid(qc)) {
        assert(false);
        return Status::kError;
    }

    if (qc.view() > pacemaker()->HighQC()->view()) {        
        if (crypto()->VerifyQC(common::GlobalInfo::Instance()->network_id(), qc) != Status::kSuccess) {
            return Status::kError; 
        }
    }

    return Status::kSuccess;
}

Status Hotstuff::VerifyTC(const TC& tc) {
    if (!IsQcTcValid(tc)) {
        assert(false);
        return Status::kError;
    }

    if (tc.view() > pacemaker()->HighTC()->view()) {
        if (crypto()->VerifyTC(common::GlobalInfo::Instance()->network_id(), tc) != Status::kSuccess) {
            ZJC_ERROR("VerifyTC error.");
            return Status::kError;
        }
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

    if (v_block.qc().view() + 1 != v_block.view() && tc && tc->view() + 1 != v_block.view()) {
        ZJC_ERROR("block view is error.");
        return Status::kError;
    }

    if (v_block.block_info().height() <= view_block_chain->LatestCommittedBlock()->block_info().height()) {
        ZJC_ERROR("new view block height error: %lu, last commited block height: %lu", 
            v_block.block_info().height(),
            view_block_chain->LatestCommittedBlock()->block_info().height());
        return Status::kError;
    }

    if (v_block.qc().view_block_hash() != v_block.parent_hash()) {
        ZJC_ERROR("qc ref is different from hash ref");
        return Status::kError;        
    }

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block = view_block_chain->Get(v_block.qc().view_block_hash());
    if (!qc_view_block) {
        // TODO try sync view
        // xufeisofly 同步策略异常，v_block().parent 的视图号不一定是 v_block.view - 1？有了 TC 之后就不是了
        // if (view_block_chain->HighQC().view() < v_block.qc().view() + 16) {
        //     kv_sync_->AddSyncViewHeight(
        //         v_block.qc().network_id(), 
        //         v_block.qc().pool_index(), 
        //         v_block.qc().view() - 1,
        //         0);
        // } else {
        //     kv_sync_->AddSyncHeight(
        //         v_block.qc().network_id(), 
        //         v_block.qc().pool_index(), 
        //         v_block.block_info().height() - 1,
        //         0);
        // }
        
        return Status::kError;
    }

    if (!view_block_chain->Extends(v_block, *qc_view_block)) {
        ZJC_ERROR("extents qc view block message is error.");
        return Status::kError;
    }

    if (view_block_chain->LatestLockedBlock() &&
        !view_block_chain->Extends(v_block, *view_block_chain->LatestLockedBlock()) && 
            v_block.view() <= view_block_chain->LatestLockedBlock()->view()) {
        ZJC_ERROR("pool: %d, block view message is error. %lu, %lu, %s, %s",
            pool_idx_, v_block.view(), view_block_chain->LatestLockedBlock()->view(),
            common::Encode::HexEncode(view_block_chain->LatestLockedBlock()->hash()).c_str(),
            common::Encode::HexEncode(v_block.parent_hash()).c_str());
        return Status::kError;
    }   

    return ret;
}

bool Hotstuff::CommitInner(
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<ViewBlock>& v_block, 
        uint64_t test_index, 
        std::shared_ptr<block::BlockToDbItem>& queue_block_item) {
#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString(v_block->debug());
#endif
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& block_info = v_block->block_info();

    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view() >= v_block->view()) {
        return false;
    }
    
    if (!latest_committed_block && v_block->view() == GenesisView) {
#ifndef NDEBUG        
        ZJC_DEBUG("NEW BLOCK CommitInner coming pool: %d, commit failed s: %d, "
            "vb view: %lu, %u_%u_%lu, propose_debug: %s",
            pool_idx_, 0, v_block->view(),
            v_block->network_id(), v_block->pool_index(), 
            block_info.height(), ProtobufToJson(cons_debug).c_str());
#endif
        return false;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();

    acceptor()->Commit(msg_ptr, queue_block_item);
    // ZJC_DEBUG("2 NEW BLOCK CommitInner coming pool: %d, commit coming s: %d, "
    //     "vb view: %lu, %u_%u_%lu, cur chain: %s, test_index: %lu",
    //     pool_idx_, 0, v_block->qc().view(),
    //     v_block->qc().network_id(), v_block->qc().pool_index(), block_info.height(),
    //     view_block_chain()->String().c_str(),
    //     test_index);

    // 提交 v_block->consensus_stat 共识数据
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = elect_info()->GetElectItem(
            v_block->network_id(),
            v_block->elect_height());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (elect_item && elect_item->IsValid()) {
        elect_item->consensus_stat(pool_idx_)->Commit(v_block);
    }    
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("pool: %d consensus stat, leader: %lu, succ: %lu, test_index: %lu",
        pool_idx_, v_block->leader_idx(),
        0,
        test_index);
    return true;
}

Status Hotstuff::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg) {
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
            ZJC_ERROR("pool: %d, leader_idx message is error, %d, %d",
                pool_idx_, leader_idx, leader->index);
            // assert(false);
            return Status::kError;
        }

        ZJC_DEBUG("use expected leader index: %u, %u", leader_idx, leader->index);
    }
    return Status::kSuccess;
}

Status Hotstuff::ConstructProposeMsg(
        const transport::MessagePtr& msg_ptr,
        const std::shared_ptr<SyncInfo>& sync_info,
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
        ZJC_DEBUG("pool: %d construct view block failed, view: %lu, %d, member_index: %d",
            pool_idx_, pacemaker()->CurView(), s,
            elect_item->LocalMember()->index);        
        return s;
    }

    // propose msg need to carry tc if it exsits.
    if (sync_info->tc) {
        pro_msg->mutable_tc()->CopyFrom(*sync_info->tc);
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
    vote_msg->set_view_block_hash(v_block->hash());

    ZJC_DEBUG("success set view block hash: %s, %u_%u_%lu",
        common::Encode::HexEncode(v_block->hash()).c_str(),
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        v_block->view());
    assert(!prefix_db_->BlockExists(v_block->hash()));
    vote_msg->set_view(v_block->view());
    vote_msg->set_elect_height(elect_height);
    vote_msg->set_leader_idx(v_block->leader_idx());
    QC qc_item;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(v_block->view());
    qc_item.set_view_block_hash(v_block->hash());
    qc_item.set_elect_height(elect_height);
    qc_item.set_leader_idx(v_block->leader_idx());
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
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto* txs = vote_msg->mutable_txs();
    wrapper()->GetTxSyncToLeader(
        v_block->leader_idx(), 
        view_block_chain_, 
        pacemaker()->HighQC()->view_block_hash(), 
        txs);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

Status Hotstuff::ConstructViewBlock(
        const transport::MessagePtr& msg_ptr, 
        ViewBlock* view_block,
        hotstuff::protobuf::TxPropose* tx_propose) {
    view_block->set_parent_hash(pacemaker()->HighQC()->view_block_hash());
    
    auto local_elect_item = elect_info_->GetElectItemWithShardingId(
        common::GlobalInfo::Instance()->network_id());
    if (local_elect_item == nullptr) {
        ZJC_DEBUG("pool index: %d, local_elect_item == nullptr", pool_idx_);
        return Status::kError;
    }

    auto local_member = local_elect_item->LocalMember();
    if (local_member == nullptr) {
        ZJC_DEBUG("pool index: %d, local_member == nullptr", pool_idx_);
        return Status::kError;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto leader_idx = local_member->index;
    auto pre_v_block = view_block_chain_->Get(view_block->parent_hash());
    if (!pre_v_block) {
        ZJC_ERROR("parent view block has not found, pool: %d, view: %lu, "
            "parent_view: %lu, leader: %lu, chain: %s",
            pool_idx_,
            pacemaker()->CurView(),
            pacemaker()->HighQC()->view(),
            leader_idx,
            view_block_chain()->String().c_str());
        return Status::kNotFound;
    }
    ZJC_DEBUG("get prev block hash: %s, height: %lu", 
        common::Encode::HexEncode(view_block->parent_hash()).c_str(), 
        pre_v_block->block_info().height());
    // Construct ViewBlock with HighQC and CurView
    view_block->mutable_qc()->CopyFrom(*pacemaker()->HighQC());
    view_block->set_view(pacemaker()->CurView());
    view_block->set_elect_height(local_elect_item->ElectHeight());
    view_block->set_leader_idx(leader_idx);
    view_block->set_network_id(common::GlobalInfo::Instance()->network_id());
    view_block->set_pool_index(pool_idx_);
    // 如果单分支最多连续打包三个默认交易
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
        ZJC_DEBUG("pool: %d wrap failed, %d", pool_idx_, static_cast<int>(s));
        return s;
    }

    ZJC_DEBUG("success failed check is empty block allowd: %d, %u_%u_%lu, "
        "tx size: %u, cur view: %lu, pre view: %lu, last_vote_view_: %lu",
        pool_idx_, view_block->network_id(), 
        view_block->pool_index(), view_block->view(),
        tx_propose->txs_size(),
        pacemaker()->CurView(),
        pre_v_block->view(),
        last_vote_view_);

    auto elect_item = elect_info_->GetElectItem(
        common::GlobalInfo::Instance()->network_id(),
        view_block->elect_height());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }

    
    ADD_DEBUG_PROCESS_TIMESTAMP();

    // set hash for view block for present
    // Note: the hash will change after the txs calculation
    // view_block->set_hash(GetBlockHash(*view_block));
    return Status::kSuccess;
}

bool Hotstuff::IsEmptyBlockAllowed(const ViewBlock& v_block) {
    if (v_block.block_info().tx_list_size() > 0) {
        return true;
    }

    auto current = std::make_shared<ViewBlock>(v_block);
    for (auto i = 0; i < AllowedEmptyBlockCnt-1; i++) {
        current = view_block_chain()->Get(current->parent_hash());
        if (!current || current->block_info().tx_list_size() > 0) {
            return true;
        }
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
        transport::TcpTransport::Instance()->AddLocalMessage(trans_msg);
        ZJC_DEBUG("2 success add local message: %lu", trans_msg->header.hash64());
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
//                 "hash64: %lu, %s:%d, leader->index: %d, local_idx: %d, gid: %s, to: %s",
//                 pool_idx_,
//                 msg_type,
//                 leader->net_id, 
//                 common::Encode::HexEncode(leader->id).c_str(), 
//                 header_msg.hash64(),
//                 common::Uint32ToIp(leader->public_ip).c_str(),
//                 leader->public_port,
//                 leader->index,
//                 local_idx,
//                 common::Encode::HexEncode(tx.gid()).c_str(),
//                 common::Encode::HexEncode(tx.to()).c_str());
//         }
//     }
// #endif

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
    if (has_user_tx) {
        has_user_tx_tag_ = true;
    }

    if (!has_user_tx_tag_ && !has_system_tx) {
        // ZJC_DEBUG("!has_user_tx_tag_ && !has_system_tx, pool: %u", pool_idx_);
        return;
    }

    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        ZJC_DEBUG("leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32, pool: %u", pool_idx_);
        return;
    }

    // 限流 2 s
    auto stuck_st = IsStuck();
    if (stuck_st != 0) {
        ZJC_DEBUG("pool: %u stuck_st != 0: %d", pool_idx_, stuck_st);
        return;
    }

    auto leader = leader_rotation()->GetLeader();
    auto local_idx = leader_rotation_->GetLocalMemberIdx();    

    if (has_system_tx) {
        if (leader->index == local_idx) {            
            ZJC_DEBUG("pool: %d, directly reset timer msg from: %lu, has_single_tx: %d, has_user_tx_tag: %d",
                pool_idx_, local_idx, has_system_tx, has_user_tx_tag_);            
            ResetReplicaTimers();
            return;
        }
    }

    if (!has_user_tx_tag_) {
        ZJC_DEBUG("pool: %u not has_user_tx_tag_.", pool_idx_);
        return;
    }    


    if (leader->index == local_idx) {
        ZJC_DEBUG("pool: %u local is leader: %lu", pool_idx_, leader->index);
        return;
    }

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
        pacemaker()->HighQC()->view_block_hash(), 
        txs);
    if (txs->empty()) {
        ZJC_WARN("pool: %u txs.empty().", pool_idx_);
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
    SendMsgToLeader(trans_msg, PRE_RESET_TIMER);
    ZJC_DEBUG("pool: %d, send prereset msg from: %lu to: %lu, has_single_tx: %d, tx size: %u",
        pool_idx_, pre_rst_timer_msg->replica_idx(), 
        leader_rotation_->GetLeader()->index, has_system_tx, txs->size());
}

uint32_t Hotstuff::GetPendingSuccNumOfLeader(const std::shared_ptr<ViewBlock>& v_block) {
    uint32_t ret = 1;
    auto current = v_block;
    auto latest_committed_block = view_block_chain()->LatestCommittedBlock();
    if (!latest_committed_block) {
        return ret;
    }

    while (current->view() > latest_committed_block->view()) {
        current = view_block_chain()->ParentBlock(*current);
        if (!current) {
            return ret;
        }
        if (current->leader_idx() == v_block->leader_idx()) {
            ret++;
        }
    }

    ZJC_DEBUG("pool: %d add succ num: %lu, leader: %lu", pool_idx_, ret, v_block->leader_idx());
    return ret;
}

} // namespace consensus

} // namespace shardora

