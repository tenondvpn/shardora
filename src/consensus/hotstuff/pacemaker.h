#pragma once

#include <common/tick.h>
#include <common/time_utils.h>
#include <protos/view_block.pb.h>
#ifdef USE_AGG_BLS
#include <consensus/hotstuff/agg_crypto.h>
#else
#include <consensus/hotstuff/crypto.h>
#endif
#include <functional>

#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_duration.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <transport/transport_utils.h>

namespace shardora {

namespace hotstuff {

using NewProposalFn = std::function<Status(
        const std::shared_ptr<SyncInfo>& sync_info,
        const transport::MessagePtr msg_ptr)>;
using StopVotingFn = std::function<void(const View &view)>;
using SyncPoolFn = std::function<void(const uint32_t &, const int32_t&)>;
using NewViewFn = std::function<void(
    const std::shared_ptr<tnet::TcpInterface> conn,
    const std::shared_ptr<SyncInfo>& sync_info)>;

class Pacemaker {
public:
    Pacemaker(
            const uint32_t& pool_idx,
#ifdef USE_AGG_BLS
            const std::shared_ptr<AggCrypto>& crypto,
#else
            const std::shared_ptr<Crypto>& crypto,
#endif
            std::shared_ptr<LeaderRotation>& leader_rotation,
            const std::shared_ptr<ViewDuration>& duration);
    ~Pacemaker();

    Pacemaker(const Pacemaker&) = delete;
    Pacemaker& operator=(const Pacemaker&) = delete;

    void SetNewProposalFn(NewProposalFn fn) {
        new_proposal_fn_ = fn;
    }

    void SetNewViewFn(NewViewFn fn) {
        new_view_fn_ = fn;
    }    

    void SetStopVotingFn(StopVotingFn fn) {
        stop_voting_fn_ = fn;
    }

    void SetSyncPoolFn(SyncPoolFn fn) {
        sync_pool_fn_ = fn;
    }

    void HandleTimerMessage(const transport::MessagePtr& msg_ptr);
    // 本地超时
    void OnLocalTimeout();
    void OnLocalTimeout_WithTC();
    void OnLocalTimeout_WithoutTC();
    // 收到超时消息
    void OnRemoteTimeout(const transport::MessagePtr& msg_ptr);
    void OnRemoteTimeout_WithTC(const transport::MessagePtr& msg_ptr);
    void OnRemoteTimeout_WithoutTC(const transport::MessagePtr& msg_ptr);
    // 视图切换
    Status AdvanceView(const std::shared_ptr<SyncInfo>& sync_info);
    // void NewTc(const std::shared_ptr<TC>& tc);
    // void NewAggQc(const std::shared_ptr<AggregateQC>& agg_qc);
    // void NewQcView(uint64_t qc_view);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);

    inline std::shared_ptr<QC> HighQC() const {
        return high_qc_;
    }    

    inline std::shared_ptr<TC> HighTC() const {
        return high_tc_;
    }

    inline std::shared_ptr<QC> LatestHigestHighQC() const {
        return latest_highest_high_qc_;
    }

    inline std::unordered_map<uint32_t, std::shared_ptr<QC>> LatestHighQCs() const {
        return latest_high_qcs_;
    }

    inline std::vector<std::shared_ptr<AggregateSignature>> LatestHighQCSigs() const {
        return latest_high_qc_sigs_;
    }

    inline View CurView() const {
        return cur_view_;
    }

    // 重置超时实例
    void ResetViewDuration(const std::shared_ptr<ViewDuration>& dur) {
        duration_ = dur;
        
        StopTimeoutTimer();
        StartTimeoutTimer();
    }

    inline uint64_t DurationUs() const {
        return duration_->Duration();
    }

private:
    void UpdateHighQC(const std::shared_ptr<QC>& qc) {
        if (high_qc_->view() < qc->view()) {
            high_qc_ = qc;
        }
    }

    void UpdateHighTC(const std::shared_ptr<TC>& tc) {
        if (high_tc_->view() < tc->view()) {
            high_tc_ = tc;
            leader_rotation_->SetExtraNonce(std::to_string(high_tc_->view()));
        }
    }

    void CollectLatestHighQCs(const view_block::protobuf::TimeoutMessage& timeout_proto);

    HashStr GetCurViewHash(const std::shared_ptr<ElectItem>& elect_item) {
        // auto leader_idx = leader_rotation_->GetLeader()->index;
        TC tc;
        CreateTc(
                common::GlobalInfo::Instance()->network_id(),
                pool_idx_,
                CurView(),
                elect_item->ElectHeight(),
                0,
                &tc);
        return GetTCMsgHash(tc);        
    }
    
    void SendTimeout(const std::shared_ptr<transport::TransportMessage>& msg_ptr);

    inline void StartTimeoutTimer() {
        last_time_us_ = common::TimeUtils::TimestampUs();
        duration_us_ = duration_->Duration();
        ZJC_INFO("pool: %d duration is %lu ms", pool_idx_, duration_us_/1000);
    }

    inline void StopTimeoutTimer() {
        last_time_us_ = 0;
        duration_us_ = 0;
    }

    inline bool IsTimeout() {
        return (last_time_us_ != 0 && common::TimeUtils::TimestampUs() - last_time_us_ > duration_us_);
    }

    inline std::shared_ptr<ElectItem> elect_item(uint32_t sharding_id, uint64_t elect_height) {
        return crypto_->GetElectItem(sharding_id, elect_height);
    }

    bool VerifyTimeoutMsg(const transport::MessagePtr& msg_ptr);

    void Propose(const transport::MessagePtr& msg_ptr, const std::shared_ptr<SyncInfo>& sync_info);

    uint32_t pool_idx_;

    std::shared_ptr<QC> high_qc_ = nullptr;
    std::shared_ptr<TC> high_tc_ = nullptr;
    View cur_view_ = 0llu;

#ifdef USE_AGG_BLS
    std::shared_ptr<AggCrypto> crypto_;
#else
    std::shared_ptr<Crypto> crypto_;
#endif
    std::shared_ptr<LeaderRotation> leader_rotation_ = nullptr;
    std::shared_ptr<ViewDuration> duration_;

    NewProposalFn new_proposal_fn_ = nullptr;
    StopVotingFn stop_voting_fn_ = nullptr;
    SyncPoolFn sync_pool_fn_ = nullptr; // 同步 HighQC HighTC
    NewViewFn new_view_fn_ = nullptr;
    uint64_t last_time_us_ = 0;
    uint64_t duration_us_ = 0;
    std::shared_ptr<transport::TransportMessage> last_timeout_ = nullptr;

    // high_qc statistics
    std::unordered_map<uint32_t, std::shared_ptr<QC>> latest_high_qcs_; // 统计 high_qcs
    std::vector<std::shared_ptr<AggregateSignature>> latest_high_qc_sigs_;
    View high_qcs_view_ = BeforeGenesisView;
    std::shared_ptr<QC> latest_highest_high_qc_ = nullptr;
};

} // namespace consensus

} // namespace shardora

