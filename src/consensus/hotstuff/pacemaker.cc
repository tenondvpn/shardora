#include <common/global_info.h>
#include <common/log.h>
#include <common/utils.h>
#include <common/defer.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <dht/dht_key.h>
#include <network/route.h>
#include <protos/transport.pb.h>
#include <protos/view_block.pb.h>
#include <transport/tcp_transport.h>

namespace shardora {

namespace hotstuff {

Pacemaker::Pacemaker(
        const uint32_t& pool_idx,
#ifdef USE_AGG_BLS
        const std::shared_ptr<AggCrypto>& c,
#else
        const std::shared_ptr<Crypto>& c,
#endif
        std::shared_ptr<LeaderRotation>& lr,
        const std::shared_ptr<ViewDuration>& d) :
    pool_idx_(pool_idx), crypto_(c), leader_rotation_(lr), duration_(d) {
    high_qc_ = std::make_shared<QC>();
    GetQCWrappedByGenesis(pool_idx_, high_qc_.get());

    high_tc_ = std::make_shared<TC>();
    auto& tc_item = *high_tc_;
    tc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    tc_item.set_pool_index(pool_idx_);
    tc_item.set_view(BeforeGenesisView);
    tc_item.set_view_block_hash("");
    tc_item.set_elect_height(1);
    tc_item.set_leader_idx(0);
    tc_item.set_sign_x("");
    tc_item.mutable_agg_sig()->CopyFrom(view_block::protobuf::AggregateSig());

    cur_view_ = GenesisView;
    StartTimeoutTimer();
}

Pacemaker::~Pacemaker() {}

void Pacemaker::HandleTimerMessage(const transport::MessagePtr& msg_ptr) {
    if (IsTimeout()) {
        ZJC_DEBUG("pool: %d timeout", pool_idx_);
        OnLocalTimeout();
    }
}

Status Pacemaker::AdvanceView(const std::shared_ptr<SyncInfo>& sync_info) {
    if (!sync_info) {
        return Status::kInvalidArgument;
    }

    if (!sync_info->qc && !sync_info->tc) {
        return Status::kInvalidArgument;
    }

    bool timeout = false;
    if (sync_info->qc) {
        UpdateHighQC(sync_info->qc);
    }

    if (sync_info->tc) {
        timeout = true;
        UpdateHighTC(sync_info->tc);
    }

#ifdef USE_AGG_BLS    
    if (sync_info->agg_qc && sync_info->agg_qc->IsValid()) {
        timeout = true;

        auto high_qc = std::make_shared<QC>();
        Status s = crypto_->VerifyAggregateQC(
                common::GlobalInfo::Instance()->network_id(),
                sync_info->agg_qc,
                high_qc);
        if (s != Status::kSuccess) {
            ZJC_ERROR("new agg qc failed, pool: %d, s: %d, view: %lu", pool_idx_, s, sync_info->agg_qc->GetView());
            return Status::kError;
        }

        // update high_qc
        UpdateHighQC(high_qc);
    }
#endif

    auto new_v = std::max(high_qc_->view(), high_tc_->view()) + 1;
    if (new_v <= cur_view_) {
        // 旧的 view
        return Status::kOldView;
    }
    
    StopTimeoutTimer();
    if (!timeout) {
        duration_->ViewSucceeded();
    }
    
    cur_view_ = new_v;
    
    duration_->ViewStarted();
    ZJC_DEBUG("to new view. pool: %lu, view: %llu", pool_idx_, cur_view_);
    
    StartTimeoutTimer();
    return Status::kSuccess;
}

void Pacemaker::OnLocalTimeout() {
#ifdef USE_TC
    OnLocalTimeout_WithTC();
#else
    OnLocalTimeout_WithoutTC();
#endif
}

void Pacemaker::OnLocalTimeout_WithTC() {
    // start a new timer for the timeout case
    StopTimeoutTimer();
    duration_->ViewTimeout();
    defer(StartTimeoutTimer());
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }
    
    if (sync_pool_fn_) {
        sync_pool_fn_(pool_idx_, 1);
    }

    auto elect_item = crypto_->GetLatestElectItem(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        assert(false);
        return;
    }
    
    auto tc_msg_hash = GetCurViewHash(elect_item);
    // if view is last one, deal directly.
    // 更换 epoch 后重新打包
    if (last_timeout_ && last_timeout_->header.has_hotstuff_timeout_proto() &&
            last_timeout_->header.hotstuff_timeout_proto().view() >= CurView() &&
            last_timeout_->header.hotstuff_timeout_proto().view_hash() == tc_msg_hash) {
        last_timeout_->times_idx = 0;
        auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>(*last_timeout_);
        ZJC_DEBUG("use exist local timeout message pool: %u, "
            "last_timeout_->header.hotstuff_timeout_proto().view(): %lu, cur view: %lu",
            pool_idx_, 
            tmp_msg_ptr->header.hotstuff_timeout_proto().view(), 
            CurView());
        SendTimeout(tmp_msg_ptr);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();

#ifdef USE_AGG_BLS
    AggregateSignature partial_sig;
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            tc_msg_hash,
            &partial_sig) != Status::kSuccess) {
        ZJC_ERROR("sign message failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(tc_msg_hash).c_str());
        return;        
    }

    timeout_msg.mutable_view_sig()->CopyFrom(partial_sig.DumpToProto());
    // 对本节点的 high qc 签名
    AggregateSignature high_qc_sig;
    auto high_qc_msg_hash = GetQCMsgHash(*HighQC()); 
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            high_qc_msg_hash,
            &high_qc_sig) != Status::kSuccess) {
        ZJC_ERROR("sign high qc failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(high_qc_msg_hash).c_str());
        return;
    }
    
    timeout_msg.mutable_high_qc()->CopyFrom(*HighQC());
    timeout_msg.mutable_high_qc_sig()->CopyFrom(high_qc_sig.DumpToProto());    
#else
    std::string bls_sign_x;
    std::string bls_sign_y;

    // 使用最新的 elect_height 签名
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            tc_msg_hash,
            &bls_sign_x,
            &bls_sign_y) != Status::kSuccess) {
        ZJC_ERROR("sign message failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(tc_msg_hash).c_str());
        return;
    }

    timeout_msg.set_sign_x(bls_sign_x);
    timeout_msg.set_sign_y(bls_sign_y);

    ZJC_DEBUG("now send local timeout msg hash: %s, view: %u, pool: %u, "
        "elect height: %lu, member index: %u, member size: %u, "
        "bls_sign_x: %s, bls_sign_y: %s, hash64: %lu",
        common::Encode::HexEncode(tc_msg_hash).c_str(),
        CurView(), pool_idx_, elect_item->ElectHeight(),
        timeout_msg.member_id(),
        leader_rotation_->MemberSize(common::GlobalInfo::Instance()->network_id()),
        bls_sign_x.c_str(),
        bls_sign_y.c_str());    
#endif
    
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());
    timeout_msg.set_view_hash(tc_msg_hash);
    timeout_msg.set_view(CurView());
    timeout_msg.set_elect_height(elect_item->ElectHeight());
    timeout_msg.set_pool_idx(pool_idx_); // 用于分配线程
    timeout_msg.set_leader_idx(0);
    
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kHotstuffTimeoutMessage);
    last_timeout_ = msg_ptr;
    // 停止对当前 view 的投票
    if (stop_voting_fn_) {
        stop_voting_fn_(CurView());
    }

    SendTimeout(msg_ptr);
}

