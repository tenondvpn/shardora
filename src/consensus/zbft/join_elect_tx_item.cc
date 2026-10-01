#include "consensus/zbft/join_elect_tx_item.h"
#include <bls/bls_utils.h>
#include <protos/tx_storage_key.h>

namespace shardora {

namespace consensus {

int JoinElectTxItem::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    auto& block = view_block.block_info();
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    uint64_t to_balance = 0;
    auto tmp_id = sec_ptr_->GetAddressWithPublicKey(from_pk_);
    auto& from = address_info->addr();
    if (tmp_id != from) {
        block_tx.set_status(consensus::kConsensusError);
        // will never happen
        //assert(false);
        return kConsensusError;
    }

    int balance_status = GetTempAccountBalance(shardora_host, from, acc_balance_map, &from_balance, &from_nonce);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        //assert(false);
        return kConsensusError;
    }

    bls::protobuf::JoinElectInfo join_info;
    auto store_gas = consensus::CalcKvStorageGas(
        tx_info->key().size(), tx_info->value().size(), true);
    do {
        gas_used = consensus::kJoinElectGas;
        if (block_tx.gas_limit() < (gas_used + store_gas)) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("1 id: %s  balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), gas_used);
            break;
        }

        if (from_balance < (gas_used + store_gas) * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusAccountBalanceError);
            SHARDORA_DEBUG("1 id: %s  balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), gas_used);
            break;
        }

        if (from_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            // will never happen
            break;
        }
        
        if (!join_info.ParseFromString(tx_info->value())) {
            break;
        }

        join_info.set_addr(from);
        if (join_info.shard_id() != network::kRootCongressNetworkId) {
            if (join_info.shard_id() != common::GlobalInfo::Instance()->network_id() ||
                    join_info.shard_id() != address_info->sharding_id()) {
                block_tx.set_status(consensus::kConsensusError);
                SHARDORA_DEBUG("shard error: %lu", join_info.shard_id());
                break;
            }
        }

        auto n = common::GlobalInfo::Instance()->each_shard_max_members();
        auto t = common::GetSignerCount(n);
        if (join_info.g2_req().verify_vec_size() != static_cast<int>(t)) {
            SHARDORA_DEBUG("join des shard error: %d,  %d, "
                "join_info.g2_req().verify_vec_size() != t %u : %u",
                join_info.shard_id(), msg_ptr->address_info->sharding_id(),
                join_info.g2_req().verify_vec_size(), t);
            block_tx.set_status(consensus::kConsensusJoinElectThreashTInvalid);
                SHARDORA_DEBUG("shard error: %lu", join_info.shard_id());
            break;
        }


        if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("id: %s balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), block_tx.gas_price());
            break;
        }

    } while (0);

