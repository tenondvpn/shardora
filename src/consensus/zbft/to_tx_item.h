#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class ToTxItem : public TxItemBase {
public:
    ToTxItem(
        const pools::protobuf::TxMessage* msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg, account_mgr, sec_ptr, addr_info) {}

    virtual ~ToTxItem() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        ZJC_DEBUG("to tx consensus coming: %s, gid: %s", 
            "common::Encode::HexEncode(tx_info.value()).c_str()", 
            common::Encode::HexEncode(tx_info.gid()).c_str());
        DefaultTxItem(tx_info, block_tx);
        // change
        if (tx_info.key().empty() ||
                tx_info.key() != protos::kNormalTos ||
                tx_info.value().empty()) {
            return consensus::kConsensusError;
        }

        pools::protobuf::AllToTxMessage all_to_txs;
        if (!all_to_txs.ParseFromString(tx_info.value())) {
            return consensus::kConsensusError;
        }

        uint32_t offset = 0;
        for (uint32_t i = 0; i < all_to_txs.to_tx_arr_size(); ++i) {
            auto storage = block_tx->add_storages();
            storage->set_key(protos::kNormalToShards);
            storage->set_value(all_to_txs.to_tx_arr(i).SerializeAsString());
            ZJC_DEBUG("success add normal to %s, val: %s", 
                protos::kNormalToShards.c_str(), 
                ProtobufToJson(all_to_txs.to_tx_arr(i)).c_str());
            assert(!storage->value().empty());
        }
        
        return consensus::kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ToTxItem);
};

};  // namespace consensus

};  // namespace shardora
