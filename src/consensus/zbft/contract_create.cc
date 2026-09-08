#include "consensus/zbft/contract_create.h"

#include "common/defer.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "contract/contract_manager.h"
#include "network/network_utils.h"
#include "shardoravm/execution.h"
#include "shardoravm/host_journal_stack.h"
#include "shardoravm/reversible_feistel_address.h"

namespace shardora {

namespace consensus {

int ContractUserCreateCall::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& pre_shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // contract create call
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    auto& from = address_info->addr();
    int balance_status = GetTempAccountBalance(pre_shardora_host, from, acc_balance_map, &from_balance, &from_nonce);
    SHARDORA_DEBUG("contract user call create called: %s, balance: %lu", 
        common::Encode::HexEncode(from).c_str(), from_balance);
    // Intrinsic gas: base (53000) + bytecode calldata bytes (EIP-2028)
    uint64_t gas_used = consensus::kCreateContractDefaultUseGas
                        + consensus::CalcCalldataGas(block_tx.contract_code());
    do {
        if (balance_status != kConsensusSuccess) {
            block_tx.set_status(balance_status);
            // will never happen
            //assert(false);
            break;
        }

        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // will never happen
            // //assert(false);
            break;
        }

	    protos::AddressInfoPtr contract_info = pre_shardora_host.view_block_chain_->ChainGetAccountInfo(block_tx.to());
        if (contract_info != nullptr) {
            block_tx.set_status(kConsensusAccountExists);
            break;
        }

        if (common::IsContractBytescodeValid(block_tx.contract_code()) != common::ValidationStatus::SUCCESS) {
            block_tx.set_status(kConsensusContractBytesCodeError);
            break;
        }

        if (block_tx.gas_price() * block_tx.gas_limit() > from_balance) {
            block_tx.set_status(kConsensusAccountBalanceError);
            break;
        }

        if (block_tx.gas_price() * block_tx.gas_limit() + block_tx.contract_prefund() > from_balance) {
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
    shardoravm::ShardorahainHost shardora_host;
    shardora_host.view_block_chain_ = pre_shardora_host.view_block_chain_;
    shardora_host.tx_context_ = pre_shardora_host.tx_context_;
    shardora_host.pre_shardora_host_ = &pre_shardora_host;
    evmc_result evmc_call_res = {};
    evmc::Result evmc_res{ evmc_call_res };
    bool check_valid = false;
    if (block_tx.status() == kConsensusSuccess) {
        InitHost(
            shardora_host, 
            block_tx, 
            block_tx.gas_limit() - gas_used, 
            block_tx.gas_price(), 
            view_block);
        // get caller prepaid gas
        shardora_host.AddTmpAccountBalance(
            block_tx.from(),
            tmp_from_balance);
        shardora_host.AddTmpAccountBalance(
            block_tx.to(),
            block_tx.amount());
        check_valid = true;
        int call_res = CreateContractCallExcute(shardora_host, block_tx, &evmc_res);
        if (evmc_res.gas_left > (int64_t)block_tx.gas_limit()) {
            gas_used = block_tx.gas_limit();
        } else {
            gas_used += block_tx.gas_limit() - evmc_res.gas_left;
        }

        if (call_res != kConsensusSuccess || evmc_res.status_code != EVMC_SUCCESS) {
            block_tx.set_status(EvmcStatusToZbftStatus(evmc_res.status_code));
            SHARDORA_DEBUG("create contract: %s failed, call_res: %d, "
                "evmc res: %d, gas_used: %lu, gas price: %lu, from_balance: %lu",
                common::Encode::HexEncode(block_tx.to()).c_str(),
                call_res,
                (int32_t)evmc_res.status_code,
                gas_used,
                block_tx.gas_price(),
                tmp_from_balance);
        }

        // ── 叠加跨链路由 gas（构造函数中调用 _crossTransfer 产生）────────────
        gas_used += static_cast<uint64_t>(shardora_host.cross_gas_charged_);
        // ─────────────────────────────────────────────────────────────────────

        if (tmp_from_balance > static_cast<int64_t>(gas_used * block_tx.gas_price())) {
            gas_used += consensus::CalcKvStorageGas(
                tx_info->key().size(), tx_info->value().size(), true);
            SHARDORA_DEBUG("create contract key: %s, value: %s", 
                tx_info->key().c_str(), 
                tx_info->value().c_str());
            if (block_tx.gas_limit() < gas_used) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu", tmp_from_balance, block_tx.gas_limit(), gas_used);
            }
        } else {
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            SHARDORA_ERROR("leader balance error: %llu, %llu", tmp_from_balance, gas_used * block_tx.gas_price());
            tmp_from_balance = 0;
        }
    }

