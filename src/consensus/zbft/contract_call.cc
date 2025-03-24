#include "consensus/zbft/contract_call.h"

#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ContractCall::HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume from 's prepayment
    auto btime = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("contract called now.");
    uint64_t from_balance = 0;
    auto preppayment_id = block_tx.to() + block_tx.from();
    GetTempAccountBalance(preppayment_id, acc_balance_map, &from_balance);
    uint64_t src_to_balance = 0;
    GetTempAccountBalance(block_tx.to(), acc_balance_map, &src_to_balance);
    int64_t new_contract_balance = static_cast<int64_t>(src_to_balance);
    uint64_t test_from_balance = from_balance;
    bool check_valid = false;
    auto gas_used = kCallContractDefaultUseGas;
    int64_t contract_balance_add = 0;
    auto gas_limit = block_tx.gas_limit();
    do {
        if (from_balance <= kCallContractDefaultUseGas * block_tx.gas_price() + block_tx.amount()) {
            block_tx.set_status(kConsensusOutOfGas);
            // assert(false);
            break;
        }

        if (block_tx.amount() >= from_balance) {
            block_tx.set_status(kConsensusOutOfPrepayment);
            ZJC_WARN("prepayent invalid user: %s, prepayment: %lu, contract: %s,"
                "amount: %lu, gas limit: %lu, gas price: %lu",
                common::Encode::HexEncode(block_tx.from()).c_str(),
                from_balance,
                common::Encode::HexEncode(block_tx.to()).c_str(),
                block_tx.amount(), gas_limit, block_tx.gas_price());
            break;
        }
    
        if (block_tx.gas_price() * gas_limit + block_tx.amount() > from_balance) {
            gas_limit = (from_balance - block_tx.amount()) / block_tx.gas_price();
        }

        if (kCallContractDefaultUseGas > gas_limit) {
            block_tx.set_status(kConsensusOutOfPrepayment);
            break;
        }
    
        

        gas_limit -= kCallContractDefaultUseGas;
        check_valid = true;
    } while (0);

    if (!check_valid) {
        if (from_balance >= gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    } else {
        new_contract_balance += block_tx.amount();
        block::protobuf::BlockTx contract_tx;
        zjcvm::Uint64ToEvmcBytes32(
            zjc_host.tx_context_.tx_gas_price,
            block_tx.gas_price());
        zjc_host.contract_mgr_ = contract_mgr_;
        zjc_host.acc_mgr_ = account_mgr_;
        zjc_host.my_address_ = block_tx.to();
        zjc_host.tx_context_.block_gas_limit = gas_limit;
        // user caller prepayment 's gas
        zjc_host.AddTmpAccountBalance(
            block_tx.from(),
            from_balance);
        zjc_host.AddTmpAccountBalance(
            block_tx.to(),
            new_contract_balance);
        if (block_tx.contract_input().size() >= protos::kContractBytesStartCode.size()) {
            evmc_result evmc_res = {};
            evmc::Result res{ evmc_res };
            int call_res = ContractExcute(address_info, new_contract_balance, zjc_host, block_tx, gas_limit, &res);
            if (call_res != kConsensusSuccess || res.status_code != EVMC_SUCCESS) {
                block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
                ZJC_DEBUG("call contract failed, call_res: %d, evmc res: %d, gas_limit: %lu, bytes: %s, input: %s!",
                    call_res, res.status_code, gas_limit, "common::Encode::HexEncode(address_info->bytes_code()).c_str()",
                    common::Encode::HexEncode(block_tx.contract_input()).c_str());
            }

            gas_used += gas_limit - res.gas_left;
            if (res.gas_left > (int64_t)gas_limit) {
                gas_used = gas_limit;
            }
        }
        

        if (from_balance > gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
            for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                // TODO(): check key exists and reserve gas
                gas_used += (block_tx.storages(i).key().size() + tx_info->value().size()) *
                    consensus::kKeyValueStorageEachBytes;
            }

            if (gas_limit < gas_used) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                ZJC_DEBUG("1 balance error: %lu, %lu, %lu",
                    from_balance, gas_limit, gas_used);
            }
        } else {
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            ZJC_ERROR("leader balance error: %llu, %llu",
                from_balance, gas_used * block_tx.gas_price());
            from_balance = 0;
        }

        int64_t tmp_from_balance = from_balance;
        if (block_tx.status() == kConsensusSuccess) {
            int64_t dec_amount = block_tx.amount() + gas_used * block_tx.gas_price();
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
            int64_t caller_balance_add = 0;
            int64_t gas_more = 0;
            int res = SaveContractCreateInfo(
                zjc_host,
                block_tx,
                contract_balance_add,
                caller_balance_add,
                gas_more);
            gas_used += gas_more;
            do {
                if (res != kConsensusSuccess) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    break;
                }

                if (gas_used > gas_limit) {
                    block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                    ZJC_DEBUG("1 balance error: %lu, %lu, %lu",
                        tmp_from_balance, gas_limit, gas_more);
                    break;
                }

                if (tmp_from_balance < int64_t(gas_used * block_tx.gas_price())) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    ZJC_ERROR("balance error: %llu, %llu",
                        tmp_from_balance, gas_more * block_tx.gas_price());
                    break;
                }

                // just dec caller_balance_add
                int64_t dec_amount = static_cast<int64_t>(block_tx.amount()) -
                    caller_balance_add +
                    static_cast<int64_t>(block_tx.contract_prepayment()) +
                    static_cast<int64_t>(gas_used * block_tx.gas_price());
                if ((int64_t)tmp_from_balance < dec_amount) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    ZJC_ERROR("leader balance error: %llu, %llu",
                        tmp_from_balance, caller_balance_add);
                    break;
                }

                tmp_from_balance -= dec_amount;
                if (new_contract_balance < -contract_balance_add) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    ZJC_ERROR("to balance error: %llu, %llu",
                        new_contract_balance, contract_balance_add);
                    break;
                }
                
                // change contract 's amount, now is contract 's new balance
                new_contract_balance += contract_balance_add;
                if (zjc_host.recorded_selfdestructs_ != nullptr && new_contract_balance > 0) {
                    auto trans_item = block_tx.add_contract_txs();
                    std::string destruct_from = std::string(
                        (char*)zjc_host.recorded_selfdestructs_->selfdestructed.bytes,
                        sizeof(zjc_host.recorded_selfdestructs_->selfdestructed.bytes));
                    std::string destruct_to = std::string(
                        (char*)zjc_host.recorded_selfdestructs_->beneficiary.bytes,
                        sizeof(zjc_host.recorded_selfdestructs_->beneficiary.bytes));
                    if (destruct_from != block_tx.to() || destruct_from == destruct_to) {
                        block_tx.set_status(consensus::kConsensusAccountBalanceError);
                        ZJC_ERROR("self destruct error not equal: %s, %s, beneficiary: %s",
                            common::Encode::HexEncode(destruct_from).c_str(),
                            common::Encode::HexEncode(block_tx.to()).c_str(),
                            common::Encode::HexEncode(destruct_to).c_str());
                        break;
                    }
                    
                    trans_item->set_from(destruct_from);
                    trans_item->set_to(destruct_to);
                    trans_item->set_amount(new_contract_balance);
                    new_contract_balance = 0;
                    ZJC_ERROR("self destruct success gid: %s, %s, %s, "
                        "beneficiary: %s, amount: %lu, status: %d",
                        common::Encode::HexEncode(block_tx.gid()).c_str(),
                        common::Encode::HexEncode(destruct_from).c_str(),
                        common::Encode::HexEncode(block_tx.to()).c_str(),
                        common::Encode::HexEncode(destruct_to).c_str(),
                        trans_item->amount(),
                        block_tx.status());
                    auto destruct_kv = block_tx.add_storages();
                    destruct_kv->set_key(protos::kContractDestruct);
                    ZJC_DEBUG("2 save storage to block tx prev storage key: %s",
                        common::Encode::HexEncode(protos::kContractDestruct).c_str());
                    acc_balance_map[block_tx.to()] = -1;
                    // zjc_host.SavePrevStorages(protos::kContractDestruct, "", true);
                }

            } while (0);
        }

        if (block_tx.status() == kConsensusSuccess) {
            from_balance = tmp_from_balance;
            if (acc_balance_map[block_tx.to()] != -1) {
                acc_balance_map[block_tx.to()] = block_tx.amount();
            }
        }

        if (block_tx.contract_input().size() < protos::kContractBytesStartCode.size()) {
            if (from_balance > 0) {
                auto trans_item = block_tx.add_contract_txs();
                trans_item->set_from(block_tx.to());
                trans_item->set_to(block_tx.from());
                trans_item->set_amount(from_balance);
                from_balance = 0;
            }
        }
    }

    if (block_tx.status() == kConsensusSuccess) {
        block_tx.set_amount(new_contract_balance);
    } else {
        block_tx.set_amount(src_to_balance);
    }
    
    acc_balance_map[preppayment_id] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ADD_TX_DEBUG_INFO((&block_tx));
    auto etime = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("contract gid %s, to: %s, user: %s, test_from_balance: %lu, prepament: %lu, "
        "gas used: %lu, gas_price: %lu, status: %d, step: %d, "
        "amount: %ld, to_balance: %ld, contract_balance_add: %ld, "
        "contract new balance: %lu, use time: %lu",
        common::Encode::HexEncode(block_tx.gid()).c_str(),
        common::Encode::HexEncode(block_tx.to()).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        test_from_balance,
        from_balance,
        gas_used,
        block_tx.gas_price(),
        block_tx.status(),
        block_tx.step(),
        block_tx.amount(),
        src_to_balance,
        contract_balance_add,
        new_contract_balance,
        (etime - btime));
    return kConsensusSuccess;
}

int ContractCall::SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& block_tx,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add,
        int64_t& gas_more) {
    // ZJC_DEBUG("now save contrat call storage acc size: %u", zjc_host.accounts_.size());
    for (auto account_iter = zjc_host.accounts_.begin();
            account_iter != zjc_host.accounts_.end(); ++account_iter) {
        // ZJC_DEBUG("now save contrat call storage acc: %s storage size: %u",
        //     common::Encode::HexEncode(std::string((char*)account_iter->first.bytes, 20)).c_str(), 
        //     account_iter->second.storage.size());
        for (auto storage_iter = account_iter->second.storage.begin();
                storage_iter != account_iter->second.storage.end(); ++storage_iter) {
            auto kv = block_tx.add_storages();
            auto str_key = std::string((char*)account_iter->first.bytes, sizeof(account_iter->first.bytes)) +
                std::string((char*)storage_iter->first.bytes, sizeof(storage_iter->first.bytes));
            kv->set_key(str_key);
            kv->set_value(std::string(
                (char*)storage_iter->second.value.bytes,
                sizeof(storage_iter->second.value.bytes)));
            // if (str_key.size() > 40)
            // ZJC_DEBUG("0 save storage to block tx prev storage key: %s, value: %s",
            //     common::Encode::HexEncode(str_key).c_str(),
            //     common::Encode::HexEncode(kv->value()).c_str());
            // zjc_host.SavePrevStorages(str_key, kv->value(), true);
            gas_more += (sizeof(account_iter->first.bytes) +
                sizeof(storage_iter->first.bytes) +
                sizeof(storage_iter->second.value.bytes)) *
                consensus::kKeyValueStorageEachBytes;
        }

        for (auto storage_iter = account_iter->second.str_storage.begin();
                storage_iter != account_iter->second.str_storage.end(); ++storage_iter) {
            auto kv = block_tx.add_storages();
            auto str_key = std::string(
                (char*)account_iter->first.bytes,
                sizeof(account_iter->first.bytes)) + storage_iter->first;
            kv->set_key(str_key);
            kv->set_value(storage_iter->second.str_val);
            // if (str_key.size() > 40)
            // ZJC_WARN("1 save storage to block tx prev storage key: %s, value: %s",
            //     common::Encode::HexEncode(str_key).c_str(),
            //     common::Encode::HexEncode(kv->value()).c_str());
            // zjc_host.SavePrevStorages(str_key, kv->value(), true);
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
            assert(false);
            return kConsensusError;
        }

        for (auto to_iter = transfer_iter->second.begin();
                to_iter != transfer_iter->second.end(); ++to_iter) {
            if (transfer_iter->first == to_iter->first) {
                assert(false);
                return kConsensusError;
            }

            if (block_tx.to() != transfer_iter->first) {
                assert(false);
                return kConsensusError;
            }

            contract_balance_add -= to_iter->second;
            // from and contract itself transfers direct
            // transfer to other address by cross sharding transfer
            auto trans_item = block_tx.add_contract_txs();
            trans_item->set_from(transfer_iter->first);
            trans_item->set_to(to_iter->first);
            trans_item->set_amount(to_iter->second);
            other_add += to_iter->second;
            ZJC_DEBUG("contract call transfer gid: %s, from: %s, to: %s, amount: %lu, contract_balance_add: %ld",
                common::Encode::HexEncode(block_tx.gid()).c_str(),
                common::Encode::HexEncode(transfer_iter->first).c_str(),
                common::Encode::HexEncode(to_iter->first).c_str(),
                to_iter->second,
                contract_balance_add);
        }
    }

    if (contract_balance_add > 0) {
        assert(false);
        return kConsensusError;
    }

    if (-contract_balance_add != other_add) {
        assert(false);
        return kConsensusError;
    }

    ZJC_DEBUG("user success call contract.");
    return kConsensusSuccess;
}

int ContractCall::ContractExcute(
        protos::AddressInfoPtr& contract_info,
        uint64_t contract_balance,
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        uint64_t gas_limit,
        evmc::Result* out_res) {
    int exec_res = zjcvm::Execution::Instance()->execute(
        contract_info->bytes_code(),
        tx.contract_input(),
        tx.from(),
        tx.to(),
        tx.from(),
        tx.amount(),
        gas_limit,
        0,
        zjcvm::kJustCall,
        zjc_host,
        out_res);
    if (exec_res != zjcvm::kZjcvmSuccess) {
        ZJC_ERROR("ContractExcute failed: %d, bytes: %s, input: %s",
            exec_res, common::Encode::HexEncode(contract_info->bytes_code()).c_str(),
            common::Encode::HexEncode(tx.contract_input()).c_str());
        assert(false);
        return kConsensusError;
    }

    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
