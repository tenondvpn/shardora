#include "consensus/zbft/root_to_tx_item.h"

#include "network/network_utils.h"
#include "protos/tx_storage_key.h"
#include "vss/vss_manager.h"

namespace zjchain {

namespace consensus {

RootToTxItem::RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr),
        max_sharding_id_(max_consensus_sharding_id),
        vss_mgr_(vss_mgr) {
    if (max_sharding_id_ < network::kConsensusShardBeginNetworkId) {
        max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    }
}

RootToTxItem::~RootToTxItem() {}

int RootToTxItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    protos::AddressInfoPtr account_info = nullptr;
    if (block_tx.to().size() == security::kUnicastAddressLength * 2) {
        // gas prepayment
        account_info = account_mgr_->GetAccountInfo(
            thread_idx,
            block_tx.to().substr(0, security::kUnicastAddressLength));
        if (account_info == nullptr) {
            block_tx.set_status(kConsensusAccountNotExists);
            return kConsensusSuccess;
        }
    } else {
        account_info = account_mgr_->GetAccountInfo(thread_idx, block_tx.to());
    }

    char des_sharding_and_pool[8];
    uint32_t* des_info = (uint32_t*)des_sharding_and_pool;
    if (account_info != nullptr) {
        des_info[0] = account_info->sharding_id();
        des_info[1] = account_info->pool_index();
    } else {
        uint32_t sharding_id = 0;
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            if (block_tx.storages(i).key() == protos::kCreateContractCallerSharding) {
                uint32_t* data = (uint32_t*)block_tx.storages(i).val_hash().c_str();
                des_info[0] = data[0];
                des_info[1] = data[1];
                break;
            }
        }

        std::mt19937_64 g2(block.height() ^ vss_mgr_->EpochRandom());
        if (sharding_id == 0) {
            des_info[0] = (g2() % (max_sharding_id_ - network::kConsensusShardBeginNetworkId + 1)) +
                network::kConsensusShardBeginNetworkId;
            // pool index just binding with address
            des_info[1] = common::GetAddressPoolIndex(block_tx.to());
        }
    }

    auto& storage = *block_tx.add_storages();
    storage.set_key(protos::kRootCreateAddressKey);
    storage.set_val_hash(std::string(des_sharding_and_pool, sizeof(des_sharding_and_pool)));
    ZJC_DEBUG("adress: %s, set sharding id: %u, pool index: %d",
        common::Encode::HexEncode(block_tx.to()).c_str(), des_info[0], des_info[1]);
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain
