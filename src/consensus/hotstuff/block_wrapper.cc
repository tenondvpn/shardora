#include <common/global_info.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {
namespace hotstuff {

BlockWrapper::BlockWrapper(
        const uint32_t pool_idx,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<ElectInfo>& elect_info) :
    pool_idx_(pool_idx), pools_mgr_(pools_mgr), tm_block_mgr_(tm_block_mgr),
    block_mgr_(block_mgr), elect_info_(elect_info) {
    txs_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr, block_mgr, tm_block_mgr);
}

BlockWrapper::~BlockWrapper(){};

// 打包一个新的 block 和 txs
Status BlockWrapper::Wrap(
        const std::shared_ptr<ViewBlock>& prev_view_block,
        const uint32_t& leader_idx,
        std::shared_ptr<block::protobuf::Block>& block,
        std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose,
        const bool& no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) {
    auto& prev_block = prev_view_block->block;
    if (!prev_block) {
        return Status::kInvalidArgument;
    }
    block->set_pool_index(pool_idx_);
    block->set_prehash(prev_block->hash());
    block->set_version(common::kTransactionVersion);
    block->set_network_id(common::GlobalInfo::Instance()->network_id());
    block->set_consistency_random(0);
    block->set_height(prev_block->height()+1);
    ZJC_DEBUG("propose block net: %u, pool: %u, set height: %lu, pre height: %lu",
        block->network_id(), block->pool_index(), block->height(), prev_block->height());
    if (block->height() <= 0) {
        return Status::kInvalidArgument;
    }

    uint64_t cur_time = common::TimeUtils::TimestampMs();
    block->set_timestamp(prev_block->timestamp() > cur_time ? prev_block->timestamp() + 1 : cur_time);

    // 打包交易
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    
    ZJC_INFO("pool: %d, txs count, all: %lu, valid: %lu, leader: %lu",
        pool_idx_, pools_mgr_->all_tx_size(pool_idx_), pools_mgr_->tx_size(pool_idx_), leader_idx);
    
    auto gid_valid_func = [&](const std::string& gid) -> bool {
        return view_block_chain->CheckTxGidValid(gid, prev_view_block->hash);
    };

    Status s = LeaderGetTxsIdempotently(txs_ptr, gid_valid_func);
    if (s != Status::kSuccess && !no_tx_allowed) {
        // 允许 3 个连续的空交易块
        return s;
    }

    if (txs_ptr) {
        ZJC_DEBUG("====3 pool: %d pop txs: %lu", pool_idx_, txs_ptr->txs.size());
        for (auto it = txs_ptr->txs.begin(); it != txs_ptr->txs.end(); it++) {
            auto* tx_info = tx_propose->add_txs();
            *tx_info = it->second->tx_info;
            assert(tx_info->gid().size() == 32);
            ZJC_DEBUG("add tx pool: %d, prehash: %s, height: %lu, "
                "step: %d, to: %s, gid: %s, tx info: %s",
                block->pool_index(),
                common::Encode::HexEncode(block->prehash()).c_str(),
                block->height(),
                tx_info->step(),
                common::Encode::HexEncode(tx_info->to()).c_str(),
                common::Encode::HexEncode(tx_info->gid()).c_str(),
                ProtobufToJson(*tx_info).c_str());
        }
        tx_propose->set_tx_type(txs_ptr->tx_type);
    }

    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return Status::kElectItemNotFound;
    }
    
    block->set_electblock_height(elect_item->ElectHeight());
    block->set_leader_index(leader_idx);
    block->set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    ZJC_DEBUG("success propose block net: %u, pool: %u, set height: %lu, pre height: %lu, elect height: %lu",
        block->network_id(), block->pool_index(),
        block->height(), prev_block->height(), elect_item->ElectHeight());
    return Status::kSuccess;
}

Status BlockWrapper::GetTxsIdempotently(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>& txs) {
    transport::protobuf::Header header;
    std::map<std::string, pools::TxItemPtr> invalid_txs;
    pools_mgr_->GetTx(pool_idx_, 1024, invalid_txs, header);
    zbft::protobuf::TxBft& txbft = *header.mutable_zbft()->mutable_tx_bft();
    for (auto it = txbft.txs().begin(); it != txbft.txs().end(); it++) {
        txs.push_back(std::make_shared<pools::protobuf::TxMessage>(*it));
    }

    return Status::kSuccess;    
}

bool BlockWrapper::HasSingleTx() {
    return txs_pools_->HasSingleTx(pool_idx_);
}
        
}
}
