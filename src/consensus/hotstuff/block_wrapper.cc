
#include <common/global_info.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/view_block_chain.h>
#include "shardoravm/shardora_host.h"

namespace shardora {
namespace hotstuff {

BlockWrapper::BlockWrapper(
        const uint32_t pool_idx,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<bls::BlsManager> bls_mgr,
        const std::shared_ptr<ElectInfo>& elect_info) :
    pool_idx_(pool_idx),
    bls_mgr_(bls_mgr),
    pools_mgr_(pools_mgr),
    tm_block_mgr_(tm_block_mgr),
    block_mgr_(block_mgr),
    elect_info_(elect_info) {
    txs_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr, block_mgr, tm_block_mgr);
}

BlockWrapper::~BlockWrapper(){};

// Package a new block and txs
Status BlockWrapper::Wrap(
        const transport::MessagePtr& msg_ptr, 
        const std::shared_ptr<ViewBlock>& prev_view_block,
        view_block::protobuf::ViewBlockItem* view_block,
        hotstuff::protobuf::TxPropose* tx_propose,
        bool no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) {
    auto wrap_begin_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto* prev_block = &prev_view_block->block_info();
    if (!prev_block) {
        SHARDORA_WARN("get prev block failed, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    auto* block = view_block->mutable_block_info();
    block->set_version(common::kTransactionVersion);
    block->set_consistency_random(0);
    block->set_chain_id(kGlobalChainId);
    block->set_all_gas(0);
    block->set_height(prev_block->height()+1);
    SHARDORA_DEBUG("propose block net: %u, pool: %u, set height: %lu, pre height: %lu",
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        block->height(), prev_block->height());
    if (block->height() <= 0) {
        SHARDORA_WARN("block->height() <= 0, pool index: %d", pool_idx_);
        return Status::kInvalidArgument;
    }

    // uint64_t cur_time = common::TimeUtils::TimestampMs();
    // block->set_timestamp(prev_block->timestamp() > cur_time ? prev_block->timestamp() + 1 : cur_time);
    // Package transactions
    ADD_DEBUG_PROCESS_TIMESTAMP();
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    const auto& parent_hash = prev_view_block->qc().view_block_hash();
    BalanceAndNonceMap merged_balance_map;
    view_block_chain->MergeAllPrevBalanceMap(parent_hash, merged_balance_map);
    auto tx_valid_func = [&](
            const address::protobuf::AddressInfo& addr_info, 
            const pools::protobuf::TxMessage& tx_info,
            uint64_t* now_nonce) -> int {
        return CheckTransactionValid(
            parent_hash,
            view_block_chain, 
            pools_mgr_,
            addr_info, 
            tx_info,
            now_nonce,
            &merged_balance_map);
    };

    auto get_txs_begin_ms = common::TimeUtils::TimestampMs();
    Status s = LeaderGetTxsIdempotently(msg_ptr, txs_ptr, tx_valid_func);
    auto get_txs_end_ms = common::TimeUtils::TimestampMs();
    if (get_txs_end_ms - get_txs_begin_ms >= 100lu) {
        SHARDORA_DEBUG("pool: %d, leader get txs use time: %lu ms, merged accounts: %zu, "
            "selected: %zu, %u_%u_%lu",
            pool_idx_,
            (get_txs_end_ms - get_txs_begin_ms),
            merged_balance_map.size(),
            (txs_ptr != nullptr ? txs_ptr->txs.size() : 0),
            view_block->qc().network_id(),
            view_block->qc().pool_index(),
            view_block->qc().view());
    }
    if (s != Status::kSuccess && !no_tx_allowed) {
        // Allow 3 consecutive empty transaction blocks
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
        // Hard limit: stop adding txs once the propose message would exceed the limit.
        // This prevents receivers from dropping the message due to the packet size limit.
        int current_size = 0;
        uint64_t proposed_gas = 0;

        for (auto it = txs_ptr->txs.begin(); it != txs_ptr->txs.end(); it++) {
            auto& tx = *((*it)->tx_info);
            auto tx_gas_limit = tx.gas_limit();
            if (!consensus::CanAddBlockGas(proposed_gas, tx_gas_limit)) {
                SHARDORA_WARN("pool: %d, block gas limit reached: current=%lu, tx_gas_limit=%lu, "
                    "limit=%lu, stopping at %d/%zu txs",
                    pool_idx_, proposed_gas, tx_gas_limit, consensus::kBlockMaxGasLimit,
                    tx_propose->txs_size(), txs_ptr->txs.size());
                break;
            }

            int tx_size = (*it)->tx_info->ByteSizeLong();
            if (current_size + tx_size > common::kMaxProposeMsgBytes) {
                SHARDORA_WARN("pool: %d, propose msg size limit reached: current=%d bytes, "
                    "tx_size=%d, limit=%d — stopping at %d/%zu txs",
                    pool_idx_, current_size, tx_size, common::kMaxProposeMsgBytes,
                    tx_propose->txs_size(), txs_ptr->txs.size());
                break;
            }
            auto* tx_info = tx_propose->add_txs();
            *tx_info = *((*it)->tx_info);
            current_size += tx_size;
            proposed_gas += tx_gas_limit;
            if (tx_info->step() == pools::protobuf::kConsensusRootElectShard) {
                pools::protobuf::ElectStatistic elect_statistic;
                if (elect_statistic.ParseFromString(tx_info->value())) {
                    auto* elect_block = elect_statistic.mutable_elect_block();
                    elect_block->set_shard_network_id(elect_statistic.sharding_id());
                    bls_mgr_->AddBlsConsensusInfo(*elect_block);
                    tx_info->set_value(SerializeDeterministic(elect_statistic));
                    // Update size estimate after elect block inflation.
                    current_size += tx_info->ByteSizeLong() - tx_size;
                }
            }
        }

        tx_propose->set_tx_type(txs_ptr->tx_type);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto elect_item = elect_info_->GetElectItemWithShardingId(common::GlobalInfo::Instance()->network_id());
    if (!elect_item) {
        return Status::kElectItemNotFound;
    }
    
    view_block->mutable_qc()->set_elect_height(elect_item->ElectHeight());
    block->set_timeblock_height(tm_block_mgr_->LatestTimestampHeight());
    {
        auto wrap_end_ms = common::TimeUtils::TimestampMs();
    }
#ifndef NDEBUG
    for (int32_t ti = 0; ti < tx_propose->txs_size(); ++ti) {
        auto& packed_tx = tx_propose->txs(ti);
        SHARDORA_DEBUG("[TX_PACKED] pool: %d, tx[%d/%d]: to=%s, nonce=%lu, "
            "step=%d, amount=%lu, view=%lu",
            pool_idx_, ti, tx_propose->txs_size(),
            common::Encode::HexEncode(packed_tx.to()).c_str(),
            packed_tx.nonce(),
            (int)packed_tx.step(),
            packed_tx.amount(),
            view_block->qc().view());
    }
#endif
    if (tx_propose->txs_size() > 0) {
        SHARDORA_DEBUG("[TX_PACKED] pool: %d, packed tx count: %d, view: %lu",
            pool_idx_, tx_propose->txs_size(), view_block->qc().view());
    }
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
