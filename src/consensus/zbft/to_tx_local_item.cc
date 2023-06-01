#include "consensus/zbft/to_tx_local_item.h"

#include "zjcvm/execution.h"

namespace zjchain {

namespace consensus {

int ToTxLocalItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume by from
    if (block_tx.storages_size() != 1) {
        block_tx.set_status(kConsensusError);
        return consensus::kConsensusSuccess;
    }

    std::string to_txs_str;
    if (!prefix_db_->GetTemporaryKv(block_tx.storages(0).val_hash(), &to_txs_str)) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get to txs info failed: %s",
            common::Encode::HexEncode(block_tx.storages(0).val_hash()).c_str());
        return consensus::kConsensusSuccess;
    }

    pools::protobuf::ToTxMessage to_txs;
    if (!to_txs.ParseFromString(to_txs_str)) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get to txs info failed: %s",
            common::Encode::HexEncode(block_tx.storages(0).val_hash()).c_str());
        return consensus::kConsensusSuccess;
    }

    block::protobuf::ConsensusToTxs block_to_txs;
    std::string str_for_hash;
    str_for_hash.reserve(to_txs.tos_size() * 48);
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint64_t to_balance = 0;
        if (to_txs.tos(i).des().size() == security::kUnicastAddressLength) {
            int balance_status = GetTempAccountBalance(
                thread_idx, to_txs.tos(i).des(), acc_balance_map, &to_balance);
            if (balance_status != kConsensusSuccess) {
                ZJC_DEBUG("create new address: %s, balance: %lu",
                    common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
                    to_txs.tos(i).amount());
                to_balance = 0;
            }
        } else if (to_txs.tos(i).des().size() == security::kUnicastAddressLength * 2) {
            to_balance = gas_prepayment_->GetAddressPrepayment(
                thread_idx,
                block.pool_index(),
                to_txs.tos(i).des().substr(0, security::kUnicastAddressLength),
                to_txs.tos(i).des().substr(security::kUnicastAddressLength, security::kUnicastAddressLength));
            ZJC_DEBUG("success add contract prepayment: %s, %lu",
                common::Encode::HexEncode(to_txs.tos(i).des()).c_str(), to_balance);
        } else {
            continue;
        }

        auto to_tx = block_to_txs.add_tos();
        to_balance += to_txs.tos(i).amount();
        to_tx->set_to(to_txs.tos(i).des());
        to_tx->set_balance(to_balance);
        str_for_hash.append(to_txs.tos(i).des());
        str_for_hash.append((char*)&to_balance, sizeof(to_balance));
        acc_balance_map[to_txs.tos(i).des()] = to_balance;
        ZJC_DEBUG("add local to: %s, balance: %lu",
            common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
            to_balance);

    }

    auto tos_hash = common::Hash::keccak256(str_for_hash);
    auto storage = block_tx.add_storages();
    storage->set_key(protos::kConsensusLocalNormalTos);
    storage->set_val_hash(tos_hash);
    std::string val = block_to_txs.SerializeAsString();
    prefix_db_->SaveTemporaryKv(tos_hash, val);
    ZJC_DEBUG("success consensus local transfer to");
    return consensus::kConsensusSuccess;
}

int ToTxLocalItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        block::protobuf::BlockTx* block_tx) {
    DefaultTxItem(tx_info, block_tx);
    // change
    if (tx_info.key().empty()) {
        return consensus::kConsensusError;
    }

    auto storage = block_tx->add_storages();
    storage->set_key(tx_info.key());
    storage->set_val_hash(tx_info.value());
    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain




