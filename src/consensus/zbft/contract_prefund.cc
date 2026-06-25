#include "consensus/zbft/contract_prefund.h"

#include "shardoravm/execution.h"

namespace shardora {

namespace consensus {

int ContractPrefund::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& pre_shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    uint64_t to_balance = 0;
    auto& from = address_info->addr();
    int balance_status = GetTempAccountBalance(pre_shardora_host, from, acc_balance_map, &from_balance, &from_nonce);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        //assert(false);
        return kConsensusSuccess;
    }

    do  {
        gas_used = consensus::kTransferGas;
        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            SHARDORA_DEBUG("addr: %s, contract: %s, nonce error: %lu, %lu", 
                common::Encode::HexEncode(from).c_str(),
                common::Encode::HexEncode(block_tx.to()).c_str(),
                from_nonce, block_tx.nonce());
            // will never happen
            //assert(false);
            break;
        }
        
        gas_used += consensus::CalcKvStorageGas(
            tx_info->key().size(), tx_info->value().size(), true);
        if (from_balance < gas_used  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("balance error: %lu, %lu, %lu",
                from_balance,
                block_tx.gas_limit(),
                block_tx.gas_price());
            break;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu",
                from_balance, block_tx.gas_limit(), gas_used);
            break;
        }
    } while (0);

    if (block_tx.status() == kConsensusSuccess) {
        uint64_t dec_amount = block_tx.amount() +
            block_tx.contract_prefund() +
            gas_used * block_tx.gas_price();
        if (from_balance >= gas_used * block_tx.gas_price()) {
            if (from_balance >= dec_amount) {
                from_balance -= dec_amount;
            } else {
                from_balance -= gas_used * block_tx.gas_price();
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("leader balance error: %llu, %llu", from_balance, dec_amount);
            }
        } else {
            from_balance = 0;
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            SHARDORA_ERROR("leader balance error: %llu, %llu",
                from_balance, gas_used * block_tx.gas_price());
        }
    } else {
        if (from_balance >= gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    }

    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    acc_balance_map[from]->set_latest_height(view_block.block_info().height());
    acc_balance_map[from]->set_tx_index(tx_index);
    //assert(acc_balance_map[from]->has_sharding_id());
    //assert(acc_balance_map[from]->has_pool_index());
    //assert(acc_balance_map[from]->has_addr());
    //assert(acc_balance_map[from]->has_type());
    //assert(acc_balance_map[from]->has_latest_height());
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    SHARDORA_DEBUG("set contract prefund called: %d, from: %s, to: %s, amount: %lu, balance: %lu",
        block_tx.status(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        common::Encode::HexEncode(block_tx.to()).c_str(),
        block_tx.contract_prefund(),
        from_balance);

    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    pre_shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    if (block_tx.status() == kConsensusSuccess) {
        pre_shardora_host.SaveKeyValue(block_tx.from(), block_tx.tx_hash(), "1");
        auto preypayment_id = block_tx.to() + block_tx.from();
        auto iter = pre_shardora_host.cross_to_map_.find(preypayment_id);
        std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
        if (iter == pre_shardora_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
            to_item_ptr->set_des(preypayment_id);
            to_item_ptr->set_prefund(block_tx.contract_prefund());
            pre_shardora_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
        } else {
            to_item_ptr = iter->second;
            to_item_ptr->set_prefund(block_tx.contract_prefund() + to_item_ptr->prefund());
        }
    }
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
