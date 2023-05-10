#include "consensus/zbft/contract_user_create_call.h"

#include "contract/contract_manager.h"
#include "zjcvm/execution.h"

namespace zjchain {

namespace consensus {

int ContractUserCreateCall::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // contract create call
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

    if (block_tx.gas_price() * block_tx.gas_limit() > from_balance) {
        block_tx.set_status(kConsensusOutOfGas);
        return kConsensusSuccess;
    }

    if (block_tx.gas_price() * block_tx.gas_limit() + block_tx.contract_prepayment() > from_balance) {
        block_tx.set_status(kConsensusAccountBalanceError);
        return kConsensusSuccess;
    }

    block::protobuf::BlockTx contract_tx;
    zjcvm::ZjchainHost zjc_host;
    zjc_host.acc_mgr_ = account_mgr_;
    zjc_host.contract_mgr_ = contract_mgr_;
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
    int call_res = CreateContractCallExcute(zjc_host, block_tx, &res);
    if (call_res != kConsensusSuccess || res.status_code != EVMC_SUCCESS) {
        block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
        ZJC_DEBUG("create contract: %s failed, call_res: %d, evmc res: %d!",
            common::Encode::HexEncode(block_tx.to()).c_str(),
            call_res, res.status_code);
    }

    auto gas_used = block_tx.gas_limit() - res.gas_left;
    if (res.gas_left > (int64_t)block_tx.gas_limit()) {
        gas_used = block_tx.gas_limit();
    }

    if (from_balance > gas_used * block_tx.gas_price()) {
        from_balance -= gas_used * block_tx.gas_price();
        gas_used = 0;
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            // TODO(): check key exists and reserve gas
            gas_used += (block_tx.storages(i).key().size() + msg_ptr->header.tx_proto().value().size()) *
                consensus::kKeyValueStorageEachBytes;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
        }
    } else {
        block_tx.set_status(consensus::kConsensusAccountBalanceError);
        ZJC_ERROR("leader balance error: %llu, %llu", from_balance, gas_used * block_tx.gas_price());
        from_balance = 0;
    }

    int64_t tmp_from_balance = from_balance;
    if (block_tx.status() == kConsensusSuccess) {
        int64_t dec_amount = block_tx.amount() +
            block_tx.contract_prepayment() +
            gas_used * block_tx.gas_price();
        if (tmp_from_balance >= int64_t(gas_used * block_tx.gas_price())) {
            if (tmp_from_balance < dec_amount) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu", tmp_from_balance, dec_amount);
            }
        } else {
            tmp_from_balance = 0;
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            ZJC_ERROR("leader balance error: %llu, %llu",
                tmp_from_balance, gas_used * block_tx.gas_price());
        }
    } else {
        if (tmp_from_balance >= int64_t(gas_used * block_tx.gas_price())) {
            tmp_from_balance -= gas_used * block_tx.gas_price();
        } else {
            tmp_from_balance = 0;
        }
    }

    if (block_tx.status() == kConsensusSuccess) {
        int64_t contract_balance_add = 0;
        int64_t caller_balance_add = 0;
        int64_t gas_more = 0;
        int res = SaveContractCreateInfo(
            zjc_host,
            block_tx,
            db_batch,
            contract_balance_add,
            caller_balance_add,
            gas_more);
        gas_used += gas_more;
        do {
            if (res != kConsensusSuccess) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                break;
            }

            if (gas_used > block_tx.gas_limit()) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                ZJC_DEBUG("1 balance error: %lu, %lu, %lu", tmp_from_balance, block_tx.gas_limit(), gas_more);
                break;
            }

            if (tmp_from_balance < int64_t(gas_used * block_tx.gas_price())) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("balance error: %llu, %llu", tmp_from_balance, gas_more * block_tx.gas_price());
                break;
            }

            // just dec caller_balance_add
            int64_t dec_amount = static_cast<int64_t>(block_tx.amount()) -
                caller_balance_add +
                static_cast<int64_t>(block_tx.contract_prepayment()) +
                static_cast<int64_t>(gas_used * block_tx.gas_price());
            if ((int64_t)tmp_from_balance < dec_amount) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu", tmp_from_balance, caller_balance_add);
                break;
            }

            tmp_from_balance -= dec_amount;
            // change contract create amount
            block_tx.set_amount(static_cast<int64_t>(block_tx.amount()) + contract_balance_add);
        } while (0);
    }

    if (block_tx.status() == kConsensusSuccess) {
        from_balance = tmp_from_balance;
    }

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    return kConsensusSuccess;
}

int ContractUserCreateCall::SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& block_tx,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add,
        int64_t& gas_more) {
    auto storage = block_tx.add_storages();
    storage->set_key(protos::kCreateContractBytesCode);
    storage->set_val_hash(zjc_host.create_bytes_code_);
    for (auto account_iter = zjc_host.accounts_.begin();
            account_iter != zjc_host.accounts_.end(); ++account_iter) {
        for (auto storage_iter = account_iter->second.storage.begin();
            storage_iter != account_iter->second.storage.end(); ++storage_iter) {
//             prefix_db_->SaveAddressStorage(
//                 account_iter->first,
//                 storage_iter->first,
//                 storage_iter->second.value,
//                 *db_batch);
            auto kv = block_tx.add_storages();
            kv->set_key(account_iter->first + storage_iter->first);
            kv->set_val_hash(std::string(
                (char*)storage_iter->second.value.bytes,
                sizeof(storage_iter->second.value.bytes)));
            gas_more += (sizeof(account_iter->first.bytes) +
                sizeof(storage_iter->first.bytes) +
                sizeof(storage_iter->second.value.bytes)) *
                consensus::kKeyValueStorageEachBytes;
        }

        for (auto storage_iter = account_iter->second.str_storage.begin();
                storage_iter != account_iter->second.str_storage.end(); ++storage_iter) {
//             prefix_db_->SaveAddressStringStorage(
//                 account_iter->first,
//                 storage_iter->first,
//                 storage_iter->second.str_val,
//                 *db_batch);
            auto kv = block_tx.add_storages();
            kv->set_key(account_iter->first + storage_iter->first);
            if (storage_iter->second.str_val.size() > 32) {
                kv->set_val_hash(common::Hash::keccak256(storage_iter->second.str_val));
                kv->set_val_size(storage_iter->second.str_val.size());
                prefix_db_->SaveTemporaryKv(kv->val_hash(), storage_iter->second.str_val);
            } else {
                kv->set_val_hash(storage_iter->second.str_val);
            }

            gas_more += (sizeof(account_iter->first.bytes) +
                storage_iter->first.size() +
                storage_iter->second.str_val.size()) *
                consensus::kKeyValueStorageEachBytes;
        }
    }

    int64_t other_add = 0;
    for (auto transfer_iter = zjc_host.to_account_value_.begin();
            transfer_iter != zjc_host.to_account_value_.end(); ++transfer_iter) {
        // transfer from must caller or contract address, other not allowed.
        if (transfer_iter->first != block_tx.from() && transfer_iter->first != block_tx.to()) {
            return kConsensusError;
        }

        for (auto to_iter = transfer_iter->second.begin();
                to_iter != transfer_iter->second.end(); ++to_iter) {
            if (transfer_iter->first == to_iter->first) {
                return kConsensusError;
            }

            if (block_tx.to() == transfer_iter->first) {
                contract_balance_add -= to_iter->second;
            }

            if (block_tx.to() == to_iter->first) {
                contract_balance_add += to_iter->second;
            }

            if (block_tx.from() == transfer_iter->first) {
                caller_balance_add -= to_iter->second;
            }

            if (block_tx.from() == to_iter->first) {
                caller_balance_add += to_iter->second;
            }

            if (to_iter->first != block_tx.to() && to_iter->first != block_tx.from()) {
                // from and contract itself transfers direct
                // transfer to other address by cross sharding transfer
                auto trans_item = block_tx.add_contract_txs();
                trans_item->set_from(transfer_iter->first);
                trans_item->set_to(to_iter->first);
                trans_item->set_amount(to_iter->second);
                other_add += to_iter->second;
            }
        }
    }

    if (caller_balance_add > 0 && contract_balance_add > 0) {
        return kConsensusError;
    }

    if (contract_balance_add > 0) {
        if (other_add + contract_balance_add != -caller_balance_add) {
            return kConsensusError;
        }
    } else {
        if (int64_t(block_tx.amount()) < -contract_balance_add) {
            return kConsensusError;
        }

        if (caller_balance_add > 0) {
            if (other_add + caller_balance_add != -contract_balance_add) {
                return kConsensusError;
            }
        } else {
            if (-(contract_balance_add + caller_balance_add) != other_add) {
                return kConsensusError;
            }
        }
    }

    ZJC_DEBUG("user success call create contract.");
    return kConsensusSuccess;
}

int ContractUserCreateCall::CreateContractCallExcute(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        evmc::Result* out_res) {
    uint32_t call_mode = zjcvm::kJustCreate;
    if (tx.has_contract_input() && !tx.contract_input().empty()) {
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
