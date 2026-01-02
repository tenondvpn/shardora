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
            SHARDORA_ERROR("new agg qc failed, pool: %d, s: %d, view: %lu", pool_idx_, (int32_t)s, agg_qc->GetView());
            return;
        }

        // update high_qc.
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
}

void Pacemaker::SendTimeout(const std::shared_ptr<transport::TransportMessage>& msg_ptr) {
}

// OnRemoteTimeout is called by Consensus
void Pacemaker::OnRemoteTimeout(const transport::MessagePtr& msg_ptr) {
}

int Pacemaker::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

} // namespace consensus

} // namespace shardora
