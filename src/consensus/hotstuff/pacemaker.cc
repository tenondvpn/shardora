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
        const std::shared_ptr<ViewDuration>& d,
        GetHighQCFn get_high_qc_fn,
        UpdateHighQCFn update_high_qc_fn,
        const pools::protobuf::PoolLatestInfo& pool_latest_info) :
    pool_idx_(pool_idx), crypto_(c), leader_rotation_(lr), duration_(d), get_high_qc_fn_(get_high_qc_fn), update_high_qc_fn_(update_high_qc_fn) {
    high_tc_ = std::make_shared<QC>();
    auto& qc_item = *high_tc_;
    qc_item.set_network_id(common::GlobalInfo::Instance()->network_id());
    qc_item.set_pool_index(pool_idx_);
    qc_item.set_view(pool_latest_info.view());
    qc_item.set_view_block_hash("");
    qc_item.set_elect_height(1);
    qc_item.set_leader_idx(0);
    cur_view_ = pool_latest_info.view() + 1;
    StartTimeoutTimer();
}

Pacemaker::~Pacemaker() {}

void Pacemaker::HandleTimerMessage(const transport::MessagePtr& msg_ptr) {
    // if (IsTimeout()) {
    //     SHARDORA_DEBUG("pool: %d timeout", pool_idx_);
    //     OnLocalTimeout();
    // }
}

void Pacemaker::NewTc(const std::shared_ptr<view_block::protobuf::QcItem>& tc) {
    StopTimeoutTimer();
    if (IsQcTcValid(*tc)) {
        if (cur_view_ < tc->view() + 1) {
            cur_view_ = tc->view() + 1;
            SHARDORA_DEBUG("success new tc view: %lu, %u_%u_%lu, pool index: %u",
                cur_view_, tc->network_id(), tc->pool_index(), tc->view(), pool_idx_);
        }

        if (high_tc_->view() < tc->view()) {
            high_tc_ = tc;
        }
            
        duration_->ViewSucceeded();
        duration_->ViewStarted();
    }
   
    SHARDORA_DEBUG("local time set start duration is new tc called start timeout: %lu", pool_idx_);
    StartTimeoutTimer();
}

void Pacemaker::NewAggQc(const std::shared_ptr<AggregateQC>& agg_qc) {
#ifdef USE_AGG_BLS 
    if (agg_qc && agg_qc->IsValid()) {
        auto high_qc = std::make_shared<QC>();
        Status s = crypto_->VerifyAggregateQC(
                common::GlobalInfo::Instance()->network_id(),
                agg_qc,
                high_qc);
        if (s != Status::kSuccess) {
            SHARDORA_ERROR("new agg qc failed, pool: %d, s: %d, view: %lu", pool_idx_, s, agg_qc->GetView());
            return;
        }

        // update high_qc
        UpdateHighQC(*high_qc);
        NewQcView(high_qc->view());
    }
#endif
}

void Pacemaker::NewQcView(uint64_t qc_view) {
    if (cur_view_ < qc_view + 1) {
        cur_view_ = qc_view + 1;
        SHARDORA_DEBUG("success new qc view: %lu, %u_%u_%lu, pool index: %u",
            qc_view, common::GlobalInfo::Instance()->network_id(), pool_idx_, qc_view, pool_idx_);
    }
}

void Pacemaker::OnLocalTimeout() {
    // TODO: check it
    return;
    // TODO(HT): test
    SHARDORA_DEBUG("OnLocalTimeout pool: %d, view: %d", pool_idx_, CurView());
    // start a new timer for the timeout case
    StopTimeoutTimer();
    duration_->ViewTimeout();
    SHARDORA_DEBUG("local time set start duration is OnLocalTimeout called start timeout: %lu", pool_idx_);
    defer(StartTimeoutTimer());
    if (leader_rotation_->GetLocalMemberIdx() == common::kInvalidUint32) {
        return;
    }

    // 超时后先触发一次同步，主要是尽量同步最新的 HighQC，降低因 HighQC 不一致造成多次超时的概率
    // 由于 HotstuffSyncer 周期性同步，这里不触发同步影响也不大
    if (sync_pool_fn_) {
        sync_pool_fn_(pool_idx_, 1);
    }

    auto elect_item = crypto_->GetLatestElectItem(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        assert(false);
        return;
    }

    // auto leader_idx = leader_rotation_->GetLeader()->index;
    TC tc;
    CreateTc(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        CurView(),
        elect_item->ElectHeight(),
        0,
        &tc);
    auto tc_msg_hash = GetTCMsgHash(tc);
    // if view is last one, deal directly.
    // 更换 epoch 后重新打包
    if (last_timeout_ && last_timeout_->header.has_hotstuff_timeout_proto() &&
            last_timeout_->header.hotstuff_timeout_proto().view() >= CurView() &&
            last_timeout_->header.hotstuff_timeout_proto().view_hash() == tc_msg_hash) {
        last_timeout_->times_idx = 0;
        auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
        tmp_msg_ptr->header.CopyFrom(last_timeout_->header);
        SHARDORA_DEBUG("use exist local timeout message pool: %u, "
            "last_timeout_->header.hotstuff_timeout_proto().view(): %lu, cur view: %lu",
            pool_idx_, 
            tmp_msg_ptr->header.hotstuff_timeout_proto().view(), 
            CurView());
        SendTimeout(tmp_msg_ptr);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;

#ifdef USE_AGG_BLS
    AggregateSignature partial_sig;
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            tc_msg_hash,
            &partial_sig) != Status::kSuccess) {
        SHARDORA_ERROR("sign message failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(tc_msg_hash).c_str());
        return;        
    }    
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
        SHARDORA_ERROR("sign message failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(tc_msg_hash).c_str());
        return;
    }

#endif
    
    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());
