#include "consensus/zbft/root_zbft.h"

#include "elect/elect_manager.h"

namespace zjchain {

namespace consensus {

RootZbft::RootZbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr, 
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr)
        : Zbft(account_mgr, security_ptr, bls_mgr, tx_ptr, pools_mgr) {
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
        default:
            RootCreateAccountAddressBlock(zjc_block);
            break;
        }
    } else {
        RootCreateAccountAddressBlock(zjc_block);
    }
}

void RootZbft::RootCreateAccountAddressBlock(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx_map = txs_ptr_->txs;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx = *tx_list->Add();
        iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), &tx);
        tx.set_status(kConsensusSuccess);
        // create address must to and have transfer amount
        if (tx.step() != pools::protobuf::kNormalTo || tx.amount() <= 0) {
            continue;
        }

        // auto acc_info = block::AccountManager::Instance()->GetAcountInfo(tx.to());
        // if (acc_info != nullptr) {
        //     continue;
        // }

        // if (tx.step() == common::kConsensusCreateContract) {
        //     uint32_t network_id = 0;
            // if (block::AccountManager::Instance()->GetAddressConsensusNetworkId(
            //         tx.from(),
            //         &network_id) != block::kBlockSuccess) {
            //     BFT_ERROR("get network_id error!");
            //     continue;
            // }

            // same to from address's network id
            // tx.set_network_id(network_id);
        // } else {
        //     tx.set_network_id(NewAccountGetNetworkId(tx.to()));
        // }
        uint64_t pool_height = 0;
        uint32_t local_pool_idx = common::kInvalidPoolIndex;
        if (tx.to() == common::kRootChainSingleBlockTxAddress ||
                tx.to() == common::kRootChainTimeBlockTxAddress ||
                tx.to() == common::kRootChainElectionBlockTxAddress) {
            local_pool_idx = common::kRootChainPoolIndex;
        } else {
            std::mt19937_64 g2(pool_height);
            local_pool_idx = g2() % common::kImmutablePoolSize;
            ZJC_DEBUG("set random pool index, pool_height: %lu, local_pool_idx: %d", pool_height, local_pool_idx);
        }

        // tx.set_pool_index(local_pool_idx);
        // add_item_index_vec(tx_vec[i]->index);
        // *add_tx = tx;
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
    iter->second->TxToBlockTx(iter->second->msg_ptr->header.tx_proto(), &tx);
    // use new node status
//     if (elect_mgr_->GetElectionTxInfo(tx) != elect::kElectSuccess) {
//         assert(false);
//         tx_list->RemoveLast();  
//         return;
//     }

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
    tx.set_from("");
    tx.set_to(common::kRootChainTimeBlockTxAddress);
    tx.set_step(pools::protobuf::kConsensusRootTimeBlock);
    tx.set_amount(0);
    tx.set_gas_limit(0);
    tx.set_gas_used(0);
    tx.set_balance(0);
    tx.set_status(kConsensusSuccess);

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
    iter->second->TxToBlockTx(src_tx, &tx);
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
    auto& tx_bft = msg_ptr->header.pipeline(0).tx_bft();
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

    hotstuff::protobuf::TxBft res_tx_bft;
    auto& ltx_msg = *res_tx_bft.mutable_ltx_prepare();
    if (DoTransaction(ltx_msg) != kConsensusSuccess) {
        return kConsensusInvalidPackage;
    }

    ltx_msg.clear_block();
    *prepare = res_tx_bft.SerializeAsString();
    return kConsensusSuccess;
}

};  // namespace consensus

};  // namespace zjchain