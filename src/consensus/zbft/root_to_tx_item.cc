#include "consensus/zbft/root_to_tx_item.h"

#include "network/network_utils.h"
#include "protos/tx_storage_key.h"

namespace zjchain {

namespace consensus {

RootToTxItem::RootToTxItem(
    uint32_t max_consensus_sharding_id,
    const transport::MessagePtr& msg,
    std::shared_ptr<block::AccountManager>& account_mgr,
    std::shared_ptr<security::Security>& sec_ptr)
    : TxItemBase(msg, account_mgr, sec_ptr),
      max_sharding_id_(max_consensus_sharding_id) {
    if (max_sharding_id_ < network::kConsensusShardBeginNetworkId) {
        max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    }
}

virtual RootToTxItem::~RootToTxItem() {}

int RootToTxItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    auto account_info = account_mgr_->GetAcountInfo(thread_idx, block_tx.to());
    char des_sharding_and_pool[8];
    uint32_t* des_info = (uint32_t*)des_sharding_and_pool;
    if (account_info != nullptr) {
        des_info[0] = account_info->sharding_id();
        des_info[1] = account_info->pool_index();
    } else {
        std::mt19937_64 g2(block.height() ^ vss_mgr_->EpochRandom());
        des_info[0] = (g2() % (max_sharding_id_ - network::kConsensusShardBeginNetworkId)) +
            network::kConsensusShardBeginNetworkId;
        des_info[1] = g2() % common::kImmutablePoolSize;
    }

    auto& storage = *block_tx->add_storages();
    storage.set_key(protos::kRootCreateAddressKey);
    storage.set_val_hash(std::string(des_sharding_and_pool, sizeof(des_sharding_and_pool)));
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain
