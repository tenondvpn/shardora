#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class CreateLibrary : public TxItemBase {
public:
    CreateLibrary(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr) {}
    virtual ~CreateLibrary() {}
    int HandleTx(
            const block::protobuf::Block& block,
            std::shared_ptr<db::DbWriteBatch>& db_batch,
            zjcvm::ZjchainHost& zjc_host,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t gas_used = 0;
        // gas just consume by from
        uint64_t from_balance = 0;
        uint64_t to_balance = 0;
        auto& from = msg_ptr->address_info->addr();
        int balance_status = GetTempAccountBalance(from, acc_balance_map, &from_balance);
        if (balance_status != kConsensusSuccess) {
            block_tx.set_status(balance_status);
            // will never happen
            assert(false);
            return kConsensusSuccess;
        }

        do  {
            gas_used = consensus::kCreateLibraryDefaultUseGas;
            for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                // TODO(): check key exists and reserve gas
                gas_used += network::kConsensusWaitingShardOffset * (
                    block_tx.storages(i).key().size() + msg_ptr->header.tx_proto().value().size()) *
                    consensus::kKeyValueStorageEachBytes;
            }

            if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                ZJC_DEBUG("balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), block_tx.gas_price());
                break;
            }

            if (block_tx.gas_limit() < gas_used) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
                break;
            }
        } while (0);

        if (block_tx.status() == kConsensusSuccess) {
            uint64_t dec_amount = gas_used * block_tx.gas_price();
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

        acc_balance_map[from] = from_balance;
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
    //     ZJC_DEBUG("handle tx success: %s, %lu, %lu, status: %d",
    //         common::Encode::HexEncode(block_tx.gid()).c_str(),
    //         block_tx.balance(),
    //         block_tx.gas_used(),
    //         block_tx.status());
        return kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(CreateLibrary);
};

};  // namespace consensus

};  // namespace shardora
