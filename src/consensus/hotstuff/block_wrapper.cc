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
        const transport::MessagePtr& msg_ptr, 
        const std::shared_ptr<ViewBlock>& prev_view_block,
        const uint32_t& leader_idx,
        view_block::protobuf::ViewBlockItem* view_block,
        hotstuff::protobuf::TxPropose* tx_propose,
        const bool& no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto* prev_block = &prev_view_block->block_info();
    if (!prev_block) {
        ZJC_WARN("get prev block failed, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    auto* block = view_block->mutable_block_info();
    block->set_version(common::kTransactionVersion);
    block->set_consistency_random(0);
    block->set_height(prev_block->height()+1);
    ZJC_DEBUG("propose block net: %u, pool: %u, set height: %lu, pre height: %lu",
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        block->height(), prev_block->height());
    if (block->height() <= 0) {
        ZJC_WARN("block->height() <= 0, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    uint64_t cur_time = common::TimeUtils::TimestampMs();
    block->set_timestamp(prev_block->timestamp() > cur_time ? prev_block->timestamp() + 1 : cur_time);

    // 打包交易
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    // ZJC_INFO("pool: %d, txs count, all: %lu, valid: %lu, leader: %lu",
    //     pool_idx_, pools_mgr_->all_tx_size(pool_idx_), pools_mgr_->tx_size(pool_idx_), leader_idx);
    auto gid_valid_func = [&](const std::string& gid) -> bool {
        return view_block_chain->CheckTxGidValid(gid, prev_view_block->qc().view_block_hash());
    };

    view_block->set_parent_hash(prev_view_block->qc().view_block_hash());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return Status::kElectItemNotFound;
    }
    
    view_block->mutable_qc()->set_elect_height(elect_item->ElectHeight());
    view_block->mutable_qc()->set_leader_idx(leader_idx);
    block->set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    Status s = LeaderGetTxsIdempotently(msg_ptr, txs_ptr, gid_valid_func);
    if (s != Status::kSuccess && !no_tx_allowed) {
        // 允许 3 个连续的空交易块
        ZJC_ERROR("leader get txs failed check is empty block allowd: %d, pool: %d, %u_%u_%lu size: %u, pool size: %u",
            s, pool_idx_, 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view(), 
            (txs_ptr != nullptr ? txs_ptr->txs.size() : 0),
            pools_mgr_->all_tx_size(pool_idx_));
        return s;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("leader get txs success check is empty block allowd: %d, pool: %d, %u_%u_%lu size: %u",
        s, pool_idx_, 
        view_block->qc().network_id(), 
        view_block->qc().pool_index(), 
        view_block->qc().view(), 
        (txs_ptr != nullptr ? txs_ptr->txs.size() : 0));
    if (txs_ptr) {
        for (auto it = txs_ptr->txs.begin(); it != txs_ptr->txs.end(); it++) {
            auto* tx_info = tx_propose->add_txs();
            *tx_info = *it->second->tx_info;
            assert(tx_info->gid().size() == 32);
            // ADD_TX_DEBUG_INFO(tx_info);
            // ZJC_DEBUG("add tx pool: %d, prehash: %s, height: %lu, "
            //     "step: %d, to: %s, gid: %s, tx info: %s",
            //     view_block->qc().pool_index(),
            //     common::Encode::HexEncode(view_block->parent_hash()).c_str(),
            //     block->height(),
            //     tx_info->step(),
            //     common::Encode::HexEncode(tx_info->to()).c_str(),
            //     common::Encode::HexEncode(tx_info->gid()).c_str(),
            //     "ProtobufToJson(*tx_info).c_str()");
        }
        tx_propose->set_tx_type(txs_ptr->tx_type);
    }

    ZJC_DEBUG("====3 success propose block net: %u, pool: %u, set height: %lu, pre height: %lu, "
        "elect height: %lu, hash: %s, parent hash: %s, %u_%u_%lu",
        view_block->qc().network_id(), view_block->qc().pool_index(),
        block->height(), prev_block->height(), elect_item->ElectHeight(),
        common::Encode::HexEncode(GetQCMsgHash(view_block->qc())).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str(),
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        view_block->qc().view());
    return Status::kSuccess;
}

bool BlockWrapper::HasSingleTx(
        const transport::MessagePtr& msg_ptr, 
        pools::CheckGidValidFunction gid_valid_fn) {
    return txs_pools_->HasSingleTx(msg_ptr, pool_idx_, gid_valid_fn);
}
        
}

}
