#pragma once

#include <unordered_map>

#include "common/node_members.h"
#include "common/unique_map.h"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"
#include "pools/tx_utils.h"

namespace zjchain {

namespace pools {

class TxPoolManager;
class ToTxsPools {
public:
    ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    ~ToTxsPools();
    void NewBlock(const std::shared_ptr<block::protobuf::Block>& block, db::DbWriteBatch& db_batch);
    int CreateToTxWithHeights(
        uint32_t sharding_id,
        uint64_t elect_height,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        std::string* to_hash);
    int LeaderCreateToHeights(pools::protobuf::ShardToTxItem& to_heights);
    bool StatisticTos(const pools::protobuf::ShardToTxItem& to_heights);

private:
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(
        const std::string& addr);
    void HandleNormalToTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx_info);
    void LoadLatestHeights();
    void HandleNormalFrom(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleCreateContractUserCall(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleContractGasPrepayment(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleRootCreateAddress(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleContractExecute(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleJoinElect(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void AddTxToMap(
        const block::protobuf::Block& block,
        const std::string& to,
        pools::protobuf::StepType type,
        uint64_t amount,
        uint32_t sharding_id,
        int32_t pool_index,
        const std::string& key);
    void HandleElectJoinVerifyVec(
        const std::string& verify_hash,
        std::vector<bls::protobuf::JoinElectInfo>& verify_reqs);
    bool PreStatisticTos(uint32_t pool_idx, uint64_t min_height, uint64_t max_height);
    void RemoveCacheBlock(uint32_t pool, uint64_t height) {
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
        std::string elect_join_g2_key;
        std::vector<bls::protobuf::JoinElectInfo> verify_reqs;
    };

    // destination shard -> pool -> height -> items
    typedef std::unordered_map<std::string, ToAddressItemInfo> TxMap;
    typedef std::map<uint64_t, TxMap> HeightMap;  // order by height
    typedef std::unordered_map <uint32_t, HeightMap> PoolMap;
    PoolMap network_txs_pools_;
    common::UniqueMap<std::string, protos::AddressInfoPtr, 10240, 16> address_map_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<pools::protobuf::ShardToTxItem>> handled_map_;
    std::string local_id_;
    uint64_t pool_consensus_heihgts_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::unordered_map<uint64_t, std::shared_ptr<block::protobuf::Block>> added_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<pools::protobuf::ShardToTxItem> prev_to_heights_ = nullptr;
    uint64_t has_statistic_height_[common::kInvalidPoolIndex] = { 1 };

    DISALLOW_COPY_AND_ASSIGN(ToTxsPools);
};

};  // namespace pools

};  // namespace zjchain
