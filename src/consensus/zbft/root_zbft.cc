#include "consensus/zbft/root_zbft.h"

#include "elect/elect_manager.h"

namespace zjchain {

namespace consensus {

RootZbft::RootZbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr, 
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr)
        : Zbft(account_mgr, security_ptr, bls_mgr, tx_ptr, pools_mgr, tm_block_mgr),
        elect_mgr_(elect_mgr) {
}

RootZbft::~RootZbft() {

}

void RootZbft::DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block) {
    if (txs_ptr_->txs.size() == 1) {
        auto& tx = *txs_ptr_->txs.begin()->second;
        switch (tx.msg_ptr->header.tx_proto().step()) {
        case pools::protobuf::kConsensusRootElectShard:
            RootCreateElectConsensusShardBlock(zjc_block);
            break;
        case pools::protobuf::kConsensusRootTimeBlock:
            RootCreateTimerBlock(zjc_block);
            break;
        case pools::protobuf::kConsensusFinalStatistic:
            RootCreateFinalStatistic(zjc_block);
            break;
        case pools::protobuf::kRootCreateAddressCrossSharding:
            RootCreateAddressCrossSharding(zjc_block);
            break;
        case pools::protobuf::kStatistic:
            RootStatistic(zjc_block);
            break;
        default:
            RootCreateAccountAddressBlock(zjc_block);
            break;
        }
    } else {
        RootCreateAccountAddressBlock(zjc_block);
    }
}

void RootZbft::RootStatistic(block::protobuf::Block& zjc_block) {
    auto& tx_map = txs_ptr_->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->msg_ptr->header.tx_proto().step() != pools::protobuf::kRootCreateAddressCrossSharding) {
        ZJC_ERROR("tx is not timeblock tx");
        return;
    }

    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), db_batch_, &tx);
}

void RootZbft::RootCreateAddressCrossSharding(block::protobuf::Block& zjc_block) {
    auto& tx_map = txs_ptr_->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->msg_ptr->header.tx_proto().step() != pools::protobuf::kRootCreateAddressCrossSharding) {
        ZJC_ERROR("tx is not timeblock tx");
        return;
    }

    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), db_batch_, &tx);
}

void RootZbft::RootCreateAccountAddressBlock(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx_map = txs_ptr_->txs;
    std::unordered_map<std::string, int64_t> acc_balance_map;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx = *tx_list->Add();
        auto& src_tx = iter->second->msg_ptr->header.tx_proto();
        iter->second->TxToBlockTx(src_tx, db_batch_, &tx);
        // create address must to and have transfer amount
        if (tx.step() != pools::protobuf::kRootCreateAddress || tx.amount() <= 0) {
            ZJC_DEBUG("tx invalid step: %d, amount: %lu, src: %d, %lu",
                tx.step(), tx.amount(), src_tx.step(), src_tx.amount());
            tx_list->RemoveLast();
            assert(false);
            continue;
        }

        int do_tx_res = iter->second->HandleTx(
            txs_ptr_->thread_index,
            zjc_block,
            db_batch_,
            acc_balance_map,
            tx);
        if (do_tx_res != kConsensusSuccess) {
            tx_list->RemoveLast();
            assert(false);
            continue;
        }
    }
}

void RootZbft::RootCreateElectConsensusShardBlock(block::protobuf::Block& zjc_block) {
    auto& tx_map = txs_ptr_->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->msg_ptr->header.tx_proto().step() != pools::protobuf::kConsensusRootElectShard) {
        assert(false);
        return;
    }

    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), db_batch_, &tx);
    // use new node status
    if (elect_mgr_->GetElectionTxInfo(tx) != elect::kElectSuccess) {
        assert(false);
        tx_list->RemoveLast();  
        return;
    }

    // (TODO): check elect is valid in the time block period,
    // one time block, one elect block
    // check after this shard statistic block coming
}

void RootZbft::RootCreateTimerBlock(block::protobuf::Block& zjc_block) {
    // create address must to and have transfer amount
    auto& tx_map = txs_ptr_->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->msg_ptr->header.tx_proto().step() != pools::protobuf::kConsensusRootTimeBlock) {
        ZJC_ERROR("tx is not timeblock tx");
        return;
    }

    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), db_batch_, &tx);

    // (TODO): check elect is valid in the time block period,
    // one time block, one elect block
    // check after this shard statistic block coming
}

void RootZbft::RootCreateFinalStatistic(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx = *tx_list->Add();
    auto& tx_map = txs_ptr_->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    auto& src_tx = iter->second->msg_ptr->header.tx_proto();
    iter->second->TxToBlockTx(src_tx, db_batch_, &tx);
    tx.set_amount(0);
    tx.set_gas_limit(0);
    tx.set_gas_used(0);
    tx.set_balance(0);
    tx.set_status(kConsensusSuccess);
    // if (src_tx.key() == tmblock::kAttrTimerBlockHeight) {
    //     block::protobuf::StatisticInfo statistic_info;
    //     uint64_t timeblock_height = 0;
    //     if (common::StringUtil::ToUint64(tx.attr(i).value(), &timeblock_height)) {
    //         block::ShardStatistic::Instance()->GetStatisticInfo(
    //             timeblock_height,
    //             &statistic_info);
    //         auto statistic_attr = tx.add_storages();
    //         statistic_attr->set_key(bft::kStatisticAttr);
    //         statistic_attr->set_value(statistic_info.SerializeAsString());
    //     }
    // }

    // (TODO): check elect is valid in the time block period,
    // one time block, one elect block
    // check after this shard statistic block coming
    
}

int RootZbft::RootBackupCheckPrepare(
        const transport::MessagePtr& msg_ptr,
        int32_t* invalid_tx_idx,
    std::string* prepare) {
    auto& tx_bft = msg_ptr->header.zbft().tx_bft();
//     std::vector<pools::TxItemPtr> tx_vec;
//     for (int32_t i = 0; i < tx_bft.ltx_prepare().gid_size(); ++i) {
//         pools::TxItemPtr local_tx_info = pools_mgr_->GetTx(
//             pool_index(),
//             tx_bft.ltx_prepare().gid(i));
//         if (local_tx_info == nullptr) {
//             continue;
//         }
// 
//         tx_vec.push_back(local_tx_info);
//     }

    auto& tx_map = txs_ptr_->txs;
    if (tx_map.empty()) {
        return kConsensusInvalidPackage;
    }

    zbft::protobuf::TxBft res_tx_bft;
    if (DoTransaction(res_tx_bft) != kConsensusSuccess) {
        return kConsensusInvalidPackage;
    }

    res_tx_bft.clear_block();
    *prepare = res_tx_bft.SerializeAsString();
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain