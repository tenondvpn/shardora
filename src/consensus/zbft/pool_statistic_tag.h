#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class PoolStatisticTag : public TxItemBase {
public:
    PoolStatisticTag(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~PoolStatisticTag() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        ZJC_DEBUG("pools statistic tag tx consensus coming: %s, nonce: %lu, val: %s", 
            common::Encode::HexEncode(tx_info.to()).c_str(), 
            tx_info.nonce(),
            common::Encode::HexEncode(tx_info.value()).c_str());
        if (!DefaultTxItem(tx_info, block_tx)) {
            return consensus::kConsensusError;
        }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
            view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        if (GetTempAccountBalance(
                zjc_host, 
                block_tx.to(), 
                acc_balance_map, 
                &to_balance, 
                &to_nonce) != consensus::kConsensusSuccess) {
            return consensus::kConsensusError;
        }

        std::string val;
        if (zjc_host.GetKeyValue(block_tx.to(), tx_info->key(), &val) == zjcvm::kZjcvmSuccess) {
            ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(tx_info->key()).c_str());
            return consensus::kConsensusError;
        }

        InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
        zjc_host.SaveKeyValue(block_tx.to(), tx_info->key(), tx_info->value());
        block_tx.set_unique_hash(tx_info->key());
        uint64_t* udata = (uint64_t*)tx_info->value().c_str();
        uint64_t statistic_height = udata[0];
        block_tx.set_nonce(to_nonce + 1);
        ZJC_WARN("success call pool statistic height: %lu, pool: %d, view: %lu, "
            "to_nonce: %lu. tx nonce: %lu, to: %s, unique hash: %s", 
            statistic_height,
            view_block.qc().pool_index(), view_block.qc().view(),
            to_nonce, block_tx.nonce(),
            common::Encode::HexEncode(block_tx.to()).c_str(),
            common::Encode::HexEncode(tx_info->key()).c_str());
        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(to_nonce + 1);
        // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
        ZJC_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(block_tx.to()).c_str(), 
            ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
        view_block.mutable_block_info()->set_height(statistic_height);
        return consensus::kConsensusSuccess;
    }
    
private:

    DISALLOW_COPY_AND_ASSIGN(PoolStatisticTag);
};

};  // namespace consensus

};  // namespace shardora
