#include "consensus/zbft/contract_create.h"

#include "consensus/hotstuff/view_block_chain.h"
#include "contract/contract_manager.h"
#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ContractUserCreateCall::HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // contract create call
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    auto& from = address_info->addr();
    int balance_status = GetTempAccountBalance(zjc_host, from, acc_balance_map, &from_balance, &from_nonce);
    ZJC_DEBUG("contract user call create called: %s, balance: %lu", 
        common::Encode::HexEncode(from).c_str(), from_balance);
    uint64_t gas_used = consensus::kTransferGas;
    do {
        if (balance_status != kConsensusSuccess) {
            block_tx.set_status(balance_status);
            // will never happen
            assert(false);
            break;
        }

        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // will never happen
            assert(false);
            break;
        }

	    protos::AddressInfoPtr contract_info = zjc_host.view_block_chain_->ChainGetAccountInfo(block_tx.to());
        if (contract_info != nullptr) {
            block_tx.set_status(kConsensusAccountExists);
            break;
        }


        if (block_tx.gas_price() * block_tx.gas_limit() > from_balance) {
            block_tx.set_status(kConsensusOutOfGas);
            break;
        }

        if (block_tx.gas_price() * block_tx.gas_limit() + block_tx.contract_prepayment() > from_balance) {
            block_tx.set_status(kConsensusAccountBalanceError);
            break;
        }

        if (gas_used >= block_tx.gas_limit()) {
            block_tx.set_status(kConsensusOutOfGas);
            // will never happen
            break;
        }
    } while(0);

    int64_t tmp_from_balance = from_balance;
    if (block_tx.status() == kConsensusSuccess) {
        InitHost(zjc_host, block_tx, block_tx.gas_limit() - gas_used, block_tx.gas_price(), view_block);
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
        gas_used = block_tx.gas_limit() - res.gas_left;
        if (call_res != kConsensusSuccess || res.status_code != EVMC_SUCCESS) {
            block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
            ZJC_DEBUG("create contract: %s failed, call_res: %d, "
                "evmc res: %d, gas_used: %lu, gas price: %lu, from_balance: %lu",
                common::Encode::HexEncode(block_tx.to()).c_str(),
                call_res,
                res.status_code,
                gas_used,
                block_tx.gas_price(),
                from_balance);
        }

        if (res.gas_left > (int64_t)block_tx.gas_limit()) {
            gas_used = block_tx.gas_limit();
        }

        if (from_balance > gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
            gas_used = 0;
            for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                // TODO(): check key exists and reserve gas
                gas_used += (block_tx.storages(i).key().size() + tx_info->value().size()) *
                    consensus::kKeyValueStorageEachBytes;
                ZJC_DEBUG("create contract key: %s, value: %s", 
                    block_tx.storages(i).key().c_str(), 
                    block_tx.storages(i).value().c_str());
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
    }

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
        int64_t gas_more = zjc_host.gas_more_;
        int res = SaveContractCreateInfo(
            zjc_host,
            block_tx,
            contract_balance_add,
            caller_balance_add);
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
            auto contract_info = std::make_shared<address::protobuf::AddressInfo>();
            contract_info->set_addr(block_tx.to());
            contract_info->set_balance(block_tx.amount());
            contract_info->set_sharding_id(view_block.qc().network_id());
            contract_info->set_pool_index(view_block.qc().pool_index());
            contract_info->set_type(address::protobuf::kWaitingRootConfirm);
            contract_info->set_bytes_code(zjc_host.create_bytes_code_);
            contract_info->set_latest_height(view_block.block_info().height());
            contract_info->set_nonce(0);
            // prefix_db_->AddAddressInfo(block_tx.to(), *contract_info, zjc_host.db_batch_);
            ZJC_DEBUG("success add contract address info: %s, %s", 
                common::Encode::HexEncode(block_tx.to()).c_str(), 
                ProtobufToJson(*contract_info).c_str());
        } while (0);
    }

    from_balance = tmp_from_balance;
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    // prefix_db_->AddAddressInfo(from, *(acc_balance_map[from]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ZJC_DEBUG("create contract called %s, user: %s, new balance: %lu, "
        "gas used: %lu, gas_price: %lu, prepayment: %lu, amount: %lu",
        common::Encode::HexEncode(block_tx.to()).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        from_balance,
        gas_used,
        block_tx.gas_price(),
        block_tx.contract_prepayment(),
        block_tx.amount());
    if (block_tx.status() == kConsensusSuccess) {
        auto iter = zjc_host.cross_to_map_.find(block_tx.to());
        std::shared_ptr<block::protobuf::ToAddressItemInfo> to_item_ptr;
        if (iter == zjc_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<block::protobuf::ToAddressItemInfo>();
            to_item_ptr->set_des(block_tx.to());
            to_item_ptr->set_amount(block_tx.amount());
            to_item_ptr->set_sharding_id(view_block.qc().network_id());
        } else {
            to_item_ptr = iter->second;
            to_item_ptr->set_amount(block_tx.amount() + to_item_ptr->amount());
        }

        if (block_tx.contract_prepayment() > 0) {
            auto preypayment_id = block_tx.to() + block_tx.from();
            auto iter = zjc_host.cross_to_map_.find(preypayment_id);
            std::shared_ptr<block::protobuf::ToAddressItemInfo> to_item_ptr;
            if (iter == zjc_host.cross_to_map_.end()) {
                to_item_ptr = std::make_shared<block::protobuf::ToAddressItemInfo>();
                to_item_ptr->set_des(preypayment_id);
                to_item_ptr->set_prepayment(block_tx.contract_prepayment());
                to_item_ptr->set_sharding_id(view_block.qc().network_id());
            } else {
                to_item_ptr = iter->second;
                to_item_ptr->set_prepayment(block_tx.contract_prepayment() + to_item_ptr->prepayment());
            }
        }
    }
    return kConsensusSuccess;
}

int ContractUserCreateCall::SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& block_tx,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add) {
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
                auto trans_item = view_block.mutable_block_info()->add_contract_txs();
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

};  // namespace shardora
