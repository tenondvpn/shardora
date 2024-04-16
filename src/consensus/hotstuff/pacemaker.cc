#include <common/global_info.h>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <dht/dht_key.h>
#include <protos/transport.pb.h>
#include <protos/view_block.pb.h>
#include <transport/tcp_transport.h>

namespace shardora {

namespace hotstuff {

Pacemaker::Pacemaker(const std::shared_ptr<Crypto>& c,
    const std::shared_ptr<LeaderRotation>& lr,
    const std::shared_ptr<ViewDuration>& d) : crypto_(c), leader_rotation_(lr), duration_(d) {}

Pacemaker::~Pacemaker() {}

Status Pacemaker::AdvanceView(const std::shared_ptr<SyncInfo>& sync_info, bool is_timeout) {
    if (!sync_info || !sync_info->view_block || !sync_info->view_block->qc) {
        return Status::kSuccess;
    }

    auto qc = sync_info->view_block->qc;
    UpdateHighQC(sync_info->view_block);
    if (qc->view < cur_view_) {
        return Status::kSuccess;
    }

    StopTimeoutTimer();
    if (!is_timeout) {
        duration_->ViewSucceeded();
    } else {
        duration_->ViewTimeout();
    }

    // TODO 如果交易池为空，则直接 return，不开启新视图
    cur_view_ = qc->view + 1;
    duration_->ViewStarted();
    StartTimeoutTimer();
    return Status::kSuccess;
}

void Pacemaker::UpdateHighQC(const std::shared_ptr<ViewBlock>& qc_wrapper_block) {
    auto qc = qc_wrapper_block->qc;
    if (!high_qc_ || high_qc_->view < qc->view) {
        high_qc_ = qc;
        high_qc_wrapper_block_ = qc_wrapper_block;
    }
}

void Pacemaker::OnLocalTimeout() {
    ZJC_DEBUG("OnLocalTimeout view: %d", CurView());
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    
    view_block::protobuf::TimeoutMessage& timeout_msg = *msg.mutable_hotstuff_timeout_proto();
    timeout_msg.set_member_id(leader_rotation_->GetLocalMemberIdx());

    if (!high_qc_wrapper_block_ || !high_qc_wrapper_block_->block) {
        return;
    }
    
    auto wrapper_block = timeout_msg.mutable_high_qc_wrapper_block();
    ViewBlock2Proto(high_qc_wrapper_block_, wrapper_block);

    auto elect_height = high_qc_wrapper_block_->block->electblock_height();
    auto msg_hash = high_qc_wrapper_block_->hash;

    std::string bls_sign_x;
    std::string bls_sign_y;
    if (crypto_->Sign(elect_height, msg_hash, &bls_sign_x, &bls_sign_y) != Status::kSuccess) {
        return;
    }
    
    timeout_msg.set_sign_x(bls_sign_x);
    timeout_msg.set_sign_y(bls_sign_y);
    timeout_msg.set_msg_hash(msg_hash);

    // TODO Stop Voting

    auto leader = leader_rotation_->GetLeader();
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id);
    transport::TcpTransport::Instance()->SetMessageHash(msg);

    // TODO ecdh sign

    if (leader->index != leader_rotation_->GetLocalMemberIdx()) {
        transport::TcpTransport::Instance()->Send(common::Uint32ToIp(leader->public_ip), leader->public_port, msg);
        return;
    }
    
    HandleMessage(msg_ptr);
    return;
}

void Pacemaker::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // TODO verify ecdh
    auto msg = msg_ptr->header;
    if (!msg.has_hotstuff_timeout_proto()) {
        return;
    }
    
    auto timeout_proto = msg.hotstuff_timeout_proto();
    // TODO 统计 bls 签名
    auto view_block = std::make_shared<ViewBlock>();
    Proto2ViewBlock(timeout_proto.high_qc_wrapper_block(), view_block);

    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign = nullptr;
    Status s = crypto_->ReconstructAndVerify(view_block->ElectHeight(),
        view_block->view,
        timeout_proto.msg_hash(),
        timeout_proto.member_id(),
        timeout_proto.sign_x(),
        timeout_proto.sign_y(),
        reconstructed_sign);
    if (s != Status::kSuccess) {
        return;
    }

    // TODO 视图切换
    auto sync_info = std::make_shared<SyncInfo>();
    sync_info->view_block = view_block;
    AdvanceView(sync_info, true);

    // TODO Create QC
    std::shared_ptr<QC> qc = nullptr;
    crypto_->CreateQC(view_block, reconstructed_sign, qc);
    // TODO New Propose
    // Propose(qc);
}


} // namespace consensus

} // namespace shardora

