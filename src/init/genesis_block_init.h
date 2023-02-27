#pragma once

#include <map>
#include <vector>

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <dkg/dkg.h>

#include "block/block_manager.h"
#include "common/bitmap.h"
#include "common/utils.h"
#include "common/user_property_key_define.h"
#include "dht/dht_utils.h"

namespace zjchain {

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
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes);

private:
    int CreateRootGenesisBlocks(
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes);
    int CreateShardGenesisBlocks(uint32_t net_id);
    void InitGenesisAccount();
    void GenerateRootAccounts();
    int GenerateRootSingleBlock(
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes);
    int GenerateShardSingleBlock();
    int CreateElectBlock(
        uint32_t shard_netid,
        std::string& root_pre_hash,
        uint64_t height,
        uint64_t prev_height,
        FILE* root_gens_init_block_file,
        const std::vector<dht::NodePtr>& genesis_nodes);
    std::string GetValidPoolBaseAddr(uint32_t network_id, uint32_t pool_index);
    int CreateBlsGenesisKeys(
        std::vector<std::shared_ptr<BLSPrivateKeyShare>>* skeys,
        std::vector<std::shared_ptr<BLSPublicKeyShare>>* pkeys,
        libff::alt_bn128_G2* common_public_key);
    void DumpLocalPrivateKey(
        uint32_t shard_netid,
        uint64_t height,
        const std::string& id,
        const std::string& prikey,
        const std::string& sec_key,
        FILE* fd);
    void ReloadBlsPri();

    std::map<uint32_t, std::string> pool_index_map_;
    std::map<uint32_t, std::string> root_account_with_pool_index_map_;
    common::Bitmap root_bitmap_{ common::kEachShardMaxNodeCount };
    common::Bitmap shard_bitmap_{ common::kEachShardMaxNodeCount };
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(GenesisBlockInit);
};

};  // namespace init

};  // namespace zjchain
