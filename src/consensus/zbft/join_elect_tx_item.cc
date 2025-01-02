#include "consensus/zbft/join_elect_tx_item.h"
#include <bls/bls_utils.h>
#include <protos/tx_storage_key.h>

namespace shardora {

namespace consensus {

int JoinElectTxItem::HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    auto& block = view_block.block_info();
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t to_balance = 0;
    auto tmp_id = sec_ptr_->GetAddress(from_pk_);
    auto& from = address_info->addr();
    if (tmp_id != from) {
        block_tx.set_status(consensus::kConsensusError);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    int balance_status = GetTempAccountBalance(from, acc_balance_map, &from_balance);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusSuccess;
    }

    bls::protobuf::JoinElectInfo join_info;
    do {
        gas_used = consensus::kJoinElectGas;
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            // TODO(): check key exists and reserve gas
            gas_used += (block_tx.storages(i).key().size() + tx_info.value().size()) *
                consensus::kKeyValueStorageEachBytes;
            if (block_tx.storages(i).key() == protos::kJoinElectVerifyG2) {
                if (!join_info.ParseFromString(block_tx.storages(i).value())) {
                    break;
                }

                if (join_info.shard_id() != network::kRootCongressNetworkId) {
                    if (join_info.shard_id() != common::GlobalInfo::Instance()->network_id() ||
                            join_info.shard_id() != address_info->sharding_id()) {
                        block_tx.set_status(consensus::kConsensusError);
                        ZJC_DEBUG("shard error: %lu", join_info.shard_id());
                        break;
                    }
                }
            }
        }

        if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("id: %s balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), block_tx.gas_price());
            break;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            ZJC_DEBUG("1 id: %s  balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), gas_used);
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
            ZJC_ERROR("id: %s leader balance error: %llu, %llu",
                common::Encode::HexEncode(from).c_str(),
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
        stoke_storage->set_value(std::string(data, sizeof(data)));
        auto pk_storage = block_tx.add_storages();
        pk_storage->set_key(protos::kNodePublicKey);
        pk_storage->set_value(from_pk_);
        auto agg_bls_pk_proto = bls::BlsPublicKey2Proto(from_agg_bls_pk_);
        if (agg_bls_pk_proto) {
            pk_storage->set_key(protos::kAggBlsPublicKey);
            pk_storage->set_value(agg_bls_pk_proto->SerializeAsString());
        }
        auto proof_proto = bls::BlsPopProof2Proto(from_agg_bls_pk_proof_);
        if (proof_proto) {
            pk_storage->set_key(protos::kAggBlsPopProof);
            pk_storage->set_value(proof_proto->SerializeAsString());
        }
    }

    acc_balance_map[from] = from_balance;
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    ZJC_DEBUG("status: %d, success join elect: %s, pool: %u, height: %lu, des shard: %d",
        block_tx.status(), common::Encode::HexEncode(from).c_str(),
        view_block.qc().pool_index(),
        block.height(),
        join_info.shard_id());
#ifndef NDEBUG
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        ZJC_DEBUG("status: %d, success join elect: %s, pool: %u, height: %lu, "
            "des shard: %d, key: %s, value size: %d",
            block_tx.status(), common::Encode::HexEncode(from).c_str(),
            view_block.qc().pool_index(),
            block.height(),
            join_info.shard_id(),
            block_tx.storages(i).key().c_str(),
            block_tx.storages(i).value().size());
    }
#endif

    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
