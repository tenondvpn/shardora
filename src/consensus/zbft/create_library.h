#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/pools.pb.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class CreateLibrary : public TxItemBase {
public:
    CreateLibrary(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~CreateLibrary() {}
    int HandleTx(
            view_block::protobuf::ViewBlockItem& view_block,
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
        do  {
            gas_used = consensus::kCreateLibraryDefaultUseGas;
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

            if (tx_info->has_key()) {
                gas_used += network::kConsensusWaitingShardOffset * (
                    tx_info->key().size() + tx_info->value().size()) *
                    consensus::kKeyValueStorageEachBytes;
                zjc_host.SaveKeyValue(from, tx_info->key(), tx_info->value());
            }

            if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                SHARDORA_DEBUG("balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), block_tx.gas_price());
                break;
            }

            if (block_tx.gas_limit() < gas_used) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
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
        SHARDORA_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(from).c_str(), 
            ProtobufToJson(*(acc_balance_map[from])).c_str());
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
        SHARDORA_DEBUG("create library called handle tx success nonce: %lu, %lu, %lu, status: %d",
            block_tx.nonce(),
            block_tx.balance(),
            block_tx.gas_used(),
            block_tx.status());
        if (block_tx.status() == kConsensusSuccess) {
            auto iter = zjc_host.cross_to_map_.find(block_tx.to());
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == zjc_host.cross_to_map_.end()) {
                to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                to_item_ptr->set_from(block_tx.from());
                to_item_ptr->set_des(block_tx.to());
                to_item_ptr->set_des_sharding_id(network::kRootCongressNetworkId);
                zjc_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
            }

            to_item_ptr->set_library_bytes(tx_info->value());
        }

        return kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(CreateLibrary);
};

};  // namespace consensus

};  // namespace shardora
