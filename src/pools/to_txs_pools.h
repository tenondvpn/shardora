#pragma once

#include <unordered_map>

#include "common/node_members.h"
#include "common/unique_map.h"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"
#include "protos/view_block.pb.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace block {
    class AccountManager;
};

namespace pools {

class TxPoolManager;
class ToTxsPools {
public:
    ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::AccountManager>& acc_mgr);
    ~ToTxsPools();
    void NewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr, 
        db::DbWriteBatch& db_batch);
    int CreateToTxWithHeights(
        uint32_t sharding_id,
        uint64_t elect_height,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        pools::protobuf::ToTxMessage& to_tx);
    int LeaderCreateToHeights(pools::protobuf::ShardToTxItem& to_heights);
    bool StatisticTos(const pools::protobuf::ShardToTxItem& to_heights);

private:
    void HandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx_info);
    void LoadLatestHeights();
    void HandleNormalFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleCreateContractUserCall(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleCreateContractByRootFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleContractGasPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleRootCreateAddress(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleContractExecute(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleJoinElect(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void AddTxToMap(
        const view_block::protobuf::ViewBlockItem& view_block,
        const std::string& to,
        pools::protobuf::StepType type,
        uint64_t amount,
        uint32_t sharding_id,
        int32_t pool_index,
        const std::string& key,
        const std::string& library_bytes,
        const std::string& from,
        uint64_t prepayment);
    void HandleElectJoinVerifyVec(
        const std::string& verify_hash,
        std::vector<bls::protobuf::JoinElectInfo>& verify_reqs);
    bool PreStatisticTos(
        uint32_t pool_idx, 
        uint64_t min_height, 
        uint64_t max_height);
    void HandleCrossShard(
        bool is_root,
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        std::unordered_map<uint32_t, std::unordered_set<CrossItem, CrossItemRecordHash>>& cross_map);

    void RemoveCacheBlock(uint32_t pool_idx, uint64_t height) {
        auto iter = added_heights_[pool_idx].find(height);
        if (iter != added_heights_[pool_idx].end()) {
            added_heights_[pool_idx].erase(iter);
        }
    }

    struct ToAddressItemInfo {
        uint64_t amount;
        int32_t pool_index;
        uint32_t sharding_id;
        pools::protobuf::StepType type;
        int32_t src_step;
        std::string elect_join_g2_value;
        std::vector<bls::protobuf::JoinElectInfo> verify_reqs;
         // for kContractCreate
        std::string library_bytes;
        std::string from;
		uint64_t prepayment;
    };

    // destination shard -> pool -> height -> items
    typedef std::unordered_map<std::string, ToAddressItemInfo> TxMap;
    typedef std::map<uint64_t, TxMap> HeightMap;  // order by height
    typedef std::unordered_map <uint32_t, HeightMap> PoolMap;
    PoolMap network_txs_pools_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<pools::protobuf::ShardToTxItem>> handled_map_;
    std::string local_id_;
    uint64_t pool_consensus_heihgts_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::unordered_map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>> added_heights_[common::kInvalidPoolIndex];
    std::unordered_set<uint64_t> valided_heights_[common::kInvalidPoolIndex];
    uint64_t erased_max_heights_[common::kInvalidPoolIndex] = { 0llu };
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<pools::protobuf::ShardToTxItem> prev_to_heights_ = nullptr;
    uint64_t has_statistic_height_[common::kInvalidPoolIndex] = { 1 };
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, std::unordered_set<CrossItem, CrossItemRecordHash>>> cross_sharding_map_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(ToTxsPools);
};

};  // namespace pools

};  // namespace shardora
