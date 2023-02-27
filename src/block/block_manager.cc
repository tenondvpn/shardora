#include "block/block_manager.h"

#include "block/block_utils.h"
#include "block/account_manager.h"
#include "block/block_proto.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "network/route.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"

namespace zjchain {

namespace block {

BlockManager::BlockManager() {
}

BlockManager::~BlockManager() {
    if (consensus_block_queues_ != nullptr) {
        delete[] consensus_block_queues_;
    }
}

int BlockManager::Init(
        std::shared_ptr<AccountManager>& account_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
    account_mgr_ = account_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    to_txs_pool_ = std::make_shared<pools::ToTxsPools>(db_);
    consensus_block_queues_ = new common::ThreadSafeQueue<BlockToDbItemPtr>[
        common::GlobalInfo::Instance()->message_handler_thread_count()];
    network::Route::Instance()->RegisterMessage(
        common::kBlockMessage,
        std::bind(&BlockManager::HandleMessage, this, std::placeholders::_1));
    bool genesis = false;
    return kBlockSuccess;
}

void BlockManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
}

void BlockManager::NetworkNewBlock(
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (block_item != nullptr) {
        db::DbWriteBach db_batch;
        AddAllAccount(block_item, db_batch);
        AddNewBlock(block_item, db_batch);
    }

    HandleAllConsensusBlocks();
}

void BlockManager::ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item) {
    consensus_block_queues_[thread_idx].push(block_item);
}

void BlockManager::HandleAllConsensusBlocks() {
    auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count();
    for (int32_t i = 0; i < thread_count; ++i) {
        while (consensus_block_queues_[i].size() > 0) {
            BlockToDbItemPtr db_item_ptr = nullptr;
            if (consensus_block_queues_[i].pop(&db_item_ptr)) {
                AddNewBlock(db_item_ptr->block_ptr, db_item_ptr->db_batch);
            }
        }
    }
}

void BlockManager::AddAllAccount(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBach& db_batch) {
    const auto& tx_list = block_item->tx_list();
    if (tx_list.empty()) {
        return;
    }

    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto account_info = account_mgr_->GetAcountInfo(block_item, tx_list[i]);
        prefix_db_->AddAddressInfo(account_info->addr(), *account_info, db_batch);
    }
}

void BlockManager::AddNewBlock(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBach& db_batch) {
    prefix_db_->SaveBlock(*block_item, db_batch);
    to_txs_pool_->NewBlock(*block_item, db_batch);
    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        ZJC_FATAL("write block to db failed!");
    }
}

int BlockManager::GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        block::protobuf::Block& block_item) {
    if (!prefix_db_->GetBlockWithHeight(network_id, pool_index, height, &block_item)) {
        return kBlockError;
    }

    return kBlockSuccess;
}

}  // namespace block

}  // namespace zjchain
