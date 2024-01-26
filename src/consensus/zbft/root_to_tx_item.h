#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace vss {
    class VssManager;
};

namespace consensus {

class RootToTxItem : public TxItemBase {
public:
    RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr);
    virtual ~RootToTxItem();

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            std::shared_ptr<db::DbWriteBatch>& db_batch,
            block::protobuf::BlockTx* block_tx) {
        DefaultTxItem(tx_info, block_tx);
        // change
        if (tx_info.key().empty() ||
                tx_info.key() != protos::kNormalTos ||
                tx_info.value().empty() ||
                tx_info.value().size() % 32 != 0) {
            return consensus::kConsensusError;
        }

        uint32_t offset = 0;
        uint32_t count = tx_info.value().size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            auto storage = block_tx->add_storages();
            std::string tmp(tx_info.value().c_str() + offset, 32);
            storage->set_key(protos::kNormalToShards);
            storage->set_val_hash(tmp);
            offset += 32;
            ZJC_DEBUG("root to tx add key: %s, value: %s",
                protos::kNormalToShards.c_str(), common::Encode::HexEncode(tmp).c_str());
        }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    uint32_t max_sharding_id_ = 0;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    
    DISALLOW_COPY_AND_ASSIGN(RootToTxItem);
};

};  // namespace consensus

};  // namespace zjchain
