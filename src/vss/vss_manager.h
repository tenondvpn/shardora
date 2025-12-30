#pragma once

#include <mutex>
#include <atomic>

#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/utils.h"
#include "protos/vss.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"
#include "vss/random_num.h"

namespace shardora {

namespace vss {

class VssManager {
public:
    VssManager();
    ~VssManager() {}
    void OnTimeBlock(const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block);

    uint64_t EpochRandom() {
        return epoch_random_;
    }

    uint64_t GetConsensusFinalRandom() {
        return epoch_random_;
    }

private:
    std::atomic<uint64_t> epoch_random_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(VssManager);
};

}  // namespace vss

}  // namespace shardora
