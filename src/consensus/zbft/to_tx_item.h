#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class ToTxItem : public TxItemBase {
public:
    ToTxItem(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}

    virtual ~ToTxItem() {}

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        ZJC_DEBUG("to tx consensus coming: %s, nonce: %lu, val: %s", 
            common::Encode::HexEncode(tx_info.to()).c_str(), 
            tx_info.nonce(),
            common::Encode::HexEncode(tx_info.value()).c_str());
        if (!DefaultTxItem(tx_info, block_tx)) {
            return consensus::kConsensusError;
        }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
            view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        if (view_block.block_info().has_normal_to()) {
            return consensus::kConsensusError;
        }

        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
        // if (to_nonce + 1 != block_tx.nonce()) {
        //     block_tx.set_status(kConsensusNonceInvalid);
        //     ZJC_WARN("failed call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
        //         view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        //     return consensus::kConsensusSuccess;
        // }

        ZJC_WARN("call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
        auto& unique_hash = tx_info->key();
        std::string val;
        if (zjc_host.GetKeyValue(block_tx.to(), unique_hash, &val) == zjcvm::kZjcvmSuccess) {
            ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
            return consensus::kConsensusError;
        }

        InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
        zjc_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
        block_tx.set_unique_hash(unique_hash);
        block_tx.set_nonce(to_nonce + 1);
        auto& all_to_txs = *view_block.mutable_block_info()->mutable_normal_to();
        if (!all_to_txs.ParseFromString(tx_info->value()) || all_to_txs.to_tx_arr_size() == 0) {
            return consensus::kConsensusError;
        }
        
        assert(all_to_txs.to_tx_arr_size() > 0);
        prefix_db_->SaveLatestToTxsHeights(all_to_txs.to_heights(), zjc_host.db_batch_);
        // for (uint32_t i = 0; i < all_to_txs.to_tx_arr_size(); ++i) {
        //     auto to_heights = all_to_txs.mutable_to_tx_arr(i);
        //     auto& heights = *to_heights->mutable_to_heights();
        //     heights.set_block_height(view_block.block_info().height());
        //     ZJC_DEBUG("new to tx coming: %lu, sharding id: %u, to_tx: %s, des sharding id: %u",
        //         view_block.block_info().height(), 
        //         heights.sharding_id(), 
        //         ProtobufToJson(*to_heights).c_str(),
        //         to_heights->to_heights().sharding_id());
        //     for (uint32_t j = 0; j < to_heights->tos_size(); ++j) {
        //         auto tos_item = to_heights->tos(j);
        //         if (tos_item.step() == pools::protobuf::kJoinElect) {
        //             for (int32_t join_i = 0; join_i < tos_item.join_infos_size(); ++join_i) {
        //                 if (tos_item.join_infos(join_i).shard_id() != network::kRootCongressNetworkId) {
        //                     continue;
        //                 }
        
        //                 prefix_db_->SaveNodeVerificationVector(
        //                     tos_item.des(),
        //                     tos_item.join_infos(join_i),
        //                     zjc_host.db_batch_);
        //                 ZJC_DEBUG("success handle kElectJoin tx: %s, net: %u, pool: %u, block net: %u, "
        //                     "block pool: %u, block height: %lu, local net id: %u", 
        //                     common::Encode::HexEncode(tos_item.des()).c_str(), 
        //                     tos_item.sharding_id(),
        //                     tos_item.pool_index(),
        //                     view_block.qc().network_id(), 
        //                     view_block.qc().pool_index(), 
        //                     view_block.block_info().height(),
        //                     common::GlobalInfo::Instance()->network_id());
        //             }
        //         }
        //     }
        // }

        acc_balance_map[block_tx.to()]->set_balance(to_balance);
        acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
        ZJC_DEBUG("success add addr: %s, value: %s", 
            common::Encode::HexEncode(block_tx.to()).c_str(), 
            ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());

        ZJC_WARN("success call time block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        return consensus::kConsensusSuccess;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ToTxItem);
};

};  // namespace consensus

};  // namespace shardora