    if (block_tx.status() == kConsensusSuccess) {
        int64_t dec_amount = block_tx.amount() +
            block_tx.contract_prefund() +
            gas_used * block_tx.gas_price();
        if (tmp_from_balance >= int64_t(gas_used * block_tx.gas_price())) {
            if (tmp_from_balance < dec_amount) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("leader balance error: %llu, %llu", tmp_from_balance, dec_amount);
            }
        } else {
            tmp_from_balance = 0;
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            SHARDORA_ERROR("leader balance error: %llu, %llu",
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
        int64_t gas_more = shardora_host.gas_more_;
        int res = SaveContractCreateInfo(
            shardora_host,
            block_tx,
            contract_balance_add,
            caller_balance_add);
        gas_used += gas_more;
        do {
            if (res != kConsensusSuccess) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                break;
            }

            // ── CrossShardBase 16x gas 自动验证 ─────────────────────────
            {
                evmc::address ca{};
                memcpy(ca.bytes, block_tx.to().data(),
                    std::min(block_tx.to().size(), sizeof(ca.bytes)));
                auto marker = shardora_host.get_storage(ca, shardoravm::kIsCrossShardBaseSlot);
                uint64_t marker_u64 = shardoravm::EvmcBytes32ToUint64(marker);
                SHARDORA_INFO("CrossShardBase sentinel check: contract=%s marker_u64=%lu marker_b31=0x%02x accounts_size=%zu",
                    common::Encode::HexEncode(block_tx.to()).c_str(),
                    marker_u64,
                    (unsigned)marker.bytes[31],
                    shardora_host.accounts_.size());
                // Dump all storage keys for recipient to diagnose slot mismatch
                {
                    auto dbg_it = shardora_host.accounts_.find(ca);
                    if (dbg_it != shardora_host.accounts_.end()) {
                        SHARDORA_INFO("CrossShardBase: recipient storage entries=%zu",
                            dbg_it->second.storage.size());
                        size_t n = 0;
                        for (auto& [k, sv] : dbg_it->second.storage) {
                            SHARDORA_INFO("  storage[%zu] slot=%s val_b31=0x%02x",
                                n++,
                                common::Encode::HexEncode(std::string((char*)k.bytes, 32)).c_str(),
                                (unsigned)sv.value.bytes[31]);
                            if (n >= 8) break;
                        }
                    } else {
                        SHARDORA_INFO("CrossShardBase: recipient NOT in accounts_!");
                    }
                }
                if (marker_u64 == 1u) {
                    gas_used *= shardoravm::kCrossShardBaseGasMultiplier;
                    SHARDORA_INFO("CrossShardBase 16x gas applied: %lu, limit: %lu, contract: %s",
                        gas_used, block_tx.gas_limit(),
                        common::Encode::HexEncode(block_tx.to()).c_str());
                }
            }
            // ─────────────────────────────────────────────────────────────

            if (gas_used > block_tx.gas_limit()) {
                block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
                SHARDORA_DEBUG("1 balance error: %lu, %lu, %lu", tmp_from_balance, block_tx.gas_limit(), gas_more);
                break;
            }

            if (tmp_from_balance < int64_t(gas_used * block_tx.gas_price())) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("balance error: %llu, %llu", tmp_from_balance, gas_more * block_tx.gas_price());
                break;
            }

            // just dec caller_balance_add
            int64_t dec_amount = static_cast<int64_t>(block_tx.amount()) -
                caller_balance_add +
                static_cast<int64_t>(block_tx.contract_prefund()) +
                static_cast<int64_t>(gas_used * block_tx.gas_price());
            if ((int64_t)tmp_from_balance < dec_amount) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("leader balance error: %llu, %llu", tmp_from_balance, caller_balance_add);
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
            contract_info->set_type(address::protobuf::kNormal);
            contract_info->set_bytes_code(shardora_host.create_bytes_code_);
            contract_info->set_latest_height(view_block.block_info().height());
            contract_info->set_tx_index(tx_index);
            contract_info->set_nonce(0);
            SHARDORA_DEBUG("success add contract address info: %s, %s", 
                common::Encode::HexEncode(block_tx.to()).c_str(), 
                ProtobufToJson(*contract_info).c_str());
            acc_balance_map[block_tx.to()] = contract_info;

            auto contract_prefund_info = std::make_shared<address::protobuf::AddressInfo>();
            contract_prefund_info->set_addr(block_tx.to() + block_tx.from());
            contract_prefund_info->set_balance(block_tx.contract_prefund());
            contract_prefund_info->set_sharding_id(view_block.qc().network_id());
            contract_prefund_info->set_pool_index(view_block.qc().pool_index());
            contract_prefund_info->set_type(address::protobuf::kNormal);
            contract_prefund_info->set_latest_height(view_block.block_info().height());
            contract_prefund_info->set_tx_index(tx_index);
            contract_prefund_info->set_nonce(0);
            SHARDORA_DEBUG("success add contract address prefund info: %s, %s, prefund: %lu", 
                common::Encode::HexEncode(block_tx.to() + from).c_str(), 
                ProtobufToJson(*contract_prefund_info).c_str(),
                block_tx.contract_prefund());
            acc_balance_map[block_tx.to() + from] = contract_prefund_info;
        } while (0);
    }

    from_balance = tmp_from_balance;
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    acc_balance_map[from]->set_latest_height(view_block.block_info().height());
    acc_balance_map[from]->set_tx_index(tx_index);
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    SHARDORA_DEBUG("create contract called %s, user: %s, new balance: %lu, "
        "gas used: %lu, gas_price: %lu, prefund: %lu, amount: %lu, status: %d",
        common::Encode::HexEncode(block_tx.to()).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        from_balance,
        gas_used,
        block_tx.gas_price(),
        block_tx.contract_prefund(),
        block_tx.amount(),
        block_tx.status());
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
    SHARDORA_DEBUG("create contract status: %d, rel: %d, output: %s, from: %s, to: %s", 
        (int32_t)evmc_res.status_code, 
        tx_hash_status.status(),
        ProtobufToJson(tx_hash_status).c_str(),
        common::Encode::HexEncode(block_tx.from()).c_str(),
        common::Encode::HexEncode(block_tx.to()).c_str());
    if (block_tx.status() == kConsensusSuccess) {
        // ── pending_cross_actions_ → cross_to_map_（构造函数中的跨链调用）────
        // key = item->des() (destination address, 20 bytes).
        // Transfers to the same recipient are accumulated; storage ops to the
        // same shadow contract are appended as CrossStorageKV entries.
        auto add_amount256 = [](pools::protobuf::ToTxMessageItem& dst,
                                 const std::string& addend) {
            if (dst.amount256().size() == 32 && addend.size() == 32) {
                std::string r = dst.amount256();
                uint32_t carry = 0;
                for (int i = 31; i >= 0; --i) {
                    uint32_t s = (uint8_t)r[i] + (uint8_t)addend[i] + carry;
                    r[i] = char(s & 0xFF); carry = s >> 8;
                }
                dst.set_amount256(r);
            } else if (!addend.empty()) {
                dst.set_amount256(addend);
            }
        };

        for (auto& action : shardora_host.pending_cross_actions_) {
            if (action.base_root_address.empty()) continue;

            if (action.type == shardoravm::CrossShardActionType::kTransfer) {
                if (action.to.empty()) continue;
                auto it = cross_to_map_.find(action.to);
                if (it == cross_to_map_.end()) {
                    auto item = std::make_shared<pools::protobuf::ToTxMessageItem>();
                    item->set_from(action.emitter);
                    item->set_des(action.to);
                    item->set_amount(action.amount);
                    if (!action.amount_bytes.empty()) item->set_amount256(action.amount_bytes);
                    item->set_sharding_id(action.dest_shard_id);
                    item->set_pool_index(static_cast<int32_t>(action.dest_pool_index));
                    item->set_des_sharding_id(action.dest_shard_id);
                    item->set_base_root_address(action.base_root_address);
                    item->set_cross_nonce(action.nonce);
                    cross_to_map_[action.to] = item;
                    SHARDORA_INFO("CrossShardBase constructor cross-transfer queued: base=%s, to=%s, amount=%lu, dest_shard=%u pool=%u",
                        common::Encode::HexEncode(action.base_root_address).c_str(),
                        common::Encode::HexEncode(action.to).c_str(),
                        action.amount, action.dest_shard_id, action.dest_pool_index);
                } else {
                    it->second->set_amount(it->second->amount() + action.amount);
                    add_amount256(*it->second, action.amount_bytes);
                    SHARDORA_INFO("CrossShardBase constructor cross-transfer accumulated: to=%s amount=%lu",
                        common::Encode::HexEncode(action.to).c_str(), action.amount);
                }
            } else if (action.type == shardoravm::CrossShardActionType::kSetStorage) {
                if (action.storage_key.empty()) continue;
                std::array<uint8_t, 20> base_arr;
                std::memcpy(base_arr.data(), action.base_root_address.data(), 20);
                auto shadow_arr = shardoravm::DeriveShardAddress(
                    base_arr, action.dest_shard_id, action.dest_pool_index);
                std::string shadow_des(reinterpret_cast<const char*>(shadow_arr.data()), 20);
                auto it = cross_to_map_.find(shadow_des);
                if (it == cross_to_map_.end()) {
                    auto item = std::make_shared<pools::protobuf::ToTxMessageItem>();
                    item->set_from(action.emitter);
                    item->set_des(shadow_des);
                    item->set_sharding_id(action.dest_shard_id);
                    item->set_pool_index(static_cast<int32_t>(action.dest_pool_index));
                    item->set_des_sharding_id(action.dest_shard_id);
                    item->set_base_root_address(action.base_root_address);
                    item->set_cross_nonce(action.nonce);
                    auto* kv = item->add_cross_storage_kv();
                    kv->set_key(action.storage_key);
                    kv->set_value(action.storage_val);
                    cross_to_map_[shadow_des] = item;
                    SHARDORA_INFO("CrossShardBase constructor cross-storage queued: base=%s, key_len=%zu, nonce=%lu, dest_shard=%u pool=%u",
                        common::Encode::HexEncode(action.base_root_address).c_str(),
                        action.storage_key.size(), action.nonce,
                        action.dest_shard_id, action.dest_pool_index);
                } else {
                    auto* kv = it->second->add_cross_storage_kv();
                    kv->set_key(action.storage_key);
                    kv->set_value(action.storage_val);
                    SHARDORA_INFO("CrossShardBase constructor cross-storage appended: base=%s, key_len=%zu",
                        common::Encode::HexEncode(action.base_root_address).c_str(),
                        action.storage_key.size());
                }
            }
        }
        // ─────────────────────────────────────────────────────────────────────

        // Detect CrossShardBase before MergeToPrev (storage/code still in shardora_host)
        std::string xsb_runtime_code;
        {
            evmc::address new_addr_evmc = shardoravm::StrToEvmcAddr(block_tx.to());
            evmc::bytes32 sentinel = shardora_host.get_storage(
                new_addr_evmc, shardoravm::kIsCrossShardBaseSlot);
            if (sentinel.bytes[31] == 1) {
                auto c2_it = shardora_host.create2_accounts_.find(new_addr_evmc);
                if (c2_it != shardora_host.create2_accounts_.end() &&
                        !c2_it->second.code.empty()) {
                    xsb_runtime_code = std::string(
                        c2_it->second.code.begin(), c2_it->second.code.end());
                } else if (!shardora_host.create_bytes_code_.empty()) {
                    // Top-level deployment: runtime bytecode returned by constructor execution
                    xsb_runtime_code = shardora_host.create_bytes_code_;
                    SHARDORA_INFO("CrossShardBase top-level deploy: runtime bytecode %zu bytes from create_bytes_code_",
                        xsb_runtime_code.size());
                } else {
                    SHARDORA_WARN("CrossShardBase deployed but runtime bytecode not found: %s",
                        common::Encode::HexEncode(block_tx.to()).c_str());
                }
            }
        }

        shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
        shardora_host.MergeToPrev();
        auto iter = pre_shardora_host.cross_to_map_.find(block_tx.to());
        std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
        if (iter == pre_shardora_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
            to_item_ptr->set_des(block_tx.to());  // 20 bytes: contract address only
            to_item_ptr->set_amount(0);  // create contract direct set balance, not cross by root
            to_item_ptr->set_sharding_id(view_block.qc().network_id());
            to_item_ptr->set_des_sharding_id(network::kUniversalNetworkId);
            if (!xsb_runtime_code.empty()) {
                to_item_ptr->set_runtime_bytecode(xsb_runtime_code);
                SHARDORA_INFO("SaveContractCreateInfo: XSB broadcast bytecode %zu bytes for addr=%s",
                    xsb_runtime_code.size(),
                    common::Encode::HexEncode(block_tx.to()).c_str());
            }

            pre_shardora_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
            SHARDORA_DEBUG("success add to tx item addr prefund id: %s, prefund: %lu",
                common::Encode::HexEncode(to_item_ptr->des()).c_str(),
                block_tx.contract_prefund());
        }

        for (auto& [map_key, new_item] : cross_to_map_) {
            auto iter = pre_shardora_host.cross_to_map_.find(map_key);
            if (iter == pre_shardora_host.cross_to_map_.end()) {
                pre_shardora_host.cross_to_map_[map_key] = new_item;
            } else {
                auto& existing = iter->second;
                if (new_item->cross_storage_kv_size() == 0) {
                    existing->set_amount(existing->amount() + new_item->amount());
                    add_amount256(*existing, new_item->amount256());
                } else {
                    for (int ki = 0; ki < new_item->cross_storage_kv_size(); ++ki) {
                        *existing->add_cross_storage_kv() = new_item->cross_storage_kv(ki);
                    }
                }
            }

            SHARDORA_DEBUG("success add to tx item addr: %s, balance: %lu",
                common::Encode::HexEncode(new_item->des()).c_str(),
                new_item->amount());
        }
    } else {
        pre_shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    }

