#pragma once

#include <deque>

#include "block/block_utils.h"
#include "ck/ck_client.h"
#include "common/config.h"
#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include "pools/to_txs_pools.h"
#include "protos/block.pb.h"
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace block {

class AccountManager;
class BlockManager {
public:
    BlockManager();
    ~BlockManager();
    int Init(
        std::shared_ptr<AccountManager>& account_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        const std::string& local_id);
    void NetworkNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item);
    void ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item);
    int GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        block::protobuf::Block& block_item);
    void NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void SetMaxConsensusShardingId(uint32_t sharding_id) {
        max_consensus_sharding_id_ = sharding_id;
    }

    void SetCreateToTxFunction(pools::CreateConsensusItemFunction func) {
        create_to_tx_cb_ = func;
    }

    void CreateToTx(uint8_t thread_idx);
    void OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members);
    pools::TxItemPtr GetToTx(uint32_t pool_index);
    void ToTxsTimeout(uint32_t sharding_id);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    void HandleToTxsMessage(const transport::MessagePtr& msg_ptr, bool recreate);
    void HandleAllConsensusBlocks(uint8_t thread_idx);
    void AddNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
    void AddAllAccount(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
    void HandleNormalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);

    static const uint64_t kCreateToTxPeriodMs = 10000u;

    std::shared_ptr<AccountManager> account_mgr_ = nullptr;
    common::ThreadSafeQueue<BlockToDbItemPtr>* consensus_block_queues_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<pools::ToTxsPools> to_txs_pool_ = nullptr;
    uint64_t prev_create_to_tx_ms_ = 0;
    common::BftMemberPtr to_tx_leader_ = nullptr;
    uint32_t max_consensus_sharding_id_ = 3;
    std::string local_id_;
    std::shared_ptr<ToTxsItem> to_txs_[network::kConsensusShardEndNetworkId] = { nullptr };
    pools::CreateConsensusItemFunction create_to_tx_cb_ = nullptr;
    uint32_t prev_pool_index_ = network::kRootCongressNetworkId;
    std::shared_ptr<ck::ClickHouseClient> ck_client_ = nullptr;
    transport::MessagePtr leader_to_txs_msg_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(BlockManager);
};

}  // namespace block

}  // namespace zjchain

