#pragma once

#include <bls/bls_manager.h>
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

namespace consensus {

class Pacemaker {
public:
    Pacemaker();
    ~Pacemaker();

    Pacemaker(const Pacemaker&) = delete;
    Pacemaker& operator=(const Pacemaker&) = delete;

    // 本地超时
    void OnLocalTimeout();
    // 收到超时消息
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    // 视图切换
    Status AdvanceView(const std::shared_ptr<SyncInfo>& sync_info, bool is_timeout);

    inline std::shared_ptr<QC> HighQC() const {
        return high_qc_;
    }

    inline View CurView() const {
        return cur_view_;
    }

private:
    void UpdateHighQC(const std::shared_ptr<ViewBlock>& qc_wrapper_block);

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

    inline uint32_t minAgreeMemberCount() const {
        // TODO
        return 0;
    }

    inline uint32_t memberCount() const {
        // TODO
        return 0;
    }

    inline libff::alt_bn128_Fr localSecKey() const {
        // TODO
        return libff::alt_bn128_Fr::one();
    }

    inline libff::alt_bn128_G1 g1Hash() const {
        // TODO
        return libff::alt_bn128_G1::one();
    }
    
    std::shared_ptr<QC> high_qc_;
    std::shared_ptr<ViewBlock> high_qc_wrapper_block_;
    View cur_view_;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<LeaderRotation> leader_rotation_ = nullptr;
    std::shared_ptr<common::Tick> one_shot_tick_ = nullptr;
    std::shared_ptr<ViewDuration> duration_;
    // std::shared_ptr<Consensus> consensus_; 获取本 epoch 共识的 elect 信息，用于签名之类
};

} // namespace consensus

} // namespace shardora

