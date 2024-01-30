#include "consensus/zbft/contract_user_create_call.h"

#include "contract/contract_manager.h"
#include "zjcvm/execution.h"

namespace zjchain {

namespace consensus {

int ContractUserCreateCall::HandleTx(
    uint8_t thread_idx, const block::protobuf::Block &block,
    std::shared_ptr<db::DbWriteBatch> &db_batch, zjcvm::ZjchainHost &zjc_host,
    std::unordered_map<std::string, int64_t> &acc_balance_map,
    block::protobuf::BlockTx &block_tx) {
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
            gas_used += (block_tx.storages(i).key().size() + msg_ptr->header.tx_proto().value().size()) *
                consensus::kKeyValueStorageEachBytes;
        }

        if (from_balance < gas_used  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("balance error: %lu, %lu, %lu",
                from_balance,
                block_tx.gas_limit(),
                block_tx.gas_price());
            break;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("1 balance error: %lu, %lu, %lu",
                from_balance, block_tx.gas_limit(), gas_used);
            break;
        }
    } while (0);

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

	// 放入 bytes code
	if (block_tx.status() == kConsensusSuccess) {
		auto storage = block_tx.add_storages();
		storage->set_key(protos::kCreateContractBytesCode);
		storage->set_val_hash(zjc_host.create_bytes_code_);
	}

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ZJC_DEBUG("set contract create called: %d, from: %s, to: %s, amount: %lu",
        block_tx.status(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        common::Encode::HexEncode(block_tx.to()).c_str(),
        block_tx.contract_prepayment());
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain
