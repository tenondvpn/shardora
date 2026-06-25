#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/pools.pb.h"
#include "security/security.h"
#include "shardoravm/execution.h"

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
        shardoravm::ShardorahainHost shardora_host;
        shardora_host.view_block_chain_ = pre_shardora_host.view_block_chain_;
        shardora_host.pre_shardora_host_ = &pre_shardora_host;
        do  {
            // Intrinsic gas: base (53000) + bytecode calldata bytes (EIP-2028)
            gas_used = consensus::kCreateLibraryDefaultUseGas
                       + consensus::CalcCalldataGas(block_tx.contract_code());
            if (balance_status != kConsensusSuccess) {
                block_tx.set_status(balance_status);
                // will never happen
                //assert(false);
                break;
            }
    
            if (from_nonce + 1 != block_tx.nonce()) {
                block_tx.set_status(kConsensusNonceInvalid);
                // will never happen
                //assert(false);
                break;
            }

            if (tx_info->has_key()) {
                gas_used += consensus::CalcKvStorageGas(
                    tx_info->key().size(), tx_info->value().size(), true);
                shardora_host.SaveKeyValue(from, tx_info->key(), tx_info->value());
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

        evmc_result evmc_call_res = {};
        evmc::Result evmc_res{ evmc_call_res };
        bool check_valid = false;
        if (block_tx.status() == kConsensusSuccess) {
            check_valid = true;
            int call_res = CreateContractCallExcute(shardora_host, block_tx, &evmc_res);
            gas_used = block_tx.gas_limit() - evmc_res.gas_left;
            if (call_res != kConsensusSuccess || evmc_res.status_code != EVMC_SUCCESS) {
                block_tx.set_status(EvmcStatusToZbftStatus(evmc_res.status_code));
                SHARDORA_DEBUG("create contract: %s failed, call_res: %d, "
                    "evmc res: %d, gas_used: %lu, gas price: %lu, from_balance: %lu",
                    common::Encode::HexEncode(block_tx.to()).c_str(),
                    call_res,
                    (int32_t)evmc_res.status_code,
                    gas_used,
                    block_tx.gas_price(),
                    from_balance);
            }

            if (evmc_res.gas_left > (int64_t)block_tx.gas_limit()) {
                gas_used = block_tx.gas_limit();
            }
            
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
        acc_balance_map[from]->set_latest_height(view_block.block_info().height());
        acc_balance_map[from]->set_tx_index(tx_index);
        SHARDORA_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(from).c_str(), 
            ProtobufToJson(*(acc_balance_map[from])).c_str());
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
        SHARDORA_DEBUG("create library called handle tx success nonce: "
            "%lu, %lu, %lu, status: %d, from: %s, to: %s",
            block_tx.nonce(),
            block_tx.balance(),
            block_tx.gas_used(),
            block_tx.status(),
            common::Encode::HexEncode(block_tx.from()).c_str(),
            common::Encode::HexEncode(block_tx.to()).c_str());

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
        SHARDORA_DEBUG("create library status: %d, output: %s, from: %s, to: %s", 
            tx_hash_status.status(),
            "",
            common::Encode::HexEncode(block_tx.from()).c_str(),
            common::Encode::HexEncode(block_tx.to()).c_str());
        if (block_tx.status() == kConsensusSuccess) {
            shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
            shardora_host.MergeToPrev();
            auto contract_info = std::make_shared<address::protobuf::AddressInfo>();
            contract_info->set_addr(block_tx.to());
            contract_info->set_balance(0);
            contract_info->set_sharding_id(view_block.qc().network_id());
            contract_info->set_pool_index(view_block.qc().pool_index());
            contract_info->set_type(address::protobuf::kNormal);
            contract_info->set_bytes_code(shardora_host.create_bytes_code_);
            contract_info->set_latest_height(view_block.block_info().height());
            contract_info->set_tx_index(tx_index);
            contract_info->set_nonce(0);
            SHARDORA_DEBUG("success add contract address info: %s, %s, library bytes: %s", 
                common::Encode::HexEncode(block_tx.to()).c_str(), 
                ProtobufToJson(*contract_info).c_str(),
                common::Encode::HexEncode(shardora_host.create_bytes_code_).c_str());
            acc_balance_map[block_tx.to()] = contract_info;

            auto iter = pre_shardora_host.cross_to_map_.find(block_tx.to());
            std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
            if (iter == pre_shardora_host.cross_to_map_.end()) {
                to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                to_item_ptr->set_from(block_tx.from());
                to_item_ptr->set_des(block_tx.to());
                to_item_ptr->set_des_sharding_id(network::kRootCongressNetworkId);
                pre_shardora_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
            }

            to_item_ptr->set_library_bytes(shardora_host.create_bytes_code_);
        } else {
            pre_shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
        }

        return kConsensusSuccess;
    }

    int CreateContractCallExcute(
            shardoravm::ShardorahainHost& shardora_host,
            block::protobuf::BlockTx& tx,
            evmc::Result* out_res) {
        uint32_t call_mode = shardoravm::kJustCreate;
        int exec_res = shardoravm::Execution::Instance()->execute(
            tx.contract_code(),
            "",
            tx.from(),
            tx.to(),
            tx.from(),
            tx.amount(),
            tx.gas_limit(),
            0,
            call_mode,
            shardora_host,
            out_res);
        if (exec_res != shardoravm::kShardoravmSuccess) {
            SHARDORA_ERROR("CreateContractCallExcute failed: %d", exec_res);
            return kConsensusError;
        }

        return kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(CreateLibrary);
};

};  // namespace consensus

};  // namespace shardora