    return kConsensusSuccess;
}

int ContractUserCreateCall::SaveContractCreateInfo(
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& block_tx,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add) {
    int64_t other_add = 0;
    for (auto transfer_iter = shardora_host.to_account_value_.begin();
            transfer_iter != shardora_host.to_account_value_.end(); ++transfer_iter) {
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
                auto iter = cross_to_map_.find(to_iter->first);
                std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
                if (iter == cross_to_map_.end()) {
                    to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
                    to_item_ptr->set_from(transfer_iter->first);
                    to_item_ptr->set_des(to_iter->first);
                    to_item_ptr->set_amount(to_iter->second);
                    cross_to_map_[to_item_ptr->des()] = to_item_ptr;
                } else {
                    to_item_ptr = iter->second;
                    to_item_ptr->set_amount(to_iter->second + to_item_ptr->amount());
                }
                
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

    SHARDORA_DEBUG("user success call create contract.");
    return kConsensusSuccess;
}

int ContractUserCreateCall::CreateContractCallExcute(
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& tx,
        evmc::Result* out_res) {
    uint32_t call_mode = shardoravm::kJustCreate;
    if (tx.has_contract_input() && !tx.contract_input().empty()) {
        call_mode = shardoravm::kCreateAndCall;
    }

    int exec_res = shardoravm::Execution::Instance()->execute(
        tx.contract_code(),
        tx.contract_input(),
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

};  // namespace consensus

};  // namespace shardora
