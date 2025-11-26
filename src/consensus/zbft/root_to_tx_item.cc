#include "consensus/zbft/root_to_tx_item.h"

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
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    auto& unique_hash = tx_info->key();
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash, &val) == zjcvm::kZjcvmSuccess) {
        SHARDORA_INFO("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }
    
    pools::protobuf::ToTxMessageItem to_item;
    if (!to_item.ParseFromString(tx_info->value())) {
        SHARDORA_INFO("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        assert(false);
        return consensus::kConsensusError;
    }

    InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    zjc_host.SaveKeyValue(block_tx.to(), unique_hash, tx_info->value());
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(to_nonce + 1);
    protos::AddressInfoPtr to_account_info = nullptr;
    auto to_addr = to_item.des().substr(0, common::kUnicastAddressLength);
    to_account_info = zjc_host.view_block_chain_->ChainGetAccountInfo(to_addr);
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
            std::mt19937_64 g2(view_block.block_info().height() ^ vss_mgr_->EpochRandom());
            sharding_id = (g2() % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
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
        if (to_item.has_library_bytes()) {
            addr_info->set_bytes_code(to_item.library_bytes());
        }

        acc_balance_map[to_addr] = addr_info;
    }

    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(to_nonce + 1);
    if (block_tx.status() == kConsensusSuccess) {
        auto iter = zjc_host.cross_to_map_.find(to_item.des());
        std::shared_ptr<pools::protobuf::ToTxMessageItem> to_item_ptr;
        if (iter == zjc_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<pools::protobuf::ToTxMessageItem>(to_item);
            to_item_ptr->set_des_sharding_id(sharding_id);
            zjc_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
        } else {
            to_item_ptr = iter->second;
            to_item_ptr->set_amount(block_tx.amount() + to_item_ptr->amount());
            if (block_tx.has_contract_code() && !block_tx.contract_code().empty()) {
                to_item_ptr->set_library_bytes(block_tx.contract_code());
            }

            if (block_tx.contract_prepayment() > 0) {
                to_item_ptr->set_prepayment(block_tx.contract_prepayment() + to_item_ptr->prepayment());
            }
        }

        SHARDORA_DEBUG("success add addr cross to: %s, to info: %s", 
            common::Encode::HexEncode(to_item.des()).c_str(), 
            ProtobufToJson(*to_item_ptr).c_str());
    }

    SHARDORA_DEBUG("success add addr to: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
