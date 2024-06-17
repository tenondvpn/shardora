#include "pools/root_cross_pool.h"
#include <cassert>

#include "common/encode.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace pools {
    
RootCrossPool::RootCrossPool() {
    des_sharding_id_ = network::kRootCongressNetworkId;
}

RootCrossPool::~RootCrossPool() {}

void RootCrossPool::Init(
        uint32_t des_pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    pool_index_ = des_pool_idx;
    CrossPool::Init(des_sharding_id_, db, kv_sync);
}

}  // namespace pools

}  // namespace shardora
