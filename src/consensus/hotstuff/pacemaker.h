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

class Pacemaker {
public:
    Pacemaker(
            const uint32_t& pool_idx,
            const std::shared_ptr<Crypto>&,
            const std::shared_ptr<LeaderRotation>&,
            const std::shared_ptr<ViewDuration>&);
    ~Pacemaker();

    Pacemaker(const Pacemaker&) = delete;
    Pacemaker& operator=(const Pacemaker&) = delete;

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
    
};

} // namespace consensus

} // namespace shardora

