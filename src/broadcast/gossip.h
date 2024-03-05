#pragma once

#include "common/bloom_filter.h"
#include "common/tick.h"
#include "dht/base_dht.h"
#include "broadcast/broadcast.h"

namespace zjchain {

namespace broadcast {

class Gossip : public Broadcast {
public:
    Gossip();
    virtual ~Gossip();
    virtual void Broadcasting(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& message);

private:
    void RandomPos(uint8_t thread_idx);

    static const uint32_t kMaxRamdonPos = 32u;

    std::vector<uint32_t> pos_vec_;
    common::Tick tick_;
    uint32_t random_poses_[kMaxRamdonPos] = {0};
    uint32_t prev_pos_ = 0;

    DISALLOW_COPY_AND_ASSIGN(Gossip);
};

}  // namespace broadcast

}  // namespace zjchain
