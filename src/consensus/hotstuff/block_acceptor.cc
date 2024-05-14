#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_executor.h>
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
#include <common/defer.h>

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
    
    tx_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr_, block_mgr_, tm_block_mgr_);
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);    
};

BlockAcceptor::~BlockAcceptor(){};

// Accept 验证 Leader 新提案信息，并执行 txs，修改 block
Status BlockAcceptor::Accept(
        std::shared_ptr<IBlockAcceptor::blockInfo>& block_info, 
        const bool& no_tx_allowed) {
    if (!block_info || !block_info->block) {
        ZJC_DEBUG("block info error!");
        return Status::kError;
    }

    auto& block = block_info->block;
    
    if (block->pool_index() != pool_idx()) {
        ZJC_DEBUG("pool_index error!");
        return Status::kError;
    }
    // 退出前计算 hash
    defer(block->set_hash(GetBlockHash(*block)));

    if (block_info->txs.empty()) {
        // 允许不打包任何交易，但 block 必须存在
        return no_tx_allowed ? Status::kSuccess : Status::kAcceptorTxsEmpty;
    }
    
    // 1. verify block
    if (!IsBlockValid(block)) {
        ZJC_DEBUG("IsBlockValid error!");
        return Status::kAcceptorBlockInvalid;
    }

    // 2. Get txs from local pool
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    Status s = Status::kSuccess;
    s = GetAndAddTxsLocally(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("invalid tx_type: %d, txs empty. pool_index: %d, view: %lu",
            block_info->tx_type, pool_idx(), block_info->view);
        return s;
    }
    
    // 3. Do txs and create block_tx
    s = DoTransactions(txs_ptr, block);
    if (s != Status::kSuccess) {
        ZJC_DEBUG("DoTransactions error!");
        return s;
    }

    // mark txs as used
    MarkBlockTxsAsUsed(block);
    
    return Status::kSuccess;
}

// AcceptSync 验证同步来的 block 信息，并更新交易池
Status BlockAcceptor::AcceptSync(const std::shared_ptr<block::protobuf::Block>& block) {
    if (!block) {
        return Status::kError;
    }

    if (block->pool_index() != pool_idx()) {
        return Status::kError;
    }

    if (!IsBlockValid(block)) {
        return Status::kAcceptorBlockInvalid;
    }

    MarkBlockTxsAsUsed(block);
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

    if (block->tx_list_size() > 0) {
        auto elect_item = elect_info_->GetElectItem(block->electblock_height());
        if (elect_item) {
            if (block->leader_index() == elect_item->LocalMember()->index) {
                // leader broadcast block to other shards
                // TODO tx_list 报错了!
                LeaderBroadcastBlock(block);
                for (uint32_t i = 0; i < block->tx_list_size(); ++i) {
                    ZJC_DEBUG("leader broadcast commit block tx over step: %d, to: %s, gid: %s, pool: %d, net: %d", 
                        block->tx_list(i).step(),
                        common::Encode::HexEncode(block->tx_list(i).to()).c_str(),
                        common::Encode::HexEncode(block->tx_list(i).gid()).c_str(),
                        block->pool_index(),
                        common::GlobalInfo::Instance()->network_id());
                }
            }
        }

        for (uint32_t i = 0; i < block->tx_list_size(); ++i) {
            ZJC_DEBUG("commit block tx over step: %d, to: %s, gid: %s", 
                block->tx_list(i).step(),
                common::Encode::HexEncode(block->tx_list(i).to()).c_str(),
                common::Encode::HexEncode(block->tx_list(i).gid()).c_str());
        }
        
        pools_mgr_->TxOver(block->pool_index(), block->tx_list());
    }

    // TODO tps measurement
    PrintTps(block->tx_list_size());    
    ZJC_DEBUG("[NEW BLOCK] hash: %s, prehash: %s, key: %u_%u_%u_%u, timestamp:%lu, txs: %lu",
        common::Encode::HexEncode(block->hash()).c_str(),
        common::Encode::HexEncode(block->prehash()).c_str(),
        block->network_id(),
        block->pool_index(),
        block->height(),
        block->electblock_height(),
        block->timestamp(),
        block->tx_list_size());
    return Status::kSuccess;
}

Status BlockAcceptor::AddTxs(const std::vector<const pools::protobuf::TxMessage*>& txs) {
    auto txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    return addTxsToPool(txs, txs_ptr);
};

