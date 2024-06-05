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
        const std::shared_ptr<Crypto>& c,
        std::shared_ptr<LeaderRotation>& lr,
        const std::shared_ptr<ViewDuration>& d) :
    pool_idx_(pool_idx), crypto_(c), leader_rotation_(lr), duration_(d) {
    
    high_qc_ = GetQCWrappedByGenesis(pool_idx_);
    high_tc_ = std::make_shared<TC>(
        common::GlobalInfo::Instance()->network_id(), 
        pool_idx_, 
        nullptr, 
        BeforeGenesisView, 
        1, 
        0);
    cur_view_ = GenesisView;
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

    auto new_v = std::max(high_qc_->view, high_tc_->view) + 1;
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

void Pacemaker::UpdateHighQC(const std::shared_ptr<QC>& qc) {
    if (high_qc_->view < qc->view) {
        high_qc_ = qc;
    }
}

void Pacemaker::UpdateHighTC(const std::shared_ptr<TC>& tc) {
    if (high_tc_->view < tc->view) {
        high_tc_ = tc;
    }
}

void Pacemaker::OnLocalTimeout() {
    // TODO(HT): test
    ZJC_DEBUG("OnLocalTimeout pool: %d, view: %d", pool_idx_, CurView());
    // start a new timer for the timeout case
    StopTimeoutTimer();
    duration_->ViewTimeout();
    defer(StartTimeoutTimer());
    // 超时后先触发一次同步，主要是尽量同步最新的 HighQC，降低因 HighQC 不一致造成多次超时的概率
    // 由于 HotstuffSyncer 周期性同步，这里不触发同步影响也不大
    if (sync_pool_fn_) {
        sync_pool_fn_(pool_idx_, 1);
    }    
    
    // if view is last one, deal directly.
    if (last_timeout_ && last_timeout_->header.has_hotstuff_timeout_proto() &&
        last_timeout_->header.hotstuff_timeout_proto().view() >= CurView()) {
        SendTimeout(last_timeout_);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    auto elect_item = crypto_->GetLatestElectItem(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return;
    }
    
    std::string bls_sign_x;
    std::string bls_sign_y;
    // TODO Leader Idx
    auto view_hash = GetViewHash(
        common::GlobalInfo::Instance()->network_id(),
        pool_idx_,
        CurView(), elect_item->ElectHeight(), 0);
    // 使用最新的 elect_height 签名
    if (crypto_->PartialSign(
            common::GlobalInfo::Instance()->network_id(),
            elect_item->ElectHeight(),
            view_hash,
            &bls_sign_x,
            &bls_sign_y) != Status::kSuccess) {
        return;
    }

    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());    
    timeout_msg.set_sign_x(bls_sign_x);
    timeout_msg.set_sign_y(bls_sign_y);
    timeout_msg.set_view_hash(view_hash);
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

    ZJC_DEBUG("now send local timeout msg hash: %s, view: %u, pool: %u",
        common::Encode::HexEncode(view_hash).c_str(), CurView(), pool_idx_);
    SendTimeout(msg_ptr);
}

void Pacemaker::SendTimeout(const std::shared_ptr<transport::TransportMessage>& msg_ptr) {
    auto& msg = msg_ptr->header;
    auto leader = leader_rotation_->GetLeader();
    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        dht::DhtKeyManager dht_key(leader->net_id, leader->id);
        msg.set_des_dht_key(dht_key.StrKey());
        transport::TcpTransport::Instance()->SetMessageHash(msg);
        ZJC_DEBUG("Send TimeoutMsg pool: %d, to ip: %s, port: %d, "
            "local_idx: %d, id: %s, local id: %s, hash64: %lu, view: %lu, highqc: %lu",
            pool_idx_,
            common::Uint32ToIp(leader->public_ip).c_str(),
            leader->public_port,
            msg.hotstuff_timeout_proto().member_id(),
            common::Encode::HexEncode(leader->id).c_str(),
            common::Encode::HexEncode(crypto_->security()->GetAddress()).c_str(),
            msg.hash64(),
            msg.hotstuff_timeout_proto().view(),
            HighQC()->view);
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
    // TODO ecdh decrypt
    auto msg = msg_ptr->header;
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
    auto& timeout_proto = msg.hotstuff_timeout_proto();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign = nullptr;
    Status s = crypto_->ReconstructAndVerifyThresSign(
            timeout_proto.elect_height(),
            timeout_proto.view(),
            timeout_proto.view_hash(),
            timeout_proto.member_id(),
            timeout_proto.sign_x(),
            timeout_proto.sign_y(),
            reconstructed_sign);
    ZJC_DEBUG("====4.0 pool: %d, view: %d, member: %d, status: %d", 
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), s);
    if (s != Status::kSuccess) {
        return;
    }
    // 视图切换
    auto tc = std::make_shared<TC>();
    s = crypto_->CreateTC(
        timeout_proto.view(),
        timeout_proto.elect_height(),
        timeout_proto.leader_idx(),
        reconstructed_sign,
        tc);
    if (s != Status::kSuccess || !tc) {
        return;
    }
    ZJC_DEBUG("====4.1 pool: %d, create tc, view: %d, member: %d, view: %d",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), tc->view);

    AdvanceView(new_sync_info()->WithTC(tc));
    
    // NewView msg broadcast
    if (new_view_fn_) {
        ZJC_DEBUG("====4.2 pool: %d, broadcast tc, view: %d, member: %d, view: %d",
            pool_idx_, timeout_proto.view(), timeout_proto.member_id(), tc->view);
        new_view_fn_(new_sync_info()->WithTC(HighTC()));
    }
    
    // New Propose
    if (new_proposal_fn_) {
        ZJC_DEBUG("now ontime called propose: %d", pool_idx_);
        new_proposal_fn_(new_sync_info()->WithQC(HighQC())->WithTC(HighTC()));
    }
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora

