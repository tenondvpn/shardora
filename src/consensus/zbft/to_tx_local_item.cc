#include "consensus/zbft/to_tx_local_item.h"

#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ToTxLocalItem::HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume by from
    if (block_tx.storages_size() != 1) {
        block_tx.set_status(kConsensusError);
        return consensus::kConsensusSuccess;
    }

    pools::protobuf::ToTxMessage to_txs;
    if (!to_txs.ParseFromString(block_tx.storages(0).value())) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get to txs info failed: %s",
            common::Encode::HexEncode(block_tx.storages(0).value()).c_str());
        return consensus::kConsensusSuccess;
    }

    block::protobuf::ConsensusToTxs block_to_txs;
    std::string str_for_hash;
    str_for_hash.reserve(to_txs.tos_size() * 48);
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint64_t to_balance = 0;
        uint64_t nonce = 0;
        // if (to_txs.tos(i).des().size() == security::kUnicastAddressLength) { // only to, for normal to tx
            int balance_status = GetTempAccountBalance(to_txs.tos(i).des(), acc_balance_map, &to_balance, &nonce);
            if (balance_status != kConsensusSuccess) {
                ZJC_DEBUG("create new address: %s, balance: %lu",
                    common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
                    to_txs.tos(i).amount());
                to_balance = 0;
            } else {
                ZJC_DEBUG("success get to balance: %s, %lu",
                    common::Encode::HexEncode(to_txs.tos(i).des()).c_str(), 
                    to_balance);
            }
        // } else if (to_txs.tos(i).des().size() == security::kUnicastAddressLength * 2) { // to + from, for gas prepayment tx
        //     to_balance = gas_prepayment_->GetAddressPrepayment(
        //         view_block.qc().pool_index(),
        //         to_txs.tos(i).des().substr(0, security::kUnicastAddressLength),
        //         to_txs.tos(i).des().substr(security::kUnicastAddressLength, security::kUnicastAddressLength));
        //     ZJC_DEBUG("success add contract prepayment: %s, %lu, gid: %s",
        //         common::Encode::HexEncode(to_txs.tos(i).des()).c_str(), 
        //         to_balance, 
        //         common::Encode::HexEncode(block_tx.gid()).c_str());
        // } else {
        //     ZJC_ERROR("local to des invalid: %s, %u",
        //         common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
        //         to_txs.tos(i).des().size());
        //     assert(false);
        //     continue;
        // }

        auto to_tx = block_to_txs.add_tos();
        to_balance += to_txs.tos(i).amount();
        to_tx->set_to(to_txs.tos(i).des());
        to_tx->set_balance(to_balance);
        str_for_hash.append(to_txs.tos(i).des());
        str_for_hash.append((char*)&to_balance, sizeof(to_balance));
        acc_balance_map[to_txs.tos(i).des()] = std::pair<int64_t, uint64_t>(static_cast<int64_t>(to_balance), nonce);
        ZJC_DEBUG("add local to: %s, balance: %lu, amount: %lu",
            common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
            to_balance,
            to_txs.tos(i).amount());
    }

    auto storage = block_tx.add_storages();
    storage->set_key(protos::kConsensusLocalNormalTos);
    storage->set_value(block_to_txs.SerializeAsString());
    ZJC_DEBUG("success consensus local transfer to");
    return consensus::kConsensusSuccess;
}

int ToTxLocalItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    DefaultTxItem(tx_info, block_tx);
    // change
    if (tx_info.key().empty()) {
        return consensus::kConsensusError;
    }

    auto storage = block_tx->add_storages();
    storage->set_key(tx_info.key());
    storage->set_value(tx_info.value());
    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora




