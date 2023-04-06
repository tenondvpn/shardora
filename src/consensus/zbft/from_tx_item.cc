#include "consensus/zbft/from_tx_item.h"

namespace zjchain {

namespace consensus {

int FromTxItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    if (block_tx.step() == pools::protobuf::kNormalFrom) {
        return NormalTx(thread_idx, block, acc_balance_map, block_tx);
    }

    if (block_tx.step() == pools::protobuf::kContractUserCreateCall) {
        return CreateContractUserCall(thread_idx, block, acc_balance_map, block_tx);
    }
}  

int FromTxItem::NormalTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t to_balance = 0;
    auto& from = msg_ptr->address_info->addr();
    int balance_status = GetTempAccountBalance(
        thread_idx, from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    do  {
        gas_used = consensus::kTransferGas;
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            // TODO(): check key exists and reserve gas
            gas_used += (block_tx.storages(i).key().size() + block_tx.storages(i).val_size()) *
                consensus::kKeyValueStorageEachBytes;
        }

        if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), block_tx.gas_price());
            break;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
            break;
        }
    } while (0);

    if (block_tx.status() == kConsensusSuccess) {
        uint64_t dec_amount = block_tx.amount() + gas_used * block_tx.gas_price();
        if (from_balance >= gas_used * block_tx.gas_price()) {
            if (from_balance >= dec_amount) {
                from_balance -= dec_amount;
            } else {
                from_balance -= gas_used * block_tx.gas_price();
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu", from_balance, dec_amount);
            }
        } else {
            from_balance = 0;
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            ZJC_ERROR("leader balance error: %llu, %llu",
                from_balance, gas_used * block_tx.gas_price());
        }
    } else {
        if (from_balance >= gas_used * block_tx.gas_price()) {
                from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    }

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
//     ZJC_DEBUG("handle tx success: %s, %lu, %lu, status: %d",
//         common::Encode::HexEncode(block_tx.gid()).c_str(),
//         block_tx.balance(),
//         block_tx.gas_used(),
//         block_tx.status());
    return kConsensusSuccess;
}

int FromTxItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    DefaultTxItem(tx_info, block_tx);
    // change
    if (!tx_info.key().empty()) {
        auto storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        if (tx_info.value().size() <= 32) {
            storage->set_val_hash(tx_info.value());
        } else {
            storage->set_val_hash(common::Hash::keccak256(tx_info.value()));
        }

        storage->set_val_size(tx_info.value().size());
    }

    return kConsensusSuccess;}

int FromTxItem::CreateContractUserCall(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // contract create call
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t to_balance = 0;
    auto& from = msg_ptr->address_info->addr();
    int balance_status = GetTempAccountBalance(
        thread_idx, from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    if (block_tx.gas_price() * block_tx.gas_limit() + block_tx.contract_prepayment() > from_balance) {
        block_tx.set_status(kConsensusOutOfGas);
        return kConsensusSuccess;
    }

    zjcvm::ZjchainHost zjc_host;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = block.height();
    zjc_host.tx_context_.block_timestamp = block.timestamp() / 1000;
    uint64_t chanin_id = (((uint64_t)block.network_id()) << 32 | (uint64_t)block.pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.thread_idx_ = thread_idx;
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.tx_gas_price,
        block_tx.gas_price());
    zjc_host.my_address_ = block_tx.to();
    zjc_host.tx_context_.block_gas_limit = block_tx.gas_limit();
    // get caller prepaid gas
    zjc_host.AddTmpAccountBalance(
        block_tx.from(),
        from_balance);
    zjc_host.AddTmpAccountBalance(
        block_tx.to(),
        block_tx.amount());
    evmc_result evmc_res = {};
    evmc::Result res{ evmc_res };
    if (CreateContractCallExcute(zjc_host, block_tx, &res) != kConsensusSuccess ||
            res.status_code != evmc::EVMC_SUCCESS) {
        block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
        ZJC_DEBUG("create contract failed!");
    }

    auto gas_used = block_tx.gas_limit() - res.gas_left;
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        // TODO(): check key exists and reserve gas
        gas_used += (block_tx.storages(i).key().size() + block_tx.storages(i).val_size()) *
            consensus::kKeyValueStorageEachBytes;
    }

    if (block_tx.gas_limit() < gas_used) {
        block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
        ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
    }

    if (block_tx.status() == kConsensusSuccess) {
        uint64_t dec_amount = block_tx.amount() +
            block_tx.contract_prepayment() +
            gas_used * block_tx.gas_price();
        if (from_balance >= gas_used * block_tx.gas_price()) {
            if (from_balance >= dec_amount) {
                from_balance -= dec_amount;
            } else {
                from_balance -= gas_used * block_tx.gas_price();
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu", from_balance, dec_amount);
            }
        } else {
            from_balance = 0;
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            ZJC_ERROR("leader balance error: %llu, %llu",
                from_balance, gas_used * block_tx.gas_price());
        }
    } else {
        if (from_balance >= gas_used * block_tx.gas_price()) {
                from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    }

    block_tx.set_gas_used(gas_used);
    if (block_tx.status() == kConsensusSuccess) {
        block_tx.set_contract_code(zjc_host.create_bytes_code_);
    }

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ZJC_DEBUG("user success call create contract.");
    return kConsensusSuccess;
}

int FromTxItem::CreateContractCallExcute(
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
