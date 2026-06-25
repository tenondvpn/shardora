#include "consensus/zbft/contract_call.h"

#include "common/defer.h"
#include "shardoravm/execution.h"

namespace shardora {

namespace consensus {

int ContractCall::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& pre_shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume from 's prefund
    auto btime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("contract called now.");
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    auto preppayment_id = block_tx.to() + block_tx.from();
    auto res = GetTempAccountBalance(pre_shardora_host, preppayment_id, acc_balance_map, &from_balance, &from_nonce);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }

    uint64_t src_to_balance = 0;
    uint64_t src_to_nonce = 0;
    res = GetTempAccountBalance(pre_shardora_host, block_tx.to(), acc_balance_map, &src_to_balance, &src_to_nonce);
    if (res != kConsensusSuccess) {
        return kConsensusError;
    }
    
    int64_t new_contract_balance = static_cast<int64_t>(src_to_balance);
    uint64_t test_from_balance = from_balance;
    bool check_valid = false;
    // Intrinsic gas: base (21000) + calldata bytes (EIP-2028: 16 per non-zero, 4 per zero byte)
    auto gas_used = kCallContractDefaultUseGas
                    + CalcCalldataGas(block_tx.contract_input());
    int64_t contract_balance_add = 0;
    auto gas_limit = block_tx.gas_limit();
    shardoravm::ShardorahainHost shardora_host;
    shardora_host.view_block_chain_ = pre_shardora_host.view_block_chain_;
    shardora_host.tx_context_ = pre_shardora_host.tx_context_;
    shardora_host.pre_shardora_host_ = &pre_shardora_host;
    do {
        if (address_info->destructed()) {
            block_tx.set_status(kConsensusContractDestructed);
            // //assert(false);
            break;
        }

        if (from_balance <= gas_used * block_tx.gas_price() + block_tx.amount()) {
            block_tx.set_status(kConsensusAccountBalanceError);
            // //assert(false);
            break;
        }

        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // //assert(false);
            break;
        }

        if (block_tx.amount() >= from_balance) {
            block_tx.set_status(kConsensusOutOfPrefund);
            SHARDORA_WARN("prefundent invalid user: %s, prefund: %lu, contract: %s,"
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
            block_tx.set_status(kConsensusOutOfPrefund);
            break;
        }

        gas_limit -= kCallContractDefaultUseGas;
        check_valid = true;
    } while (0);

    evmc_result call_contract_res = {};
    evmc::Result evmc_res{ call_contract_res };
    hotstuff::BalanceAndNonceMap dep_contract_balance_map;
    if (!check_valid) {
        if (from_balance >= gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    } else {
        new_contract_balance += block_tx.amount();
        InitHost(shardora_host, block_tx, gas_limit, block_tx.gas_price(), view_block);
        // user caller prefund 's gas
        shardora_host.AddTmpAccountBalance(
            block_tx.from(),
            from_balance);
        shardora_host.AddTmpAccountBalance(
            block_tx.to(),
            new_contract_balance);
        if (block_tx.contract_input().size() >= protos::kContractBytesStartCode.size()) {
            SHARDORA_DEBUG("now call contract address: %s, bytes: %s", 
                common::Encode::HexEncode(address_info->addr()).c_str(), 
                common::Encode::HexEncode(address_info->bytes_code()).c_str());
            auto evm_begin_us = common::TimeUtils::TimestampUs();
            int call_res = ContractExcute(address_info, new_contract_balance, shardora_host, block_tx, gas_limit, &evmc_res);
            auto evm_end_us = common::TimeUtils::TimestampUs();
            auto evm_elapsed_us = evm_end_us - evm_begin_us;
            if (evm_elapsed_us > 1000) {  // log if > 1ms
                SHARDORA_WARN("ContractExcute slow: %lu us, tx_idx: %d, contract: %s, "
                    "input_size: %lu, status: %d, gas_used: %lu",
                    evm_elapsed_us, tx_index,
                    common::Encode::HexEncode(address_info->addr()).c_str(),
                    block_tx.contract_input().size(),
                    (int32_t)evmc_res.status_code,
                    (evmc_res.gas_left > (int64_t)gas_limit) ? gas_limit : (gas_limit - evmc_res.gas_left));
            }
            if (call_res != kConsensusSuccess || evmc_res.status_code != EVMC_SUCCESS) {
                block_tx.set_status(EvmcStatusToZbftStatus(evmc_res.status_code));
                SHARDORA_DEBUG("call contract failed, call_res: %d, evmc res: %d, gas_limit: %lu, bytes: %s, input: %s!",
                    call_res, (int32_t)evmc_res.status_code, gas_limit, "common::Encode::HexEncode(address_info->bytes_code()).c_str()",
                    common::Encode::HexEncode(block_tx.contract_input()).c_str());
            }

            if (evmc_res.gas_left > (int64_t)gas_limit) {
                gas_used = gas_limit;
            } else {
                gas_used += gas_limit - evmc_res.gas_left;
            }
        }

        if (from_balance > gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
            gas_used += consensus::CalcKvStorageGas(
                tx_info->key().size(), tx_info->value().size(), true);
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
            int64_t gas_more = shardora_host.gas_more_;
            int res = SaveContractCreateInfo(
                shardora_host,
                block_tx,
                dep_contract_balance_map,
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
                    static_cast<int64_t>(block_tx.contract_prefund()) +
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
                if (shardora_host.recorded_selfdestructs_ != nullptr && new_contract_balance > 0) {
                    std::string destruct_from = std::string(
                        (char*)shardora_host.recorded_selfdestructs_->selfdestructed.bytes,
                        sizeof(shardora_host.recorded_selfdestructs_->selfdestructed.bytes));
                    std::string destruct_to = std::string(
                        (char*)shardora_host.recorded_selfdestructs_->beneficiary.bytes,
                        sizeof(shardora_host.recorded_selfdestructs_->beneficiary.bytes));
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

    // if (!acc_balance_map[block_tx.to()]->destructed()) {
        acc_balance_map[block_tx.to()]->set_nonce(0);
        acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
        acc_balance_map[block_tx.to()]->set_tx_index(tx_index);
        if (block_tx.status() == kConsensusSuccess) {
            acc_balance_map[block_tx.to()]->set_balance(new_contract_balance);
        } else {
            acc_balance_map[block_tx.to()]->set_balance(src_to_balance);
        }
    // }
    
    // must prefund's nonce, not caller or contract
    acc_balance_map[preppayment_id]->set_balance(from_balance);
    acc_balance_map[preppayment_id]->set_nonce(block_tx.nonce());
    acc_balance_map[preppayment_id]->set_latest_height(view_block.block_info().height());
    acc_balance_map[preppayment_id]->set_tx_index(tx_index);
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ADD_TX_DEBUG_INFO((&block_tx));
    auto etime = common::TimeUtils::TimestampMs();
    auto handle_tx_elapsed = etime - btime;
    if (handle_tx_elapsed > 5) {  // log if HandleTx > 5ms
        SHARDORA_WARN("ContractCall::HandleTx slow: %lu ms, tx_idx: %d, nonce: %lu, "
            "contract: %s, caller: %s, status: %d, gas_used: %lu",
            handle_tx_elapsed, tx_index, block_tx.nonce(),
            common::Encode::HexEncode(block_tx.to()).c_str(),
            common::Encode::HexEncode(block_tx.from()).c_str(),
            block_tx.status(), gas_used);
    }
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

     for (auto event_iter = shardora_host.recorded_logs_.begin();
            event_iter != shardora_host.recorded_logs_.end(); ++event_iter) {
        auto log = block_tx.add_events();
        log->set_data((*event_iter).data);
        for (auto topic_iter = (*event_iter).topics.begin();
                topic_iter != (*event_iter).topics.end(); ++topic_iter) {
            log->add_topics(std::string((char*)(*topic_iter).bytes, sizeof((*topic_iter).bytes)));
        }
    }

    block::protobuf::TxHashStatus tx_hash_status;
    *tx_hash_status.mutable_events() = block_tx.events();
    if (check_valid) {
        tx_hash_status.set_status(kConsensusSuccess);
        if (evmc_res.status_code != EVMC_SUCCESS) {
            tx_hash_status.set_status(evmc_res.status_code);
        } 
        
        if (block_tx.status() != kConsensusSuccess) {
            tx_hash_status.set_status(block_tx.status());
        }
            
        const auto evmc_output = SafeEvmcOutput(evmc_res.raw());
        tx_hash_status.set_output(evmc_output);
        block_tx.set_output(evmc_output);
    } else {
        tx_hash_status.set_status(block_tx.status());
    }

    auto status_val = tx_hash_status.SerializeAsString();
    SHARDORA_DEBUG("call contract status: %d, rel: %d, txhash: %s, output: %s, from: %s, to: %s", 
        (int32_t)evmc_res.status_code, 
        tx_hash_status.status(),
        common::Encode::HexEncode(block_tx.tx_hash()).c_str(),
        ProtobufToJson(tx_hash_status).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        common::Encode::HexEncode(block_tx.to()).c_str());
    if (block_tx.status() == kConsensusSuccess) {
        for (auto iter = dep_contract_balance_map.begin(); iter != dep_contract_balance_map.end(); ++iter) {
            acc_balance_map[iter->first] = iter->second;
        }

        for (auto iter = shardora_host.create2_accounts_.begin();
                iter != shardora_host.create2_accounts_.end(); ++iter) {
            auto contract_info = std::make_shared<address::protobuf::AddressInfo>();
            auto id = std::string((char*)iter->first.bytes, sizeof(iter->first.bytes));
            contract_info->set_addr(id);
            contract_info->set_balance(shardoravm::EvmcBytes32ToUint64(iter->second.balance));
            contract_info->set_sharding_id(view_block.qc().network_id());
            contract_info->set_pool_index(view_block.qc().pool_index());
            contract_info->set_type(address::protobuf::kNormal);
            contract_info->set_bytes_code(
                reinterpret_cast<const void*>(iter->second.code.data()), 
                iter->second.code.size());
            contract_info->set_latest_height(view_block.block_info().height());
            contract_info->set_tx_index(tx_index);
            contract_info->set_nonce(0);
            acc_balance_map[id] = contract_info;
        }

        shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
        shardora_host.MergeToPrev();
        for (auto exists_iter = cross_to_map_.begin(); exists_iter != cross_to_map_.end(); ++exists_iter) {
            auto iter = pre_shardora_host.cross_to_map_.find(exists_iter->first);
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == pre_shardora_host.cross_to_map_.end()) {
                pre_shardora_host.cross_to_map_[exists_iter->first] = exists_iter->second;
            } else {
                to_item_ptr = iter->second;
                to_item_ptr->set_amount(exists_iter->second->amount() + to_item_ptr->amount());
            }
        }
    } else {
        pre_shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    }

    return kConsensusSuccess;
}

int ContractCall::SaveContractCreateInfo(
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& block_tx,
        hotstuff::BalanceAndNonceMap& dep_contract_balance_map,
        int64_t& contract_balance_add) {
    int64_t other_add = 0;
    for (auto transfer_iter = shardora_host.to_account_value_.begin();
            transfer_iter != shardora_host.to_account_value_.end(); ++transfer_iter) {
        // transfer from must caller or contract address, other not allowed.
        // if (transfer_iter->first != block_tx.from() && transfer_iter->first != block_tx.to()) {
        //     //assert(false);
        //     return kConsensusError;
        // }

        for (auto to_iter = transfer_iter->second.begin();
                to_iter != transfer_iter->second.end(); ++to_iter) {
            if (transfer_iter->first == to_iter->first) {
                //assert(false);
                return kConsensusError;
            }

            if (block_tx.to() != transfer_iter->first) {
                auto addr_info = shardora_host.view_block_chain_->ChainGetAccountInfo(to_iter->first);
                if (addr_info == nullptr) {
                    //assert(false);
                    return kConsensusError;
                }

                if (addr_info->destructed()) {
                    //assert(false);
                    return kConsensusError;
                }

                if (addr_info->pool_index() != shardora_host.view_block_chain_->pool_index()) {
                    //assert(false);
                    return kConsensusError;
                }

                if (addr_info->balance() < to_iter->second) {
                    //assert(false);
                    return kConsensusError;
                }

                if (addr_info->bytes_code().empty()) {
                    //assert(false);
                    return kConsensusError;
                }

                addr_info->set_balance(addr_info->balance() - to_iter->second);
                dep_contract_balance_map[addr_info->addr()] = addr_info;
            } else {
                contract_balance_add -= to_iter->second;
                other_add += to_iter->second;
            }

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
            
            SHARDORA_DEBUG("contract call transfer nonce: %lu, from: %s, to: %s, amount: %lu, contract_balance_add: %ld",
                block_tx.nonce(),
                common::Encode::HexEncode(transfer_iter->first).c_str(),
                common::Encode::HexEncode(to_iter->first).c_str(),
                to_iter->second,
                contract_balance_add);
        }
    }

    if (contract_balance_add > 0) {
        //assert(false);
        return kConsensusError;
    }

    if (-contract_balance_add != other_add) {
        //assert(false);
        return kConsensusError;
    }

    SHARDORA_DEBUG("user success call contract.");
    return kConsensusSuccess;
}

int ContractCall::ContractExcute(
        protos::AddressInfoPtr& contract_info,
        uint64_t contract_balance,
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& tx,
        uint64_t gas_limit,
        evmc::Result* out_res) {
    int exec_res = shardoravm::Execution::Instance()->execute(
        contract_info->bytes_code(),
        tx.contract_input(),
        tx.from(),
        tx.to(),
        tx.from(),
        tx.amount(),
        gas_limit,
        0,
        shardoravm::kJustCall,
        shardora_host,
        out_res);
    if (exec_res != shardoravm::kShardoravmSuccess) {
        SHARDORA_ERROR("ContractExcute failed: %d, bytes: %s, input: %s",
            exec_res, common::Encode::HexEncode(contract_info->bytes_code()).c_str(),
            common::Encode::HexEncode(tx.contract_input()).c_str());
        // //assert(false);
        return kConsensusError;
    }

    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
