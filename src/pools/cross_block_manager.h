#pragma once

#include "block/block_utils.h"
#include "common/global_info.h"
#include "common/tick.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "network/network_utils.h"
#include "sync/key_value_sync.h"

namespace shardora {

namespace pools {

class CrossBlockManager {
public:
    CrossBlockManager(
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<sync::KeyValueSync>& kv_sync)
            : db_(db), kv_sync_(kv_sync) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        cross_tick_.CutOff(
            10000000lu,
            std::bind(&CrossBlockManager::Ticking, this));
    }

    ~CrossBlockManager() {}

    void UpdateMaxShardingId(uint32_t shard_id) {
        if (max_sharding_id_ < shard_id) {
            max_sharding_id_ = shard_id;
        }
    }

    void UpdateMaxHeight(uint32_t shard_id, uint64_t height) {
        if (height == common::kInvalidUint64) {
            return;
        }

        if (shard_id >= network::kConsensusShardEndNetworkId) {
            return;
        }
        
        if (cross_synced_max_heights_[shard_id] < height ||
                cross_synced_max_heights_[shard_id] == common::kInvalidUint64) {
            cross_synced_max_heights_[shard_id] = height;
            ZJC_DEBUG("success update cross synced max height net: %u, height: %lu", shard_id, height);
        }
    }

private: 
    void Ticking() {
        auto now_tm_ms = common::TimeUtils::TimestampMs();
        CheckCrossSharding();
        auto etime = common::TimeUtils::TimestampMs();
        if (etime - now_tm_ms >= 10) {
            ZJC_DEBUG("CrossBlockManager handle message use time: %lu", (etime - now_tm_ms));
        }

        cross_tick_.CutOff(
            10000000lu,
            std::bind(&CrossBlockManager::Ticking, this));
    }

    void CheckCrossSharding() {
        auto local_sharding_id = common::GlobalInfo::Instance()->network_id();
        if (local_sharding_id == common::kInvalidUint32) {
            return;
        }

        if (local_sharding_id >= network::kConsensusShardEndNetworkId) {
            local_sharding_id -= network::kConsensusWaitingShardOffset;
        }

        db::DbWriteBatch wbatch;
        if (local_sharding_id == network::kRootCongressNetworkId) {
            for (uint32_t i = network::kConsensusShardBeginNetworkId; i <= max_sharding_id_; ++i) {
                CheckCross(local_sharding_id, i, wbatch);
            }
        } else {
            CheckCross(local_sharding_id, network::kRootCongressNetworkId, wbatch);
        }

        auto st = db_->Put(wbatch);
        if (!st.ok()) {
            ZJC_FATAL("flush to db failed!");
        }
    }

    void CheckCross(
            uint32_t local_sharding_id,
            uint32_t sharding_id,
            db::DbWriteBatch& wbatch) {
        uint64_t prev_checked_height = cross_checked_max_heights_[sharding_id];
        if (prev_checked_height == common::kInvalidUint64) {
            if (prefix_db_->GetCheckCrossHeight(local_sharding_id, sharding_id, &prev_checked_height)) {
                cross_checked_max_heights_[sharding_id] = prev_checked_height;
            }
        }

        if (cross_synced_max_heights_[sharding_id] != common::kInvalidUint64 &&
                prev_checked_height > cross_synced_max_heights_[sharding_id]) {
            ZJC_DEBUG("check failed local_sharding_id: %u, sharding_id: %u, prev_checked_height: %lu, %lu",
                local_sharding_id,
                sharding_id,
                prev_checked_height,
                cross_synced_max_heights_[sharding_id]);
            return;
        }

        auto check_height = prev_checked_height;
        while (true) {
            view_block::protobuf::ViewBlockItem view_block;
            if (!prefix_db_->GetBlockWithHeight(
                    sharding_id,
                    common::kImmutablePoolSize,
                    check_height,
                    &view_block)) {
                ZJC_DEBUG("failed get block net: %u, pool: %u, height: %lu, max height: %lu",
                    sharding_id, common::kImmutablePoolSize, check_height,
                    cross_synced_max_heights_[sharding_id]);
                if (cross_synced_max_heights_[sharding_id] != common::kInvalidUint64) {
                    uint32_t count = 0;
                    for (uint64_t h = check_height; h <= cross_synced_max_heights_[sharding_id] && ++count < 64; ++h) {
                        // TODO 目前创世块也会进入这个逻辑，导致创世块数据生成报错，临时注释
                        if (!kv_sync_) {
                            continue;
                        }

                        ZJC_DEBUG("now add sync height 1, %u_%u_%lu", 
                            sharding_id,
                            common::kImmutablePoolSize,
                            h);
                        kv_sync_->AddSyncHeight(
                                sharding_id,
                                common::kImmutablePoolSize,
                                h,
                                sync::kSyncPriLow);
                    }
                }

                break;
            }

            if (cross_synced_max_heights_[sharding_id] < check_height) {
                cross_synced_max_heights_[sharding_id] = check_height;
            }

            ++check_height;
        }

        if (check_height != prev_checked_height) {
            ZJC_DEBUG("refresh cross block height local_sharding_id: %u, sharding_id: %u, height: %lu",
                local_sharding_id, sharding_id, check_height);
            prefix_db_->SaveCheckCrossHeight(local_sharding_id, sharding_id, check_height, wbatch);
            cross_checked_max_heights_[sharding_id] = check_height;
        }
    }

    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    volatile uint32_t max_sharding_id_ = 3;
    common::Tick cross_tick_;
    volatile uint64_t cross_synced_max_heights_[network::kConsensusShardEndNetworkId] = { 1 };
    volatile uint64_t cross_checked_max_heights_[network::kConsensusShardEndNetworkId] = { 1 };
    bool inited_heights_ = false;

    DISALLOW_COPY_AND_ASSIGN(CrossBlockManager);
};

}  // namespace pools

}  // namespace shardora

