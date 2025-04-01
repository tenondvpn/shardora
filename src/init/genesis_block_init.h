#pragma once

#include <consensus/hotstuff/types.h>
#include <map>
#include <unordered_map>
#include <vector>

#include <dkg/dkg.h>
#include <yaml-cpp/node/node.h>
#include <json/json.hpp>

#include "common/bitmap.h"
#include "common/utils.h"
#include "db/db.h"
#include "dht/dht_utils.h"
#include "init/init_utils.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h"
#include "protos/init.pb.h"

namespace shardora {

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
        const GenisisNetworkType& net_type,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
        const std::set<uint32_t>& valid_net_ids_set);
    inline void SetGenesisConfig(const YAML::Node& genesis_config) {
        genesis_config_ = genesis_config;
    }

private:
    std::unordered_map<std::string, uint64_t> GetGenesisAccountBalanceMap(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards);
    int CreateRootGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map);
    int CreateShardGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map);
    void PrepareCreateGenesisBlocks(uint32_t shard_node_net_id);
    void ComputeG2sForNodes(const std::vector<std::string>& prikeys);
    int CreateShardNodesBlocks(
        std::unordered_map<uint32_t, std::string>& pool_prev_hash_map,
        std::unordered_map<uint32_t, hotstuff::HashStr> pool_prev_vb_hash_map,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        pools::protobuf::StatisticTxItem& init_heights,
        hotstuff::View* pool_latest_view,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map); // 节点对应的余额
    uint32_t GetNetworkIdOfGenesisAddress(const std::string& address);
    const std::map<uint32_t, std::string> GetGenesisAccount(uint32_t net_id);
    void InitShardGenesisAccount();
    // void GenerateRootAccounts();
    int GenerateRootSingleBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        FILE* root_gens_init_block_file,
        uint64_t* root_pool_height,
        hotstuff::View* root_pool_view);
    int GenerateShardSingleBlock(uint32_t sharding_id);
    int CreateElectBlock(
        uint32_t shard_netid,
        std::string& root_pre_hash,
        std::string& root_pre_vb_hash,
        uint64_t height,
        uint64_t prev_height,
        hotstuff::View view,
        FILE* root_gens_init_block_file,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes);
    void CreateDefaultAccount();
    void AddBlockItemToCache(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        db::DbWriteBatch& db_batch);
    bool CreateNodePrivateInfo(
        uint32_t shard_id,
        uint64_t elect_height,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes);
    bool BlsAggSignViewBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        const view_block::protobuf::QcItem& commit_qc,
        std::shared_ptr<libff::alt_bn128_G1>& agg_sign);
    std::shared_ptr<hotstuff::QC> CreateCommitQC(
            const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
            const std::shared_ptr<hotstuff::ViewBlock>& vblock);
    void SetPrevElectInfo(
        const elect::protobuf::ElectBlock& elect_block,
        block::protobuf::BlockTx& block_tx);
    void SaveGenisisPoolHeights(uint32_t shard_id);
    int CreateAllQc(
        uint32_t  network_id,
        uint32_t  pool_index,
        uint64_t view,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes, 
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr);
    void CreatePoolsAddressInfo(uint16_t network_id);
    void PrintGenisisAccounts();

    std::map<uint32_t, std::map<uint32_t, std::map<std::string, uint64_t>>> net_pool_index_map_; // net => (pool => addr)
    uint32_t net_pool_index_map_addr_count_ = 0;
    std::map<uint32_t, std::string> root_account_with_pool_index_map_;
    common::Bitmap root_bitmap_{ common::kEachShardMaxNodeCount };
    common::Bitmap shard_bitmap_{ common::kEachShardMaxNodeCount };
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    libff::alt_bn128_G2 common_pk_[16] = { libff::alt_bn128_G2::zero() };
    YAML::Node genesis_config_;
    nlohmann::json bls_pk_json_;
    std::shared_ptr<address::protobuf::AddressInfo> immutable_pool_address_info_;
    std::shared_ptr<address::protobuf::AddressInfo> pool_address_info_[common::kImmutablePoolSize];
    
    DISALLOW_COPY_AND_ASSIGN(GenesisBlockInit);
};

bool CheckRecomputeG2s(uint32_t local_member_index, uint32_t valid_t,
                       const std::string &id,
                       const std::shared_ptr<protos::PrefixDb> &prefix_db,
                       bls::protobuf::JoinElectBlsInfo &verfy_final_vals);
void ComputeG2ForNode(const std::string &prikey, uint32_t k,
                      const std::shared_ptr<protos::PrefixDb> &prefix_db,
                      const std::vector<std::string> &prikeys);

};  // namespace init

};  // namespace shardora
