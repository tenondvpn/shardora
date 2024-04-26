#include <common/global_info.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_wrapper.h>

namespace shardora {
namespace hotstuff {

BlockWrapper::BlockWrapper(
        const uint32_t pool_idx,
        const std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        const std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<ElectInfo>& elect_info) :
    pool_idx_(pool_idx), pools_mgr_(pools_mgr), tm_block_mgr_(tm_block_mgr),
    block_mgr_(block_mgr), elect_info_(elect_info) {
    txs_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr, block_mgr, tm_block_mgr);
}

BlockWrapper::~BlockWrapper(){};

// 打包一个新的 block 和 txs
Status BlockWrapper::Wrap(
        const std::shared_ptr<block::protobuf::Block>& prev_block,
        const uint32_t& leader_idx,
        std::shared_ptr<block::protobuf::Block>& block,
        std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) {
    if (!prev_block) {
        return Status::kInvalidArgument;
    }
    block->set_pool_index(pool_idx_);
    block->set_prehash(prev_block->hash());
    block->set_version(common::kTransactionVersion);
    block->set_network_id(common::GlobalInfo::Instance()->network_id());
    block->set_consistency_random(0);
    block->set_height(prev_block->height()+1);
    if (block->height() <= 0) {
        return Status::kInvalidArgument;
    }

    uint64_t cur_time = common::TimeUtils::TimestampMs();
    block->set_timestamp(prev_block->timestamp() > cur_time ? prev_block->timestamp() : cur_time);

    // 打包交易
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    Status s = GetTxs(txs_ptr);
    if (s != Status::kSuccess) {
        return s;
    }
    
    for (auto it = txs_ptr->txs.begin(); it != txs_ptr->txs.end(); it++) {
        auto* tx_info = tx_propose->add_txs();
        *tx_info = it->second->tx_info;
    }
    tx_propose->set_tx_type(txs_ptr->tx_type);
    
    if (txs_ptr->tx_type != pools::protobuf::kNormalFrom) {
        block->set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    }

    auto elect_item = elect_info_->GetElectItem();
    if (!elect_item) {
        return Status::kError;
    }
    block->set_electblock_height(elect_item->ElectHeight());
    block->set_leader_index(leader_idx);
    
    return Status::kSuccess;
}
        
}
}
