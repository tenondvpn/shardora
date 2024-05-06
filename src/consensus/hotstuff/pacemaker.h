#pragma once

#include <common/tick.h>
#include <functional>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_duration.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

using NewProposalFn = std::function<void(const std::shared_ptr<SyncInfo> &sync_info)>;
using StopVotingFn = std::function<void(const View &view)>;
using OnUpdateHighTC = std::function<void(const std::shared_ptr<TC> &)>;
using SyncPoolFn = std::function<void(const uint32_t&)>;

class Pacemaker {
public:
    Pacemaker(
            const uint32_t& pool_idx,
            const std::shared_ptr<Crypto>& crypto,
            std::shared_ptr<LeaderRotation>& leader_rotation,
            const std::shared_ptr<ViewDuration>& duration);
    ~Pacemaker();

    Pacemaker(const Pacemaker&) = delete;
    Pacemaker& operator=(const Pacemaker&) = delete;

    void SetNewProposalFn(NewProposalFn fn) {
        new_proposal_fn_ = fn;
    }

    void SetStopVotingFn(StopVotingFn fn) {
        stop_voting_fn_ = fn;
    }

    void SetOnUpdateHighTcFn(OnUpdateHighTC fn) {
        on_update_high_tc_fn_ = fn;
    }

    void SetSyncPoolFn(SyncPoolFn fn) {
        sync_pool_fn_ = fn;
    }

    // 本地超时
    void OnLocalTimeout();
    // 收到超时消息
    void OnRemoteTimeout(const transport::MessagePtr& msg_ptr);
    // 视图切换
    Status AdvanceView(const std::shared_ptr<SyncInfo>& sync_info);

    inline std::shared_ptr<QC> HighQC() const {
        return high_qc_;
    }

    inline std::shared_ptr<TC> HighTC() const {
        return high_tc_;
    }

    inline View CurView() const {
        return cur_view_;
    }

    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);

private:
    void UpdateHighQC(const std::shared_ptr<QC>& qc);
    void UpdateHighTC(const std::shared_ptr<TC>& tc);

    inline void StartTimeoutTimer() {
        one_shot_tick_ = std::make_shared<common::Tick>();
        one_shot_tick_->CutOff(duration_->Duration(), std::bind(&Pacemaker::OnLocalTimeout, this));
    }

    inline void StopTimeoutTimer() {
        if (!one_shot_tick_) {
            return;
        }
        one_shot_tick_->Destroy();
    }

    uint32_t pool_idx_;
    std::shared_ptr<QC> high_qc_ = nullptr;
    std::shared_ptr<TC> high_tc_ = nullptr;
    View cur_view_;
    std::shared_ptr<Crypto> crypto_;
    std::shared_ptr<LeaderRotation> leader_rotation_ = nullptr;
    std::shared_ptr<common::Tick> one_shot_tick_ = nullptr;
    std::shared_ptr<ViewDuration> duration_;
    NewProposalFn new_proposal_fn_ = nullptr;
    StopVotingFn stop_voting_fn_ = nullptr;
    OnUpdateHighTC on_update_high_tc_fn_ = nullptr;
    SyncPoolFn sync_pool_fn_ = nullptr; // 同步 HighQC HighTC
};

} // namespace consensus

} // namespace shardora

