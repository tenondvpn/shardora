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
        std::shared_ptr<TC> tc,
        std::shared_ptr<AggregateQC> agg_qc,
        const transport::MessagePtr msg_ptr)>;
using StopVotingFn = std::function<void(const View &view)>;
using SyncPoolFn = std::function<void(const uint32_t &, const int32_t&)>;
using GetHighQCFn = std::function<QC()>;
using UpdateHighQCFn = std::function<void(const QC&)>;

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
            const std::shared_ptr<ViewDuration>& duration,
            GetHighQCFn get_high_qc_fn,
            UpdateHighQCFn update_high_qc_fn,
            const pools::protobuf::PoolLatestInfo& pool_latest_info);
    ~Pacemaker();

    Pacemaker(const Pacemaker&) = delete;
    Pacemaker& operator=(const Pacemaker&) = delete;

    void SetNewProposalFn(NewProposalFn fn) {
        new_proposal_fn_ = fn;
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
    // 收到超时消息
    void OnRemoteTimeout(const transport::MessagePtr& msg_ptr);
    // 视图切换
    void NewTc(const std::shared_ptr<TC>& tc);
    void NewAggQc(const std::shared_ptr<AggregateQC>& agg_qc);
    void NewQcView(uint64_t qc_view);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);

    inline std::shared_ptr<TC> HighTC() const {
        return high_tc_;
    }

    inline View CurView() const {
        return cur_view_;
    }

    inline QC HighQC() const {
        return get_high_qc_fn_();
    }

    void UpdateHighQC(const QC& qc) {
        update_high_qc_fn_(qc);
    }

    // 重置超时实例
    void ResetViewDuration(const std::shared_ptr<ViewDuration>& dur) {
        duration_ = dur;
        
        StopTimeoutTimer();
        SHARDORA_DEBUG("local time set start duration is reset view duration called start timeout: %lu", pool_idx_);
        StartTimeoutTimer();
    }

    inline uint64_t DurationUs() const {
        return duration_->Duration();
    }

private:
    void SendTimeout(const std::shared_ptr<transport::TransportMessage>& msg_ptr);

    inline void StartTimeoutTimer() {
        last_time_us_ = common::TimeUtils::TimestampUs();
        duration_us_ = duration_->Duration();
        SHARDORA_DEBUG("pool: %d local time set start duration is %lu ms", pool_idx_, duration_us_/1000);
    }

    inline void StopTimeoutTimer() {
        last_time_us_ = 0;
        duration_us_ = 0;
        SHARDORA_DEBUG("pool: %d local time set stop timer called!", pool_idx_);
    }

    inline bool IsTimeout() {
        // duration_us_ = 0;
        bool timeout = (last_time_us_ != 0 && 
            (common::TimeUtils::TimestampUs() - last_time_us_) > (duration_us_ + 10000000lu));
        // SHARDORA_DEBUG("pool: %u, local time last_time_us_: %lu, duration_us_: %lu, now time: %lu, dec: %lu, timeout: %d",
        //     pool_idx_, last_time_us_, duration_us_, 
        //     common::TimeUtils::TimestampUs(), 
        //     (common::TimeUtils::TimestampUs() - last_time_us_),
        //     timeout);
        return timeout;
    }

    inline std::shared_ptr<ElectItem> elect_item(uint32_t sharding_id, uint64_t elect_height) {
        return crypto_->GetElectItem(sharding_id, elect_height);
    }
    

    uint32_t pool_idx_;
    View cur_view_ = 0llu;

#ifdef USE_AGG_BLS
    std::shared_ptr<AggCrypto> crypto_;
#else
    std::shared_ptr<Crypto> crypto_;
#endif
    std::shared_ptr<LeaderRotation> leader_rotation_ = nullptr;
    std::shared_ptr<ViewDuration> duration_;
    std::shared_ptr<TC> high_tc_ = nullptr;
    GetHighQCFn get_high_qc_fn_ = nullptr;
    UpdateHighQCFn update_high_qc_fn_ = nullptr;
    NewProposalFn new_proposal_fn_ = nullptr;
    StopVotingFn stop_voting_fn_ = nullptr;
    SyncPoolFn sync_pool_fn_ = nullptr; // 同步 HighQC HighTC
    uint64_t last_time_us_ = 0;
    uint64_t duration_us_ = 0;
    std::shared_ptr<transport::TransportMessage> last_timeout_ = nullptr;
#ifdef USE_AGG_BLS
    std::unordered_map<uint32_t, std::shared_ptr<QC>> high_qcs_; // 统计 high_qcs
    std::vector<std::shared_ptr<AggregateSignature>> high_qc_sigs_;
    View high_qcs_view_ = BeforeGenesisView;
#endif
};

} // namespace consensus

} // namespace shardora