void Pacemaker::OnLocalTimeout_WithoutTC() {
    // start a new timer for the timeout case
    StopTimeoutTimer();
    duration_->ViewTimeout();
    defer(StartTimeoutTimer());
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    if (sync_pool_fn_) {
        sync_pool_fn_(pool_idx_, 1);
    }

    auto elect_item = crypto_->GetLatestElectItem(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        assert(false);
        return;
    }

    auto tc_msg_hash = GetCurViewHash(elect_item);
    // if view is last one, deal directly.
    // 更换 epoch 后重新打包
    if (last_timeout_ && last_timeout_->header.has_hotstuff_timeout_proto() &&
            last_timeout_->header.hotstuff_timeout_proto().view() >= CurView() &&
            last_timeout_->header.hotstuff_timeout_proto().view_hash() == tc_msg_hash) {
        last_timeout_->times_idx = 0;
        auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>(*last_timeout_);
        SendTimeout(tmp_msg_ptr);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();
#ifdef USE_AGG_BLS
    // 对本节点的 high qc 签名
    AggregateSignature high_qc_sig;
    auto high_qc_msg_hash = GetQCMsgHash(*HighQC()); 
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            high_qc_msg_hash,
            &high_qc_sig) != Status::kSuccess) {
        ZJC_ERROR("sign high qc failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(high_qc_msg_hash).c_str());
        return;
    }    
    
    timeout_msg.mutable_high_qc_sig()->CopyFrom(high_qc_sig.DumpToProto());
#endif
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());
    timeout_msg.mutable_high_qc()->CopyFrom(*HighQC());
    timeout_msg.set_view_hash(tc_msg_hash);
    timeout_msg.set_view(CurView());
    timeout_msg.set_elect_height(elect_item->ElectHeight());
    timeout_msg.set_pool_idx(pool_idx_); // 用于分配线程
    timeout_msg.set_leader_idx(0);
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kHotstuffTimeoutMessage);
    last_timeout_ = msg_ptr;
    
    // 停止对当前 view 的投票
    if (stop_voting_fn_) {
        stop_voting_fn_(CurView());
    }

    SendTimeout(msg_ptr);
}

void Pacemaker::SendTimeout(const std::shared_ptr<transport::TransportMessage>& msg_ptr) {
    if (leader_rotation_->MemberSize(
            common::GlobalInfo::Instance()->network_id()) == common::kInvalidUint32) {
        return;
    }

    if (msg_ptr->header.hotstuff_timeout_proto().member_id() >=
            leader_rotation_->MemberSize(common::GlobalInfo::Instance()->network_id())) {
        return;
    }

    auto& msg = msg_ptr->header;
    auto leader = leader_rotation_->GetLeader();
    leader_rotation_->SetExpectedLeader(leader);
    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        dht::DhtKeyManager dht_key(leader->net_id, leader->id);
        msg.set_des_dht_key(dht_key.StrKey());
        transport::TcpTransport::Instance()->SetMessageHash(msg);
        ZJC_DEBUG("Send TimeoutMsg pool: %d, to ip: %s, port: %d, "
            "local_idx: %d, leader idx: %d, id: %s, local id: %s, hash64: %lu, view: %lu, hightc: %lu",
            pool_idx_,
            common::Uint32ToIp(leader->public_ip).c_str(),
            leader->public_port,
            msg.hotstuff_timeout_proto().member_id(),
            leader->index,
            common::Encode::HexEncode(leader->id).c_str(),
            common::Encode::HexEncode(crypto_->security()->GetAddress()).c_str(),
            msg.hash64(),
            msg.hotstuff_timeout_proto().view(),
            high_tc_->view());
        if (leader->public_ip == 0 || leader->public_port == 0) {
            network::Route::Instance()->Send(msg_ptr);
        } else {
            transport::TcpTransport::Instance()->Send(
                common::Uint32ToIp(leader->public_ip), 
                leader->public_port, 
                msg);
        }
    } else {
        OnRemoteTimeout(msg_ptr);
    }
}

void Pacemaker::OnRemoteTimeout(const transport::MessagePtr& msg_ptr) {
#ifdef USE_TC
    OnRemoteTimeout_WithTC(msg_ptr);
#else
    OnRemoteTimeout_WithoutTC(msg_ptr);
#endif
}

// OnRemoteTimeout 由 Consensus 调用
void Pacemaker::OnRemoteTimeout_WithTC(const transport::MessagePtr& msg_ptr) {
    auto msg = msg_ptr->header;
    auto& timeout_proto = msg.hotstuff_timeout_proto();
    ZJC_DEBUG("====4.0 start pool: %d, view: %d, member: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        msg_ptr->header.hash64());

    if (!VerifyTimeoutMsg(msg_ptr)) {
        return;
    } 

    std::shared_ptr<AggregateQC> agg_qc = nullptr;
#ifdef USE_AGG_BLS
    // 统计 high_qc，用于生成 AggQC
    CollectLatestHighQCs(timeout_proto);
    
    // 生成 TC
    AggregateSignature partial_sig;
    if (!partial_sig.LoadFromProto(timeout_proto.view_sig())) {
        return;
    }
    
    AggregateSignature agg_sig;
    Status s = crypto_->VerifyAndAggregateSig(
            timeout_proto.elect_height(),
            timeout_proto.view(),
            timeout_proto.view_hash(),
            partial_sig,
            agg_sig);    
    if (s != Status::kSuccess || !agg_sig.IsValid()) {
        return;
    }

    auto new_tc = std::make_shared<TC>();
    auto& tc = *new_tc;
    CreateTc(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        timeout_proto.view(),
        timeout_proto.elect_height(),
        timeout_proto.leader_idx(),
        &tc);
    tc.mutable_agg_sig()->CopyFrom(agg_sig.DumpToProto());
    
// #ifdef ENABLE_FAST_HOTSTUFF    
    agg_qc = crypto_->CreateAggregateQC(
            common::GlobalInfo::Instance()->network_id(),
            timeout_proto.elect_height(),
            timeout_proto.view(),
            LatestHighQCs(),
            LatestHighQCSigs());
    if (!agg_qc || !agg_qc->IsValid()) {
        return;
    }
// #endif
    
#else
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign = nullptr;
    Status s = crypto_->ReconstructAndVerifyThresSign(
        msg_ptr,
        timeout_proto.elect_height(),
        timeout_proto.view(),
        timeout_proto.view_hash(),
        timeout_proto.member_id(),
        timeout_proto.sign_x(),
        timeout_proto.sign_y(),
        reconstructed_sign);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("====4.5 over 1 pool: %d, view: %d, member: %d, hash64: %lu, status: %d", 
            pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
            msg_ptr->header.hash64(),
            s);
        return;
    }

    auto new_tc = std::make_shared<view_block::protobuf::QcItem>();
    auto& tc = *new_tc;
    CreateTc(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        timeout_proto.view(),
        timeout_proto.elect_height(),
        timeout_proto.leader_idx(),
        &tc);
    tc.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->X));
    tc.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->Y));
#endif
    // view change
    AdvanceView(new_sync_info()->WithTC(new_tc)->WithAggQC(agg_qc));
    
    ZJC_DEBUG("====4.1 pool: %d, create tc, view: %lu, member: %d, "
        "tc view: %lu, cur view: %lu, high_tc_: %lu",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        tc.view(), CurView(), high_tc_->view());

    Propose(
        msg_ptr,
        new_sync_info()->WithQC(HighQC())->WithTC(HighTC())->WithAggQC(agg_qc));
}

// NewView msg broadcast
// TC 在 Propose 之前单独同步，不然假设 Propose 卡死，Replicas 就会一直卡死在这个视图
// 广播 TC 的同时也应该广播 HighQC，防止只有 Leader 拥有该 HighQC，这会出现如下情况：
// 假如 Leader 是 1<-2，但 HighQC 是 3，即将打包 4
// 但由于 3 不存在需要从其他节点处同步，但又由于 HighQC3 只有 Leader 拥有，其他节点无法同步 3 给 Leader，造成卡死
// 即 Leader 有 QC 无块，Replicas 有块无 QC
void Pacemaker::Propose(const transport::MessagePtr& msg_ptr, const std::shared_ptr<SyncInfo>& sync_info) {    
    auto propose_st = Status::kError;
    // New Propose
    if (new_proposal_fn_) {
        propose_st = new_proposal_fn_(sync_info, msg_ptr);
    }

    if (propose_st != Status::kSuccess && new_view_fn_) {
        new_view_fn_(nullptr, sync_info);
    }    
}

void Pacemaker::OnRemoteTimeout_WithoutTC(const transport::MessagePtr& msg_ptr) {
    auto msg = msg_ptr->header;
    auto& timeout_proto = msg.hotstuff_timeout_proto();
    ZJC_DEBUG("====4.0 start pool: %d, view: %d, member: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        msg_ptr->header.hash64());

    if (!VerifyTimeoutMsg(msg_ptr)) {
        return;
    }

    CollectLatestHighQCs(timeout_proto);
    
    auto elect_item = crypto_->GetElectItem(common::GlobalInfo::Instance()->network_id(), timeout_proto.elect_height());
    if (!elect_item) {
        return;
    }

    if (LatestHighQCs().size() < elect_item->t() || !LatestHigestHighQC()) {
        return;
    }

    AdvanceView(new_sync_info()->WithQC(LatestHigestHighQC()));

    Propose(msg_ptr, new_sync_info()->WithQC(HighQC())->WithTC(HighTC()));
}

bool Pacemaker::VerifyTimeoutMsg(const transport::MessagePtr& msg_ptr) {
    auto msg = msg_ptr->header;
    auto& timeout_proto = msg.hotstuff_timeout_proto();
    assert(msg.type() == common::kHotstuffTimeoutMessage);
    if (!msg.has_hotstuff_timeout_proto()) {
        assert(false);
        return false;
    }
    
    if (msg.hotstuff_timeout_proto().pool_idx() != pool_idx_) {
        assert(false);
        return false;
    }

    // 统计 bls 签名
    if (timeout_proto.member_id() >= leader_rotation_->MemberSize(common::GlobalInfo::Instance()->network_id())) {
        assert(false);
        return false;
    }

    if (timeout_proto.view() < CurView()) {
        ZJC_DEBUG("====4.5 over 0 pool: %d, view: %d, curview: %lu, member: %d, hash64: %lu", 
            pool_idx_, timeout_proto.view(), CurView(), timeout_proto.member_id(),
            msg_ptr->header.hash64());
        return false;
    }

    return true;
}

void Pacemaker::CollectLatestHighQCs(const view_block::protobuf::TimeoutMessage& timeout_proto) {
    // find highest high_qc
    if (timeout_proto.view() < high_qcs_view_) {
        return;
    }

    if (timeout_proto.view() > high_qcs_view_) {
        latest_high_qcs_.clear();
        latest_highest_high_qc_ = nullptr;
        latest_high_qc_sigs_.clear();
        high_qcs_view_ = timeout_proto.view();
    }
    
    auto high_qc_of_node = std::make_shared<QC>(timeout_proto.high_qc());
    
    latest_high_qcs_.insert(std::make_pair(timeout_proto.member_id(), high_qc_of_node));
    if (!latest_highest_high_qc_ || latest_highest_high_qc_->view() < high_qc_of_node->view()) {
        latest_highest_high_qc_ = high_qc_of_node;
    }

    // for USE_TC and USE_AGG_BLS
    if (timeout_proto.has_high_qc_sig()) {
        auto high_qc_sig_of_node = std::make_shared<AggregateSignature>();
        if (!high_qc_sig_of_node->LoadFromProto(timeout_proto.high_qc_sig())) {
            return;
        }
        latest_high_qc_sigs_.push_back(high_qc_sig_of_node);
    }    
    
    CHECK_MEMORY_SIZE(latest_high_qcs_);
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora

