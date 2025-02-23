#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace vss {
    class VssManager;
};

namespace consensus {

class RootToTxItem : public TxItemBase {
public:
    RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info);
    virtual ~RootToTxItem();

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        DefaultTxItem(tx_info, block_tx);
        // // change
        // if (tx_info.key().empty() ||
        //         tx_info.key() != protos::kNormalTos ||
        //         tx_info.value().empty()) {
        //     assert(false);
        //     return consensus::kConsensusError;
        // }

        // pools::protobuf::AllToTxMessage all_to_txs;
        // if (!all_to_txs.ParseFromString(tx_info.value())) {
        //     assert(false);
        //     return consensus::kConsensusError;
        // }

        // uint32_t offset = 0;
        // for (uint32_t i = 0; i < all_to_txs.to_tx_arr_size(); ++i) {
        //     auto storage = block_tx->add_storages();
        //     storage->set_key(protos::kNormalToShards);
        //     storage->set_value(all_to_txs.to_tx_arr(i).SerializeAsString());
        //     ZJC_DEBUG("root to tx add key: %s, value: %s",
        //         protos::kNormalToShards.c_str(), common::Encode::HexEncode(storage->value()).c_str());
        // }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    uint32_t max_sharding_id_ = 0;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    
    DISALLOW_COPY_AND_ASSIGN(RootToTxItem);
};

};  // namespace consensus

};  // namespace shardora
