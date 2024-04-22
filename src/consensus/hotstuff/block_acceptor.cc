#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/types.h>
#include <consensus/zbft/contract_call.h>
#include <consensus/zbft/contract_user_call.h>
#include <consensus/zbft/contract_user_create_call.h>
#include <consensus/zbft/from_tx_item.h>
#include <consensus/zbft/root_to_tx_item.h>
#include <consensus/zbft/to_tx_local_item.h>
#include <protos/pools.pb.h>
#include <protos/zbft.pb.h>
#include <zjcvm/zjcvm_utils.h>

namespace shardora {

namespace hotstuff {

BlockAcceptor::BlockAcceptor(
        const uint32_t &pool_idx,
        const std::shared_ptr<security::Security> &security,
        const std::shared_ptr<block::AccountManager> &account_mgr,
        const std::shared_ptr<ElectInfo> &elect_info,
        const std::shared_ptr<vss::VssManager> &vss_mgr,
        const std::shared_ptr<contract::ContractManager> &contract_mgr,
        const std::shared_ptr<db::Db> &db,
        const std::shared_ptr<consensus::ContractGasPrepayment> &gas_prepayment,
        std::shared_ptr<pools::TxPoolManager> &pools_mgr,
        std::shared_ptr<block::BlockManager> &block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager> &tm_block_mgr,
        consensus::BlockCacheCallback new_block_cache_callback):
    pool_idx_(pool_idx), security_ptr_(security), account_mgr_(account_mgr),
    elect_info_(elect_info), vss_mgr_(vss_mgr), contract_mgr_(contract_mgr),
    db_(db), gas_prepayment_(gas_prepayment), pools_mgr_(pools_mgr),
    block_mgr_(block_mgr), tm_block_mgr_(tm_block_mgr), new_block_cache_callback_(new_block_cache_callback) {
    
    db_batch_ = std::make_shared<db::DbWriteBatch>();
    tx_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr_, block_mgr_, tm_block_mgr_);
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    
    RegisterTxsFunc(pools::protobuf::kNormalTo,
        std::bind(&BlockAcceptor::GetToTxs, this, std::placeholders::_1, std::placeholders::_2));
    RegisterTxsFunc(pools::protobuf::kStatistic,
        std::bind(&BlockAcceptor::GetStatisticTxs, this, std::placeholders::_1, std::placeholders::_2));
    RegisterTxsFunc(pools::protobuf::kCross,
        std::bind(&BlockAcceptor::GetCrossTxs, this, std::placeholders::_1, std::placeholders::_2));
    RegisterTxsFunc(pools::protobuf::kConsensusRootElectShard,
        std::bind(&BlockAcceptor::GetElectTxs, this, std::placeholders::_1, std::placeholders::_2));
    RegisterTxsFunc(pools::protobuf::kConsensusRootTimeBlock,
        std::bind(&BlockAcceptor::GetTimeBlockTxs, this, std::placeholders::_1, std::placeholders::_2));
};

BlockAcceptor::~BlockAcceptor(){};

Status BlockAcceptor::Accept(std::shared_ptr<IBlockAcceptor::blockInfo>& block_info) {
    if (!block_info || !block_info->block) {
        return Status::kError;
    }

    auto& block = block_info->block;
    
    if (block->pool_index() != pool_idx()) {
        return Status::kError;
    }

    if (block_info->txs.empty()) {
        // 允许不打包任何交易，但 block 必须存在
        return Status::kSuccess;
    }
    
    // 1. verify block
    if (!IsBlockValid(block)) {
        return Status::kAcceptorBlockInvalid;
    }

    // 2. Get txs from local pool
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;

    Status s = Status::kSuccess;
    s = GetTxsFromLocal(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("invalid tx_type: %d, txs empty. pool_index: %d, view: %lu",
            block_info->tx_type, pool_idx(), block_info->view);
        return s;
    }

    FilterInvalidTxs(txs_ptr);
    
    // 3. Do txs and create block_tx
    s = DoTransactions(txs_ptr, block);
    if (s != Status::kSuccess) {
        return s;
    }
    
    return Status::kSuccess;
}

Status BlockAcceptor::Commit(std::shared_ptr<block::protobuf::Block>& block) {
    // commit block
    auto db_batch = std::make_shared<db::DbWriteBatch>();
    auto queue_item_ptr = std::make_shared<block::BlockToDbItem>(block, db_batch);
    new_block_cache_callback_(
            queue_item_ptr->block_ptr,
            *queue_item_ptr->db_batch);
    block_mgr_->ConsensusAddBlock(queue_item_ptr);
    // TODO if local node is leader, broadcast block
    auto elect_item = elect_info_->GetElectItem(block->electblock_height());
    if (!elect_item) {
        return Status::kError;
    }
    if (block->leader_index() == elect_item->LocalMember()->index) {
        // leader broadcast block to other shards
        LeaderBroadcastBlock(block);
    }
    pools_mgr_->TxOver(block->pool_index(), block->tx_list());

    // TODO tps measurement

    ZJC_DEBUG("[NEW BLOCK] hash: %s, key: %u_%u_%u_%u, timestamp:%lu, txs: %lu",
        common::Encode::HexEncode(block->hash()).c_str(),
        block->network_id(),
        block->pool_index(),
        block->height(),
        block->electblock_height(),
        block->timestamp(),
        block->tx_list_size());
    return Status::kSuccess;
}

Status BlockAcceptor::FetchTxsFromPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs) {
    // TODO 不应该和 zbft 耦合
    zbft::protobuf::TxBft txbft;
    std::map<std::string, pools::TxItemPtr> invalid_txs;
    pools_mgr_->GetTx(pool_idx(), 1024, invalid_txs, &txbft);
    
    for (auto it = txbft.txs().begin(); it != txbft.txs().end(); it++) {
        txs.push_back(std::make_shared<pools::protobuf::TxMessage>(*it));
    }
    return Status::kSuccess;
}

Status BlockAcceptor::AddTxsToPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs) {
    auto txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    return addTxsToPool(txs, txs_ptr);
};

Status BlockAcceptor::addTxsToPool(
        std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    assert(txs.size() > 0);
    std::map<std::string, pools::TxItemPtr> txs_map;
    for (uint32_t i = 0; i < uint32_t(txs.size()); i++) {
        auto& tx = txs[i];
        ZJC_DEBUG("get tx message step: %d", tx->step());
        protos::AddressInfoPtr address_info = nullptr;
        if (tx->step() == pools::protobuf::kContractExcute) {
            address_info = account_mgr_->GetAccountInfo(tx->to());
        } else {
            if (security_ptr_->IsValidPublicKey(tx->pubkey())) {
                address_info = account_mgr_->GetAccountInfo(security_ptr_->GetAddress(tx->pubkey()));
            } else {
                address_info = account_mgr_->pools_address_info(pool_idx());
            }
        }

        if (!address_info) {
            return Status::kError;
        }

        pools::TxItemPtr tx_ptr = nullptr;
        switch (tx->step()) {
        case pools::protobuf::kNormalFrom:
            tx_ptr = std::make_shared<consensus::FromTxItem>(
                    *tx, account_mgr_, security_ptr_, address_info);
            break;
        case pools::protobuf::kRootCreateAddress:
            tx_ptr = std::make_shared<consensus::RootToTxItem>(
                    elect_info_->max_consensus_sharding_id(),
                    *tx,
                    vss_mgr_,
                    account_mgr_,
                    security_ptr_,
                    address_info);
            break;
        case pools::protobuf::kContractCreate:
            tx_ptr = std::make_shared<consensus::ContractUserCreateCall>(
                    contract_mgr_, 
                    db_, 
                    *tx, 
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            break;
        case pools::protobuf::kContractExcute:
            tx_ptr = std::make_shared<consensus::ContractCall>(
                    contract_mgr_, 
                    gas_prepayment_, 
                    db_, 
                    *tx,
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            break;
        case pools::protobuf::kContractGasPrepayment:
            tx_ptr = std::make_shared<consensus::ContractUserCall>(
                    db_, 
                    *tx,
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            break;
        case pools::protobuf::kConsensusLocalTos:
            tx_ptr = std::make_shared<consensus::ToTxLocalItem>(
                    *tx, 
                    db_, 
                    gas_prepayment_, 
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            break;
        default:
            ZJC_FATAL("invalid tx step: %d", tx->step());
            return Status::kError;
        }

        if (tx_ptr != nullptr) {
            tx_ptr->unique_tx_hash = pools::GetTxMessageHash(*tx);
            // TODO: verify signature
            txs_map[tx_ptr->unique_tx_hash] = tx_ptr;
        }
    }

    if (txs_ptr != nullptr) {
        txs_ptr->txs = txs_map;
    }

    int res = pools_mgr_->BackupConsensusAddTxs(pool_idx(), txs_map);
    if (res != pools::kPoolsSuccess) {
        ZJC_ERROR("invalid consensus, txs invalid.");
        return Status::kError;
    }

    return Status::kSuccess;
}

Status BlockAcceptor::GetTxsFromLocal(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    auto txs_func = GetTxsFunc(block_info->tx_type);
    Status s = txs_func(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        return s;
    }

    if (!txs_ptr) {
        ZJC_ERROR("invalid consensus, tx empty.");
        return Status::kAcceptorTxsEmpty;
    }

    if (txs_ptr != nullptr && txs_ptr->txs.size() != block_info->txs.size()) {
        ZJC_ERROR("invalid consensus, txs not equal to leader.");
        return Status::kAcceptorTxsEmpty;
    }
    
    txs_ptr->pool_index = block_info->block->pool_index();
    return Status::kSuccess;
}

bool BlockAcceptor::IsBlockValid(const std::shared_ptr<block::protobuf::Block>& zjc_block) {
    // 校验 block prehash，latest height 等
    uint64_t pool_height = pools_mgr_->latest_height(pool_idx());
    if (zjc_block->height() <= pool_height || pool_height == common::kInvalidUint64) {
        return false;
    }
    // 新块的时间戳必须大于上一个块的时间戳
    uint64_t preblock_time = pools_mgr_->latest_timestamp(pool_idx());
    if (zjc_block->timestamp() <= preblock_time) {
        return false;
    }
    
    return true;
}

void BlockAcceptor::FilterInvalidTxs(std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    if (!txs_ptr) {
        return;
    }
    // TODO 验签
}

Status BlockAcceptor::DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        std::shared_ptr<block::protobuf::Block>& zjc_block) {
    // 执行交易
    auto tx_list = zjc_block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    tx_list->Reserve(tx_map.size());
    std::unordered_map<std::string, int64_t> acc_balance_map;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = zjc_block->height();
    zjc_host.tx_context_.block_timestamp = zjc_block->timestamp() / 1000;
    uint64_t chain_id = (((uint64_t)zjc_block->network_id()) << 32 | (uint64_t)zjc_block->pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chain_id);

    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) { 
        auto& tx_info = iter->second->tx_info;
        auto& block_tx = *tx_list->Add();
        int res = iter->second->TxToBlockTx(tx_info, db_batch_, &block_tx);
        if (res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        if (block_tx.step() == pools::protobuf::kContractExcute) {
            block_tx.set_from(security_ptr_->GetAddress(
                iter->second->tx_info.pubkey()));
        } else {
            block_tx.set_from(iter->second->address_info->addr());
        }

        block_tx.set_status(consensus::kConsensusSuccess);
        int do_tx_res = iter->second->HandleTx(
            *zjc_block,
            db_batch_,
            zjc_host,
            acc_balance_map,
            block_tx);
        if (do_tx_res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        for (auto event_iter = zjc_host.recorded_logs_.begin();
                event_iter != zjc_host.recorded_logs_.end(); ++event_iter) {
            auto log = block_tx.add_events();
            log->set_data((*event_iter).data);
            for (auto topic_iter = (*event_iter).topics.begin();
                    topic_iter != (*event_iter).topics.end(); ++topic_iter) {
                log->add_topics(std::string((char*)(*topic_iter).bytes, sizeof((*topic_iter).bytes)));
            }
        }

        zjc_host.recorded_logs_.clear();
    }
    
    return Status::kSuccess;
}

Status BlockAcceptor::GetDefaultTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    return addTxsToPool(block_info->txs, txs_ptr);
}

Status BlockAcceptor::GetToTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetToTxs(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetStatisticTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetStatisticTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetCrossTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetCrossTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess; 
}

Status BlockAcceptor::GetElectTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    if (block_info->txs.size() == 1) {
        auto txhash = pools::GetTxMessageHash(*block_info->txs[0]);
        txs_ptr = tx_pools_->GetElectTx(pool_idx(), txhash);           
    }
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetTimeBlockTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetTimeblockTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

void BlockAcceptor::LeaderBroadcastBlock(const std::shared_ptr<block::protobuf::Block>& block) {
    if (block->pool_index() == common::kRootChainPoolIndex) {
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            BroadcastBlock(network::kNodeNetworkId, block);
        } else {
            BroadcastBlock(network::kRootCongressNetworkId, block);
        }

        return;
    }

    if (block->tx_list_size() != 1) {
        return;
    }

    switch (block->tx_list(0).step()) {
    case pools::protobuf::kRootCreateAddressCrossSharding:
    case pools::protobuf::kNormalTo:
        ZJC_DEBUG("broadcast to block step: %u, height: %lu",
            block->tx_list(0).step(), block->height());
        BroadcastLocalTosBlock(block);
        break;
    case pools::protobuf::kConsensusRootElectShard:
        BroadcastBlock(network::kNodeNetworkId, block);
        break;
    default:
        break;
    }    
}

void BlockAcceptor::BroadcastBlock(
        uint32_t des_shard,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(des_shard);
    msg.set_type(common::kBlockMessage);
    dht::DhtKeyManager dht_key(des_shard);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& tx = block_item->tx_list(0);
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        std::string val;
        if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
            auto kv = msg.mutable_sync()->add_items();
            kv->set_key(tx.storages(i).val_hash());
            kv->set_value(val);
        }
    }

    *msg.mutable_block() = *block_item;
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto* brdcast = msg.mutable_broadcast();
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success broadcast to %u, pool: %u, height: %lu, hash64: %lu",
        des_shard, block_item->pool_index(), block_item->height(), msg.hash64());
}

void BlockAcceptor::BroadcastLocalTosBlock(
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    auto& tx = block_item->tx_list(0);
    pools::protobuf::ToTxMessage to_tx;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        std::string val;
        if (prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
            ZJC_DEBUG("get to tx storage key: %s, value: %s",
                common::Encode::HexEncode(tx.storages(i).val_hash()).c_str(),
                common::Encode::HexEncode(val).c_str());
            if (tx.storages(i).key() != protos::kNormalToShards) {
                assert(false);
                continue;
            }

            if (!to_tx.ParseFromString(val)) {
                assert(false);
                continue;
            }

            if (to_tx.to_heights().sharding_id() == common::GlobalInfo::Instance()->network_id()) {
                continue;
            }

            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            auto& msg = msg_ptr->header;
            auto kv = msg.mutable_sync()->add_items();
            kv->set_key(tx.storages(i).val_hash());
            kv->set_value(val);
            msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
            msg.set_type(common::kBlockMessage);
            dht::DhtKeyManager dht_key(to_tx.to_heights().sharding_id());
            msg.set_des_dht_key(dht_key.StrKey());
            transport::TcpTransport::Instance()->SetMessageHash(msg);
            *msg.mutable_block() = *block_item;
            auto* brdcast = msg.mutable_broadcast();
            network::Route::Instance()->Send(msg_ptr);
            ZJC_DEBUG("success broadcast cross tos height: %lu, sharding id: %u",
                block_item->height(), to_tx.to_heights().sharding_id());
        } else {
            assert(false);
        }
    }
}

} // namespace hotstuff

} // namespace shardora

