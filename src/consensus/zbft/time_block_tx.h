#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
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
            assert(false);
            return consensus::kConsensusError;
        }

        if (tx_info.key().empty()) {
            assert(false);
            return consensus::kConsensusError;
        }

        unique_hash_ = tx_info.key();
        auto* storage = block_tx->add_storages();
        storage->set_key(protos::kAttrTimerBlock);
        storage->set_value(tx_info.value());
        ZJC_DEBUG("root to tx consensus coming: %s, nonce: %lu, key: %s, val: %s", 
            common::Encode::HexEncode(tx_info.to()).c_str(),  
            tx_info.nonce(),
            common::Encode::HexEncode(tx_info.key()).c_str(),
            common::Encode::HexEncode(tx_info.value()).c_str());
        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
        auto str_key = block_tx.to() + unique_hash_;
        std::string val;
        if (zjc_host.GetKeyValue(block_tx.to(), unique_hash_, &val) == zjcvm::kZjcvmSuccess) {
            ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash_).c_str());
            return consensus::kConsensusError;
        }

        InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
        zjc_host.SaveKeyValue(block_tx.to(), unique_hash_, "1");
        block_tx.set_unique_hash(unique_hash_);
        block_tx.set_nonce(to_nonce + 1);
        ZJC_WARN("success call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
        // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
        ZJC_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(block_tx.to()).c_str(), 
            ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());

        return consensus::kConsensusSuccess;
    }

private:
    std::string unique_hash_;

    DISALLOW_COPY_AND_ASSIGN(TimeBlockTx);
};

};  // namespace consensus

};  // namespace shardora
