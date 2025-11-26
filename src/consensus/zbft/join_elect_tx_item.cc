#include "consensus/zbft/join_elect_tx_item.h"
#include <bls/bls_utils.h>
#include <protos/tx_storage_key.h>

namespace shardora {

namespace consensus {

int JoinElectTxItem::HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    auto& block = view_block.block_info();
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t from_nonce = 0;
    uint64_t to_balance = 0;
    auto tmp_id = sec_ptr_->GetAddress(from_pk_);
    auto& from = address_info->addr();
    if (tmp_id != from) {
        block_tx.set_status(consensus::kConsensusError);
        // will never happen
        assert(false);
        return kConsensusError;
    }

    int balance_status = GetTempAccountBalance(zjc_host, from, acc_balance_map, &from_balance, &from_nonce);
    if (balance_status != kConsensusSuccess) {
        block_tx.set_status(balance_status);
        // will never happen
        assert(false);
        return kConsensusError;
    }

    bls::protobuf::JoinElectInfo join_info;
    do {
        gas_used = consensus::kJoinElectGas;
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

        if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("id: %s balance error: %lu, %lu, %lu",
                common::Encode::HexEncode(from).c_str(),
                from_balance, block_tx.gas_limit(), block_tx.gas_price());
            break;
        }

        if (block_tx.gas_limit() < gas_used) {
            block_tx.set_status(consensus::kConsensusUserSetGasLimitError);
            SHARDORA_DEBUG("1 id: %s  balance error: %lu, %lu, %lu",
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
            SHARDORA_ERROR("id: %s leader balance error: %llu, %llu",
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
        auto agg_bls_pk_proto = bls::BlsPublicKey2Proto(from_agg_bls_pk_);
        if (agg_bls_pk_proto) {
            *join_info.mutable_bls_pk() = *agg_bls_pk_proto;
        }

        auto proof_proto = bls::BlsPopProof2Proto(from_agg_bls_pk_proof_);
        if (proof_proto) {
            *join_info.mutable_bls_proof() = *proof_proto;
        }

        join_info.set_stoke(stoke);
        join_info.set_public_key(from_pk_);
    }

    acc_balance_map[from]->set_balance(from_balance);
    acc_balance_map[from]->set_nonce(block_tx.nonce());
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(from).c_str(), 
        ProtobufToJson(*(acc_balance_map[from])).c_str());
    block_tx.set_balance(from_balance);
    block_tx.set_gas_used(gas_used);
    auto* block_join_info = view_block.mutable_block_info()->add_joins();
    *block_join_info = join_info;
    SHARDORA_DEBUG("status: %d, success join elect: %s, pool: %u, height: %lu, des shard: %d",
        block_tx.status(), common::Encode::HexEncode(from).c_str(),
        view_block.qc().pool_index(),
        block.height(),
        join_info.shard_id());

    if (block_tx.status() == kConsensusSuccess) {
        auto iter = zjc_host.cross_to_map_.find(block_tx.to());
        std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
        if (iter == zjc_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>();
            to_item_ptr->set_des(block_tx.from());
            zjc_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
        } else {
            to_item_ptr = iter->second;
        }

        // *to_item_ptr->mutable_join_info() = join_info;
    }

    assert(false);
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
