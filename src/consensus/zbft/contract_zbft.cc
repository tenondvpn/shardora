#include "consensus/zbft/contract_zbft.h"

#include "zjcvm/execution.h"

namespace zjchain {

namespace consensus {

ContractZbft::ContractZbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr)
        : Zbft(account_mgr, sec_ptr, bls_mgr, tx_ptr, pools_mgr, tm_block_mgr) {}

ContractZbft::~ContractZbft() {}

int ContractZbft::Init() {
    return kConsensusSuccess;
}

int ContractZbft::Start() {
    return kConsensusSuccess;
}

int ContractZbft::CallContract(
        pools::TxItemPtr& tx_info,
        evmc::Result* out_res) {
    std::string input;
    auto& tx = tx_info->msg_ptr->header.tx_proto();
    if (tx.key() == protos::kContractInputCode) {
        input = tx.value();
    }

    // auto contract_info = block::AccountManager::Instance()->GetAcountInfo(tx_info->tx.to());
    // if (contract_info == nullptr) {
    //     ZJC_ERROR("contract address not exists[%s]",
    //         common::Encode::HexEncode(tx_info->tx.to()).c_str());
    //     return kConsensusError;
    // }

    // uint32_t address_type = block::kNormalAddress;
    // if (contract_info->GetAddressType(&address_type) != block::kBlockSuccess  ||
    //         address_type != block::kContractAddress) {
    //     ZJC_ERROR("contract address not exists[%s]",
    //         common::Encode::HexEncode(tx_info->tx.to()).c_str());
    //     return kConsensusError;
    // }

    std::string bytes_code;
    // if (contract_info->GetBytesCode(&bytes_code) != block::kBlockSuccess) {
    //     ZJC_ERROR("contract bytes code not exists[%s]",
    //         common::Encode::HexEncode(tx_info->tx.to()).c_str());
    //     return kConsensusError;
    // }

    auto& from = tx_info->msg_ptr->address_info->addr();
    int exec_res = zjcvm::Execution::Instance()->execute(
        bytes_code,
        input,
        from,
        tx.to(),
        from,
        tx.amount(),
        tx.gas_limit(),
        0,
        zjcvm::kJustCall,
        zjc_host_,
        out_res);
    if (exec_res != zjcvm::kZjcvmSuccess) {
        return kConsensusError;
    }

    return kConsensusSuccess;
}

int ContractZbft::AddCallContract(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& out_tx) {
    switch (out_tx.step()) {
    case pools::protobuf::kContractUserCall:
        return CallContractDefault(tx_info, acc_balance_map, out_tx);
    case pools::protobuf::kContractCallExcute:
        return CallContractExceute(tx_info, acc_balance_map, out_tx);
    case pools::protobuf::kContractBroadcast:
        return CallContractCalled(tx_info, acc_balance_map, out_tx);
    default:
        break;
    }

    return kConsensusError;
}

int ContractZbft::CallContractDefault(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx) {
    uint64_t from_balance = 0;
    uint64_t to_balance = 0;
    auto& from = tx_info->msg_ptr->address_info->addr();
    int balance_status = kConsensusSuccess;// GetTempAccountBalance(from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        tx.set_status(balance_status);
        assert(false);
        return kConsensusError;
    }

    uint64_t gas_used = kCallContractDefaultUseGas;
    // at least kCallContractDefaultUseGas + kTransferGas to call contract.
    if (from_balance < tx.gas_limit() * tx.gas_price() ||
            from_balance <= (gas_used + kTransferGas) * tx.gas_price() ||
            tx.gas_limit() < (gas_used + kTransferGas)) {
        ZJC_ERROR("from balance error from_balance: %lu,"
            "tx.gas_limit() * tx.gas_price(): %lu,"
            "(gas_used + kTransferGas) * tx.gas_price(): %lu,"
            "tx.gas_limit(): %lu, (gas_used + kTransferGas): %lu",
            from_balance,
            ((gas_used + kTransferGas) * tx.gas_price()),
            ((gas_used + kTransferGas) * tx.gas_price()),
            tx.gas_limit(),
            (gas_used + kTransferGas));
        tx.set_status(kConsensusAccountBalanceError);
    }

    if (from_balance >= gas_used * tx.gas_price()) {
        from_balance -= gas_used * tx.gas_price();
    } else {
        from_balance = 0;
        tx.set_status(kConsensusAccountBalanceError);
    }
    
    acc_balance_map[tx_info->msg_ptr->address_info->addr()] = from_balance;
    tx.set_balance(from_balance);
    tx.set_gas_used(gas_used);
    if (tx.status() == kConsensusSuccess) {
        tx.set_gas_limit(tx_info->msg_ptr->header.tx_proto().gas_limit() - gas_used);
    }

    return kConsensusSuccess;
}

int ContractZbft::CallContractExceute(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx) {
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t caller_balance = tx.balance();
    uint64_t contract_balance = 0;
    int balance_status = kConsensusSuccess; 
//     GetTempAccountBalance(
//         tx.to(),
//         acc_balance_map,
//         &contract_balance);
    if (balance_status != kConsensusSuccess) {
        tx.set_status(balance_status);
        return kConsensusError;
    }

    evmc_result evmc_res = {};
    evmc::Result res{ evmc_res };
    zjc_host_.my_address_ = tx.to();
    do
    {
        if (caller_balance < tx.gas_limit() * tx.gas_price()) {
            ZJC_ERROR("caller_balance: %lu <= tx_info->tx.gas_limit() * tx.gas_price(): %lu ",
                caller_balance, tx.gas_limit() * tx.gas_price());
            tx.set_status(kConsensusUserSetGasLimitError);
            break;
        }

        if (tx.step() == common::kConsensusCallContract) {
            // will return from address's remove zjc and gas used
            zjc_host_.AddTmpAccountBalance(
                tx_info->msg_ptr->address_info->addr(),
                caller_balance);
            zjc_host_.AddTmpAccountBalance(
                tx.to(),
                contract_balance);
            int call_res = CallContract(tx_info, &res);
            gas_used = tx.gas_limit() - res.gas_left;
            if (call_res != kConsensusSuccess) {
                ZJC_ERROR("call contract failed![%d]", call_res);
                tx.set_status(kConsensusExecuteContractFailed);
                break;
            }

            if (res.status_code != EVMC_SUCCESS) {
                ZJC_ERROR("call contract failed! res.status_code[%d]", res.status_code);
                tx.set_status(kConsensusExecuteContractFailed);
                break;
            }
        } else {
            if (tx_info->msg_ptr->header.tx_proto().key() == protos::kContractBytesCode) {
                ZJC_ERROR("kContractBytesCode find failed!");
                tx.set_status(kConsensusCreateContractKeyError);
                break;
            }

            // if (security::Secp256k1::Instance()->GetContractAddress(
            //         tx_info->tx.from(),
            //         tx_info->tx.gid(),
            //         tx_info->attr_map[kContractBytesCode]) != tx_info->tx.to()) {
            //     ZJC_ERROR("contract address not eq!");
            //     tx.set_status(kConsensusCreateContractKeyError);
            //     break;
            // }
            std::string bytes_code;
            zjc_host_.AddTmpAccountBalance(
                tx_info->msg_ptr->address_info->addr(),
                caller_balance);
            int call_res = CreateContractCallExcute(
                tx_info,
                tx.gas_limit() - gas_used,
                bytes_code,
                &res);
            gas_used += tx.gas_limit() - gas_used - res.gas_left;
            if (call_res != kConsensusSuccess) {
                ZJC_ERROR("CreateContractCallExcute error!");
                tx.set_status(kConsensusCreateContractKeyError);
                break;
            }

            if (res.status_code != EVMC_SUCCESS) {
                ZJC_ERROR("res.status_code != EVMC_SUCCESS!");
                tx.set_status(kConsensusExecuteContractFailed);
                break;
            }

            if (gas_used > tx.gas_limit()) {
                ZJC_ERROR("gas_used > tx_info->tx.gas_limit()!");
                tx.set_status(kConsensusUserSetGasLimitError);
                break;
            }

            auto bytes_code_attr = tx.add_storages();
            bytes_code_attr->set_key(protos::kContractCreatedBytesCode);
            bytes_code_attr->set_val_hash(zjc_host_.create_bytes_code_);
        }
    } while (0);

    // use execute contract transfer amount to change from balance
    int64_t contract_balance_add = 0;
    int64_t caller_balance_add = 0;
    if (tx.status() == kConsensusSuccess) {
        for (auto account_iter = zjc_host_.accounts_.begin();
                account_iter != zjc_host_.accounts_.end(); ++account_iter) {
            for (auto storage_iter = account_iter->second.storage.begin();
                    storage_iter != account_iter->second.storage.end(); ++storage_iter) {
                std::string id(
                    (char*)account_iter->first.bytes,
                    sizeof(account_iter->first.bytes));
                std::string key(
                    (char*)storage_iter->first.bytes,
                    sizeof(storage_iter->first.bytes));
                std::string value(
                    (char*)storage_iter->second.value.bytes,
                    sizeof(storage_iter->second.value.bytes));
                auto attr = tx.add_storages();
                attr->set_key(key);
                attr->set_val_hash(value);
            }

            for (auto storage_iter = account_iter->second.str_storage.begin();
                storage_iter != account_iter->second.str_storage.end(); ++storage_iter) {
                std::string id(
                    (char*)account_iter->first.bytes,
                    sizeof(account_iter->first.bytes));
                auto attr = tx.add_storages();
                attr->set_key(storage_iter->first);
                attr->set_val_hash(storage_iter->second.str_val);
            }
        }

        auto& from = tx_info->msg_ptr->address_info->addr();
        for (auto transfer_iter = zjc_host_.to_account_value_.begin();
                transfer_iter != zjc_host_.to_account_value_.end(); ++transfer_iter) {
            // transfer from must caller or contract address, other not allowed.
            assert(transfer_iter->first == from || transfer_iter->first == tx.to());
            for (auto to_iter = transfer_iter->second.begin();
                    to_iter != transfer_iter->second.end(); ++to_iter) {
                assert(transfer_iter->first != to_iter->first);
                if (tx.to() == transfer_iter->first) {
                    contract_balance_add -= to_iter->second;
                }

                if (tx.to() == to_iter->first) {
                    contract_balance_add += to_iter->second;
                }

                if (from == transfer_iter->first) {
                    caller_balance_add -= to_iter->second;
                }

                if (from == to_iter->first) {
                    caller_balance_add += to_iter->second;
                }

                auto trans_item = tx.add_contract_txs();
                trans_item->set_from(transfer_iter->first);
                trans_item->set_to(to_iter->first);
                trans_item->set_amount(to_iter->second);
            }
        }

        if ((int64_t)caller_balance + caller_balance_add - tx.amount() >= gas_used * tx.gas_price()) {
        } else {
            if (tx.status() == kConsensusSuccess) {
                tx.set_status(kConsensusAccountBalanceError);
            }
        }

        if (tx.status() == kConsensusSuccess) {
            if (caller_balance_add < 0) {
                if (caller_balance < (uint64_t)(-caller_balance_add) + tx.amount()) {
                    if (tx.status() == kConsensusSuccess) {
                        tx.set_status(kConsensusAccountBalanceError);
                    }
                }
            }
        }

        if (tx.status() == kConsensusSuccess) {
            if (tx.amount() > 0) {
                if (caller_balance < tx.amount()) {
                    if (tx.status() == kConsensusSuccess) {
                        tx.set_status(kConsensusAccountBalanceError);
                    }
                }
            }
        }

        if (tx.status() == kConsensusSuccess) {
            if (contract_balance_add < 0) {
                if (contract_balance < (uint64_t)(-contract_balance_add)) {
                    if (tx.status() == kConsensusSuccess) {
                        tx.set_status(kConsensusAccountBalanceError);
                    }
                } else {
                    contract_balance -= (uint64_t)(-contract_balance_add);
                }
            } else {
                contract_balance += contract_balance_add;
            }
        }
    } else {
        if (caller_balance >= gas_used * tx.gas_price()) {
        } else {
            if (tx.status() == kConsensusSuccess) {
                tx.set_status(kConsensusAccountBalanceError);
            }
        }
    }

    contract_balance += tx.amount();
    auto caller_balance_attr = tx.add_storages();
    caller_balance_attr->set_key(protos::kContractCallerChangeAmount);
    caller_balance_attr->set_val_hash(std::to_string(caller_balance_add - (int64_t)tx.amount()));
    auto gas_limit_attr = tx.add_storages();
    gas_limit_attr->set_key(protos::kContractCallerGasUsed);
    gas_limit_attr->set_val_hash(std::to_string(gas_used));
    acc_balance_map[tx.to()] = contract_balance;
    tx.set_balance(contract_balance);
    tx.set_gas_used(0);
    return kConsensusSuccess;
}

int ContractZbft::CallContractCalled(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx) {
    // gas just consume by from
    uint64_t from_balance = 0;
    auto& from = tx_info->msg_ptr->address_info->addr();
    int balance_status = kConsensusSuccess;
    //GetTempAccountBalance(from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        tx.set_status(balance_status);
        assert(false);
        return kConsensusError;
    }

    // auto account_info = block::AccountManager::Instance()->GetAcountInfo(tx_info->tx.from());
    // if (!account_info->locked()) {
    //     ZJC_ERROR("account not locked for contrtact: %s",
    //         common::Encode::HexEncode(tx_info->tx.to()).c_str());
    //     return kConsensusError;
    // }

    int64_t caller_balance_add = 0;
    uint64_t caller_gas_used = 0;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kContractCallerChangeAmount) {
            if (!common::StringUtil::ToInt64(
                    tx.storages(i).val_hash(),
                    &caller_balance_add)) {
                return kConsensusError;
            }

            if (tx.status() == kConsensusSuccess) {
                if (caller_balance_add < 0) {
                    if (from_balance < (uint64_t)(-caller_balance_add)) {
                        return kConsensusError;
                    }

                    from_balance -= (uint64_t)(-caller_balance_add);
                } else {
                    from_balance += (uint64_t)(caller_balance_add);
                }
            }
        }

        if (tx.storages(i).key() == protos::kContractCallerGasUsed) {
            if (!common::StringUtil::ToUint64(tx.storages(i).val_hash(), &caller_gas_used)) {
                return kConsensusError;
            }

            if (from_balance >= caller_gas_used * tx.gas_price()) {
                from_balance -= caller_gas_used * tx.gas_price();
            } else {
                assert(tx.status() != kConsensusSuccess);
                from_balance = 0;
                if (tx.status() == kConsensusSuccess) {
                    tx.set_status(kConsensusAccountBalanceError);
                }
            }
        }
    }

    acc_balance_map[from] = from_balance;
    tx.set_balance(from_balance);
    tx.set_gas_used(caller_gas_used);
    return kConsensusSuccess;
}

int ContractZbft::CreateContractCallExcute(
        pools::TxItemPtr& tx_info,
        uint64_t gas_limit,
        const std::string& bytes_code,
        evmc::Result* out_res) {
    std::string input;
    uint32_t call_mode = zjcvm::kJustCreate;
    auto tx = tx_info->msg_ptr->header.tx_proto();
    if (tx.key() == protos::kContractInputCode) {
        input = tx.value();
        call_mode = zjcvm::kCreateAndCall;
    }
//     tvm::Execution exec;
    int exec_res = zjcvm::Execution::Instance()->execute(
        bytes_code,
        input,
        tx_info->msg_ptr->address_info->addr(),
        tx.to(),
        tx_info->msg_ptr->address_info->addr(),
        tx.amount(),
        gas_limit,
        0,
        call_mode,
        zjc_host_,
        out_res);
    if (exec_res != zjcvm::kZjcvmSuccess) {
        ZJC_ERROR("CreateContractCallExcute failed: %d", exec_res);
        return kConsensusError;
    }

    return kConsensusSuccess;
}


};  // namespace consensus

};  // namespace zjchain