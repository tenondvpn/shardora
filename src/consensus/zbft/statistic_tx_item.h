#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class StatisticTxItem : public TxItemBase {
public:
    StatisticTxItem(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~StatisticTxItem() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        ZJC_DEBUG("pools statistic tag tx consensus coming: %s, nonce: %lu, val: %s", 
            common::Encode::HexEncode(tx_info.to()).c_str(), 
            tx_info.nonce(),
            common::Encode::HexEncode(tx_info.value()).c_str());
        if (!DefaultTxItem(tx_info, block_tx)) {
            return consensus::kConsensusError;
        }

        // change
        if (tx_info.key().empty() || tx_info.value().empty()) {
            return consensus::kConsensusError;
        }

        unique_hash_ = tx_info.key();
        auto* storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        storage->set_value(tx_info.value());
        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        if (GetTempAccountBalance(
                zjc_host, 
                block_tx.to(), 
                acc_balance_map, 
                &to_balance, 
                &to_nonce) != consensus::kConsensusSuccess) {
            return consensus::kConsensusError;
        }

        auto str_key = block_tx.to() + unique_hash_;
        std::string val;
        if (zjc_host.GetKeyValue(block_tx.to(), unique_hash_, &val) == zjcvm::kZjcvmSuccess) {
            ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash_).c_str());
            return consensus::kConsensusError;
        }

        pools::protobuf::ElectStatistic elect_statistic;
        if (!elect_statistic.ParseFromString(block_tx.storages(0).value())) {
            assert(false);
            return consensus::kConsensusError;
        }
    
        if (elect_statistic.sharding_id() != view_block.qc().network_id()) {
            ZJC_DEBUG("invalid sharding id %u, %u", elect_statistic.sharding_id(), view_block.qc().network_id());
            return consensus::kConsensusError;
        }

        address::protobuf::KeyValueInfo kv_info;
        kv_info.set_value("1");
        kv_info.set_height(to_nonce + 1);
        zjc_host.SaveKeyValue(block_tx.to(), unique_hash_, "1");
        prefix_db_->SaveTemporaryKv(str_key, kv_info.SerializeAsString(), zjc_host.db_batch_);
        block_tx.set_unique_hash(unique_hash_);
        block_tx.set_nonce(to_nonce + 1);
        ZJC_WARN("success call statistic tx pool: %d, view: %lu, "
            "to_nonce: %lu. tx nonce: %lu, to: %s, unique hash: %s", 
            view_block.qc().pool_index(), view_block.qc().view(),
            to_nonce, block_tx.nonce(),
            common::Encode::HexEncode(block_tx.to()).c_str(),
            common::Encode::HexEncode(unique_hash_).c_str());
        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(to_nonce + 1);
        // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
        ZJC_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(block_tx.to()).c_str(), 
            ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());

        pools::protobuf::PoolStatisticTxInfo pool_st_info;
        pool_st_info.set_height(elect_statistic.statistic_height());
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto statistic_info = pool_st_info.add_pool_statisitcs();
            statistic_info->set_pool_index(i);
            statistic_info->set_min_height(elect_statistic.height_info().heights(i).min_height());
            statistic_info->set_max_height(elect_statistic.height_info().heights(i).max_height());
        }

        prefix_db_->SaveLatestPoolStatisticTag(elect_statistic.sharding_id(), pool_st_info, zjc_host.db_batch_);
        zjc_host.statisitc_tx_ = &block_tx;
        return consensus::kConsensusSuccess;
    }

private:
    std::string unique_hash_;

    DISALLOW_COPY_AND_ASSIGN(StatisticTxItem);
};

};  // namespace consensus

};  // namespace shardora