    if (block_tx.status() == kConsensusSuccess) {
        // Check if this is a stake or redeem operation
        if (join_info.has_stake_op()) {
            if (join_info.stake_op() == bls::protobuf::STAKE_OP_STAKE) {
                // Handle stake operation
                return HandleStakeOperation(tx_index, view_block, shardora_host, acc_balance_map, block_tx, join_info, from, from_balance, gas_used, store_gas);
            } else if (join_info.stake_op() == bls::protobuf::STAKE_OP_REDEEM) {
                // Handle redeem operation
                return HandleRedeemOperation(tx_index, view_block, shardora_host, acc_balance_map, block_tx, join_info, from, from_balance, gas_used);
            }
        }
        
        // Normal join_elect without staking
        uint64_t stake_amount = join_info.stake_amount();
        
        if (stake_amount > 0) {
            // Validate stake amount is multiple of minimum unit (8 * 10^8)
            static const uint64_t kMinStakeUnit = 8 * 100000000llu;
            if (stake_amount % kMinStakeUnit != 0) {
                block_tx.set_status(consensus::kConsensusError);
                SHARDORA_ERROR("Invalid stake amount: %lu, must be multiple of %lu",
                    stake_amount, kMinStakeUnit);
                from_balance -= gas_used * block_tx.gas_price();
            } else if (from_balance < stake_amount + (gas_used + store_gas) * block_tx.gas_price()) {
                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("Insufficient balance for stake: have %lu, need %lu + gas",
                    from_balance, stake_amount);
                from_balance -= gas_used * block_tx.gas_price();
            } else {
                // Transfer stake to pool address
                uint32_t pool_index = common::GetAddressPoolIndex(from);
                std::string pool_address = common::GetPoolAddress(pool_index);
                
                // Check if there's existing stake info (for additional staking)
                uint64_t existing_stake = 0;
                uint64_t existing_timestamp = 0;
                bool has_existing_stake = prefix_db_->GetStakeInfo(
                    from, &existing_stake, &existing_timestamp);
                
                // Calculate total staked amount
                uint64_t total_staked = existing_stake + stake_amount;
                
                // Deduct stake amount and gas from sender
                from_balance -= stake_amount + (gas_used + store_gas) * block_tx.gas_price();
                gas_used += store_gas;
                
                // Add stake to pool address
                uint64_t pool_balance = 0;
                uint64_t pool_nonce = 0;
                int pool_status = GetTempAccountBalance(shardora_host, pool_address, acc_balance_map, &pool_balance, &pool_nonce);
                if (pool_status == kConsensusSuccess) {
                    pool_balance += stake_amount;
                    acc_balance_map[pool_address]->set_balance(pool_balance);
                    acc_balance_map[pool_address]->set_latest_height(view_block.block_info().height());
                }
                
                // Save/Update stake info with new lock period starting from block timestamp
                // Lock period resets to 7 days from now on each additional stake
                // Use block timestamp (not user-provided) to prevent timestamp manipulation
                // Use db_batch to delay write until block commit
                uint64_t block_timestamp = view_block.block_info().timestamp();
                prefix_db_->SaveStakeInfo(
                    from,
                    total_staked,  // Save total staked amount
                    block_timestamp,  // Use block timestamp (not user-provided)
                    view_block.block_info().height(),
                    shardora_host.db_batch_);  // Use db_batch for delayed write
                
                // Update join_info with total staked for FTS calculation
                join_info.set_total_staked(total_staked);
                
                auto* block_join_info = view_block.mutable_block_info()->add_joins();
                *block_join_info = join_info;
                
                if (has_existing_stake) {
                    SHARDORA_DEBUG("Additional stake: added %lu coins (total now: %lu) to pool %u address %s, "
                        "lock period reset to block timestamp: %lu (previous: %lu)",
                        stake_amount, total_staked, pool_index,
                        common::Encode::HexEncode(pool_address).c_str(),
                        block_timestamp, existing_timestamp);
                } else {
                    SHARDORA_DEBUG("Initial stake: %lu coins to pool %u address %s, block timestamp: %lu",
                        stake_amount, pool_index,
                        common::Encode::HexEncode(pool_address).c_str(),
                        block_timestamp);
                }
            }
        } else {
            // stake_amount == 0: join elect without additional staking
            if (from_balance >= (gas_used + store_gas) * block_tx.gas_price()) {
                from_balance -= (gas_used + store_gas) * block_tx.gas_price();
                gas_used += store_gas;
                auto* block_join_info = view_block.mutable_block_info()->add_joins();
                *block_join_info = join_info;
                
                // Check if user has existing stake for logging
                uint64_t existing_stake = 0;
                uint64_t existing_timestamp = 0;
                if (prefix_db_->GetStakeInfo(from, &existing_stake, &existing_timestamp)) {
                    SHARDORA_DEBUG("Join elect with existing stake: addr=%s, existing_stake=%lu, no additional stake",
                        common::Encode::HexEncode(from).c_str(), existing_stake);
                } else {
                    SHARDORA_DEBUG("Join elect without stake: addr=%s", common::Encode::HexEncode(from).c_str());
                }
            } else {
                if (from_balance >= (gas_used) * block_tx.gas_price()) {
                    from_balance -= (gas_used) * block_tx.gas_price();
                } else {
                    from_balance = 0;
                }

                block_tx.set_status(consensus::kConsensusAccountBalanceError);
                SHARDORA_ERROR("id: %s balance error for gas: %llu, %llu",
                    common::Encode::HexEncode(from).c_str(),
                    from_balance, (gas_used + store_gas) * block_tx.gas_price());
            }
        }
    } else {
        if (from_balance >= gas_used * block_tx.gas_price()) {
                from_balance -= gas_used * block_tx.gas_price();
        } else {
            from_balance = 0;
        }
    }

    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    
    // Set stoke for FTS calculation
    // Priority: 1. Stake info (if exists), 2. Historical min stoke
    uint64_t stoke = 0;
    uint64_t stake_timestamp = 0;
    if (prefix_db_->GetStakeInfo(from, &stoke, &stake_timestamp)) {
        // Use staked amount as stoke (total_staked)
        join_info.set_stoke(stoke);
        SHARDORA_DEBUG("Using stake info for stoke: addr=%s, stoke=%lu",
            common::Encode::HexEncode(from).c_str(), stoke);
    } else {
        // Fallback to historical min stoke
        prefix_db_->GetElectNodeMinStoke(common::GlobalInfo::Instance()->network_id(), from, &stoke);
        join_info.set_stoke(stoke);
        SHARDORA_DEBUG("Using historical min stoke: addr=%s, stoke=%lu",
            common::Encode::HexEncode(from).c_str(), stoke);
    }
    
