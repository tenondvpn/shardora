#pragma once

#include "block/block_utils.h"

namespace zjchain {

namespace block {

class CrossBlockManager {
public:
    CrossBlockManager();
    ~CrossBlockManager();
    void HandleNewCrossStatistic(const pools::protobuf::CrossShardStatistic& cross_statistic);

private:


    DISALLOW_COPY_AND_ASSIGN(CrossBlockManager);
};

}  // namespace block

}  // namespace zjchain

