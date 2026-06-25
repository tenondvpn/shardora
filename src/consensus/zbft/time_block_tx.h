#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/timeblock.pb.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class TimeBlockTx : public TxItemBase {
public:
    TimeBlockTx(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~TimeBlockTx() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        if (!DefaultTxItem(tx_info, block_tx)) {
            //assert(false);
            return consensus::kConsensusError;
        }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
            uint32_t tx_index,
            view_block::protobuf::ViewBlockItem& view_block,
            shardoravm::ShardorahainHost& shardora_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        GetTempAccountBalance(shardora_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
        auto& unique_hash = tx_info->key();
        std::string val;
        if (shardora_host.GetKeyValue(block_tx.to(), unique_hash, &val) == shardoravm::kShardoravmSuccess) {
            SHARDORA_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
            return consensus::kConsensusError;
        }

        auto& timer_block = *view_block.mutable_block_info()->mutable_timer_block();
        if (!timer_block.ParseFromString(tx_info->value())) {
            SHARDORA_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
            return consensus::kConsensusError;
        }

        timer_block.set_height(view_block.block_info().height());
        InitHost(shardora_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
        block::protobuf::TxHashStatus tx_hash_status;
        tx_hash_status.set_status(block_tx.status());
        auto status_val = tx_hash_status.SerializeAsString();
        shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
        shardora_host.SaveKeyValue(block_tx.to(), unique_hash, tx_info->value());
        block_tx.set_unique_hash(unique_hash);
        SHARDORA_WARN("success call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), block_tx.nonce(), block_tx.nonce());
        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
        acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
        acc_balance_map[block_tx.to()]->set_tx_index(tx_index);
        SHARDORA_DEBUG("success add addr: %s, value: %s, unique hash: %s", 
            common::Encode::HexEncode(block_tx.to()).c_str(), 
            ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str(),
            common::Encode::HexEncode(unique_hash).c_str());

        prefix_db_->SaveLatestTimeBlock(timer_block, shardora_host.db_batch_);
        view_block.mutable_block_info()->add_unique_hashs(block_tx.unique_hash());
        return consensus::kConsensusSuccess;
    }

private:

    DISALLOW_COPY_AND_ASSIGN(TimeBlockTx);
};

};  // namespace consensus

};  // namespace shardora
