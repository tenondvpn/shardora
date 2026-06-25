#include "consensus/zbft/to_tx_local_item.h"

#include "shardoravm/execution.h"

namespace shardora {

namespace consensus {

int ToTxLocalItem::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    pools::protobuf::ToTxMessageItem to_tx_item;
    if (!to_tx_item.ParseFromString(tx_info->value())) {
        block_tx.set_status(kConsensusError);
        SHARDORA_WARN("local get to txs info failed: %s, unique: %s",
            common::Encode::HexEncode(tx_info->value()).c_str(),
            common::Encode::HexEncode(tx_info->key()).c_str());
        return consensus::kConsensusSuccess;
    }

    uint64_t src_to_balance = 0;
    uint64_t src_to_nonce = 0;
    GetTempAccountBalance(shardora_host, block_tx.to(), acc_balance_map, &src_to_balance, &src_to_nonce);
    auto& unique_hash = tx_info->key();
    std::string val;
    if (shardora_host.GetKeyValue(block_tx.to(), unique_hash, &val) == shardoravm::kShardoravmSuccess) {
        SHARDORA_DEBUG("unique hash has consensus: %s, %s, %lu", 
            common::Encode::HexEncode(unique_hash).c_str(),
            common::Encode::HexEncode(to_tx_item.des()).c_str(),
            to_tx_item.amount());
        if (!acc_balance_map[block_tx.to()]->has_balance()) {
            acc_balance_map.erase(block_tx.to());
        }
        
        return consensus::kConsensusError;
    }

    InitHost(shardora_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    shardora_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(0);
    auto& block_to_txs = *view_block.mutable_block_info()->mutable_local_to();
    CreateLocalToTx(tx_index, view_block, shardora_host, acc_balance_map, to_tx_item, block_to_txs);
    SHARDORA_DEBUG("success call to tx local block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu, %s, %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), src_to_nonce, block_tx.nonce(),
        common::Encode::HexEncode(to_tx_item.des()).c_str(), to_tx_item.amount());
    acc_balance_map[block_tx.to()]->set_balance(src_to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
    acc_balance_map[block_tx.to()]->set_tx_index(tx_index);
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
    SHARDORA_DEBUG("success consensus local transfer to unique hash: %s, %s",
        common::Encode::HexEncode(unique_hash).c_str(), 
        ProtobufToJson(block_to_txs).c_str());
    view_block.mutable_block_info()->add_unique_hashs(block_tx.unique_hash());
    return consensus::kConsensusSuccess;
}

void ToTxLocalItem::CreateLocalToTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        const pools::protobuf::ToTxMessageItem& to_tx_item, 
        block::protobuf::ConsensusToTxs& block_to_txs) {
    if (to_tx_item.des().size() != common::kUnicastAddressLength && 
            to_tx_item.des().size() != common::kPreypamentAddressLength) {
        SHARDORA_ERROR("invalid to tx item: %s", ProtobufToJson(to_tx_item).c_str());
        //assert(false);
        return;
    }

    auto new_addr_func = [&](const std::string& addr, uint64_t amount) {
        uint64_t to_balance = 0;
        uint64_t nonce = 0;
        int balance_status = GetTempAccountBalance(
            shardora_host,
            addr, 
            acc_balance_map, 
            &to_balance, 
            &nonce);
        if (balance_status != kConsensusSuccess) {
            SHARDORA_DEBUG("create new address: %s, balance: %lu",
                common::Encode::HexEncode(addr).c_str(),
                amount);
            to_balance = 0;
            auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
            addr_info->set_addr(addr);
            addr_info->set_sharding_id(view_block.qc().network_id());
            addr_info->set_pool_index(view_block.qc().pool_index());
            addr_info->set_type(address::protobuf::kNormal);
            addr_info->set_latest_height(view_block.block_info().height());
            acc_balance_map[addr] = addr_info;
        } else {
            SHARDORA_DEBUG("success get to balance: %s, %lu",
                common::Encode::HexEncode(addr).c_str(), 
                to_balance);
        }

        if (amount <= 0 && 
                to_tx_item.library_bytes().empty()) {
            SHARDORA_DEBUG("failed just contract set prefund add addr: %s, to item: %s", 
                common::Encode::HexEncode(addr).c_str(), 
                ProtobufToJson(to_tx_item).c_str());
            return;
        }

        to_balance += amount;
        acc_balance_map[addr]->set_balance(to_balance);
        acc_balance_map[addr]->set_nonce(nonce);
        acc_balance_map[addr]->set_latest_height(view_block.block_info().height());
        acc_balance_map[addr]->set_tx_index(tx_index);
        if (!to_tx_item.library_bytes().empty()) {
            acc_balance_map[addr]->set_bytes_code(to_tx_item.library_bytes());
        }

        SHARDORA_DEBUG("success add addr: %s, value: %s, to item: %s", 
            common::Encode::HexEncode(addr).c_str(), 
            ProtobufToJson(*(acc_balance_map[addr])).c_str(),
            ProtobufToJson(to_tx_item).c_str());
        SHARDORA_DEBUG("add local to: %s, balance: %lu, amount: %lu",
            common::Encode::HexEncode(addr).c_str(),
            to_balance,
            amount);
    };

    auto addr = to_tx_item.des();
    if (to_tx_item.des().size() == common::kPreypamentAddressLength) {
        addr = addr.substr(0, common::kUnicastAddressLength);
        new_addr_func(to_tx_item.des(), to_tx_item.prefund());
    }
    
    new_addr_func(addr, to_tx_item.amount());
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




