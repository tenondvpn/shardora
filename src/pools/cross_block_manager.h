#pragma once

#include "block/block_utils.h"
#include "common/global_info.h"
#include "common/tick.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "network/network_utils.h"
#include "sync/key_value_sync.h"

namespace zjchain {

namespace pools {

class CrossBlockManager {
public:
    CrossBlockManager(
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<sync::KeyValueSync>& kv_sync)
            : db_(db), kv_sync_(kv_sync) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        tick_.CutOff(
            10000000lu,
            std::bind(&CrossBlockManager::Ticking, this, std::placeholders::_1));
    }

    ~CrossBlockManager() {}

    void UpdateMaxHeight(uint32_t shard_id, uint64_t height) {
        assert(shard_id < network::kConsensusShardEndNetworkId);
        if (cross_synced_max_heights_[shard_id] < height ||
                cross_synced_max_heights_[shard_id] == common::kInvalidUint64) {
            cross_synced_max_heights_[shard_id] = height;
        }
    }

private:
    void Ticking(uint8_t thread_idx) {
        CheckCrossSharding(thread_idx);
        tick_.CutOff(
            10000000lu,
            std::bind(&CrossBlockManager::Ticking, this, std::placeholders::_1));
    }

    void CheckCrossSharding(uint8_t thread_idx) {
        auto local_sharding_id = common::GlobalInfo::Instance()->network_id();
        if (local_sharding_id == common::kInvalidUint32) {
            return;
        }

        if (local_sharding_id >= network::kConsensusShardEndNetworkId) {
            local_sharding_id -= network::kConsensusWaitingShardOffset;
        }

        db::DbWriteBatch wbatch;
        if (local_sharding_id == network::kRootCongressNetworkId) {
            for (uint32_t i = network::kRootCongressNetworkId; i <= max_sharding_id_; ++i) {
                CheckCross(thread_idx, local_sharding_id, i, wbatch);
            }
        } else {
            CheckCross(thread_idx, local_sharding_id, network::kRootCongressNetworkId, wbatch);
        }

        auto st = db_->Put(wbatch);
        ZJC_DEBUG("put 2");
        if (!st.ok()) {
            ZJC_FATAL("flush to db failed!");
        }
    }

    void CheckCross(
            uint8_t thread_idx,
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
                prev_checked_height < cross_synced_max_heights_[sharding_id]) {
            return;
        }

        auto check_height = prev_checked_height;
        while (true) {
            block::protobuf::Block block;
            if (!prefix_db_->GetBlockWithHeight(
                    sharding_id,
                    common::kRootChainPoolIndex,
                    check_height,
                    &block)) {
                ZJC_DEBUG("failed get block net: %u, pool: %u, height: %lu",
                    sharding_id, common::kRootChainPoolIndex, check_height);
                break;
            }

            bool height_valid = true;
            for (int32_t tx_idx = 0; tx_idx < block.tx_list_size(); ++tx_idx) {
                if (sharding_id == network::kRootCongressNetworkId) {
                    if (block.tx_list(tx_idx).step() != pools::protobuf::kCross) {
                        continue;
                    }
                } else {
                    if (block.tx_list(tx_idx).step() != pools::protobuf::kStatistic) {
                        continue;
                    }
                }

                ZJC_DEBUG("handle cross tx.");
                auto& block_tx = block.tx_list(tx_idx);
                for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                    const pools::protobuf::CrossShardStatistic* cross_statistic = nullptr;
                    pools::protobuf::CrossShardStatistic tmp_cross_statistic;
                    pools::protobuf::ElectStatistic statistic;
                    if (sharding_id == network::kRootCongressNetworkId) {
                        if (block_tx.storages(i).key() != protos::kShardCross) {
                            continue;
                        }

                        std::string cross_val;
                        if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &cross_val)) {
                            assert(false);
                            break;
                        }

                        if (!tmp_cross_statistic.ParseFromString(cross_val)) {
                            assert(false);
                            break;
                        }

                        cross_statistic = &tmp_cross_statistic;
                    } else {
                        if (block_tx.storages(i).key() != protos::kShardStatistic) {
                            continue;
                        }

                        std::string val;
                        if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                            assert(false);
                            break;
                        }

                        if (!statistic.ParseFromString(val)) {
                            assert(false);
                            break;
                        }

                        cross_statistic = &statistic.cross();
                    }

                    if (cross_statistic == nullptr) {
                        continue;
                    }

                    for (int32_t cross_idx = 0; cross_idx < cross_statistic->crosses_size(); ++cross_idx) {
                        auto& cross = cross_statistic->crosses(cross_idx);
                        ZJC_DEBUG("cross shard block src net: %u, src pool: %u, height: %lu,"
                            "des net: %u, local_sharding_id: %u",
                            cross.src_shard(),
                            cross.src_pool(),
                            cross.height(),
                            cross.des_shard(),
                            local_sharding_id);
                        if (cross.des_shard() != local_sharding_id &&
                                cross.des_shard() != network::kNodeNetworkId &&
                                cross.des_shard() + network::kConsensusWaitingShardOffset !=
                                local_sharding_id) {
                            continue;
                        }

                        if (!prefix_db_->BlockExists(
                                cross.src_shard(),
                                cross.src_pool(),
                                cross.height())) {
                            kv_sync_->AddSyncHeight(
                                thread_idx,
                                cross.src_shard(),
                                cross.src_pool(),
                                cross.height(),
                                sync::kSyncPriLow);
                            height_valid = false;
                        }
                    }
                }
            }

            if (!height_valid) {
                break;
            }

            cross_synced_max_heights_[sharding_id] = check_height;
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
    uint32_t max_sharding_id_ = 3;
    common::Tick tick_;
    volatile uint64_t cross_synced_max_heights_[network::kConsensusShardEndNetworkId] = { common::kInvalidUint64 };
    volatile uint64_t cross_checked_max_heights_[network::kConsensusShardEndNetworkId] = { common::kInvalidUint64 };
    bool inited_heights_ = false;

    DISALLOW_COPY_AND_ASSIGN(CrossBlockManager);
};

}  // namespace pools

}  // namespace zjchain

