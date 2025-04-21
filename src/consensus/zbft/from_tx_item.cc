#include "consensus/zbft/from_tx_item.h"

namespace shardora {

namespace consensus {

int FromTxItem::HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    uint64_t to_balance = 0;
    auto& from = address_info->addr();
    int balance_status = GetTempAccountBalance(zjc_host, from, acc_balance_map, &from_balance, &from_nonce);
    auto src_banalce = from_balance;
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    do  {
        gas_used = consensus::kTransferGas; // 转账交易费计算
        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // will never happen
            break;
        }

        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            // TODO(): check key exists and reserve gas
            gas_used += (block_tx.storages(i).key().size() + tx_info->value().size()) *
                consensus::kKeyValueStorageEachBytes;
            auto str_key = block_tx.from() + block_tx.storages(i).key();
            block::protobuf::KeyValueInfo kv_info;
            kv_info.set_value(tx_info->value());
            kv_info.set_nonce(block_tx.nonce());
            zjc_host.db_batch_.Put(str_key, kv_info.SerializeAsString());
        }

        // 余额不足
        if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), block_tx.gas_price());
            break;
        }

        // gas limit 设置小了
        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
            break;
        }
    } while (0);

    if (block_tx.status() == kConsensusSuccess) {
        // 要转的金额 + 交易费
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

    // 剪掉来源账户的金额
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    // prefix_db_->AddAddressInfo(from, *(acc_balance_map[from]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    // ZJC_DEBUG("handle tx success: %s, %lu, %lu, status: %d, from: %s, to: %s, amount: %lu, src_banalce: %lu, %u_%u_%lu, height: %lu",
    //     common::Encode::HexEncode(block_tx.gid()).c_str(),
    //     block_tx.balance(),
    //     block_tx.gas_used(),
    //     block_tx.status(),
    //     common::Encode::HexEncode(block_tx.from()).c_str(),
    //     common::Encode::HexEncode(block_tx.to()).c_str(),
    //     block_tx.amount(),
    //     src_banalce,
    //     view_block.qc().network_id(),
    //     view_block.qc().pool_index(),
    //     view_block.qc().view(),
    //     view_block.block_info().height());
    // ADD_TX_DEBUG_INFO((&block_tx));
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
