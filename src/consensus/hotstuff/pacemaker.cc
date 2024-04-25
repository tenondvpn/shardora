#include <common/global_info.h>
#include <common/log.h>
#include <common/utils.h>
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
        const std::shared_ptr<LeaderRotation>& lr,
        const std::shared_ptr<ViewDuration>& d) :
    pool_idx_(pool_idx), crypto_(c), leader_rotation_(lr), duration_(d) {
    
    high_qc_ = GetQCWrappedByGenesis();
    high_tc_ = std::make_shared<TC>(nullptr, BeforeGenesisView);
    cur_view_ = GenesisView;
}

Pacemaker::~Pacemaker() {}

Status Pacemaker::AdvanceView(const std::shared_ptr<SyncInfo>& sync_info) {
    if (!sync_info) {
        return Status::kInvalidArgument;
    }

    if (!sync_info->qc && !sync_info->tc) {
        return Status::kInvalidArgument;
    }

    View qc_view = 0;
    bool timeout = false;
    if (sync_info->qc) {
        UpdateHighQC(sync_info->qc);
        if (sync_info->qc->view < cur_view_ && cur_view_ != BeforeGenesisView) {
            return Status::kSuccess;
        }
        qc_view = sync_info->qc->view;
    }

    View tc_view = 0;
    if (sync_info->tc) {
        timeout = false;
        UpdateHighTC(sync_info->tc);
        if (sync_info->tc->view < cur_view_ && cur_view_ != BeforeGenesisView) {
            return Status::kSuccess;
        }
        tc_view = sync_info->tc->view;
    }

    auto v = std::max(qc_view, tc_view);
    if (v < cur_view_) {
        // 旧的 view
        return Status::kSuccess;
    }
    
    StopTimeoutTimer();
    if (timeout) {
        duration_->ViewTimeout();
    } else {
        duration_->ViewSucceeded();
    }
    
    // TODO 如果交易池为空，则直接 return，不开启新视图
    cur_view_ = v + 1; 
    
    duration_->ViewStarted();
    StartTimeoutTimer();
    return Status::kSuccess;
}

void Pacemaker::UpdateHighQC(const std::shared_ptr<QC>& qc) {
    if (qc->view == BeforeGenesisView) {
        return;
    }
    if (!high_qc_ || high_qc_->view < qc->view || high_qc_->view == BeforeGenesisView) {
        high_qc_ = qc;
    }
}

void Pacemaker::UpdateHighTC(const std::shared_ptr<TC>& tc) {
    if (tc->view == BeforeGenesisView) {
        return;
    }
    if (!high_tc_ || high_tc_->view < tc->view || high_tc_->view == BeforeGenesisView) {
        high_tc_ = tc;
    }
}

void Pacemaker::OnLocalTimeout() {
    ZJC_DEBUG("OnLocalTimeout pool: %d, view: %d", pool_idx_, CurView());
    // start a new timer for the timeout case
    StartTimeoutTimer();
    
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
    if (crypto_->Sign(
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

    // TODO Stop Voting

    auto leader = leader_rotation_->GetLeader();
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kHotstuffTimeoutMessage);
    transport::TcpTransport::Instance()->SetMessageHash(msg);

    // TODO ecdh encrypt

    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        ZJC_DEBUG("Send TimeoutMsg to ip: %s, port: %d",
            common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port);
        transport::TcpTransport::Instance()->Send(common::Uint32ToIp(leader->public_ip), leader->public_port, msg);
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
    
    auto timeout_proto = msg.hotstuff_timeout_proto();
    ZJC_DEBUG("OnRemoteTimeout pool: %d, view: %d, member: %d", pool_idx_, timeout_proto.view(), timeout_proto.member_id());
    // TODO 统计 bls 签名
    
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign = nullptr;
    Status s = crypto_->ReconstructAndVerify(
            timeout_proto.elect_height(),
            timeout_proto.view(),
            timeout_proto.view_hash(),
            timeout_proto.member_id(),
            timeout_proto.sign_x(),
            timeout_proto.sign_y(),
            reconstructed_sign);
    if (s != Status::kSuccess) {
        return;
    }
    // TODO 视图切换
    
    auto tc = std::make_shared<TC>();
    s = crypto_->CreateTC(timeout_proto.view(), reconstructed_sign, tc);
    if (s != Status::kSuccess || !tc) {
        return;
    }
    ZJC_DEBUG("CreateTC pool: %d, view: %d, member: %d, view: %d",
        pool_idx_, timeout_proto.view(), timeout_proto.member_id(), tc->view);
    
    auto sync_info = std::make_shared<SyncInfo>();
    AdvanceView(sync_info->WithTC(tc));
    
    // TODO New Propose
    if (new_proposal_fn_) {
        new_proposal_fn_(pool_idx_, sync_info->WithQC(HighQC())->WithTC(HighTC()));
    }
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora

