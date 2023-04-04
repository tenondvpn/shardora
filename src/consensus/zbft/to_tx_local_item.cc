#include "consensus/zbft/to_tx_local_item.h"

namespace zjchain {

namespace consensus {


int ToTxLocalItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
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
        if (to_txs.tos(i).amount() > 0) {
            // dispatch to txs to tx pool
            uint64_t to_balance = 0;
            int balance_status = GetTempAccountBalance(
                thread_idx, to_txs.tos(i).des(), acc_balance_map, &to_balance);
            if (balance_status != kConsensusSuccess) {
                ZJC_DEBUG("create new address: %s, balance: %lu",
                    common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
                    to_txs.tos(i).amount());
                to_balance = 0;
                std::string tx_str = prefix_db_->GetAddressTmpBytesCode(to_txs.tos(i).des());
                if (!tx_str.empty()) {
                    block::protobuf::BlockTx tx;
                    if (!tx.ParseFromString(tx_str)) {
                        ZJC_DEBUG("parser contract tx failed!");
                        block_tx.set_status(kConsensusError);
                        return consensus::kConsensusSuccess;
                    }

                    // contract create call
                    zjcvm::ZjchainHost zjc_host;
                    zjc_host.my_address_ = to;
                    // get caller prepaid gas
                    zjc_host.AddTmpAccountBalance(
                        tx.from(),
                        tx.contract_prepayment());
                    zjc_host_.AddTmpAccountBalance(
                        tx.to(),
                        to_txs.tos(i).amount());
                    evmc_result evmc_res = {};
                    evmc::Result res{ evmc_res };
                    if (CreateContractCallExcute(zjc_host, tx, &res) != kConsensusSuccess) {
                        ZJC_DEBUG("create contract failed!");
                        block_tx.set_status(kConsensusError);
                        return consensus::kConsensusSuccess;
                    }
                }
            }

            auto to_tx = block_to_txs.add_tos();
            to_balance += to_txs.tos(i).amount();
            to_tx->set_to(to_txs.tos(i).des());
            to_tx->set_balance(to_balance);
            str_for_hash.append(to_txs.tos(i).des());
            str_for_hash.append((char*)&to_balance, sizeof(to_balance));
            acc_balance_map[to_txs.tos(i).des()] = to_balance;
        }
    }

    auto tos_hash = common::Hash::keccak256(str_for_hash);
    auto storage = block_tx.add_storages();
    storage->set_key(protos::kConsensusLocalNormalTos);
    storage->set_val_hash(tos_hash);
    prefix_db_->SaveTemporaryKv(tos_hash, block_to_txs.SerializeAsString());
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
    storage->set_val_hash(tx_info.value());
    storage->set_val_size(0);
    return consensus::kConsensusSuccess;
}


int ToTxLocalItem::CreateContractCallExcute(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        evmc::Result* out_res) {
    uint32_t call_mode = zjcvm::kJustCreate;
    if (!tx.has_contract_input()) {
        call_mode = zjcvm::kCreateAndCall;
    }

    int exec_res = zjcvm::Execution::Instance()->execute(
        tx.contract_code(),
        tx.contract_input(),
        tx.from(),
        tx.to(),
        tx.from(),
        tx.amount(),
        tx.gas_limit(),
        0,
        call_mode,
        zjc_host,
        out_res);
    if (exec_res != zjcvm::kZjcvmSuccess) {
        ZJC_ERROR("CreateContractCallExcute failed: %d", exec_res);
        return kConsensusError;
    }

    return kConsensusSuccess;
}
};  // namespace consensus

};  // namespace zjchain




