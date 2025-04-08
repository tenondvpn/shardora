#include "consensus/zbft/to_tx_local_item.h"

#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ToTxLocalItem::HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    // gas just consume by from
    if (block_tx.storages_size() != 1) {
        block_tx.set_status(kConsensusError);
        return consensus::kConsensusSuccess;
    }

    pools::protobuf::ToTxMessage to_txs;
    if (!to_txs.ParseFromString(block_tx.storages(0).value())) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get to txs info failed: %s",
            common::Encode::HexEncode(block_tx.storages(0).value()).c_str());
        return consensus::kConsensusSuccess;
    }

    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    if (to_nonce + 1 != block_tx.nonce()) {
        block_tx.set_status(kConsensusNonceInvalid);
        ZJC_WARN("failed call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        return consensus::kConsensusSuccess;
    }

    auto str_key = block_tx.to() + unique_hash_;
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash_, &val) == zjcvm::kZjcvmSuccess) {
        ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash_).c_str());
        return consensus::kConsensusError;
    }

    address::protobuf::KeyValueInfo kv_info;
    kv_info.set_value(tx_info->value());
    kv_info.set_height(block_tx.nonce());
    zjc_host.SaveKeyValue(block_tx.to(), unique_hash_, "1");
    zjc_host.db_batch_.Put(str_key, kv_info.SerializeAsString());
    block::protobuf::ConsensusToTxs block_to_txs;
    std::string str_for_hash;
    str_for_hash.reserve(to_txs.tos_size() * 48);
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint64_t to_balance = 0;
        uint64_t nonce = 0;
        int balance_status = GetTempAccountBalance(
            zjc_host,
            to_txs.tos(i).des(), 
            acc_balance_map, 
            &to_balance, 
            &nonce);
        if (balance_status != kConsensusSuccess) {
            ZJC_DEBUG("create new address: %s, balance: %lu",
                common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
                to_txs.tos(i).amount());
            to_balance = 0;
            auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
            addr_info->set_addr(to_txs.tos(i).des());
            addr_info->set_sharding_id(view_block.qc().network_id());
            addr_info->set_pool_index(view_block.qc().pool_index());
            addr_info->set_type(address::protobuf::kNormal);
            addr_info->set_latest_height(view_block.block_info().height());
            acc_balance_map[to_txs.tos(i).des()] = addr_info;
        } else {
            ZJC_DEBUG("success get to balance: %s, %lu",
                common::Encode::HexEncode(to_txs.tos(i).des()).c_str(), 
                to_balance);
        }

        auto to_tx = block_to_txs.add_tos();
        to_balance += to_txs.tos(i).amount();
        to_tx->set_to(to_txs.tos(i).des());
        to_tx->set_balance(to_balance);
        str_for_hash.append(to_txs.tos(i).des());
        str_for_hash.append((char*)&to_balance, sizeof(to_balance));
        acc_balance_map[to_txs.tos(i).des()]->set_balance(to_balance);
        acc_balance_map[to_txs.tos(i).des()]->set_nonce(nonce);
        prefix_db_->AddAddressInfo(
            to_txs.tos(i).des(), 
            *(acc_balance_map[to_txs.tos(i).des()]), 
            zjc_host.db_batch_);
        ZJC_DEBUG("add local to: %s, balance: %lu, amount: %lu",
            common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
            to_balance,
            to_txs.tos(i).amount());
    }

    ZJC_WARN("failed call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
    auto storage = block_tx.add_storages();
    storage->set_key(protos::kConsensusLocalNormalTos);
    storage->set_value(block_to_txs.SerializeAsString());
    ZJC_DEBUG("success consensus local transfer to");
    return consensus::kConsensusSuccess;
}

int ToTxLocalItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    if (!DefaultTxItem(tx_info, block_tx)) {
        return consensus::kConsensusError;
    }
    // change
    if (tx_info.key().empty()) {
        return consensus::kConsensusError;
    }

    unique_hash_ = tx_info.key();
    auto storage = block_tx->add_storages();
    storage->set_key(tx_info.key());
    storage->set_value(tx_info.value());
    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora




