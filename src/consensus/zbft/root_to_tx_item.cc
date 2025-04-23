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
    protos::AddressInfoPtr account_info = nullptr;
    if (block_tx.to().size() == security::kUnicastAddressLength * 2) {
        // gas prepayment
        account_info = zjc_host.view_block_chain_->ChainGetAccountInfo(
            block_tx.to().substr(0, security::kUnicastAddressLength));
        // if (account_info == nullptr) {
        //     block_tx.set_status(kConsensusAccountNotExists);
        //     return kConsensusSuccess;
        // }
    } else {
        account_info = zjc_host.view_block_chain_->ChainGetAccountInfo(block_tx.to());
    }

    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    auto& unique_hash = tx_info->key();
    auto str_key = block_tx.to() + unique_hash;
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash, &val) == zjcvm::kZjcvmSuccess) {
        ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }
    
    InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    zjc_host.SaveKeyValue(block_tx.to(), unique_hash, tx_info->value());
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(to_nonce + 1);
    uint32_t sharding_id = 0;
    if (account_info != nullptr) {
        sharding_id = account_info->sharding_id();
    } else {
        if (block_tx.step() == pools::protobuf::kCreateLibrary || 
                block_tx.step() == pools::protobuf::kContractCreate) {
            // 合约创建，用户指定 sharding
            uint32_t* data = (uint32_t*)tx_info->value().c_str();
            sharding_id = data[0];
        }

        if (sharding_id == 0) {
            std::mt19937_64 g2(view_block.block_info().height() ^ vss_mgr_->EpochRandom());
            sharding_id = (g2() % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
                network::kConsensusShardBeginNetworkId;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        addr_info->set_addr(block_tx.to());
        addr_info->set_sharding_id(sharding_id);
        addr_info->set_pool_index(common::GetAddressPoolIndex(block_tx.to()));
        addr_info->set_type(address::protobuf::kNormal);
        addr_info->set_latest_height(view_block.block_info().height());
        acc_balance_map[block_tx.to()] = addr_info;
    }

    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    if (block_tx.status() == kConsensusSuccess) {
        auto iter = zjc_host.cross_to_map_.find(block_tx.to());
        std::shared_ptr<block::protobuf::ToAddressItemInfo> to_item_ptr;
        if (iter == zjc_host.cross_to_map_.end()) {
            to_item_ptr = std::make_shared<block::protobuf::ToAddressItemInfo>();
            to_item_ptr->set_des(block_tx.to());
            to_item_ptr->set_amount(block_tx.amount());
            to_item_ptr->set_des_sharding_id(sharding_id);
            zjc_host.cross_to_map_[to_item_ptr->des()] = to_item_ptr;
        } else {
            to_item_ptr = iter->second;
            to_item_ptr->set_amount(block_tx.amount() + to_item_ptr->amount());
        }
    }

    // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());

    ZJC_DEBUG("adress: %s, set sharding id: %u, pool index: %d",
        common::Encode::HexEncode(block_tx.to()).c_str(), sharding_id, common::GetAddressPoolIndex(block_tx.to()));
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
