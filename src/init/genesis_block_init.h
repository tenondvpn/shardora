#pragma once

#include <map>
#include <vector>

#include <dkg/dkg.h>

#include "common/bitmap.h"
#include "common/utils.h"
#include "db/db.h"
#include "dht/dht_utils.h"
#include "init/init_utils.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h"
#include "protos/init.pb.h"

namespace zjchain {

namespace block {
    class AccountManager;
    class BlockManager;
}

namespace pools {
    class TxPoolManager;
}

namespace init {

class GenesisBlockInit {
public:
    GenesisBlockInit(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<db::Db>& db);
    ~GenesisBlockInit();
    int CreateGenesisBlocks(
        uint32_t net_id,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes);

private:
    int CreateRootGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes);
    int CreateShardGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id);
    int CreateShardNodesBlocks(
        std::unordered_map<uint32_t, std::string>& pool_prev_hash_map,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        pools::protobuf::ToTxHeights& init_heights);
    void InitGenesisAccount();
    void GenerateRootAccounts();
    int GenerateRootSingleBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        FILE* root_gens_init_block_file,
        uint64_t* root_pool_height);
    int GenerateShardSingleBlock(uint32_t sharding_id);
    int CreateElectBlock(
        uint32_t shard_netid,
        std::string& root_pre_hash,
        uint64_t height,
        uint64_t prev_height,
        FILE* root_gens_init_block_file,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes);
    std::string GetValidPoolBaseAddr(uint32_t pool_index);
    void CreateDefaultAccount();
    void AddBlockItemToCache(
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch);
    bool CheckRecomputeG2s(
        uint32_t local_member_index,
        uint32_t member_count,
        const std::string& id,
        bls::protobuf::JoinElectBlsInfo& verfy_final_vals);
    bool CreateNodePrivateInfo(
        uint32_t shard_id,
        uint64_t elect_height,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes);
    bool BlsAggSignBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        std::shared_ptr<block::protobuf::Block>& block);

    std::map<uint32_t, std::string> pool_index_map_;
    std::map<uint32_t, std::string> root_account_with_pool_index_map_;
    common::Bitmap root_bitmap_{ common::kEachShardMaxNodeCount };
    common::Bitmap shard_bitmap_{ common::kEachShardMaxNodeCount };
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    libff::alt_bn128_G2 common_pk_[16] = { libff::alt_bn128_G2::zero() };

    DISALLOW_COPY_AND_ASSIGN(GenesisBlockInit);
};

};  // namespace init

};  // namespace zjchain
