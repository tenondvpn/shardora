#include <common/global_info.h>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
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
    network::Route::Instance()->RegisterMessage(common::kHotstuffTimeoutMessage,
        std::bind(&Pacemaker::HandleMessage, this, std::placeholders::_1));
}

Pacemaker::~Pacemaker() {}

Status Pacemaker::AdvanceView(const std::shared_ptr<SyncInfo>& sync_info) {
    if (!sync_info) {
        return Status::kInvalidArgument;
    }

    if (!sync_info->qc && !sync_info->tc) {
        return Status::kInvalidArgument;
    }

    if (sync_info->qc) {
        UpdateHighQC(sync_info->qc);
        if (sync_info->qc->view < cur_view_) {
            return Status::kSuccess;
        }

        StopTimeoutTimer();
        duration_->ViewSucceeded();

        // TODO 如果交易池为空，则直接 return，不开启新视图
        cur_view_ = sync_info->qc->view + 1;
    } else {
        UpdateHighTC(sync_info->tc);
        if (sync_info->tc->view < cur_view_) {
            return Status::kSuccess;
        }

        StopTimeoutTimer();
        duration_->ViewTimeout();

        cur_view_ = sync_info->tc->view + 1;
    }
    
    duration_->ViewStarted();
    StartTimeoutTimer();
    return Status::kSuccess;
}

void Pacemaker::UpdateHighQC(const std::shared_ptr<QC>& qc) {
    if (!high_qc_ || high_qc_->view < qc->view || high_qc_->view == GenesisView - 1) {
        high_qc_ = qc;
    }
}

void Pacemaker::UpdateHighTC(const std::shared_ptr<TC>& tc) {
    if (!high_tc_ || high_tc_->view < tc->view || high_tc_->view == GenesisView - 1) {
        high_tc_ = tc;
    }
}

void Pacemaker::OnLocalTimeout() {
    ZJC_DEBUG("OnLocalTimeout view: %d", CurView());
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
        HandleMessage(msg_ptr);
    }    
    
    return;
}

void Pacemaker::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // TODO ecdh decrypt
    auto msg = msg_ptr->header;
    assert(msg.type() == common::kHotstuffTimeoutMessage);
    
    if (!msg.has_hotstuff_timeout_proto()) {
        return;
    }
    
    auto timeout_proto = msg.hotstuff_timeout_proto();
    ZJC_DEBUG("OnRemoteTimeout view: %d, member: %d", timeout_proto.view(), timeout_proto.member_id());;
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
    crypto_->CreateTC(timeout_proto.view(), reconstructed_sign, tc);
    auto sync_info = std::make_shared<SyncInfo>();
    sync_info->tc = tc;
    
    AdvanceView(sync_info);
    
    // TODO New Propose
    // Propose(qc);
}


} // namespace consensus

} // namespace shardora