#ifdef USE_AGG_BLS
    timeout_msg.mutable_view_sig()->CopyFrom(partial_sig.DumpToProto());
    // 对本节点的 high qc 签名
    AggregateSignature high_qc_sig;
#ifdef ENABLE_FAST_HOTSTUFF    
    auto high_qc_msg_hash = GetQCMsgHash(HighQC()); 
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            high_qc_msg_hash,
            &high_qc_sig) != Status::kSuccess) {
        SHARDORA_ERROR("sign high qc failed: %u, elect height: %lu, hash: %s",
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            common::Encode::HexEncode(high_qc_msg_hash).c_str());
        return;
    }
#endif    
    
    timeout_msg.mutable_high_qc()->CopyFrom(HighQC());
    timeout_msg.mutable_high_qc_sig()->CopyFrom(high_qc_sig.DumpToProto());
#else
    timeout_msg.set_sign_x(bls_sign_x);
    timeout_msg.set_sign_y(bls_sign_y);
    timeout_msg.set_view_hash(tc_msg_hash);
    timeout_msg.set_view(CurView());
    timeout_msg.set_elect_height(elect_item->ElectHeight());
    timeout_msg.set_pool_idx(pool_idx_); // 用于分配线程
    timeout_msg.set_leader_idx(0);
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kHotstuffTimeoutMessage);
    last_timeout_ = msg_ptr;
    // 停止对当前 view 的投票
    // if (stop_voting_fn_) {
    //     stop_voting_fn_(CurView());
    // }

    SHARDORA_DEBUG("now send local timeout msg hash: %s, view: %u, pool: %u, "
        "elect height: %lu, member index: %u, member size: %u, "
        "bls_sign_x: %s, bls_sign_y: %s, hash64: %lu",
        common::Encode::HexEncode(tc_msg_hash).c_str(),
        CurView(), pool_idx_, elect_item->ElectHeight(),
        timeout_msg.member_id(),
        leader_rotation_->MemberSize(common::GlobalInfo::Instance()->network_id()),
        bls_sign_x.c_str(),
        bls_sign_y.c_str());    
#endif
    timeout_msg.set_view_hash(tc_msg_hash);
    timeout_msg.set_view(CurView());
    timeout_msg.set_elect_height(elect_item->ElectHeight());
    timeout_msg.set_pool_idx(pool_idx_); // 用于分配线程
    timeout_msg.set_leader_idx(0);
    
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kHotstuffTimeoutMessage);
    last_timeout_ = msg_ptr;
    // 停止对当前 view 的投票
    // if (stop_voting_fn_) {
    //     stop_voting_fn_(CurView());
    // }

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
        SHARDORA_DEBUG("Send TimeoutMsg pool: %d, to ip: %s, port: %d, "
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

// OnRemoteTimeout 由 Consensus 调用
void Pacemaker::OnRemoteTimeout(const transport::MessagePtr& msg_ptr) {
    auto msg = msg_ptr->header;
    auto& timeout_proto = msg.hotstuff_timeout_proto();
    SHARDORA_DEBUG("====4.0 start pool: %d, view: %d, member: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        msg_ptr->header.hash64());
    // TODO ecdh decrypt
    assert(msg.type() == common::kHotstuffTimeoutMessage);
    if (!msg.has_hotstuff_timeout_proto()) {
        assert(false);
        return;
    }
    
    if (msg.hotstuff_timeout_proto().pool_idx() != pool_idx_) {
        assert(false);
        return;
    }

    // 统计 bls 签名
    if (timeout_proto.member_id() >= leader_rotation_->MemberSize(common::GlobalInfo::Instance()->network_id())) {
        assert(false);
        return;
    }

    if (timeout_proto.view() < CurView()) {
        SHARDORA_DEBUG("====4.5 over 0 pool: %d, view: %d, curview: %lu, member: %d, hash64: %lu", 
            pool_idx_, timeout_proto.view(), CurView(), timeout_proto.member_id(),
            msg_ptr->header.hash64());
        return;
    }

#ifdef USE_AGG_BLS
    // 统计 high_qc，用于生成 AggQC
    if (timeout_proto.view() < high_qcs_view_) {
        return;
    }

    if (timeout_proto.view() > high_qcs_view_) {
        high_qcs_.clear();
        high_qc_sigs_.clear();
        high_qcs_view_ = timeout_proto.view();
    }
    
    auto high_qc_of_node = std::make_shared<QC>(timeout_proto.high_qc());
    auto high_qc_sig_of_node = std::make_shared<AggregateSignature>();
    if (!high_qc_sig_of_node->LoadFromProto(timeout_proto.high_qc_sig())) {
        return;
    }
    
    high_qcs_.insert(std::make_pair(timeout_proto.member_id(), high_qc_of_node));
    CHECK_MEMORY_SIZE(high_qcs_);
    high_qc_sigs_.push_back(high_qc_sig_of_node);
    
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
    SHARDORA_DEBUG("====4.0 pool: %d, view: %d, member: %d, status: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), s,
        msg_ptr->header.hash64());    
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

    std::shared_ptr<AggregateQC> agg_qc = nullptr;
#ifdef ENABLE_FAST_HOTSTUFF    
    agg_qc = crypto_->CreateAggregateQC(
            common::GlobalInfo::Instance()->network_id(),
            timeout_proto.elect_height(),
            timeout_proto.view(),
            high_qcs_,
            high_qc_sigs_);
    if (!agg_qc || !agg_qc->IsValid()) {
        return;
    }
#endif
    // view change
    NewTc(new_tc);
    NewAggQc(agg_qc);
    // AdvanceView(new_sync_info()->WithTC(tc)->WithAggQC(agg_qc));

    // NewView msg broadcast
    // TC 在 Propose 之前单独同步，不然假设 Propose 卡死，Replicas 就会一直卡死在这个视图
    // 广播 TC 的同时也应该广播 HighQC，防止只有 Leader 拥有该 HighQC，这会出现如下情况：
    // 假如 Leader 是 1<-2，但 HighQC 是 3，即将打包 4
    // 但由于 3 不存在需要从其他节点处同步，但又由于 HighQC3 只有 Leader 拥有，其他节点无法同步 3 给 Leader，造成卡死
    // 即 Leader 有 QC 无块，Replicas 有块无 QC
    auto propose_st = Status::kError;
    // New Propose
    if (new_proposal_fn_) {
        SHARDORA_DEBUG("now ontime called propose: %d", pool_idx_);
        propose_st = new_proposal_fn_(new_tc, agg_qc, msg_ptr);
    }
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
    SHARDORA_DEBUG("====4.0.1 pool: %d, view: %d, member: %d, status: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), (int32_t)s,
        msg_ptr->header.hash64());
    if (s != Status::kSuccess) {
        SHARDORA_DEBUG("====4.5 over 1 pool: %d, view: %d, member: %d, hash64: %lu, status: %d", 
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
    // 视图切换
    SHARDORA_DEBUG("====4.1 pool: %d, create tc, view: %lu, member: %d, "
        "tc view: %lu, cur view: %lu, high_tc_: %lu",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        tc.view(), CurView(), high_tc_->view());
    // NewTc(new_tc);
    SHARDORA_DEBUG("====4.1.0 pool: %d, create tc, view: %lu, member: %d, "
        "tc view: %lu, cur view: %lu, high_tc_: %lu",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        tc.view(), CurView(), high_tc_->view());
    // NewView msg broadcast
    // TC 在 Propose 之前单独同步，不然假设 Propose 卡死，Replicas 就会一直卡死在这个视图
    // 广播 TC 的同时也应该广播 HighQC，防止只有 Leader 拥有该 HighQC，这会出现如下情况：
    // 假如 Leader 是 1<-2，但 HighQC 是 3，即将打包 4
    // 但由于 3 不存在需要从其他节点处同步，但又由于 HighQC3 只有 Leader 拥有，其他节点无法同步 3 给 Leader，造成卡死
    // 即 Leader 有 QC 无块，Replicas 有块无 QC
    auto propose_st = Status::kError;
    // New Propose
    if (new_proposal_fn_) {
        SHARDORA_DEBUG("now ontime called propose: %d", pool_idx_);
        propose_st = new_proposal_fn_(new_tc, nullptr, msg_ptr);
    }

    SHARDORA_DEBUG("====4.1.1 pool: %d, create tc, view: %lu, member: %d, "
        "tc view: %lu, cur view: %lu, high_tc_: %lu",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        tc.view(), CurView(), high_tc_->view());
    SHARDORA_DEBUG("====4.5 over 2 pool: %d, view: %d, member: %d, hash64: %lu", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(),
        msg_ptr->header.hash64());
#endif
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora

