#include "consensus/zbft/to_tx_local_item.h"

#include "zjcvm/execution.h"

namespace shardora {

namespace consensus {

int ToTxLocalItem::HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    pools::protobuf::ToTxMessageItem to_tx_item;
    if (!to_tx_item.ParseFromString(tx_info->value())) {
        block_tx.set_status(kConsensusError);
        ZJC_WARN("local get to txs info failed: %s",
            common::Encode::HexEncode(tx_info->value()).c_str());
        return consensus::kConsensusSuccess;
    }

    uint64_t src_to_balance = 0;
    uint64_t src_to_nonce = 0;
    GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &src_to_balance, &src_to_nonce);
    // if (to_nonce + 1 != block_tx.nonce()) {
    //     block_tx.set_status(kConsensusNonceInvalid);
    //     ZJC_WARN("failed call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
    //         view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
    //     return consensus::kConsensusSuccess;
    // }

    auto& unique_hash = tx_info->key();
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash, &val) == zjcvm::kZjcvmSuccess) {
        ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }

    InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    zjc_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(src_to_nonce + 1);
    block::protobuf::ConsensusToTxs& block_to_txs = *view_block.mutable_block_info()->mutable_local_to();
    // dispatch to txs to tx pool
    uint64_t to_balance = 0;
    uint64_t nonce = 0;
    int balance_status = GetTempAccountBalance(
        zjc_host,
        to_tx_item.des(), 
        acc_balance_map, 
        &to_balance, 
        &nonce);
    if (balance_status != kConsensusSuccess) {
        ZJC_DEBUG("create new address: %s, balance: %lu",
            common::Encode::HexEncode(to_tx_item.des()).c_str(),
            to_tx_item.amount());
        to_balance = 0;
        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        addr_info->set_addr(to_tx_item.des());
        addr_info->set_sharding_id(view_block.qc().network_id());
        addr_info->set_pool_index(view_block.qc().pool_index());
        addr_info->set_type(address::protobuf::kNormal);
        addr_info->set_latest_height(view_block.block_info().height());
        acc_balance_map[to_tx_item.des()] = addr_info;
    } else {
        ZJC_DEBUG("success get to balance: %s, %lu",
            common::Encode::HexEncode(to_tx_item.des()).c_str(), 
            to_balance);
    }

    auto to_tx = block_to_txs.add_tos();
    to_balance += to_tx_item.amount();
    to_tx->set_to(to_tx_item.des());
    to_tx->set_balance(to_balance);
    to_tx->set_nonce(nonce);
    acc_balance_map[to_tx_item.des()]->set_balance(to_balance);
    acc_balance_map[to_tx_item.des()]->set_nonce(nonce);
    // prefix_db_->AddAddressInfo(to_tx_item.des(), *(acc_balance_map[to_tx_item.des()]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(to_tx_item.des()).c_str(), 
        ProtobufToJson(*(acc_balance_map[to_tx_item.des()])).c_str());

    ZJC_DEBUG("add local to: %s, balance: %lu, amount: %lu",
        common::Encode::HexEncode(to_tx_item.des()).c_str(),
        to_balance,
        to_tx_item.amount());

    ZJC_WARN("success call to tx local block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), src_to_nonce, block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_balance(src_to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
    ZJC_DEBUG("success consensus local transfer to unique hash: %s, %s",
        common::Encode::HexEncode(unique_hash).c_str(), 
        ProtobufToJson(block_to_txs).c_str());
    return consensus::kConsensusSuccess;
}

int ToTxLocalItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    if (!DefaultTxItem(tx_info, block_tx)) {
        return consensus::kConsensusError;
    }

    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora




