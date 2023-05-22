#include "consensus/zbft/join_elect_tx_item.h"

namespace zjchain {

namespace consensus {

int JoinElectTxItem::HandleTx(
        uint8_t thread_idx,
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
    int balance_status = GetTempAccountBalance(
        thread_idx, from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    do  {
        gas_used = consensus::kJoinElectGas;
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            // TODO(): check key exists and reserve gas
            gas_used += (block_tx.storages(i).key().size() + msg_ptr->header.tx_proto().value().size()) *
                consensus::kKeyValueStorageEachBytes;
            if (block_tx.storages(i).key() == protos::kJoinElectVerifyG2) {
                std::string val;
                if (!prefix_db_->GetTemporaryKv(block.tx_list(i).storages(i).val_hash(), &val)) {
                    break;
                }

                init::protobuf::JoinElectInfo join_info;
                if (!join_info.ParseFromString(val)) {
                    break;
                }

                if (join_info.shard_id() != network::kRootCongressNetworkId) {
                    if (join_info.shard_id() != common::GlobalInfo::Instance()->network_id() ||
                            join_info.shard_id() != msg_ptr->address_info->sharding_id()) {
                        block_tx.set_status(consensus::kConsensusError);
                        ZJC_DEBUG("shard error: %lu", join_info.shard_id());
                        break;
                    }
                }
            }
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
            from_balance -= gas_used * block_tx.gas_price();
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

    if (elect_mgr_->IsIdExistsInAnyShard(from)) {
        block_tx.set_status(kConsensusElectNodeExists);
    } else {
        uint64_t stoke = 0;
        prefix_db_->GetElectNodeMinStoke(common::GlobalInfo::Instance()->network_id(), from, &stoke);
        auto stoke_storage = block_tx.add_storages();
        stoke_storage->set_key(protos::kElectNodeStoke);
        char data[8];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = stoke;
        stoke_storage->set_val_hash(std::string(data, sizeof(data)));
    }

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ZJC_DEBUG("status: %d, success join elect: %s, pool: %u, height: %lu",
        block_tx.status(), common::Encode::HexEncode(from).c_str(),
        block.pool_index(),
        block.height());
//     ZJC_DEBUG("handle tx success: %s, %lu, %lu, status: %d",
//         common::Encode::HexEncode(block_tx.gid()).c_str(),
//         block_tx.balance(),
//         block_tx.gas_used(),
//         block_tx.status());
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain
