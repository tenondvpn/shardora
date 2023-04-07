#pragma once

#include <unordered_map>

#include "common/node_members.h"
#include "common/unique_map.h"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"
#include "pools/tx_utils.h"

namespace zjchain {

namespace pools {

class ToTxsPools {
public:
    ToTxsPools(std::shared_ptr<db::Db>& db, const std::string& local_id, uint32_t max_sharding_id);
    ~ToTxsPools();
    void NewBlock(const block::protobuf::Block& block, db::DbWriteBatch& db_batch);
    int CreateToTxWithHeights(
        uint32_t sharding_id,
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* to_hash);
    int LeaderCreateToHeights(uint32_t sharding_id, pools::protobuf::ToTxHeights& to_heights);

private:
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(
        const std::string& addr);
    void HandleNormalToTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx_info,
        db::DbWriteBatch& db_batch);
    void LoadLatestHeights();
    void HandleNormalFrom(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateContractUserCall(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleRootCreateAddress(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void AddTxToMap(
        const block::protobuf::Block& block,
        const std::string& to,
        pools::protobuf::StepType type,
        uint64_t amount,
        uint64_t des_sharding_id,
        int32_t pool_index);

    struct ToAddressItemInfo {
        uint64_t amount;
        int32_t pool_index;
        pools::protobuf::StepType type;
    };

    // destination shard -> pool -> height -> items
    typedef std::unordered_map<std::string, ToAddressItemInfo> TxMap;
    typedef std::map<uint64_t, TxMap> HeightMap;  // order by height
    typedef std::unordered_map <uint32_t, HeightMap> PoolMap;
    typedef std::unordered_map <uint32_t, PoolMap> ShardingMap;
    ShardingMap network_txs_pools_;
    common::UniqueMap<std::string, protos::AddressInfoPtr, 10240, 16> address_map_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<pools::protobuf::ToTxHeights>> handled_map_;
    std::string local_id_;
    uint64_t pool_consensus_heihgts_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(ToTxsPools);
};

};  // namespace pools

};  // namespace zjchain