Status BlockAcceptor::addTxsToPool(
        const std::vector<const pools::protobuf::TxMessage*>& txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    if (txs.size() == 0) {
        return Status::kAcceptorTxsEmpty;
    }
    
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
        case pools::protobuf::kRootCreateAddressCrossSharding:
        case pools::protobuf::kNormalTo: {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            auto tx_item = tx_pools_->GetToTxs(pool_idx(), "");
            if (tx_item != nullptr && !tx_item->txs.empty()) {
                tx_ptr = tx_item->txs.begin()->second;
            }

            break;
        }
        case pools::protobuf::kStatistic:
        {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            auto tx_item = tx_pools_->GetStatisticTx(pool_idx(), "");
            if (tx_item != nullptr && !tx_item->txs.empty()) {
                tx_ptr = tx_item->txs.begin()->second;
            }
            
            break;
        }
        case pools::protobuf::kCross:
        {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            auto tx_item = tx_pools_->GetCrossTx(pool_idx(), "");
            if (tx_item != nullptr && !tx_item->txs.empty()) {
                tx_ptr = tx_item->txs.begin()->second;
            }
            
            break;
        }
        case pools::protobuf::kConsensusRootElectShard:
            if (txs.size() == 1) {
                auto txhash = pools::GetTxMessageHash(*txs[0]);
                auto tx_item = tx_pools_->GetElectTx(pool_idx(), txhash);           
                if (tx_item != nullptr && !tx_item->txs.empty()) {
                    tx_ptr = tx_item->txs.begin()->second;
                }
                
                break;
            }
        case pools::protobuf::kConsensusRootTimeBlock:
        {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            auto tx_item = tx_pools_->GetTimeblockTx(pool_idx(), "");
            if (tx_item != nullptr && !tx_item->txs.empty()) {
                tx_ptr = tx_item->txs.begin()->second;
            }
            
            break;
        }
        default:
            // TODO 没完！还需要支持其他交易的写入
            // break;
            ZJC_FATAL("invalid tx step: %d", tx->step());
            return Status::kError;
        }

        if (tx_ptr != nullptr) {
            tx_ptr->unique_tx_hash = pools::GetTxMessageHash(*tx);
            // TODO: verify signature
            txs_map[tx_ptr->unique_tx_hash] = tx_ptr;
        }
    }

    if (txs_ptr == nullptr) {
        txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    }

    txs_ptr->txs = txs_map;
    // 放入交易池并弹出（避免重复打包）
    ZJC_DEBUG("success add txs size: %u", txs_map.size());
    int res = pools_mgr_->BackupConsensusAddTxs(pool_idx(), txs_map);
    if (res != pools::kPoolsSuccess) {
        ZJC_ERROR("invalid consensus, txs invalid.");
        return Status::kError;
    }

    return Status::kSuccess;
}

Status BlockAcceptor::GetAndAddTxsLocally(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    // auto txs_func = GetTxsFunc(block_info->tx_type);
    // Status s = txs_func(block_info, txs_ptr);
    // if (s != Status::kSuccess) {
    //     return s;
    // }
    
    auto add_txs_status = addTxsToPool(block_info->txs, txs_ptr);
    if (add_txs_status != Status::kSuccess) {
        ZJC_ERROR("invalid consensus, add_txs_status failed: %d.", add_txs_status);
        return add_txs_status;
    }

    if (!txs_ptr) {
        ZJC_ERROR("invalid consensus, tx empty.");
        return Status::kAcceptorTxsEmpty;
    }

    if (txs_ptr->txs.size() != block_info->txs.size()) {
        ZJC_ERROR("invalid consensus, txs not equal to leader %u, %u",
            txs_ptr->txs.size(), block_info->txs.size());
        return Status::kAcceptorTxsEmpty;
    }
    
    txs_ptr->pool_index = block_info->block->pool_index();
    return Status::kSuccess;
}

bool BlockAcceptor::IsBlockValid(const std::shared_ptr<block::protobuf::Block>& zjc_block) {
    // 校验 block prehash，latest height 等
    uint64_t pool_height = pools_mgr_->latest_height(pool_idx());
    if (zjc_block->height() <= pool_height || pool_height == common::kInvalidUint64) {
        ZJC_DEBUG("Accept height error: %lu, %lu", zjc_block->height(), pool_height);
        return false;
    }
    // 新块的时间戳必须大于上一个块的时间戳
    uint64_t preblock_time = pools_mgr_->latest_timestamp(pool_idx());
    if (zjc_block->timestamp() <= preblock_time) {
        ZJC_DEBUG("Accept timestamp error: %lu, %lu", zjc_block->timestamp(), preblock_time);
        return false;
    }
    
    return true;
}

void BlockAcceptor::MarkBlockTxsAsUsed(const std::shared_ptr<block::protobuf::Block>& block) {
    // mark txs as used
    std::vector<std::string> gids;
    for (uint32_t i = 0; i < uint32_t(block->tx_list().size()); i++) {
        auto& gid = block->tx_list(i).gid();
        gids.push_back(gid);
    }

    std::map<std::string, pools::TxItemPtr> _;
    pools_mgr_->GetTxByGids(pool_idx(), gids, _);    
}

Status BlockAcceptor::DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        std::shared_ptr<block::protobuf::Block>& block) {
    Status s = BlockExecutorFactory().Create(security_ptr_)->DoTransactionAndCreateTxBlock(
            txs_ptr, block);
    if (s != Status::kSuccess) {
        return s;
    }

    // recalculate hash
    block->set_is_commited_block(false);
    // block->set_hash(GetBlockHash(*block));
    return s;
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
    case pools::protobuf::kRootCreateAddress:
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
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = msg_ptr->header;
        auto kv = msg.mutable_sync()->add_items();
        kv->set_key(tx.storages(i).key());
        kv->set_value(tx.storages(i).value());
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
      
    }
}

} // namespace hotstuff

} // namespace shardora
