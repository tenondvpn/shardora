#include "consensus/zbft/contract_call.h"

#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ContractCall::HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume from 's prepayment
    auto btime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("contract called now.");
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    auto preppayment_id = block_tx.to() + block_tx.from();
    auto res = GetTempAccountBalance(zjc_host, preppayment_id, acc_balance_map, &from_balance, &from_nonce);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }

    uint64_t src_to_balance = 0;
    uint64_t src_to_nonce = 0;
    res = GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &src_to_balance, &src_to_nonce);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }
    
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

        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // assert(false);
            break;
        }

        if (block_tx.amount() >= from_balance) {
            block_tx.set_status(kConsensusOutOfPrepayment);
            SHARDORA_WARN("prepayent invalid user: %s, prepayment: %lu, contract: %s,"
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
        InitHost(zjc_host, block_tx, gas_limit, block_tx.gas_price(), view_block);
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
            SHARDORA_DEBUG("now call contract address: %s, bytes: %s", 
                common::Encode::HexEncode(address_info->addr()).c_str(), 
                common::Encode::HexEncode(address_info->bytes_code()).c_str());
            int call_res = ContractExcute(address_info, new_contract_balance, zjc_host, block_tx, gas_limit, &res);
            if (call_res != kConsensusSuccess || res.status_code != EVMC_SUCCESS) {
                block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
                SHARDORA_DEBUG("call contract failed, call_res: %d, evmc res: %d, gas_limit: %lu, bytes: %s, input: %s!",
                    call_res, (int32_t)res.status_code, gas_limit, "common::Encode::HexEncode(address_info->bytes_code()).c_str()",
                    common::Encode::HexEncode(block_tx.contract_input()).c_str());
            }

            gas_used += gas_limit - res.gas_left;
            if (res.gas_left > (int64_t)gas_limit) {
                gas_used = gas_limit;
            }
        }
        

        if (from_balance > gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
            gas_used += (tx_info->key().size() + tx_info->value().size()) *
                consensus::kKeyValueStorageEachBytes;
            if (gas_limit < gas_used) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu",
                    from_balance, gas_limit, gas_used);
            }
        } else {
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            SHARDORA_ERROR("leader balance error: %llu, %llu",
                from_balance, gas_used * block_tx.gas_price());
            from_balance = 0;
        }

        int64_t tmp_from_balance = from_balance;
        if (block_tx.status() == kConsensusSuccess) {
            int64_t dec_amount = block_tx.amount() + gas_used * block_tx.gas_price();
            if (tmp_from_balance >= int64_t(gas_used * block_tx.gas_price())) {
                if (tmp_from_balance < dec_amount) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    SHARDORA_ERROR("leader balance error: %llu, %llu", tmp_from_balance, dec_amount);
                }
            } else {
                tmp_from_balance = 0;
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("leader balance error: %llu, %llu",
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
            contract_balance_add = 0;
            int64_t gas_more = zjc_host.gas_more_;
            int res = SaveContractCreateInfo(
                zjc_host,
                block_tx,
                contract_balance_add);
            gas_used += gas_more;
            do {
                if (res != kConsensusSuccess) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    break;
                }

                if (gas_used > gas_limit) {
                    block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                    SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu",
                        tmp_from_balance, gas_limit, gas_more);
                    break;
                }

                if (tmp_from_balance < int64_t(gas_used * block_tx.gas_price())) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    SHARDORA_ERROR("balance error: %llu, %llu",
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
                    SHARDORA_ERROR("leader balance error: %llu, %llu",
                        tmp_from_balance, caller_balance_add);
                    break;
                }

                tmp_from_balance -= dec_amount;
                if (new_contract_balance < -contract_balance_add) {
                    block_tx.set_status(consensus::kConsensusAccountBalanceError);
                    SHARDORA_ERROR("to balance error: %llu, %llu",
                        new_contract_balance, contract_balance_add);
                    break;
                }
                
                // change contract 's amount, now is contract 's new balance
                new_contract_balance += contract_balance_add;
                if (zjc_host.recorded_selfdestructs_ != nullptr && new_contract_balance > 0) {
                    std::string destruct_from = std::string(
                        (char*)zjc_host.recorded_selfdestructs_->selfdestructed.bytes,
                        sizeof(zjc_host.recorded_selfdestructs_->selfdestructed.bytes));
                    std::string destruct_to = std::string(
                        (char*)zjc_host.recorded_selfdestructs_->beneficiary.bytes,
                        sizeof(zjc_host.recorded_selfdestructs_->beneficiary.bytes));
                    if (destruct_from != block_tx.to() || destruct_from == destruct_to) {
                        block_tx.set_status(consensus::kConsensusAccountBalanceError);
                        SHARDORA_ERROR("self destruct error not equal: %s, %s, beneficiary: %s",
                            common::Encode::HexEncode(destruct_from).c_str(),
                            common::Encode::HexEncode(block_tx.to()).c_str(),
                            common::Encode::HexEncode(destruct_to).c_str());
                        break;
                    }

                    auto iter = cross_to_map_.find(destruct_to);
                    std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
                    if (iter == cross_to_map_.end()) {
                        to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                        to_item_ptr->set_from(destruct_from);
                        to_item_ptr->set_des(destruct_to);
                        to_item_ptr->set_amount(new_contract_balance);
                        cross_to_map_[to_item_ptr->des()] = to_item_ptr;
                    } else {
                        to_item_ptr = iter->second;
                        to_item_ptr->set_amount(new_contract_balance + to_item_ptr->amount());
                    }

                    SHARDORA_ERROR("self destruct success nonce: %lu, %s, %s, "
                        "beneficiary: %s, amount: %lu, status: %d",
                        block_tx.nonce(),
                        common::Encode::HexEncode(destruct_from).c_str(),
                        common::Encode::HexEncode(block_tx.to()).c_str(),
                        common::Encode::HexEncode(destruct_to).c_str(),
                        new_contract_balance,
                        block_tx.status());
                    new_contract_balance = 0;
                    acc_balance_map[block_tx.to()]->set_balance(0);
                    acc_balance_map[block_tx.to()]->set_destructed(true);
                }

            } while (0);
        }

        if (block_tx.status() == kConsensusSuccess) {
            from_balance = tmp_from_balance;
            if (!acc_balance_map[block_tx.to()]->destructed()) {
                acc_balance_map[block_tx.to()]->set_balance(block_tx.amount());
                acc_balance_map[block_tx.to()]->set_nonce(0);
            }
        }

        if (block_tx.contract_input().size() < protos::kContractBytesStartCode.size()) {
            if (from_balance > 0) {
                auto iter = cross_to_map_.find(block_tx.from());
                std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
                if (iter == cross_to_map_.end()) {
                    to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                    to_item_ptr->set_from(block_tx.to());
                    to_item_ptr->set_des(block_tx.from());
                    to_item_ptr->set_amount(from_balance);
                    cross_to_map_[to_item_ptr->des()] = to_item_ptr;
                } else {
                    to_item_ptr = iter->second;
                    to_item_ptr->set_amount(from_balance + to_item_ptr->amount());
                }
                
                from_balance = 0;
            }
        }
    }

    if (block_tx.status() == kConsensusSuccess) {
        block_tx.set_amount(new_contract_balance);
    } else {
        block_tx.set_amount(src_to_balance);
    }
    
    // must prepayment's nonce, not caller or contract
    acc_balance_map[preppayment_id]->set_balance(from_balance);
    acc_balance_map[preppayment_id]->set_nonce(block_tx.nonce());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ADD_TX_DEBUG_INFO((&block_tx));
    auto etime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("contract nonce %lu, to: %s, user: %s, test_from_balance: %lu, prepament: %lu, "
        "gas used: %lu, gas_price: %lu, status: %d, step: %d, "
        "amount: %ld, to_balance: %ld, contract_balance_add: %ld, "
        "contract new balance: %lu, use time: %lu",
        block_tx.nonce(),
        common::Encode::HexEncode(block_tx.to()).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        test_from_balance,
        from_balance,
        gas_used,
        block_tx.gas_price(),
        block_tx.status(),
        (int32_t)block_tx.step(),
        block_tx.amount(),
        src_to_balance,
        contract_balance_add,
        new_contract_balance,
        (etime - btime));
    if (block_tx.status() == kConsensusSuccess) {
        for (auto exists_iter = cross_to_map_.begin(); exists_iter != cross_to_map_.end(); ++exists_iter) {
            auto iter = zjc_host.cross_to_map_.find(exists_iter->first);
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == zjc_host.cross_to_map_.end()) {
                zjc_host.cross_to_map_[exists_iter->first] = exists_iter->second;
            } else {
                to_item_ptr = iter->second;
                to_item_ptr->set_amount(exists_iter->second->amount() + to_item_ptr->amount());
            }
        }
    }

    return kConsensusSuccess;
}

int ContractCall::SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& block_tx,
        int64_t& contract_balance_add) {
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
            auto iter = cross_to_map_.find(to_iter->first);
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == cross_to_map_.end()) {
                to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                to_item_ptr->set_from(transfer_iter->first);
                to_item_ptr->set_des(to_iter->first);
                to_item_ptr->set_amount(to_iter->second);
                cross_to_map_[to_item_ptr->des()] = to_item_ptr;
            } else {
                to_item_ptr = iter->second;
                to_item_ptr->set_amount(to_iter->second + to_item_ptr->amount());
            }
            
            other_add += to_iter->second;
            SHARDORA_DEBUG("contract call transfer nonce: %lu, from: %s, to: %s, amount: %lu, contract_balance_add: %ld",
                block_tx.nonce(),
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

    SHARDORA_DEBUG("user success call contract.");
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
        SHARDORA_ERROR("ContractExcute failed: %d, bytes: %s, input: %s",
            exec_res, common::Encode::HexEncode(contract_info->bytes_code()).c_str(),
            common::Encode::HexEncode(tx.contract_input()).c_str());
        assert(false);
        return kConsensusError;
    }

    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