    join_info.set_public_key(from_pk_);
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    acc_balance_map[from]->set_latest_height(view_block.block_info().height());
    acc_balance_map[from]->set_tx_index(tx_index);
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    SHARDORA_DEBUG("status: %d, success join elect: %s, pool: %u, height: %lu, des shard: %d",
        block_tx.status(), common::Encode::HexEncode(from).c_str(),
        view_block.qc().pool_index(),
        block.height(),
        join_info.shard_id());
    return kConsensusSuccess;
}

int JoinElectTxItem::HandleStakeOperation(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx,
        bls::protobuf::JoinElectInfo& join_info,
        const std::string& from,
        uint64_t& from_balance,
        uint64_t gas_used,
        uint64_t store_gas) {
    
    // Stake operations can only be processed in root shard
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Stake operation can only be processed in root shard, current shard: %u",
            common::GlobalInfo::Instance()->network_id());
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        acc_balance_map[from]->set_latest_height(view_block.block_info().height());
        acc_balance_map[from]->set_tx_index(tx_index);
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
        return kConsensusError;
    }
    
    uint64_t stake_amount = join_info.stake_amount();
    
    // Validate stake amount is multiple of minimum unit (8 * 10^8)
    static const uint64_t kMinStakeUnit = 8 * 100000000llu;
    if (stake_amount % kMinStakeUnit != 0) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Invalid stake amount: %lu, must be multiple of %lu",
            stake_amount, kMinStakeUnit);
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusError;
    }
    
    if (from_balance < stake_amount + (gas_used + store_gas) * block_tx.gas_price()) {
        block_tx.set_status(consensus::kConsensusAccountBalanceError);
        SHARDORA_ERROR("Insufficient balance for stake: have %lu, need %lu + gas",
            from_balance, stake_amount);
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusAccountBalanceError;
    }
    
    // Get root stake pool address
    std::string root_pool_address = common::GetRootStakePoolAddress();
    
    // Check if there's existing stake info (for additional staking)
    uint64_t existing_stake = 0;
    uint64_t existing_timestamp = 0;
    bool has_existing_stake = prefix_db_->GetStakeInfo(
        from, &existing_stake, &existing_timestamp);
    
    // Calculate total staked amount
    uint64_t total_staked = existing_stake + stake_amount;
    
    // Deduct stake amount and gas from sender
    from_balance -= stake_amount + (gas_used + store_gas) * block_tx.gas_price();
    gas_used += store_gas;
    
    // Add stake to root pool address
    uint64_t pool_balance = 0;
    uint64_t pool_nonce = 0;
    int pool_status = GetTempAccountBalance(shardora_host, root_pool_address, acc_balance_map, &pool_balance, &pool_nonce);
    if (pool_status == kConsensusSuccess) {
        pool_balance += stake_amount;
        acc_balance_map[root_pool_address]->set_balance(pool_balance);
        acc_balance_map[root_pool_address]->set_latest_height(view_block.block_info().height());
    }
    
    // Save/Update stake info with block timestamp
    // Use block timestamp (not user-provided) to prevent timestamp manipulation
    // Use db_batch to delay write until block commit
    uint64_t block_timestamp = view_block.block_info().timestamp();
    prefix_db_->SaveStakeInfo(
        from,
        total_staked,  // Save total staked amount
        block_timestamp,  // Use block timestamp (not user-provided)
        view_block.block_info().height(),
        shardora_host.db_batch_);  // Use db_batch for delayed write
    
    // Update join_info with total staked for FTS calculation
    join_info.set_total_staked(total_staked);
    join_info.set_stoke(total_staked);  // Use total_staked for PoS weight
    
    auto* block_join_info = view_block.mutable_block_info()->add_joins();
    *block_join_info = join_info;
    
    // Update account balance
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    acc_balance_map[from]->set_latest_height(view_block.block_info().height());
    acc_balance_map[from]->set_tx_index(tx_index);
    
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    
    if (has_existing_stake) {
        SHARDORA_DEBUG("Additional stake in root shard: addr=%s, added=%lu, total=%lu, "
            "block_timestamp=%lu (previous: %lu), pool=%s",
            common::Encode::HexEncode(from).c_str(),
            stake_amount, total_staked,
            block_timestamp, existing_timestamp,
            common::Encode::HexEncode(root_pool_address).c_str());
    } else {
        SHARDORA_DEBUG("Initial stake in root shard: addr=%s, amount=%lu, block_timestamp=%lu, pool=%s",
            common::Encode::HexEncode(from).c_str(),
            stake_amount, block_timestamp,
            common::Encode::HexEncode(root_pool_address).c_str());
    }
    
    return kConsensusSuccess;
}

