#pragma once

#include <consensus/hotstuff/types.h>

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
    void HandleMessage();
    // 视图切换
    void AdvanceView(const std::shared_ptr<QC> qc);

    inline std::shared_ptr<QC> HighQC() const {
        return high_qc_;
    }

    inline View CurView() const {
        return cur_view_;
    }

private:
    std::shared_ptr<QC> high_qc_;
    View cur_view_;
};

} // namespace consensus

} // namespace shardora

