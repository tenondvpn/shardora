#pragma once

#include <memory>

#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace elect {

class ElectBlockManager {
public:
    ElectBlockManager() {
        for (uint32_t i = 0; i < network::kConsensusShardEndNetworkId; ++i) {
            latest_elect_blocks_[i] = nullptr;
        }
    }

    ~ElectBlockManager() {}

    void Init(std::shared_ptr<db::Db>& db) {
        db_ = db;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        for (uint32_t i = network::kRootCongressNetworkId; i < network::kConsensusShardEndNetworkId; ++i) {
            elect::protobuf::ElectBlock block;
            if (!prefix_db_->GetLatestElectBlock(i, &block)) {
                break;
            }

            AddElectBlockToCache(block);
        }
    }

    void OnNewElectBlock(
            uint64_t height,
            const elect::protobuf::ElectBlock& block,
            db::DbWriteBatch& db_batch) {
        if (block.shard_network_id() >= network::kConsensusShardEndNetworkId) {
            return;
        }

        if (latest_elect_blocks_[block.shard_network_id()] != nullptr) {
            if (height <= latest_elect_blocks_[block.shard_network_id()]->elect_height()) {
                return;
            }
        }
        
        auto* w_block = const_cast<elect::protobuf::ElectBlock*>(&block);
        w_block->set_elect_height(height);
        AddElectBlockToCache(block);
        prefix_db_->SaveLatestElectBlock(block, db_batch);
    }

    std::shared_ptr<elect::protobuf::ElectBlock> GetLatestElectBlock(uint32_t sharding_id) {
        return latest_elect_blocks_[sharding_id];
    }

private:
    void AddElectBlockToCache(const elect::protobuf::ElectBlock& block) {
        auto elect_block = std::make_shared<elect::protobuf::ElectBlock>(block);
        latest_elect_blocks_[elect_block->shard_network_id()] = elect_block;
    }

    std::shared_ptr<elect::protobuf::ElectBlock> latest_elect_blocks_[network::kConsensusShardEndNetworkId];
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ElectBlockManager);
};

};  // namespace elect

};  // namespace zjchain
