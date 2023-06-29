#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ToTxItem : public TxItemBase {
public:
    ToTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr) {}

    virtual ~ToTxItem() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            std::shared_ptr<db::DbWriteBatch>& db_batch,
            block::protobuf::BlockTx* block_tx) {
        ZJC_DEBUG("to tx consensus coming: %s", common::Encode::HexEncode(tx_info.value()).c_str());
        DefaultTxItem(tx_info, block_tx);
        // change
        if (tx_info.key().empty() ||
                tx_info.key() != protos::kNormalTos ||
                tx_info.value().empty() ||
                tx_info.value().size() % 32 != 0) {
            return consensus::kConsensusError;
        }

        uint32_t offset = 0;
        uint32_t count = tx_info.value().size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            auto storage = block_tx->add_storages();
            std::string tmp(tx_info.value().c_str() + offset, 32);
            storage->set_key(protos::kNormalToShards);
            storage->set_val_hash(tmp);
            offset += 32;
        }
        
        return consensus::kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ToTxItem);
};

};  // namespace consensus

};  // namespace zjchain
