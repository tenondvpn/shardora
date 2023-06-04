#include "pools/cross_pool.h"
#include <cassert>

#include "common/encode.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"

namespace zjchain {

namespace pools {
    
CrossPool::CrossPool() {}

CrossPool::~CrossPool() {}

void CrossPool::Init(
        uint32_t des_sharding_id,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    des_sharding_id_ = des_sharding_id;
    kv_sync_ = kv_sync;
    pool_index_ = common::kRootChainPoolIndex;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    InitLatestInfo();
    InitHeightTree();
}

void CrossPool::InitHeightTree() {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    height_tree_ptr_ = std::make_shared<HeightTreeLevel>(
        des_sharding_id_,
        pool_index_,
        latest_height_,
        db_);
    height_tree_ptr_->Set(0);
    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
            break;
        }
    }
}

uint32_t CrossPool::SyncMissingBlocks(uint8_t thread_idx, uint64_t now_tm_ms) {
    if (height_tree_ptr_ == nullptr) {
        return 0;
    }

    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        kv_sync_->AddSyncHeight(
            thread_idx,
            des_sharding_id_,
            pool_index_,
            0,
            sync::kSyncHigh);
        ZJC_DEBUG("des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
            "invaid heights size: %u, height: %lu",
            des_sharding_id_, pool_index_, latest_height_, 1, 0);
        return 1;
    }

    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    if (invalid_heights.size() > 0) {
        for (uint32_t i = 0; i < invalid_heights.size(); ++i) {
            if (prefix_db_->BlockExists(des_sharding_id_, pool_index_, invalid_heights[i])) {
                height_tree_ptr_->Set(invalid_heights[i]);
                ZJC_DEBUG("exists des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
                    "invaid heights size: %u, height: %lu",
                    des_sharding_id_, pool_index_, latest_height_,
                    invalid_heights.size(), invalid_heights[i]);
                continue;
            }

            ZJC_DEBUG("des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
                "invaid heights size: %u, height: %lu",
                des_sharding_id_, pool_index_, latest_height_,
                invalid_heights.size(), invalid_heights[i]);
            kv_sync_->AddSyncHeight(
                thread_idx,
                des_sharding_id_,
                pool_index_,
                invalid_heights[i],
                sync::kSyncHigh);
        }
    }

    return invalid_heights.size();
}

}  // namespace pools

}  // namespace zjchain
