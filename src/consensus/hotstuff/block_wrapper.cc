
#include <common/global_info.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/view_block_chain.h>
#include "zjcvm/zjc_host.h"

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
        SHARDORA_WARN("get prev block failed, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    auto* block = view_block->mutable_block_info();
    block->set_version(common::kTransactionVersion);
    block->set_consistency_random(0);
    block->set_height(prev_block->height()+1);
    SHARDORA_DEBUG("propose block net: %u, pool: %u, set height: %lu, pre height: %lu",
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        block->height(), prev_block->height());
    if (block->height() <= 0) {
        SHARDORA_WARN("block->height() <= 0, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    uint64_t cur_time = common::TimeUtils::TimestampMs();
    block->set_timestamp(prev_block->timestamp() > cur_time ? prev_block->timestamp() + 1 : cur_time);
    // 打包交易
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    // SHARDORA_INFO("pool: %d, txs count, all: %lu, valid: %lu, leader: %lu",
    //     pool_idx_, pools_mgr_->all_tx_size(pool_idx_), pools_mgr_->tx_size(pool_idx_), leader_idx);
    auto tx_valid_func = [&](
            const address::protobuf::AddressInfo& addr_info, 
            pools::protobuf::TxMessage& tx_info) -> int {
        if (pools::IsUserTransaction(tx_info.step())) {
            return view_block_chain->CheckTxNonceValid(
                addr_info.addr(), 
                tx_info.nonce(), 
                prev_view_block->qc().view_block_hash());
        }
        
        zjcvm::ZjchainHost zjc_host;
        zjc_host.parent_hash_ = prev_view_block->qc().view_block_hash();
        zjc_host.view_block_chain_ = view_block_chain;
        std::string val;
        if (zjc_host.GetKeyValue(tx_info.to(), tx_info.key(), &val) == zjcvm::kZjcvmSuccess) {
            SHARDORA_DEBUG("not user tx unique hash exists: to: %s, unique hash: %s, step: %d",
                common::Encode::HexEncode(tx_info.to()).c_str(),
                common::Encode::HexEncode(tx_info.key()).c_str(),
                (int32_t)tx_info.step());
            return 1;
        }

        SHARDORA_INFO("not user tx unique hash success to: %s, unique hash: %s, parent_hash: %s",
            common::Encode::HexEncode(tx_info.to()).c_str(),
            common::Encode::HexEncode(tx_info.key()).c_str(),
            common::Encode::HexEncode(zjc_host.parent_hash_).c_str());
        return 0;
    };

    Status s = LeaderGetTxsIdempotently(msg_ptr, txs_ptr, tx_valid_func);
    if (s != Status::kSuccess && !no_tx_allowed) {
        // 允许 3 个连续的空交易块
        SHARDORA_DEBUG("leader get txs failed check is empty block allowd: %d, "
            "pool: %d, %u_%u_%lu size: %u, pool size: %u",
            (int32_t)s, pool_idx_, 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->qc().view(), 
            (txs_ptr != nullptr ? txs_ptr->txs.size() : 0),
            pools_mgr_->all_tx_size(pool_idx_));
        return s;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("leader get txs success check is empty block allowd: %d, pool: %d, %u_%u_%lu size: %u",
        (int32_t)s, pool_idx_, 
        view_block->qc().network_id(), 
        view_block->qc().pool_index(), 
        view_block->qc().view(), 
        (txs_ptr != nullptr ? txs_ptr->txs.size() : 0));
    view_block->set_parent_hash(prev_view_block->qc().view_block_hash());
    if (txs_ptr) {
        for (auto it = txs_ptr->txs.begin(); it != txs_ptr->txs.end(); it++) {
            auto* tx_info = tx_propose->add_txs();
            *tx_info = *((*it)->tx_info);
            // ADD_TX_DEBUG_INFO(tx_info);
            // SHARDORA_DEBUG("add tx pool: %d, prehash: %s, height: %lu, "
            //     "step: %d, to: %s, nonce: %lu, tx info: %s",
            //     view_block->qc().pool_index(),
            //     common::Encode::HexEncode(view_block->parent_hash()).c_str(),
            //     block->height(),
            //     tx_info->step(),
            //     common::Encode::HexEncode(tx_info->to()).c_str(),
            //     tx_info->nonce(),
            //     "ProtobufToJson(*tx_info).c_str()");
        }

        tx_propose->set_tx_type(txs_ptr->tx_type);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return Status::kElectItemNotFound;
    }
    
    view_block->mutable_qc()->set_elect_height(elect_item->ElectHeight());
    view_block->mutable_qc()->set_leader_idx(leader_idx);
    block->set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    SHARDORA_DEBUG("====3 success propose block net: %u, pool: %u, set height: %lu, pre height: %lu, "
        "elect height: %lu, timeblock height: %lu, hash: %s, parent hash: %s, %u_%u_%lu",
        view_block->qc().network_id(), view_block->qc().pool_index(),
        block->height(), prev_block->height(), elect_item->ElectHeight(),
        block->timeblock_height(),
        common::Encode::HexEncode(GetQCMsgHash(view_block->qc())).c_str(),
        common::Encode::HexEncode(view_block->parent_hash()).c_str(),
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        view_block->qc().view());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

bool BlockWrapper::HasSingleTx(
        const transport::MessagePtr& msg_ptr, 
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    return txs_pools_->HasSingleTx(msg_ptr, pool_idx_, tx_valid_func);
}
        
}

}