int JoinElectTxItem::HandleRedeemOperation(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx,
        bls::protobuf::JoinElectInfo& join_info,
        const std::string& from,
        uint64_t& from_balance,
        uint64_t gas_used) {
    
    // Redeem operations can only be processed in root shard
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Redeem operation can only be processed in root shard, current shard: %u",
            common::GlobalInfo::Instance()->network_id());
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        acc_balance_map[from]->set_latest_height(view_block.block_info().height());
        acc_balance_map[from]->set_tx_index(tx_index);
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
        return kConsensusError;
    }
    
    // Get stake info
    uint64_t total_staked = 0;
    uint64_t stake_timestamp = 0;
    
    if (!prefix_db_->GetStakeInfo(from, &total_staked, &stake_timestamp)) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("No stake info found for address: %s",
            common::Encode::HexEncode(from).c_str());
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusError;
    }
    
    // Check if lock period has passed using timestamps
    static const uint64_t kStakeLockSeconds = 1008 * 600;  // 604,800 seconds = 7 days
    uint64_t current_timestamp = view_block.block_info().timestamp();
    uint64_t seconds_passed = current_timestamp - stake_timestamp;
    
    if (seconds_passed < kStakeLockSeconds) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Stake lock period not passed: %lu/%lu seconds (%lu days)",
            seconds_passed, kStakeLockSeconds, seconds_passed / 86400);
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusError;
    }
    
    // Transfer stake back from root pool address to user
    std::string root_pool_address = common::GetRootStakePoolAddress();
    uint64_t pool_balance = 0;
    uint64_t pool_nonce = 0;
    
    int pool_status = GetTempAccountBalance(shardora_host, root_pool_address, 
                                             acc_balance_map, &pool_balance, &pool_nonce);
    if (pool_status != kConsensusSuccess) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Failed to get root pool balance");
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusError;
    }
    
    if (pool_balance < total_staked) {
        block_tx.set_status(consensus::kConsensusError);
        SHARDORA_ERROR("Insufficient root pool balance: have %lu, need %lu",
            pool_balance, total_staked);
        from_balance -= gas_used * block_tx.gas_price();
        acc_balance_map[from]->set_balance(from_balance);
        acc_balance_map[from]->set_nonce(block_tx.nonce());
        return kConsensusError;
    }
    
    // Deduct gas from sender
    from_balance -= gas_used * block_tx.gas_price();
    
    // Transfer total staked amount from pool to user
    pool_balance -= total_staked;
    from_balance += total_staked;
    
    // Update balances
    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    acc_balance_map[from]->set_latest_height(view_block.block_info().height());
    acc_balance_map[from]->set_tx_index(tx_index);
    
    acc_balance_map[root_pool_address]->set_balance(pool_balance);
    acc_balance_map[root_pool_address]->set_latest_height(view_block.block_info().height());
    
    // Remove stake info
    // Use db_batch to delay write until block commit
    prefix_db_->RemoveStakeInfo(from, shardora_host.db_batch_);
    
    // Set stoke to 0 for redeem operation (no PoS weight)
    join_info.set_stoke(0);
    join_info.set_total_staked(0);
    
    // Add to block joins to record the redeem operation
    auto* block_join_info = view_block.mutable_block_info()->add_joins();
    *block_join_info = join_info;
    
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    
    SHARDORA_DEBUG("Redeemed stake in root shard: addr=%s, amount=%lu, "
        "seconds_passed=%lu, stake_timestamp=%lu, current_timestamp=%lu, pool=%s, stoke set to 0",
        common::Encode::HexEncode(from).c_str(),
        total_staked, seconds_passed, stake_timestamp, current_timestamp,
        common::Encode::HexEncode(root_pool_address).c_str());
    
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
