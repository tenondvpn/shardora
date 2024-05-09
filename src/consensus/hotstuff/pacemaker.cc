#include <common/global_info.h>
#include <common/log.h>
#include <common/utils.h>
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
    
    high_qc_ = GetQCWrappedByGenesis();
    high_tc_ = std::make_shared<TC>(nullptr, BeforeGenesisView);
    cur_view_ = GenesisView;
}

Pacemaker::~Pacemaker() {}

void Pacemaker::HandleTimerMessage(const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("pool: %d timer", pool_idx_);
    if (IsTimeout()) {
        StopTimeoutTimer();
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
    if (timeout) {
        duration_->ViewTimeout();
    } else {
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
    // if (pool_idx_ != 62) {
    //     return;
    // }

    ZJC_DEBUG("OnLocalTimeout pool: %d, view: %d", pool_idx_, CurView());
    // start a new timer for the timeout case
    StartTimeoutTimer();
    
    // 超时后先触发一次同步，主要是尽量同步最新的 HighQC，降低因 HighQC 不一致造成多次超时的概率
    // 由于 HotstuffSyncer 周期性同步，这里不触发同步影响也不大
    if (sync_pool_fn_) {
        sync_pool_fn_(pool_idx_);
    }    
    
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    
    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());

    auto elect_item = crypto_->GetLatestElectItem();
    if (!elect_item) {
        return;
    }
    
    std::string bls_sign_x;
    std::string bls_sign_y;
    auto view_hash = GetViewHash(CurView());
    // 使用最新的 elect_height 签名
    if (crypto_->PartialSign(
            elect_item->ElectHeight(),
            view_hash,
            &bls_sign_x,
            &bls_sign_y) != Status::kSuccess) {
        return;
    }
    
    timeout_msg.set_sign_x(bls_sign_x);
    timeout_msg.set_sign_y(bls_sign_y);
    timeout_msg.set_view_hash(GetViewHash(CurView()));
    timeout_msg.set_view(CurView());
    timeout_msg.set_elect_height(elect_item->ElectHeight());
    timeout_msg.set_pool_idx(pool_idx_); // 用于分配线程

    auto leader = leader_rotation_->GetLeader();
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    
    msg.set_type(common::kHotstuffTimeoutMessage);
    transport::TcpTransport::Instance()->SetMessageHash(msg);

    // 停止对当前 view 的投票
    if (stop_voting_fn_) {
        stop_voting_fn_(CurView());
    }

    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        dht::DhtKeyManager dht_key(leader->net_id, leader->id);
        msg.set_des_dht_key(dht_key.StrKey());
        ZJC_DEBUG("Send TimeoutMsg pool: %d, to ip: %s, port: %d, "
            "local_idx: %d, id: %s, local id: %s, hash64: %lu",
            pool_idx_,
            common::Uint32ToIp(leader->public_ip).c_str(), 
            leader->public_port, 
            timeout_msg.member_id(),
            common::Encode::HexEncode(leader->id).c_str(),
            common::Encode::HexEncode(crypto_->security()->GetAddress()).c_str(),
            msg.hash64());
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
    
    return;
}

// OnRemoteTimeout 由 Consensus 调用
void Pacemaker::OnRemoteTimeout(const transport::MessagePtr& msg_ptr) {
    // TODO ecdh decrypt
    auto msg = msg_ptr->header;
    assert(msg.type() == common::kHotstuffTimeoutMessage);
    
    if (!msg.has_hotstuff_timeout_proto()) {
        return;
    }
    
    if (msg.hotstuff_timeout_proto().pool_idx() != pool_idx_) {
        return;
    }
    
    // 统计 bls 签名
    auto timeout_proto = msg.hotstuff_timeout_proto();
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
    s = crypto_->CreateTC(timeout_proto.view(), reconstructed_sign, tc);
    if (s != Status::kSuccess || !tc) {
        return;
    }
    ZJC_DEBUG("====4.1 pool: %d, create tc, view: %d, member: %d, view: %d",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), tc->view);

    AdvanceView(new_sync_info()->WithTC(tc));

    assert(new_proposal_fn_ != nullptr);
    
    // NewView msg broadcast
    if (new_view_fn_) {
        new_view_fn_(new_sync_info()->WithTC(HighTC()));
    }
    
    // New Propose
    if (new_proposal_fn_) {
        new_proposal_fn_(new_sync_info()->WithQC(HighQC())->WithTC(HighTC()));
    }
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora

