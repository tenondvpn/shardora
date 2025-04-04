#include "consensus/zbft/contract_create_by_root_to_tx_item.h"

#include "contract/contract_manager.h"
#include "zjcvm/execution.h"
#include <common/log.h>
#include <consensus/consensus_utils.h>
#include "consensus/hotstuff/view_block_chain.h"
#include <db/db.h>
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <protos/tx_storage_key.h>
#include <zjcvm/zjcvm_utils.h>

namespace shardora {

namespace consensus {
// 处理 ContractCreate 交易的 Local 部分（to 部分，已经过 root 分配 shard）
// from 部分已经在 ContractUserCreateCall::HandleTx 处理完成
int ContractCreateByRootToTxItem::HandleTx(
		const view_block::protobuf::ViewBlockItem& view_block,
		zjcvm::ZjchainHost& zjc_host,
		hotstuff::BalanceAndNonceMap& acc_balance_map,
		block::protobuf::BlockTx& block_tx) {
	if (block_tx.storages_size() != 1) {
        block_tx.set_status(kConsensusError);
        return consensus::kConsensusSuccess;
    }

	pools::protobuf::ToTxMessage cc_to_tx;
	if (!cc_to_tx.ParseFromString(block_tx.storages(0).value())) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get cc to txs info failed: %s",
            common::Encode::HexEncode(block_tx.storages(0).value()).c_str());
        return consensus::kConsensusSuccess;
    }

	if (cc_to_tx.tos_size() <= 0) {
		return consensus::kConsensusSuccess;
	}
	auto cc_item = cc_to_tx.tos(0);
	block_tx.set_to(cc_item.des());
	block_tx.set_amount(cc_item.amount());
	block_tx.set_contract_code(cc_item.library_bytes());
	ZJC_DEBUG("create contract info, contract_code: %s, from: %s, gas_limit: %lu, gas_price: %lu, prepayment: %lu, to: %s",
		block_tx.contract_code().c_str(),
		common::Encode::HexEncode(block_tx.from()).c_str(),
		block_tx.gas_limit(),
		block_tx.gas_price(),
		block_tx.contract_prepayment(),
		common::Encode::HexEncode(block_tx.to()).c_str());
	
	// TODO 从 kv 中读取 cc tx info
	protos::AddressInfoPtr contract_info = zjc_host.view_block_chain_->ChainGetAccountInfo(block_tx.to());
	if (contract_info != nullptr) {
		ZJC_ERROR("contract addr already exsit, to: %s", common::Encode::HexEncode(block_tx.to()).c_str());
		block_tx.set_status(kConsensusAccountExists);
		return kConsensusSuccess;
	}

	auto from = block_tx.from(); // realy contract from
	uint64_t from_prepayment = block_tx.contract_prepayment();

	if (block_tx.gas_price() * block_tx.gas_limit() + block_tx.amount() > from_prepayment) {
		ZJC_ERROR("out of prepayment, to: %s, prepayment: %lu, gas_price: %lu, gas_limit: %lu, amount: %lu",
			common::Encode::HexEncode(block_tx.to()).c_str(),
			from_prepayment,
			block_tx.gas_price(),
			block_tx.gas_limit(),
			block_tx.amount());
		block_tx.set_status(kConsensusOutOfPrepayment);
		return kConsensusSuccess;
	}

	block::protobuf::BlockTx contract_tx;
    zjcvm::Uint64ToEvmcBytes32(zjc_host.tx_context_.tx_gas_price, block_tx.gas_price());
	
    zjc_host.contract_mgr_ = contract_mgr_;
    zjc_host.acc_mgr_ = account_mgr_;
    zjc_host.my_address_ = block_tx.to();
    zjc_host.tx_context_.block_gas_limit = block_tx.gas_limit();

	zjc_host.AddTmpAccountBalance(block_tx.from(), from_prepayment);
	zjc_host.AddTmpAccountBalance(block_tx.to(), block_tx.amount());

	evmc_result evmc_res = {};
	evmc::Result res { evmc_res };
	// TODO gaslimit: 0, from: ""
	int call_res = CreateContractCallExcute(zjc_host, block_tx, &res);
	auto gas_used = block_tx.gas_limit() - res.gas_left;
	if (call_res != kConsensusSuccess || res.status_code != EVMC_SUCCESS) {
		block_tx.set_status(EvmcStatusToZbftStatus(res.status_code));
		ZJC_DEBUG("create contract local: %s failed, call_res: %d, "
			"evmc res: %d, gas_used: %lu, gas price: %lu, from_prepayment: %lu",
			common::Encode::HexEncode(block_tx.to()).c_str(),
			call_res,
			res.status_code,
			gas_used,
			block_tx.gas_price(),
			from_prepayment);
	}
	// ???
	if (res.gas_left > (int64_t)block_tx.gas_limit()) {
		gas_used = block_tx.gas_limit();
	}

	if (from_prepayment > gas_used * block_tx.gas_price()) {
		from_prepayment -= gas_used * block_tx.gas_price();
		gas_used = 0;
		for (int i = 0; i < block_tx.storages_size(); i++) {
			gas_used += (block_tx.storages(i).key().size() + tx_info->value().size()) * consensus::kKeyValueStorageEachBytes;
		}

		if (block_tx.gas_limit() < gas_used) {
			block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
			ZJC_DEBUG("gas limit not enough: %lu, %lu, %lu", from_prepayment, block_tx.gas_limit(), gas_used);
		}
	} else {
		block_tx.set_status(consensus::kConsensusAccountBalanceError);
		ZJC_ERROR("from prepayment error: %llu, %llu", from_prepayment, gas_used * block_tx.gas_price());
		from_prepayment = 0;
	}

	int64_t tmp_from_balance = from_prepayment;
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
		int64_t contract_balance_add = 0;
		int64_t caller_balance_add = 0;
		int64_t gas_more = 0;

		int res = SaveContractCreateInfo(zjc_host, block_tx, contract_balance_add, caller_balance_add, gas_more);
		gas_used += gas_more;
		do {
            if (res != kConsensusSuccess) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                break;
            }

            if (gas_used > block_tx.gas_limit()) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_prepayment, block_tx.gas_limit(), gas_more);
                break;
            }

            if (from_prepayment < gas_used * block_tx.gas_price()) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("balance error: %llu, %llu", from_prepayment, gas_more * block_tx.gas_price());
                break;
            }

            // just dec caller_balance_add
            int64_t dec_amount = static_cast<int64_t>(block_tx.amount()) -
                caller_balance_add +
                static_cast<int64_t>(gas_used * block_tx.gas_price());
            if ((int64_t)from_prepayment < dec_amount) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu", from_prepayment, caller_balance_add);
                break;
            }

            tmp_from_balance -= dec_amount;
            // change contract create amount
            block_tx.set_amount(static_cast<int64_t>(block_tx.amount()) + contract_balance_add);
        } while (0);

		from_prepayment = tmp_from_balance;
		
        auto preppayment_id = block_tx.to() + block_tx.from();
        acc_balance_map[preppayment_id]->set_balance(from_prepayment);
        acc_balance_map[preppayment_id]->set_nonce(block_tx.nonce());
        prefix_db_->AddAddressInfo(preppayment_id, *(acc_balance_map[preppayment_id]), zjc_host.db_batch_);
		acc_balance_map[block_tx.to()]->set_balance(block_tx.amount());
		acc_balance_map[block_tx.to()]->set_nonce(0);
        prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
		block_tx.set_contract_prepayment(from_prepayment);
		block_tx.set_gas_used(gas_used);
		ZJC_DEBUG("create contract local called to: %s, contract_from: %s, "
            "prepayment: %lu, gas used: %lu, gas_price: %lu",
            common::Encode::HexEncode(block_tx.to()).c_str(),
            common::Encode::HexEncode(block_tx.from()).c_str(),
            from_prepayment,
            gas_used,
            block_tx.gas_price());
	}
	return kConsensusSuccess;
}

int ContractCreateByRootToTxItem::CreateContractCallExcute(
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

int ContractCreateByRootToTxItem::SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& block_tx,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add,
        int64_t& gas_more) {
    auto storage = block_tx.add_storages();
    storage->set_key(protos::kCreateContractBytesCode);
    storage->set_value(zjc_host.create_bytes_code_);
    // zjc_host.SavePrevStorages(protos::kCreateContractBytesCode, zjc_host.create_bytes_code_, true);
    for (auto account_iter = zjc_host.accounts_.begin();
            account_iter != zjc_host.accounts_.end(); ++account_iter) {
        for (auto storage_iter = account_iter->second.storage.begin();
            storage_iter != account_iter->second.storage.end(); ++storage_iter) {
            auto kv = block_tx.add_storages();
            auto str_key = std::string((char*)account_iter->first.bytes, sizeof(account_iter->first.bytes)) +
                std::string((char*)storage_iter->first.bytes, sizeof(storage_iter->first.bytes));
            kv->set_key(str_key);
            kv->set_value(std::string(
                (char*)storage_iter->second.value.bytes,
                sizeof(storage_iter->second.value.bytes)));
            // zjc_host.SavePrevStorages(str_key, kv->value(), true);
            gas_more += (sizeof(account_iter->first.bytes) +
                sizeof(storage_iter->first.bytes) +
                sizeof(storage_iter->second.value.bytes)) *
                consensus::kKeyValueStorageEachBytes;
            address::protobuf::KeyValueInfo kv_info;
            kv_info.set_value(kv->value());
            kv_info.set_height(block_tx.nonce());
            zjc_host.db_batch_.Put(kv->key(), kv_info.SerializeAsString());
        }

        for (auto storage_iter = account_iter->second.str_storage.begin();
                storage_iter != account_iter->second.str_storage.end(); ++storage_iter) {
            auto kv = block_tx.add_storages();
            auto str_key = std::string(
                (char*)account_iter->first.bytes,
                sizeof(account_iter->first.bytes)) + storage_iter->first;
            kv->set_key(str_key);
            kv->set_value(storage_iter->second.str_val);
            // zjc_host.SavePrevStorages(str_key, kv->value(), true);
            gas_more += (sizeof(account_iter->first.bytes) +
                storage_iter->first.size() +
                storage_iter->second.str_val.size()) *
                consensus::kKeyValueStorageEachBytes;
            address::protobuf::KeyValueInfo kv_info;
            kv_info.set_value(kv->value());
            kv_info.set_height(block_tx.nonce());
            zjc_host.db_batch_.Put(kv->key(), kv_info.SerializeAsString());
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

}; // namespace consensus

}; // namespace shardora



