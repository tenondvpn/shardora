#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "shardoravm/shardora_host.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace contract {
    class ContractManager;
};

namespace consensus {

class ContractRefund : public TxItemBase {
public:
    ContractRefund(
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractRefund() {}
    virtual int HandleTx(
            uint32_t tx_index,
            view_block::protobuf::ViewBlockItem& view_block,
            shardoravm::ShardorahainHost& pre_shardora_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        auto btime = common::TimeUtils::TimestampMs();
        SHARDORA_DEBUG("contract called now.");
        uint64_t from_balance = 0;
        uint64_t from_nonce = 0;
        auto preppayment_id = block_tx.to() + block_tx.from();
        auto res = GetTempAccountBalance(pre_shardora_host, preppayment_id, acc_balance_map, &from_balance, &from_nonce);
        if (res != kConsensusSuccess) {
            return kConsensusError;
        }

        uint64_t test_from_balance = from_balance;
        auto gas_used = kTransferGas;
        auto gas_limit = block_tx.gas_limit();
        do {
            if (block_tx.amount() != 0) {
                block_tx.set_status(kConsensusError);
                // //assert(false);
                break;
            }
            if (from_balance <= kTransferGas * block_tx.gas_price()) {
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

            if (gas_limit < kTransferGas) {
                block_tx.set_status(kConsensusUserSetGasLimitError);
                break;
            }
        } while (0);

        if (from_balance >= gas_used * block_tx.gas_price()) {
            from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }

        uint32_t status_code = block_tx.status();
        if (block_tx.status() == kConsensusSuccess && from_balance > 0) {
            auto iter = pre_shardora_host.cross_to_map_.find(block_tx.to());
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == pre_shardora_host.cross_to_map_.end()) {
                to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                to_item_ptr->set_des(block_tx.from());
                to_item_ptr->set_amount(from_balance);
                pre_shardora_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
                SHARDORA_DEBUG("success add cross to shard array: %s, %lu",
                    common::Encode::HexEncode(block_tx.from()).c_str(),
                    from_balance);
            } else {
                to_item_ptr = iter->second;
                to_item_ptr->set_amount(from_balance + to_item_ptr->amount());
            }

            acc_balance_map[preppayment_id]->set_balance(0);
        } else {
            SHARDORA_DEBUG("failed add cross to shard array: %s, %lu",
                common::Encode::HexEncode(block_tx.to()).c_str(),
                block_tx.amount());
            acc_balance_map[preppayment_id]->set_balance(from_balance);
        }

        // must prefund's nonce, not caller or contract
        acc_balance_map[preppayment_id]->set_nonce(block_tx.nonce());
        acc_balance_map[preppayment_id]->set_latest_height(view_block.block_info().height());
        acc_balance_map[preppayment_id]->set_tx_index(tx_index);
        block_tx.set_balance(acc_balance_map[preppayment_id]->balance());
        block_tx.set_gas_used(gas_used);
        
        block::protobuf::TxHashStatus tx_hash_status;
        tx_hash_status.set_status(block_tx.status());
        auto status_val = tx_hash_status.SerializeAsString();
        pre_shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
        return kConsensusSuccess;
    }

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractRefund);
};

};  // namespace consensus

};  // namespace shardora
