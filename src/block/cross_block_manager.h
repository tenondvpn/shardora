#pragma once

#include "block/block_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "network/network_utils.h"
#include "sync/key_value_sync.h"

namespace zjchain {

namespace block {

class CrossBlockManager {
public:
    CrossBlockManager(
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<sync::KeyValueSync>& kv_sync)
            : db_(db), kv_sync_(kv_sync) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }

    ~CrossBlockManager() {}

private:
    void CheckCrossSharding(uint8_t thread_idx) {
        local_sharding_id_ = common::GlobalInfo::Instance()->network_id();
        if (local_sharding_id_ == common::kInvalidUint32) {
            return;
        }

        if (local_sharding_id_ >= network::kConsensusShardEndNetworkId) {
            local_sharding_id_ -= network::kConsensusWaitingShardOffset;
        }

        db::DbWriteBatch wbatch;
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
                common::GlobalInfo::Instance()->network_id() ==
                network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset) {
            for (uint32_t i = network::kConsensusShardBeginNetworkId; i <= max_sharding_id_; ++i) {
                CheckCross(thread_idx, i, wbatch);
            }
        } else {
            CheckCross(thread_idx, network::kRootCongressNetworkId, wbatch);
        }

        auto st = db_->Put(wbatch);
        if (!st.ok()) {
            ZJC_FATAL("flush to db failed!");
        }
    }

    void CheckCross(uint8_t thread_idx, uint32_t sharding_id, db::DbWriteBatch& wbatch) {
        uint64_t prev_checked_height = 0;
        prefix_db_->GetCheckCrossHeight(local_sharding_id_, sharding_id, &prev_checked_height);
        auto check_height = prev_checked_height;
        while (true) {
            block::protobuf::Block block;
            if (!prefix_db_->GetBlockWithHeight(
                    sharding_id,
                    common::kRootChainPoolIndex,
                    check_height,
                    &block)) {
                break;
            }

            bool height_valid = true;
            for (int32_t tx_idx = 0; tx_idx < block.tx_list_size(); ++tx_idx) {
                if (block.tx_list(tx_idx).step() != pools::protobuf::kCross) {
                    continue;
                }

                auto& block_tx = block.tx_list(tx_idx);
                for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                    if (block_tx.storages(i).key() != protos::kShardCross) {
                        continue;
                    }

                    if (cross_statistic_tx_ != nullptr) {
                        if (block_tx.storages(i).val_hash() == cross_statistic_tx_->tx_hash) {
                            cross_statistic_tx_ = nullptr;
                        }
                    }

                    std::string cross_val;
                    if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &cross_val)) {
                        assert(false);
                        break;
                    }

                    pools::protobuf::CrossShardStatistic cross_statistic;
                    if (!cross_statistic.ParseFromString(cross_val)) {
                        assert(false);
                        break;
                    }

                    for (int32_t cross_idx = 0; cross_idx < cross_statistic.crosses_size(); ++cross_idx) {
                        auto& cross = cross_statistic.crosses(cross_idx);
                        if (cross.des_shard() != common::GlobalInfo::Instance()->network_id() &&
                                cross.des_shard() != network::kNodeNetworkId &&
                                cross.des_shard() !=
                                common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset) {
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

            ++check_height;
        }

        if (check_height != prev_checked_height) {
            prefix_db_->SaveCheckCrossHeight(local_sharding_id_, sharding_id, check_height, wbatch);
        }
    }

    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t max_sharding_id_ = 3;
    uint32_t local_sharding_id_ = 0;

    DISALLOW_COPY_AND_ASSIGN(CrossBlockManager);
};

}  // namespace block

}  // namespace zjchain

