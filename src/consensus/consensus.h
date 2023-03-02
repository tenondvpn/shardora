#pragma once

#include "consensus/consensus_utils.h"
#include "elect/elect_manager.h"
#include "pools/tx_pool_manager.h"

namespace zjchain {

namespace consensus {

class Consensus {
public:
    virtual int OnNewElectBlock(uint32_t sharding_id) = 0;
    virtual int Start(uint8_t thread_index) = 0;

protected:
    Consensus() {}
    virtual ~Consensus() {}
};

};  // namespace consensus

};  // namespace zjchain
