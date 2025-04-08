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
        const view_block::protobuf::ViewBlockItem& view_block,
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
    auto str_key = block_tx.to() + unique_hash_;
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash_, &val) == zjcvm::kZjcvmSuccess) {
        ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash_).c_str());
        return consensus::kConsensusError;
    }
    
    address::protobuf::KeyValueInfo kv_info;
    kv_info.set_value("1");
    kv_info.set_height(to_nonce + 1);
    zjc_host.SaveKeyValue(block_tx.to(), unique_hash_, "1");
    prefix_db_->SaveTemporaryKv(str_key, kv_info.SerializeAsString(), zjc_host.db_batch_);
    block_tx.set_unique_hash(unique_hash_);
    block_tx.set_nonce(to_nonce + 1);
    char des_sharding_and_pool[8];
    uint32_t* des_info = (uint32_t*)des_sharding_and_pool;
    if (account_info != nullptr) {
        des_info[0] = account_info->sharding_id();
        des_info[1] = account_info->pool_index();
    } else {
        uint32_t sharding_id = 0;
        if (block_tx.step() == pools::protobuf::kCreateLibrary || 
                block_tx.step() == pools::protobuf::kContractCreate) {
            // 合约创建，用户指定 sharding
            uint32_t* data = (uint32_t*)block_tx.storages(0).value().c_str();
            des_info[0] = data[0];
            des_info[1] = data[1];
            sharding_id = data[0];
        }

        if (sharding_id == 0) {
            std::mt19937_64 g2(view_block.block_info().height() ^ vss_mgr_->EpochRandom());
            des_info[0] = (g2() % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
                network::kConsensusShardBeginNetworkId;
            // pool index just binding with address
            des_info[1] = common::GetAddressPoolIndex(block_tx.to());
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        addr_info->set_addr(block_tx.to());
        addr_info->set_sharding_id(des_info[0]);
        addr_info->set_pool_index(common::GetAddressPoolIndex(block_tx.to()));
        addr_info->set_type(address::protobuf::kNormal);
        addr_info->set_latest_height(view_block.block_info().height());
        acc_balance_map[block_tx.to()] = addr_info;
    }

    zjc_host.root_create_address_tx_ = &block_tx;
    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    auto& storage = *block_tx.add_storages();
    storage.set_key(protos::kRootCreateAddressKey);
    storage.set_value(std::string((char*)&des_sharding_and_pool, sizeof(des_sharding_and_pool)));
    ZJC_DEBUG("adress: %s, set sharding id: %u, pool index: %d",
        common::Encode::HexEncode(block_tx.to()).c_str(), des_info[0], des_info[1]);
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora
