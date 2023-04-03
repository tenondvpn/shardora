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
        uint64_t block_height,
        const block::protobuf::BlockTx& tx_info,
        db::DbWriteBatch& db_batch);
    void LoadLatestHeights();
    void HandleNormalFrom(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleRootCreateAddress(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void AddTxToMap(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        uint64_t des_sharding_id,
        int32_t pool_index);

    // destination shard -> pool -> height -> items
    typedef std::unordered_map<std::string, std::pair<uint64_t, int32_t>> TxMap;
    typedef std::map<uint64_t, TxMap> HeightMap;  // order by height
    typedef std::unordered_map <uint32_t, HeightMap> PoolMap;
    typedef std::unordered_map <uint32_t, PoolMap> ShardingMap;
    ShardingMap network_txs_pools_;
    common::UniqueMap<std::string, protos::AddressInfoPtr> address_map_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<pools::protobuf::ToTxHeights>> handled_map_;
    std::string local_id_;

    DISALLOW_COPY_AND_ASSIGN(ToTxsPools);
};

};  // namespace pools

};  // namespace zjchain
