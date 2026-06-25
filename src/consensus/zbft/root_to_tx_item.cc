#include "consensus/zbft/root_to_tx_item.h"

#include "common/hash.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "network/network_utils.h"
#include "protos/tx_storage_key.h"
#include "vss/vss_manager.h"

namespace shardora {

namespace consensus {

RootToTxItem::RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info),
        max_sharding_id_(max_consensus_sharding_id),
        vss_mgr_(vss_mgr) {
    if (max_sharding_id_ < network::kConsensusShardBeginNetworkId) {
        max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    }
}

RootToTxItem::~RootToTxItem() {}

int RootToTxItem::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(shardora_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    auto& unique_hash = tx_info->key();
    std::string val;
    if (shardora_host.GetKeyValue(block_tx.to(), unique_hash, &val) == shardoravm::kShardoravmSuccess) {
        SHARDORA_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }
    
    pools::protobuf::ToTxMessageItem to_item;
    if (!to_item.ParseFromString(tx_info->value())) {
        SHARDORA_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        //assert(false);
        return consensus::kConsensusError;
    }

    InitHost(shardora_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    shardora_host.SaveKeyValue(block_tx.to(), unique_hash, tx_info->value());
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(0);
    protos::AddressInfoPtr to_account_info = nullptr;
    auto to_addr = to_item.des().substr(0, common::kUnicastAddressLength);
    to_account_info = shardora_host.view_block_chain_->ChainGetAccountInfo(to_addr);
    uint32_t sharding_id = 0;
    if (to_account_info != nullptr) {
        sharding_id = to_account_info->sharding_id();
    } else {
        if (to_item.has_sharding_id() && 
                to_item.sharding_id() >= network::kConsensusShardBeginNetworkId && 
                to_item.sharding_id() < network::kConsensusShardEndNetworkId) {
            sharding_id = to_item.sharding_id();
        }

        if (sharding_id == 0) {
            uint64_t hash_value = common::Hash::Hash64(to_addr);
            sharding_id = (hash_value % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
                network::kConsensusShardBeginNetworkId;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        addr_info->set_addr(to_addr);
        addr_info->set_sharding_id(sharding_id);
        addr_info->set_pool_index(common::GetAddressPoolIndex(to_addr));
        addr_info->set_type(address::protobuf::kNormal);
        addr_info->set_latest_height(view_block.block_info().height());
        addr_info->set_balance(0);
        addr_info->set_nonce(0);
        addr_info->set_latest_height(view_block.block_info().height());
        addr_info->set_tx_index(tx_index);
        if (to_item.has_library_bytes()) {
            addr_info->set_bytes_code(to_item.library_bytes());
        }

        acc_balance_map[to_addr] = addr_info;
    }

    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
    acc_balance_map[block_tx.to()]->set_tx_index(tx_index);

    uint32_t status_code = block_tx.status();
    if (block_tx.status() == kConsensusSuccess) {
        auto iter = shardora_host.cross_to_map_.find(to_item.des());
        std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
        if (iter == shardora_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>(to_item);
            to_item_ptr->set_des_sharding_id(sharding_id);
            shardora_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
        } else {
            to_item_ptr = iter->second;
            to_item_ptr->set_amount(block_tx.amount() + to_item_ptr->amount());
            if (block_tx.has_contract_code() && !block_tx.contract_code().empty()) {
                to_item_ptr->set_library_bytes(block_tx.contract_code());
            }

            if (block_tx.contract_prefund() > 0) {
                to_item_ptr->set_prefund(block_tx.contract_prefund() + to_item_ptr->prefund());
            }
        }

        SHARDORA_DEBUG("success add addr cross to: %s, sharding_id: %u, to info: %s", 
            common::Encode::HexEncode(to_item.des()).c_str(), 
            sharding_id,
            ProtobufToJson(*to_item_ptr).c_str());
    }

    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);

    SHARDORA_DEBUG("success add addr to: %s, value: %s, unique hash: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str(),
        common::Encode::HexEncode(unique_hash).c_str());
    view_block.mutable_block_info()->add_unique_hashs(block_tx.unique_hash());
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
