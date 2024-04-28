#pragma once
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

class ViewDuration {
public:
    ViewDuration() = default;
    ~ViewDuration() {};

    ViewDuration(const ViewDuration&) = delete;
    ViewDuration& operator=(const ViewDuration&) = delete;

    void ViewStarted() {};

    void ViewSucceeded() {};

    void ViewTimeout() {};

    inline uint64_t Duration() const {
        return 10000000;
    }

private:
    uint64_t duration_;
};

} // namespace consensus

} // namespace shardora
    
