#include "consensus/zbft/to_tx_item.h"

namespace zjchain {

namespace consensus {


int ToTxItem::HandleTx(
        uint8_t thread_idx,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    return consensus::kConsensusSuccess;
}

int ToTxItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    DefaultTxItem(tx_info, block_tx);
    // change
    if (tx_info.key().empty()) {
        return consensus::kConsensusError;
    }

    auto storage = block_tx->add_storages();
    storage->set_key(tx_info.key());
    storage->set_val_hash(tx_info.value());
    storage->set_val_size(0);
    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain




