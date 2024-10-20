#include "block/block_manager.h"

#include "block/block_utils.h"
#include "block/account_manager.h"
#include "block/block_proto.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"
#include "transport/processor.h"
#include <common/log.h>
#include <network/network_status.h>
#include <network/network_utils.h>
#include <protos/pools.pb.h>
#include <protos/tx_storage_key.h>
#include <transport/transport_utils.h>
#include "db/db_utils.h"
#include "common/defer.h"

namespace shardora {

namespace block {

static const std::string kShardElectPrefix = common::Encode::HexDecode(
    "227a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe2");

BlockManager::BlockManager(transport::MultiThreadHandler& net_handler) : net_handler_(net_handler) {}

BlockManager::~BlockManager() {
    if (consensus_block_queues_ != nullptr) {
        delete[] consensus_block_queues_;
    }
}

int BlockManager::Init(
        std::shared_ptr<AccountManager>& account_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<pools::ShardStatistic>& statistic_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
        const std::string& local_id,
        DbBlockCallback new_block_callback) {
    account_mgr_ = account_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    new_block_callback_ = new_block_callback;
    statistic_mgr_ = statistic_mgr;
    security_ = security;
    contract_mgr_ = contract_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    to_txs_pool_ = std::make_shared<pools::ToTxsPools>(
        db_, local_id, max_consensus_sharding_id_, pools_mgr_, account_mgr_);
    if (common::GlobalInfo::Instance()->for_ck_server()) {
        ck_client_ = std::make_shared<ck::ClickHouseClient>("127.0.0.1", "", "", db, contract_mgr_);
        ZJC_DEBUG("support ck");
    }

    consensus_block_queues_ = new common::ThreadSafeQueue<BlockToDbItemPtr>[common::kMaxThreadCount];
    network::Route::Instance()->RegisterMessage(
        common::kBlockMessage,
        std::bind(&BlockManager::HandleMessage, this, std::placeholders::_1));
    // test_sync_block_tick_.CutOff(
    //     100000lu,
    //     std::bind(&BlockManager::ConsensusTimerMessage, this));

    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&BlockManager::ConsensusTimerMessage, this, std::placeholders::_1));
    bool genesis = false;
    pop_tx_tick_.CutOff(200000lu, std::bind(&BlockManager::PopTxTicker, this));
    leader_prev_get_to_tx_tm_ = common::TimeUtils::TimestampMs();
    return kBlockSuccess;
}

int BlockManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {    
    return transport::kFirewallCheckSuccess;
}

void BlockManager::ConsensusTimerMessage(const transport::MessagePtr& message) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_timer_ms_ + 100lu > now_tm_ms) {
        return;
    }

    prev_timer_ms_ = now_tm_ms;
    HandleAllNewBlock();
    auto now_tm = common::TimeUtils::TimestampUs();
    if (prev_create_statistic_tx_tm_us_ < now_tm) {
        prev_create_statistic_tx_tm_us_ = now_tm + 10000000lu;
        CreateStatisticTx();
    }

    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 10) {
        ZJC_DEBUG("BlockManager handle message use time: %lu", (etime - now_tm_ms));
    }
}

void BlockManager::OnNewElectBlock(
        uint32_t sharding_id, 
        uint64_t elect_height, 
        common::MembersPtr& members) {
    if (sharding_id > max_consensus_sharding_id_) {
        max_consensus_sharding_id_ = sharding_id;
    }

    if (sharding_id == common::GlobalInfo::Instance()->network_id() ||
            sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        if (elect_height <= latest_elect_height_) {
            return;
        }

        latest_members_ = members;
        latest_elect_height_ = elect_height;
    }
}

void BlockManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    if (network::NetsInfo::Instance()->IsClosed(msg_ptr->header.src_sharding_id())) {
        ZJC_WARN("wrong shard status: %d %d.",
            msg_ptr->header.src_sharding_id(),
            network::NetsInfo::Instance()->net_info(msg_ptr->header.src_sharding_id())->Status());
        return;
    }    
    // assert(false);
    // TODO to_tx_msg_queue_ 没有用啊
    if (msg_ptr->header.block_proto().has_shard_to() > 0) {
        to_tx_msg_queue_.push(msg_ptr);
        ZJC_DEBUG("queue size to_tx_msg_queue_: %d", to_tx_msg_queue_.size());
    }

    // TODO statistic_tx_msg_queue_ 没有用啊
    if (msg_ptr->header.block_proto().has_statistic_tx()) {
        statistic_tx_msg_queue_.push(msg_ptr);
        ZJC_DEBUG("queue size statistic_tx_msg_queue_: %d", statistic_tx_msg_queue_.size());
    }

    if (msg_ptr->header.has_view_block() && msg_ptr->header.view_block().has_block_info()) {
        ZJC_DEBUG("view block message coming net: %u, pool: %u, height: %lu, hash64: %lu",
            msg_ptr->header.view_block().block_info().network_id(),
            msg_ptr->header.view_block().block_info().pool_index(),
            msg_ptr->header.view_block().block_info().height(),
            msg_ptr->header.hash64());
        auto& header = msg_ptr->header;
        auto local_net = common::GlobalInfo::Instance()->network_id();
        if (local_net >= network::kConsensusShardEndNetworkId) {
            local_net -= network::kConsensusWaitingShardOffset;
        }

        // 过滤掉自己给自己发的消息
        if (header.view_block().block_info().network_id() == local_net) {
            ZJC_DEBUG("network block failed cache new block coming sharding id: %u, "
                "pool: %d, height: %lu, tx size: %u, hash: %s",
                header.block().network_id(),
                header.block().pool_index(),
                header.block().height(),
                header.block().tx_list_size(),
                common::Encode::HexEncode(header.block().hash()).c_str());
            return;
        }

        auto block_ptr = std::make_shared<block::protobuf::Block>(header.view_block().block_info());
        if (verify_view_block_fn_ && verify_view_block_fn_(header.view_block())) {
            // just one thread
            auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
            block_from_network_queue_[thread_idx].push(block_ptr);
            ZJC_DEBUG("success add new network block 2 net: %u, pool: %u, height: %lu",
                block_ptr->network_id(), block_ptr->pool_index(), block_ptr->height());
        } else {
            ZJC_ERROR("failed add new network block 2 net: %u, pool: %u, height: %lu",
                block_ptr->network_id(), block_ptr->pool_index(), block_ptr->height());
        }
    }
}

void BlockManager::HandleAllNewBlock() {
    // 同步的 NetworkNewBlock 也会走这个逻辑
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        while (true) {
            std::shared_ptr<block::protobuf::Block> block_ptr = nullptr;
            block_from_network_queue_[i].pop(&block_ptr);
            if (block_ptr == nullptr) {
                break;
            }

            db::DbWriteBatch db_batch;
            // TODO 更新 pool info，每次 AddNewBlock 之前需要更新 pool latest info
            if (UpdateBlockItemToCache(block_ptr, db_batch)) {
                ZJC_DEBUG("from network new block coming sharding id: %u, pool: %d, height: %lu, "
                    "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
                    block_ptr->network_id(),
                    block_ptr->pool_index(),
                    block_ptr->height(),
                    block_ptr->tx_list_size(),
                    common::Encode::HexEncode(block_ptr->hash()).c_str(),
                    block_ptr->electblock_height(),
                    block_ptr->timeblock_height());
                AddNewBlock(block_ptr, db_batch);
            }   
        }
    }

    HandleAllConsensusBlocks();
}

// 更新 pool 最新状态
bool BlockManager::UpdateBlockItemToCache(
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    // if (!block->is_commited_block()) {
    //     assert(false);
    //     return false;
    // }

    // if (prefix_db_->BlockExists(block->hash())) {
    //     ZJC_DEBUG("failed cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
    //               block->network_id(),
    //               block->pool_index(),
    //               block->height(),
    //               block->tx_list_size(),
    //               common::Encode::HexEncode(block->hash()).c_str());
    //     return false;
    // }

    // if (prefix_db_->BlockExists(block->network_id(), block->pool_index(), block->height())) {
    //     ZJC_DEBUG("failed cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
    //               block->network_id(),
    //               block->pool_index(),
    //               block->height(),
    //               block->tx_list_size(),
    //               common::Encode::HexEncode(block->hash()).c_str());
    //     return false;
    // }

    return true;
}

void BlockManager::GenesisNewBlock(
    const std::shared_ptr<block::protobuf::Block>& block_item) {
    db::DbWriteBatch db_batch;
    AddNewBlock(block_item, db_batch);
}

void BlockManager::AddWaitingCheckSignBlock(
        const std::shared_ptr<block::protobuf::Block>& block_ptr) {
    auto net_iter = waiting_check_sign_blocks_.find(block_ptr->network_id());
    if (net_iter == waiting_check_sign_blocks_.end()) {
        waiting_check_sign_blocks_[block_ptr->network_id()] =
            std::map<uint64_t, std::queue<std::shared_ptr<block::protobuf::Block>>>();
        net_iter = waiting_check_sign_blocks_.find(block_ptr->network_id());
    }

    auto elect_height_iter = net_iter->second.find(block_ptr->electblock_height());
    if (elect_height_iter == net_iter->second.end()) {
        net_iter->second[block_ptr->electblock_height()] =
            std::queue<std::shared_ptr<block::protobuf::Block>>();
        elect_height_iter = net_iter->second.find(block_ptr->electblock_height());
    }

    elect_height_iter->second.push(block_ptr);
    if (elect_height_iter->second.size() >= 1024) {
        elect_height_iter->second.pop();
    }
}

void BlockManager::CheckWaitingBlocks(uint32_t shard, uint64_t elect_height) {
    assert(false);
    // auto net_iter = waiting_check_sign_blocks_.find(shard);
    // if (net_iter == waiting_check_sign_blocks_.end()) {
    //     return;
    // }

    // auto height_iter = net_iter->second.find(elect_height);
    // if (height_iter == net_iter->second.end()) {
    //     return;
    // }

    // auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    // while (!height_iter->second.empty()) {
    //     auto block_item = height_iter->second.front();
    //     height_iter->second.pop();
    //     if (block_agg_valid_func_ != nullptr && block_agg_valid_func_(*block_item) == 0) {
    //         ZJC_ERROR("verification agg sign failed hash: %s, signx: %s, "
    //             "net: %u, pool: %u, height: %lu",
    //             common::Encode::HexEncode(block_item->hash()).c_str(),
    //             common::Encode::HexEncode(block_item->bls_agg_sign_x()).c_str(),
    //             block_item->network_id(),
    //             block_item->pool_index(),
    //             block_item->height());
    //         continue;
    //     }

    //     ZJC_DEBUG("success add new network block 0 net: %u, pool: %u, height: %lu",
    //         block_item->network_id(), block_item->pool_index(), block_item->height());
    //     block_from_network_queue_[thread_idx].push(block_item);
    // }
}

void BlockManager::ConsensusAddBlock(
        const BlockToDbItemPtr& block_item) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    consensus_block_queues_[thread_idx].push(block_item);
    ZJC_DEBUG("queue size thread_idx: %d consensus_block_queues_: %d",
        thread_idx, consensus_block_queues_[thread_idx].size());
}

void BlockManager::HandleAllConsensusBlocks() {
    for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
        while (true) {
            BlockToDbItemPtr db_item_ptr = nullptr;
            consensus_block_queues_[i].pop(&db_item_ptr);
            if (db_item_ptr == nullptr) {
                break;
            }

            ZJC_DEBUG("from consensus new block coming sharding id: %u, pool: %d, height: %lu, "
                    "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
                    db_item_ptr->block_ptr->network_id(),
                    db_item_ptr->block_ptr->pool_index(),
                    db_item_ptr->block_ptr->height(),
                    db_item_ptr->block_ptr->tx_list_size(),
                    common::Encode::HexEncode(db_item_ptr->block_ptr->hash()).c_str(),
                    db_item_ptr->block_ptr->electblock_height(),
                    db_item_ptr->block_ptr->timeblock_height());
            AddNewBlock(db_item_ptr->block_ptr, *db_item_ptr->db_batch);
        }
    }
}

void BlockManager::GenesisAddAllAccount(
        uint32_t des_sharding_id,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch) {
    // (TODO: XX): for create contract error, check address's shard and pool index valid, fix it
    const auto& tx_list = block_item->tx_list();
    if (tx_list.empty()) {
        return;
    }

    // one block must be one consensus pool
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        GenesisAddOneAccount(des_sharding_id, tx_list[i], block_item->height(), db_batch);
    }
}

void BlockManager::GenesisAddOneAccount(uint32_t des_sharding_id,
                                        const block::protobuf::BlockTx& tx,
                                        const uint64_t& latest_height,
                                        db::DbWriteBatch& db_batch) {
    auto& account_id = account_mgr_->GetTxValidAddress(tx);
    auto account_info = std::make_shared<address::protobuf::AddressInfo>();
    account_info->set_pool_index(common::GetAddressPoolIndex(account_id));
    account_info->set_addr(account_id);
    account_info->set_type(address::protobuf::kNormal);
    account_info->set_sharding_id(des_sharding_id);
    account_info->set_latest_height(latest_height);
    account_info->set_balance(tx.balance());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.step() ==  pools::protobuf::kContractCreate && tx.storages(i).key() == protos::kCreateContractBytesCode) {
            auto& bytes_code = tx.storages(i).value();
            account_info->set_type(address::protobuf::kContract);
            account_info->set_bytes_code(bytes_code);
            break;
        }
    }
    
    ZJC_DEBUG("genesis add new account %s : %lu, shard: %u",
              common::Encode::HexEncode(account_info->addr()).c_str(),
              account_info->balance(),
              des_sharding_id);
    
    prefix_db_->AddAddressInfo(account_info->addr(), *account_info, db_batch);    
}

void BlockManager::HandleStatisticTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        db::DbWriteBatch& db_batch) {
    consensused_timeblock_height_ = block.timeblock_height();
    prefix_db_->SaveConsensusedStatisticTimeBlockHeight(
        block.network_id(),
        block.timeblock_height(),
        db_batch);
    ZJC_DEBUG("success handled statistic block time block height: %lu, net: %u",
        consensused_timeblock_height_,
        block.network_id());
    uint32_t net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        if (block_tx.storages(i).key() == protos::kShardStatistic) {
            if (!elect_statistic.ParseFromString(block_tx.storages(i).value())) {
                assert(false);
                continue;
            }

            if (elect_statistic.sharding_id() != net_id) {
                ZJC_DEBUG("invalid sharding id %u, %u", elect_statistic.sharding_id(), net_id);
                continue;
            }

            auto iter = shard_statistics_map_.find(elect_statistic.height_info().tm_height());
            if (iter != shard_statistics_map_.end()) {
                ZJC_DEBUG("success remove shard statistic block tm height: %lu", iter->first);
                shard_statistics_map_.erase(iter);
                auto tmp_ptr = std::make_shared<StatisticMap>(shard_statistics_map_);
                shard_statistics_map_ptr_queue_.push(tmp_ptr);
            }

            break;
        }
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        HandleStatisticBlock(block, block_tx, elect_statistic, db_batch);
    }
}

void BlockManager::ConsensusShardHandleRootCreateAddress(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx) {
    if (network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
        return;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("get normal to tx key: %s", tx.storages(i).key().c_str());
        if (tx.storages(i).key() != protos::kRootCreateAddressKey) {
            continue;
        }

        uint32_t* des_sharding_and_pool = (uint32_t*)(tx.storages(i).value().c_str());
        if (des_sharding_and_pool[0] != common::GlobalInfo::Instance()->network_id()) {
            return;
        }

        if (des_sharding_and_pool[1] >= common::kInvalidPoolIndex) {
            return;
        }
        
        pools::protobuf::ToTxMessage to_txs;
        auto* tos = to_txs.add_tos();
        tos->set_amount(tx.amount());
        tos->set_des(tx.to());
        tos->set_sharding_id(des_sharding_and_pool[0]);
        tos->set_pool_index(des_sharding_and_pool[1]);
        to_txs.mutable_to_heights()->set_sharding_id(des_sharding_and_pool[0]);
        ZJC_DEBUG("success handle root create address: %u, local net: %u, step: %d",
            to_txs.to_heights().sharding_id(),
            common::GlobalInfo::Instance()->network_id(),
            tx.step());
        HandleLocalNormalToTx(to_txs, tx);
    }
}

void BlockManager::HandleNormalToTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("get normal to tx key: %s", tx.storages(i).key().c_str());
        if (tx.storages(i).key() != protos::kNormalToShards) {
            continue;
        }

        pools::protobuf::ToTxMessage to_txs;
        if (!to_txs.ParseFromString(tx.storages(i).value())) {
            ZJC_WARN("parse to txs failed.");
            continue;
        }

        ZJC_DEBUG("success handle tox tx heights net: %u, local net: %u, step: %d",
            to_txs.to_heights().sharding_id(),
            common::GlobalInfo::Instance()->network_id(),
            tx.step());
        if (!network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
            if (to_txs.to_heights().sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                ZJC_WARN("sharding invalid: %u, %u",
                    to_txs.to_heights().sharding_id(),
                    common::GlobalInfo::Instance()->network_id());
    //             assert(false);
                continue;
            }

            ZJC_DEBUG("success add local transfer tx tos hash: %s",
                common::Encode::HexEncode(tx.storages(i).value()).c_str());
            HandleLocalNormalToTx(to_txs, tx);
        } else {
            ZJC_DEBUG("root handle normal to tx to_txs size: %u", to_txs.tos_size());
            RootHandleNormalToTx(block, to_txs, db_batch);
        }
    }
}

void BlockManager::RootHandleNormalToTx(
        const block::protobuf::Block& block,
        pools::protobuf::ToTxMessage& to_txs,
        db::DbWriteBatch& db_batch) {
    // 将 NormalTo 中的多个 tx 拆分成多个 kRootCreateAddress tx
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto tos_item = to_txs.tos(i);
        ZJC_INFO("to tx step: %d, new address %s, amount: %lu, prepayment: %lu, gid: %s, contract_from: %s",
            tos_item.step(),
            common::Encode::HexEncode(tos_item.des()).c_str(),
            tos_item.amount(),
            tos_item.prepayment(),
            common::Encode::HexEncode("gid").c_str(),
            common::Encode::HexEncode(tos_item.contract_from()).c_str());

        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_step(pools::protobuf::kRootCreateAddress);
        // 如果 shard 已经制定了 Contract Account 的 shard，直接创建，不需要 root 再分配
        // 如果没有，则需要 root 继续创建 kRootCreateAddress 交易
        if (tos_item.step() == pools::protobuf::kContractCreate) {
            // that's contract address, just add address
            auto account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(tos_item.pool_index());
            account_info->set_addr(tos_item.des());
            account_info->set_type(address::protobuf::kContract);
            account_info->set_sharding_id(tos_item.sharding_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(tos_item.amount());
            prefix_db_->AddAddressInfo(tos_item.des(), *account_info);
            ZJC_DEBUG("create add contract direct: %s, amount: %lu, sharding: %u, pool index: %u",
                common::Encode::HexEncode(tos_item.des()).c_str(),
                tos_item.amount(),
                tos_item.sharding_id(),
                tos_item.pool_index());
           continue;
        }

        if (tos_item.step() == pools::protobuf::kJoinElect) {
            for (int32_t i = 0; i < tos_item.join_infos_size(); ++i) {
                if (tos_item.join_infos(i).shard_id() != network::kRootCongressNetworkId) {
                    continue;
                }

                prefix_db_->SaveNodeVerificationVector(
                    tos_item.des(),
                    tos_item.join_infos(i),
                    db_batch);
                ZJC_DEBUG("success handle kElectJoin tx: %s, net: %u, pool: %u, block net: %u, "
                    "block pool: %u, block height: %lu, local net id: %u", 
                    common::Encode::HexEncode(tos_item.des()).c_str(), 
                    tos_item.sharding_id(),
                    tos_item.pool_index(),
                    block.network_id(), block.pool_index(), block.height(),
                    common::GlobalInfo::Instance()->network_id());
            }

            continue;
        }
        
        // for ContractCreateByRootFrom tx
        if (isContractCreateToTxMessageItem(tos_item)) {
            tx->set_contract_code(tos_item.library_bytes());
            tx->set_contract_from(tos_item.contract_from());
            tx->set_contract_prepayment(tos_item.prepayment());
        }
        
        tx->set_pubkey("");
        tx->set_to(tos_item.des());
        auto gid = common::Hash::keccak256(
            tos_item.des() + "_" +
            std::to_string(block.height()) + "_" +
            std::to_string(i));
        tx->set_gas_limit(0);
        tx->set_amount(tos_item.amount());
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        
        auto pool_index = common::Hash::Hash32(tos_item.des()) % common::kImmutablePoolSize;
        msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("create new address %s, amount: %lu, prepayment: %lu, gid: %s, contract_from: %s",
            common::Encode::HexEncode(tos_item.des()).c_str(),
            tos_item.amount(),
            tos_item.prepayment(),
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(tos_item.contract_from()).c_str());
    }
}

// TODO refactor needed!
void BlockManager::HandleLocalNormalToTx(
        const pools::protobuf::ToTxMessage& to_txs,
        const block::protobuf::BlockTx& tx) {
    uint32_t step = tx.step();
    // 根据 to 聚合转账类 localtotx
    std::unordered_map<std::string, std::shared_ptr<localToTxInfo>> addr_amount_map;
    // 摘出 contract_create 类 localtotx
    std::vector<std::shared_ptr<localToTxInfo>> contract_create_tx_infos;
    // 分两类统计
    // 1. 单纯的转账交易（包括 prepayment），这种类型的交易聚合 amount 最终一起执行，聚合成 ConsensusLocalTos 交易即可
    // 2. 合约账户创建交易，涉及到 evm，需要按照 ContractCreate 交易执行
    // 根据有无 contract_from 字段区分
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = common::kInvalidPoolIndex;
        auto to_tx = to_txs.tos(i);
        auto addr = to_tx.des();
        if (to_tx.des().size() == security::kUnicastAddressLength * 2) { // gas_prepayment tx des = to + from
            addr = to_tx.des().substr(0, security::kUnicastAddressLength); // addr = to
        }
        
        auto account_info = account_mgr_->GetAccountInfo(addr);
        if (account_info == nullptr) {
            // 只接受 root 发回来的块
            if (step != pools::protobuf::kRootCreateAddress) {
                ZJC_WARN("failed add local transfer tx tos id: %s",
                    common::Encode::HexEncode(addr).c_str());
                continue;
            }

            if (!to_tx.has_sharding_id() || !to_tx.has_pool_index()) {
                assert(false);
                continue;
            }

            if (to_tx.sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                assert(false);
                continue;
            }

            if (to_tx.pool_index() >= common::kImmutablePoolSize) {
                assert(false);
                continue;
            }

            sharding_id = to_tx.sharding_id();
            pool_index = to_tx.pool_index();
            ZJC_DEBUG("root create address coming %s, shard: %u, pool: %u",
                common::Encode::HexEncode(addr).c_str(), sharding_id, pool_index);
        } else {
            if (to_tx.sharding_id() != account_info->sharding_id()) {
                continue;
            }

            if (to_tx.pool_index() != account_info->pool_index()) {
                continue;
            }

            sharding_id = account_info->sharding_id();
            pool_index = account_info->pool_index();
        }

        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            assert(false);
            continue;
        }

        // 转账类型交易根据 to 地址聚合到一个 map 中
        if (!to_tx.has_library_bytes()) {
            auto iter = addr_amount_map.find(to_tx.des());
            if (iter == addr_amount_map.end()) {
                addr_amount_map[to_tx.des()] = std::make_shared<localToTxInfo>(to_tx.des(),
                    to_tx.amount(), pool_index, "", "", 0);
            } else {
                iter->second->amount += to_tx.amount();
            }
        } else { // 合约创建交易统计到一个 vector
            auto info = std::make_shared<localToTxInfo>(to_tx.des(),
                to_tx.amount(),
                pool_index,
                to_tx.library_bytes(),
                to_tx.contract_from(),
                to_tx.prepayment());
            contract_create_tx_infos.push_back(info); // TODO prepayment 也需要传输过来
        }
    }

    // 1. 处理转账类交易
    createConsensusLocalToTxs(tx, addr_amount_map);
    // 2. 生成 ContractCreateByRootTo 交易
    createContractCreateByRootToTxs(contract_create_tx_infos);
}

void BlockManager::createConsensusLocalToTxs(
        const block::protobuf::BlockTx& to_tx,
        std::unordered_map<std::string, std::shared_ptr<localToTxInfo>> addr_amount_map) {
    // 根据 pool_index 将 addr_amount_map 中的转账交易分类，一个 pool 生成一个 Consensuslocaltos，其中可能包含给多个地址的转账交易
    std::unordered_map<uint32_t, pools::protobuf::ToTxMessage> to_tx_map;
    for (auto iter = addr_amount_map.begin(); iter != addr_amount_map.end(); ++iter) {
        auto to_iter = to_tx_map.find(iter->second->pool_index);
        if (to_iter == to_tx_map.end()) {
            pools::protobuf::ToTxMessage to_tx;
            to_tx_map[iter->second->pool_index] = to_tx;
            to_iter = to_tx_map.find(iter->second->pool_index);
        }

        // 每个 kConsensusLocalTos 是一个 pool 的，且只有一个 to_item
        auto to_item = to_iter->second.add_tos();
        to_item->set_pool_index(iter->second->pool_index);
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second->amount);

        ZJC_DEBUG("success add local transfer to %s, %lu",
            common::Encode::HexEncode(iter->first).c_str(),
            iter->second->amount);
    }

    // 一个 pool 生成一个 Consensuslocaltos
    for (auto iter = to_tx_map.begin(); iter != to_tx_map.end(); ++iter) {
        // 48 ? des = to(20) + from(20)  = 40, 40 + pool(4) + amount(8) = 52
        for (int32_t i = 0; i < iter->second.tos_size(); ++i) {
            uint32_t pool_idx = iter->second.tos(i).pool_index();
            uint64_t amount = iter->second.tos(i).amount();
            ZJC_DEBUG("ammount success add local transfer to %s, %lu",
                common::Encode::HexEncode(iter->second.tos(i).des()).c_str(), amount);
        }

        // 由于是异步的，因此需要持久化 kv 来传递数据，但是 to 需要填充以分配交易池
        // pool index 是指定好的，而不是 shard 分配的，所以需要将 to 设置为 pool addr
        auto val = iter->second.SerializeAsString();
        auto tos_hash = common::Hash::keccak256(val);
        prefix_db_->SaveTemporaryKv(tos_hash, val);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        msg_ptr->address_info = account_mgr_->pools_address_info(iter->first);
        auto tx = msg_ptr->header.mutable_tx_proto();
        // 将 tos_hash 存入 kv，用于 HandleTx 时获取 val
        tx->set_key(protos::kLocalNormalTos);
        tx->set_value(val);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        auto gid = common::Hash::keccak256(to_tx.gid());
        tx->set_gas_limit(0);
        tx->set_amount(0); // 具体 amount 在 kv 中
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("success add local transfer tx tos hash: %s, gid: %s, src to tx gid: %s",
            common::Encode::HexEncode(tos_hash).c_str(),
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(to_tx.gid()).c_str());
    }
}

void BlockManager::createContractCreateByRootToTxs(
        std::vector<std::shared_ptr<localToTxInfo>> contract_create_tx_infos) {
    std::unordered_map<uint32_t, pools::protobuf::ToTxMessage> to_cc_tx_map;
    for (uint32_t i = 0; i < contract_create_tx_infos.size(); i++) {
        auto contract_create_tx = contract_create_tx_infos[i];
        uint32_t pool_index = contract_create_tx->pool_index;
        auto to_iter = to_cc_tx_map.find(pool_index);
        if (to_iter == to_cc_tx_map.end()) {
            pools::protobuf::ToTxMessage to_tx;
            to_cc_tx_map[pool_index] = to_tx;
            to_iter = to_cc_tx_map.find(pool_index);
        }

        auto to_item = to_iter->second.add_tos();
        to_item->set_pool_index(pool_index);
        to_item->set_des(contract_create_tx->des);
        to_item->set_amount(contract_create_tx->amount);
        to_item->set_library_bytes(contract_create_tx->library_bytes);
        to_item->set_contract_from(contract_create_tx->contract_from);
        to_item->set_prepayment(contract_create_tx->contract_prepayment);
        
        ZJC_DEBUG("success add local contract create to %s, %lu, "
            "contract_from %s, contract_code: %s, prepayment: %lu",
            common::Encode::HexEncode(contract_create_tx->des).c_str(),
            contract_create_tx->amount,
            common::Encode::HexEncode(contract_create_tx->contract_from).c_str(),
            common::Encode::HexEncode(contract_create_tx->library_bytes).c_str(),
            contract_create_tx->contract_prepayment);
    }
    
    for (auto iter = to_cc_tx_map.begin(); iter != to_cc_tx_map.end(); iter++) {
        if (iter->second.tos_size() <= 0) {
            continue;
        }

        auto to_msg = iter->second.tos(0); 
        std::string str_for_hash;
        str_for_hash.append(to_msg.des());
        uint32_t pool_idx = to_msg.pool_index();
        str_for_hash.append(reinterpret_cast<char*>(&pool_idx), sizeof(pool_idx));
        uint64_t amount = to_msg.amount();
        str_for_hash.append(reinterpret_cast<char*>(&amount), sizeof(amount));
        std::string contract_code = to_msg.library_bytes();
        str_for_hash.append(contract_code);
        std::string contract_from = to_msg.contract_from();
        str_for_hash.append(contract_from);
        auto cc_hash = common::Hash::keccak256(str_for_hash);
        
        auto val = iter->second.SerializeAsString();
        prefix_db_->SaveTemporaryKv(cc_hash, val);
        // 与 consensuslocaltos 不同，每个交易只有一个 contractcreate，不必持久化
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        // 指定 root 分配的 pool
        msg_ptr->address_info = account_mgr_->pools_address_info(iter->first);
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kCreateContractLocalInfo);
        tx->set_value(cc_hash);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kContractCreateByRootTo);
        auto gid = common::Hash::keccak256(cc_hash);
        // TODO 暂时写死用于调试，实际需要 ContractCreateByRootFrom 交易传
        tx->set_gas_limit(1000000);
        tx->set_gas_price(1);
        tx->set_gid(gid);

        // 真正的 des 存在 kv 中
        tx->set_amount(to_msg.amount());
        tx->set_contract_code(to_msg.library_bytes());
        tx->set_contract_from(to_msg.contract_from());
        tx->set_contract_prepayment(to_msg.prepayment());
        
        ZJC_DEBUG("create contract to tx add to pool, to: %s, gid: %s, "
            "cc_hash: %s, pool_idx: %lu, amount: %lu, contract_from: %s",
            common::Encode::HexEncode(to_msg.des()).c_str(),
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(cc_hash).c_str(),
            pool_idx, amount, common::Encode::HexEncode(contract_from).c_str());
        pools_mgr_->HandleMessage(msg_ptr);        
    }
}

void BlockManager::AddNewBlock(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch) {
    // TODO: check all block saved success
    ZJC_DEBUG("new block coming sharding id: %u, pool: %d, height: %lu, "
        "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
        block_item->network_id(),
        block_item->pool_index(),
        block_item->height(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(block_item->hash()).c_str(),
        block_item->electblock_height(),
        block_item->timeblock_height());
    assert(block_item->electblock_height() >= 1);
    // block 两条信息持久化
    if (!prefix_db_->SaveBlock(*block_item, db_batch)) {
        ZJC_DEBUG("block saved: %lu", block_item->height());
        return;
    }

    // db_batch 并没有用，只是更新下 to_txs_pool 的状态，如高度
    to_txs_pool_->NewBlock(block_item, db_batch);

    // 当前节点和 block 分配的 shard 不同，要跨分片交易
    if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
            block_item->network_id() + network::kConsensusWaitingShardOffset !=
            common::GlobalInfo::Instance()->network_id()) {
        pools_mgr_->OnNewCrossBlock(block_item);
        ZJC_DEBUG("new cross block coming: %u, %u, %lu",
            block_item->network_id(), block_item->pool_index(), block_item->height());
    }

    const auto& tx_list = block_item->tx_list();
    // 处理交易信息
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        ZJC_DEBUG("new block coming sharding id: %u, pool: %d, height: %lu, "
            "tx size: %u, hash: %s, elect height: %lu, tm height: %lu, gid: %s, status: %d, step: %d",
            block_item->network_id(),
            block_item->pool_index(),
            block_item->height(),
            block_item->tx_list_size(),
            common::Encode::HexEncode(block_item->hash()).c_str(),
            block_item->electblock_height(),
            block_item->timeblock_height(),
            common::Encode::HexEncode(tx_list[i].gid()).c_str(),
            tx_list[i].status(),
            tx_list[i].step());
        if (tx_list[i].status() != consensus::kConsensusSuccess) {
            continue;
        }

        switch (tx_list[i].step()) {
        case pools::protobuf::kRootCreateAddress:
            // ZJC_DEBUG("success handle root create address tx.");
            ConsensusShardHandleRootCreateAddress(*block_item, tx_list[i]);
            break;
        case pools::protobuf::kNormalTo: {
            HandleNormalToTx(*block_item, tx_list[i], db_batch);
            if (network::IsSameToLocalShard(block_item->network_id())) {
                auto tmp_latest_to_block_ptr_index = (latest_to_block_ptr_index_ + 1) % 2;
                latest_to_block_ptr_[tmp_latest_to_block_ptr_index] = block_item;
                latest_to_block_ptr_index_ = tmp_latest_to_block_ptr_index;
            }

            ZJC_DEBUG("success handle to tx network: %u, pool: %u, height: %lu, "
                "gid: %s, bls: %s, %s",
                block_item->network_id(),
                block_item->pool_index(),
                block_item->height(),
                common::Encode::HexEncode(tx_list[i].gid()).c_str(),
                common::Encode::HexEncode(block_item->bls_agg_sign_x()).c_str(),
                common::Encode::HexEncode(block_item->bls_agg_sign_y()).c_str());
            break;
        }
        case pools::protobuf::kConsensusRootTimeBlock:
            prefix_db_->SaveLatestTimeBlock(block_item->height(), db_batch);
            break;
        case pools::protobuf::kStatistic:
            HandleStatisticTx(*block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kCross:
            assert(false);
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectTx(*block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kJoinElect:
            HandleJoinElectTx(*block_item, tx_list[i], db_batch);
            break;
        default:
            break;
        }
    }

    if (new_block_callback_ != nullptr) {
        if (!new_block_callback_(block_item, db_batch)) {
            ZJC_DEBUG("block call back failed!");
            assert(false);
            return;
        }
    }

    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        ZJC_FATAL("write block to db failed!");
    }

    ZJC_DEBUG("success new block coming sharding id: %u, pool: %d, height: %lu, "
        "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
        block_item->network_id(),
        block_item->pool_index(),
        block_item->height(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(block_item->hash()).c_str(),
        block_item->electblock_height(),
        block_item->timeblock_height());
    if (ck_client_ != nullptr) {
        ck_client_->AddNewBlock(block_item);
        ZJC_DEBUG("add to ck.");
    }
}

// HandleJoinElectTx 持久化 JoinElect 交易相关信息
void BlockManager::HandleJoinElectTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    // 存放了一个 from => balance 的映射
    prefix_db_->SaveElectNodeStoke(
        tx.from(),
        block.electblock_height(),
        tx.balance(),
        db_batch);
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            // 解析参与选举的信息
            bls::protobuf::JoinElectInfo join_info;
            ZJC_DEBUG("now parse join elect info: %u", tx.storages(i).value().size());
            if (!join_info.ParseFromString(tx.storages(i).value())) {
                assert(false);
                break;
            }

            if (join_info.g2_req().verify_vec_size() <= 0) {
                ZJC_DEBUG("success handle kElectJoin tx: %s, not has verfications.",
                    common::Encode::HexEncode(tx.from()).c_str());
                break;
            }

            prefix_db_->SaveNodeVerificationVector(tx.from(), join_info, db_batch);
            ZJC_DEBUG("success handle kElectJoin tx: %s, net: %u, pool: %u, height: %lu, local net id: %u",
                common::Encode::HexEncode(tx.from()).c_str(), 
                block.network_id(), 
                block.pool_index(), 
                block.height(),
                common::GlobalInfo::Instance()->network_id());
            break;
        }
    }
}

void BlockManager::HandleElectTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("handle elect tx storage size: %u", tx.storages_size());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("handle elect tx storage index: %u, key: %s, protos::kElectNodeAttrElectBlock: %s",
            i, tx.storages(i).key().c_str(), protos::kElectNodeAttrElectBlock.c_str());
        if (tx.storages(i).key() == protos::kElectNodeAttrElectBlock) {
            elect::protobuf::ElectBlock elect_block;
            if (!elect_block.ParseFromString(tx.storages(i).value())) {
                assert(false);
                return;
            }

            AddMiningToken(block.hash(), elect_block);
            // 尝试扩容
            TryDynamicSharding(elect_block);
            if (shard_elect_tx_[elect_block.shard_network_id()] != nullptr) {
                if (shard_elect_tx_[elect_block.shard_network_id()]->tx_ptr->tx_info.gid() == tx.gid()) {
                    shard_elect_tx_[elect_block.shard_network_id()] = nullptr;
                    ZJC_DEBUG("success erase elect tx: %u", elect_block.shard_network_id());
                }
            }

            ZJC_DEBUG("success add elect block elect height: %lu, net: %u, "
                "pool: %u, height: %lu, common pk: %s, prev elect height: %lu", 
                block.electblock_height(),
                block.network_id(),
                block.pool_index(),
                block.height(),
                common::Encode::HexEncode(
                elect_block.prev_members().common_pubkey().SerializeAsString()).c_str(),
                elect_block.prev_members().prev_elect_height());
            // 将 elect block 中的 common_pk 持久化
            if (elect_block.prev_members().prev_elect_height() > 0) {
                prefix_db_->SaveElectHeightCommonPk(
                    elect_block.shard_network_id(),
                    elect_block.prev_members().prev_elect_height(),
                    elect_block.prev_members(),
                    db_batch);
            }
        }
    }
}

// 尝试分片扩容
void BlockManager::TryDynamicSharding(const elect::protobuf::ElectBlock& elect_block) {
    if (!elect_block.has_dynamic_sharding_info()) {
        return;
    }
    
    auto dynamic_sharding_info = elect_block.dynamic_sharding_info();
    auto shard_id = dynamic_sharding_info.network_id();

    ZJC_DEBUG("dynamic sharding begin, s: %d, act: %d, cur: %d, has preopened: %d, biggest opened: %d",
        shard_id,
        dynamic_sharding_info.action(),
        network::NetsInfo::Instance()->net_info(shard_id)->Status(),
        network::NetsInfo::Instance()->HasPreopenedNetwork(),
        network::NetsInfo::Instance()->BiggestOpenedNetId());
    defer({
            ZJC_DEBUG("dynamic sharding end, s: %d, act: %d, cur: %d",
                shard_id,
                dynamic_sharding_info.action(),
                network::NetsInfo::Instance()->net_info(shard_id)->Status()
                );
        });
    
    if (shard_id < network::kConsensusShardBeginNetworkId ||
        shard_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    if (dynamic_sharding_info.action() == uint32_t(network::ShardStatus::kPreopened)) {
        if (network::NetsInfo::Instance()->HasPreopenedNetwork() ||
            shard_id != network::NetsInfo::Instance()->BiggestOpenedNetId()+1) {
            return;
        }        
        network::NetsInfo::Instance()->SetPreopened(shard_id);        
        return;
    }

    if (dynamic_sharding_info.action() == uint32_t(network::ShardStatus::kOpened)) {
        if (network::NetsInfo::Instance()->PreopenedNetworkId() != shard_id) {
            return;
        }
        network::NetsInfo::Instance()->SetOpened(shard_id);
        return;
    }
    
    return;
}

void BlockManager::AddMiningToken(
        const std::string& block_hash,
        const elect::protobuf::ElectBlock& elect_block) {
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        return;
    }

    std::unordered_map<uint32_t, pools::protobuf::ToTxMessage> to_tx_map;
    for (int32_t i = 0; i < elect_block.in_size(); ++i) {
        if (elect_block.in(i).mining_amount() <= 0) {
            continue;
        }

        auto id = security_->GetAddress(elect_block.in(i).pubkey());
        auto account_info = account_mgr_->GetAccountInfo(id);
        if (account_info == nullptr ||
                account_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
            continue;
        }

        auto pool_index = common::GetAddressPoolIndex(id);
        auto to_iter = to_tx_map.find(pool_index);
        if (to_iter == to_tx_map.end()) {
            pools::protobuf::ToTxMessage to_tx;
            to_tx_map[pool_index] = to_tx;
            to_iter = to_tx_map.find(pool_index);
        }

        auto to_item = to_iter->second.add_tos();
        to_item->set_pool_index(pool_index);
        to_item->set_des(id);
        to_item->set_amount(elect_block.in(i).mining_amount());
        ZJC_DEBUG("mining success add local transfer to %s, %lu",
            common::Encode::HexEncode(id).c_str(), elect_block.in(i).mining_amount());
    }

    for (auto iter = to_tx_map.begin(); iter != to_tx_map.end(); ++iter) {
        std::string str_for_hash;
        str_for_hash.reserve(iter->second.tos_size() * 48);
        for (int32_t i = 0; i < iter->second.tos_size(); ++i) {
            str_for_hash.append(iter->second.tos(i).des());
            uint32_t pool_idx = iter->second.tos(i).pool_index();
            str_for_hash.append((char*)&pool_idx, sizeof(pool_idx));
            uint64_t amount = iter->second.tos(i).amount();
            str_for_hash.append((char*)&amount, sizeof(amount));
        }

        auto val = iter->second.SerializeAsString();
        auto tos_hash = common::Hash::keccak256(str_for_hash);
        prefix_db_->SaveTemporaryKv(tos_hash, val);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        msg_ptr->address_info = account_mgr_->pools_address_info(iter->first);
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kLocalNormalTos);
        tx->set_value(val);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        auto gid = common::Hash::keccak256(tos_hash + block_hash);
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("success create kConsensusLocalTos gid: %s", common::Encode::HexEncode(gid).c_str());
    }
}

void BlockManager::LoadLatestBlocks() {
    if (!prefix_db_->GetConsensusedStatisticTimeBlockHeight(
            common::GlobalInfo::Instance()->network_id(),
            &consensused_timeblock_height_)) {
        ZJC_ERROR("init latest consensused statistic time block height failed!");
    }

    timeblock::protobuf::TimeBlock tmblock;
    db::DbWriteBatch db_batch;
    if (prefix_db_->GetLatestTimeBlock(&tmblock)) {
        auto tmblock_ptr = std::make_shared<block::protobuf::Block>();
        auto& block = *tmblock_ptr;
        if (GetBlockWithHeight(
                network::kRootCongressNetworkId,
                common::kRootChainPoolIndex,
                tmblock.height(),
                block) == kBlockSuccess) {
            if (new_block_callback_ != nullptr) {
                new_block_callback_(tmblock_ptr, db_batch);
            }
        }
    }

    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        elect::protobuf::ElectBlock elect_block;
        if (!prefix_db_->GetLatestElectBlock(i, &elect_block)) {
            ZJC_ERROR("get elect latest block failed: %u", i);
            break;
        }

        auto elect_block_ptr = std::make_shared<block::protobuf::Block>();
        auto& block = *elect_block_ptr;
        if (GetBlockWithHeight(
                network::kRootCongressNetworkId,
                elect_block.shard_network_id() % common::kImmutablePoolSize,
                elect_block.elect_height(),
                block) == kBlockSuccess) {
            if (new_block_callback_ != nullptr) {
                new_block_callback_(elect_block_ptr, db_batch);
            }

            ZJC_INFO("get block with height success: %u, %u, %lu",
                network::kRootCongressNetworkId,
                elect_block.shard_network_id() % common::kImmutablePoolSize,
                elect_block.elect_height());
        } else {
            ZJC_FATAL("get block with height failed: %u, %u, %lu",
                network::kRootCongressNetworkId,
                elect_block.shard_network_id() % common::kImmutablePoolSize,
                elect_block.elect_height());
        }
    }

    db_->Put(db_batch);
}

int BlockManager::GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        block::protobuf::Block& block_item) {
    if (!prefix_db_->GetBlockWithHeight(network_id, pool_index, height, &block_item)) {
        return kBlockError;
    }

    return kBlockSuccess;
}

void BlockManager::CreateStatisticTx() {
#ifndef NDEBUG
    static auto now_thread_id_tmp = std::this_thread::get_id();
    auto tmp_thread_id_tmp = std::this_thread::get_id();
    assert(now_thread_id_tmp == tmp_thread_id_tmp);
#endif
    if (create_statistic_tx_cb_ == nullptr) {
        ZJC_DEBUG("create_statistic_tx_cb_ == nullptr");
        return;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    uint64_t timeblock_height = prev_timeblock_height_;
    if (statistic_mgr_->StatisticWithHeights(
            elect_statistic,
            timeblock_height) != pools::kPoolsSuccess) {
        ZJC_DEBUG("failed StatisticWithHeights!");
        return;
    }

    // TODO: fix invalid hash
    std::string statistic_hash = common::Hash::keccak256(elect_statistic.SerializeAsString());
    ZJC_DEBUG("success create statistic message hash: %s, timeblock_height: %lu", 
        common::Encode::HexEncode(statistic_hash).c_str(), timeblock_height);
    {
        ZJC_DEBUG("LLLLLL statistic :%s", ProtobufToJson(elect_statistic).c_str());
    }
    if (!statistic_hash.empty()) {
        auto tm_statistic_iter = shard_statistics_map_.find(timeblock_height);
        if (tm_statistic_iter == shard_statistics_map_.end()) {
            auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
            auto* tx = new_msg_ptr->header.mutable_tx_proto();
            tx->set_key(protos::kShardStatistic);
            tx->set_value(elect_statistic.SerializeAsString());
            tx->set_pubkey("");
            tx->set_step(pools::protobuf::kStatistic);
            auto gid = common::Hash::keccak256(
                statistic_hash + std::to_string(common::GlobalInfo::Instance()->network_id()));
            tx->set_gas_limit(0);
            tx->set_amount(0);
            tx->set_gas_price(common::kBuildinTransactionGasPrice);
            tx->set_gid(gid);
            auto tx_ptr = std::make_shared<BlockTxsItem>();
            tx_ptr->tx_ptr = create_statistic_tx_cb_(new_msg_ptr);
            tx_ptr->tx_ptr->time_valid += kStatisticValidTimeout;
            tx_ptr->tx_hash = statistic_hash;
            tx_ptr->tx_ptr->unique_tx_hash = tx_ptr->tx_hash;
            tx_ptr->timeout = common::TimeUtils::TimestampMs() + kStatisticTimeoutMs;
            tx_ptr->stop_consensus_timeout = tx_ptr->timeout + kStopConsensusTimeoutMs;
            ZJC_INFO("success add statistic tx: %s, statistic elect height: %lu, "
                "heights: %s, timeout: %lu, kStatisticTimeoutMs: %lu, now: %lu, "
                "gid: %s, timeblock_height: %lu",
                common::Encode::HexEncode(statistic_hash).c_str(),
                0,
                "", tx_ptr->timeout,
                kStatisticTimeoutMs, common::TimeUtils::TimestampMs(),
                common::Encode::HexEncode(gid).c_str(),
                timeblock_height);
            shard_statistics_map_[timeblock_height] = tx_ptr;
            auto tmp_ptr = std::make_shared<StatisticMap>(shard_statistics_map_);
            shard_statistics_map_ptr_queue_.push(tmp_ptr);
        }
    }
}

// Only for root
void BlockManager::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        const pools::protobuf::ElectStatistic& elect_statistic,
        db::DbWriteBatch& db_batch) {
    if (create_elect_tx_cb_ == nullptr) {
        ZJC_INFO("create_elect_tx_cb_ == nullptr");
        return;
    }

    // 时间粒度一个 epoch, 10min 一个选举块
    if (prefix_db_->ExistsStatisticedShardingHeight(
            block.network_id(),
            block.timeblock_height())) {
        ZJC_INFO("prefix_db_->ExistsStatisticedShardingHeight net: %u, tm height: %lu",
            block.network_id(), block.timeblock_height());
        return;
    }

    if (elect_statistic.statistics_size() <= 0) {
        return;
    }

    prefix_db_->SaveStatisticedShardingHeight(
        block.network_id(),
        block.timeblock_height(),
        elect_statistic,
        db_batch);
    for (uint32_t i = 0; i < elect_statistic.join_elect_nodes_size(); ++i) {
        ZJC_DEBUG("sharding: %u, new elect node: %s, balance: %lu, shard: %u, pos: %u", 
            elect_statistic.sharding_id(), 
            common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
            elect_statistic.join_elect_nodes(i).stoke(),
            elect_statistic.join_elect_nodes(i).shard(),
            elect_statistic.join_elect_nodes(i).elect_pos());
    }

    assert(block.network_id() == elect_statistic.sharding_id());
    ZJC_DEBUG("success handle statistic block net: %u, sharding: %u, "
        "pool: %u, height: %lu, elect height: %lu",
        block.network_id(), elect_statistic.sharding_id(), block.pool_index(), 
        block.timeblock_height(), elect_statistic.statistics(elect_statistic.statistics_size() - 1).elect_height());
    // create elect transaction now for block.network_id
    auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
    new_msg_ptr->address_info = account_mgr_->pools_address_info(elect_statistic.sharding_id());
    auto* tx = new_msg_ptr->header.mutable_tx_proto();
    tx->set_key(protos::kShardElection);
    char data[16];
    uint64_t* tmp = (uint64_t*)data;
    tmp[0] = block.network_id();
    tmp[1] = block.timeblock_height();
    tx->set_value(std::string(data, sizeof(data)));
    tx->set_pubkey("");
    tx->set_to(new_msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kConsensusRootElectShard);
    auto gid = common::Hash::keccak256(kShardElectPrefix + tx->value());
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    auto shard_elect_tx = std::make_shared<BlockTxsItem>();
    shard_elect_tx->tx_ptr = create_elect_tx_cb_(new_msg_ptr);
    shard_elect_tx->tx_ptr->time_valid += kElectValidTimeout;
    shard_elect_tx->tx_ptr->unique_tx_hash = pools::GetTxMessageHash(
        shard_elect_tx->tx_ptr->tx_info);
    shard_elect_tx->timeout = common::TimeUtils::TimestampMs() + kElectTimeout;
    shard_elect_tx->stop_consensus_timeout = shard_elect_tx->timeout + kStopConsensusTimeoutMs;
    // 在 root 网络生成 kConsensusRootElectShard 交易，等待共识
    shard_elect_tx_[block.network_id()] = shard_elect_tx;
    ZJC_INFO("success add elect tx: %u, %lu, gid: %s, txhash: %s, statistic elect height: %lu",
        block.network_id(), block.timeblock_height(),
        common::Encode::HexEncode(gid).c_str(),
        common::Encode::HexEncode(shard_elect_tx->tx_ptr->unique_tx_hash).c_str(),
        0);
}

pools::TxItemPtr BlockManager::GetToTx(uint32_t pool_index, const std::string& heights_str) {
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        return nullptr;
    }

    if (pool_index != 0) {
        return nullptr;
    }

    pools::protobuf::ShardToTxItem heights;
    if (heights_str.empty()) {
        auto cur_time = common::TimeUtils::TimestampMs();
        if (leader_prev_get_to_tx_tm_ > cur_time) {
            return nullptr;
        }

        ZJC_DEBUG("now leader get to to tx.");
        leader_prev_get_to_tx_tm_ = cur_time + 3000lu;
        auto latest_to_block_ptr = latest_to_block_ptr_[latest_to_block_ptr_index_];
        if (latest_to_block_ptr != nullptr && latest_to_block_ptr->timestamp() + 10000lu >= cur_time) {
            return nullptr;
        }

        if (to_txs_pool_->LeaderCreateToHeights(heights) != pools::kPoolsSuccess) {
            return nullptr;
        }
    } else {
        if (!heights.ParseFromString(heights_str)) {
            assert(false);
            return nullptr;
        }
    }

    std::string string_for_hash;
    for (uint32_t i = 0; i < heights.heights_size(); ++i) {
        auto height = heights.heights(i);
        string_for_hash.append((char*)&height, sizeof(height));
    }

    auto height_hash = common::Hash::keccak256(string_for_hash);
    auto iter = heights_str_map_.find(height_hash);
    if (iter != heights_str_map_.end()) {
        std::string gid = common::Hash::keccak256("0000");
        auto latest_to_block = latest_to_block_ptr_[latest_to_block_ptr_index_];
        if (latest_to_block != nullptr) {
            gid = common::Hash::keccak256(
                std::to_string(latest_to_block->height()) +
                std::to_string(latest_to_block->timestamp()));
        }
        
        auto tx_ptr = iter->second;
        tx_ptr->tx_info.set_gid(gid);
        ZJC_DEBUG("success get exists to tx tx info: %s, gid: %s, val: %s, heights: %s", 
            ProtobufToJson(tx_ptr->tx_info).c_str(),
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str(), 
            common::Encode::HexEncode(tx_ptr->tx_info.value()).c_str(),
            ProtobufToJson(heights).c_str());
        return iter->second;
    }

    auto tx_ptr = HandleToTxsMessage(heights);
    if (tx_ptr != nullptr) {
        heights_str_map_[height_hash] = tx_ptr;
        ZJC_DEBUG("success get to tx tx info: %s, gid: %s, val: %s, heights: %s", 
            ProtobufToJson(tx_ptr->tx_info).c_str(),
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str(), 
            common::Encode::HexEncode(tx_ptr->tx_info.value()).c_str(),
            ProtobufToJson(heights).c_str());
    } else {
        ZJC_DEBUG("failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
    }

    return tx_ptr;
}

pools::TxItemPtr BlockManager::HandleToTxsMessage(
        const pools::protobuf::ShardToTxItem& heights) {
    std::string str_heights;
    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        str_heights += std::to_string(i) + "_" + std::to_string(heights.heights(i)) + " ";
    }

    if (create_to_tx_cb_ == nullptr) {
        return nullptr;
    }

    bool all_valid = true;
    // 聚合不同 to shard 的交易
    if (!to_txs_pool_->StatisticTos(heights)) {
        ZJC_DEBUG("statistic tos failed!");
        return nullptr;
    }

    pools::protobuf::AllToTxMessage all_to_txs;
    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id <= max_consensus_sharding_id_; ++sharding_id) {
        auto& to_tx = *all_to_txs.add_to_tx_arr();
        if (to_txs_pool_->CreateToTxWithHeights(
                sharding_id,
                0,
                heights,
                to_tx) != pools::kPoolsSuccess) {
            all_valid = false;
            all_to_txs.mutable_to_tx_arr()->RemoveLast();
            ZJC_DEBUG("1 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        }
    }

    if (all_to_txs.to_tx_arr_size() == 0) {
        ZJC_DEBUG("2 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        return nullptr;
    }
    
    auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
    new_msg_ptr->address_info = account_mgr_->pools_address_info(0 % common::kImmutablePoolSize);
    auto* tx = new_msg_ptr->header.mutable_tx_proto();
    tx->set_key(protos::kNormalTos);
    // TODO: fix hash invalid
    auto tos_hashs = common::Hash::keccak256(all_to_txs.SerializeAsString());
    tx->set_value(all_to_txs.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to(new_msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kNormalTo);
    std::string gid = common::Hash::keccak256("0000");
    auto latest_to_block = latest_to_block_ptr_[latest_to_block_ptr_index_];
    if (latest_to_block != nullptr) {
        gid = common::Hash::keccak256(
            std::to_string(latest_to_block->height()) +
            std::to_string(latest_to_block->timestamp()));
        ZJC_DEBUG("success get to tx  latest height: %lu, tm: %lu, "
            "gid: %s, heights: %s tx info: %s, hash: %s",
            latest_to_block->height(),
            latest_to_block->timestamp(),
            common::Encode::HexEncode(gid).c_str(),
            ProtobufToJson(heights).c_str(),
            ProtobufToJson(all_to_txs).c_str(),
            common::Encode::HexEncode(common::Hash::keccak256(tx->value())).c_str());
    } else {
        ZJC_DEBUG("success get to tx  latest height: %lu, tm: %lu, "
            "gid: %s, heights: %s tx info: %s, hash: %s",
            0,
            0,
            common::Encode::HexEncode(gid).c_str(),
            ProtobufToJson(heights).c_str(),
            ProtobufToJson(all_to_txs).c_str(),
            common::Encode::HexEncode(common::Hash::keccak256(tx->value())).c_str());
    }

    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    auto tx_ptr = create_to_tx_cb_(new_msg_ptr);
    tx_ptr->time_valid += kToValidTimeout;
    tx_ptr->unique_tx_hash = tos_hashs;
    return tx_ptr;
}

bool BlockManager::HasSingleTx(uint32_t pool_index) {
    if (HasToTx(pool_index)) {
        ZJC_DEBUG("success check has to tx.");
        return true;
    }

    if (HasStatisticTx(pool_index)) {
        ZJC_DEBUG("success check has statistic tx.");
        return true;
    }

    if (HasElectTx(pool_index)) {
        ZJC_DEBUG("success check has elect tx.");
        return true;
    }

    return false;
}

void BlockManager::PopTxTicker() {
    std::shared_ptr<StatisticMap> static_tmp_map = nullptr;
    while (shard_statistics_map_ptr_queue_.pop(&static_tmp_map)) {}
    if (static_tmp_map != nullptr) {
        for (auto iter = static_tmp_map->begin(); iter != static_tmp_map->end(); ++iter) {
            ZJC_DEBUG("now pop statistic tx tx hash: %s, tm height: %lu",
                common::Encode::HexEncode(iter->second->tx_ptr->tx_info.gid()).c_str(), iter->first);
        }

        auto valid_got_latest_statistic_map_ptr_index_tmp = (valid_got_latest_statistic_map_ptr_index_ + 1) % 2;
        got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_tmp] = static_tmp_map;
        valid_got_latest_statistic_map_ptr_index_ = valid_got_latest_statistic_map_ptr_index_tmp;
    }

    pop_tx_tick_.CutOff(50000lu, std::bind(&BlockManager::PopTxTicker, this));
}

bool BlockManager::HasToTx(uint32_t pool_index) {
    return false;
}

bool BlockManager::HasStatisticTx(uint32_t pool_index) {
    if (pool_index != common::kRootChainPoolIndex) {
        return false;
    }

    auto statistic_map_ptr = got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_];
    if (statistic_map_ptr == nullptr) {
        return false;
    }

    if (statistic_map_ptr->empty()) {
        return false;
    }

    auto iter = statistic_map_ptr->begin();
    auto shard_statistic_tx = iter->second;
    if (shard_statistic_tx == nullptr) {
        ZJC_DEBUG("shard_statistic_tx == nullptr");
        return false;
    }

    if (shard_statistic_tx != nullptr) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (iter->first >= latest_timeblock_height_) {
            return false;
        }

        if (prev_timeblock_tm_sec_ + (common::kRotationPeriod / (1000lu * 1000lu)) > (now_tm / 1000000lu)) {
            return false;
        }

        return true;
    }

    return false;
}

bool BlockManager::HasElectTx(uint32_t pool_index) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        if (shard_elect_tx_[i] == nullptr) {
            continue;
        }

        return true;
    }

    return false;
}

pools::TxItemPtr BlockManager::GetStatisticTx(
        uint32_t pool_index, 
        const std::string& tx_gid) {
    bool leader = tx_gid.empty();
    while (shard_statistics_map_ptr_queue_.size() > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(50000ull));
    }

    auto statistic_map_ptr = got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_];
    if (statistic_map_ptr == nullptr) {
        ZJC_DEBUG("statistic_map_ptr == nullptr");
        return nullptr;
    }

    if (statistic_map_ptr->empty()) {
        ZJC_DEBUG("statistic_map_ptr->empty()");
        return nullptr;
    }

    std::shared_ptr<BlockTxsItem> shard_statistic_tx = nullptr;
    auto iter = statistic_map_ptr->begin();
    for (; iter != statistic_map_ptr->end(); ++iter) {
        if (leader) {
            shard_statistic_tx = iter->second;
            break;
        }

        if (iter->second->tx_ptr->tx_info.gid() == tx_gid) {
            shard_statistic_tx = iter->second;
            break;
        }
    }

    if (shard_statistic_tx == nullptr) {
        ZJC_DEBUG("shard_statistic_tx == nullptr, tx_gid: %s, is leader: %d",
            common::Encode::HexEncode(tx_gid).c_str(),
            leader);
        return nullptr;
    }

    static uint64_t prev_get_tx_tm = common::TimeUtils::TimestampMs();
    auto now_tx_tm = common::TimeUtils::TimestampMs();
    if (now_tx_tm > prev_get_tx_tm + 10000) {
        prev_get_tx_tm = now_tx_tm;
    }

    if (shard_statistic_tx != nullptr) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (leader && shard_statistic_tx->tx_ptr->time_valid > now_tm) {
            ZJC_DEBUG("leader get tx failed: %lu, %lu", shard_statistic_tx->tx_ptr->time_valid, now_tm);
            return nullptr;
        }

        if (iter->first >= latest_timeblock_height_) {
            if (leader) {
                ZJC_DEBUG("iter->first >= latest_timeblock_height_: %lu, %lu",
                    iter->first, latest_timeblock_height_);
            }

            return nullptr;
        }

        if (prev_timeblock_tm_sec_ + (common::kRotationPeriod / (1000lu * 1000lu)) > (now_tm / 1000000lu)) {
            static uint64_t prev_get_tx_tm1 = common::TimeUtils::TimestampMs();
            if (now_tx_tm > prev_get_tx_tm1 + 10000) {
                ZJC_DEBUG("failed get statistic tx: %lu, %lu, %lu", 
                    prev_timeblock_tm_sec_, 
                    (common::kRotationPeriod / 1000000lu), 
                    (now_tm / 1000000lu));
                prev_get_tx_tm1 = now_tx_tm;
            }
            
            return nullptr;
        }

        if (leader && shard_statistic_tx->tx_ptr->time_valid > now_tm) {
            ZJC_DEBUG("time_valid invalid!");
            return nullptr;
        }

        shard_statistic_tx->tx_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto& tx = shard_statistic_tx->tx_ptr->tx_info;
        tx.set_to(shard_statistic_tx->tx_ptr->address_info->addr());
        ZJC_DEBUG("success get statistic tx hash: %s, prev_timeblock_tm_sec_: %lu, "
            "height: %lu, latest time block height: %lu, is leader: %d",
            common::Encode::HexEncode(shard_statistic_tx->tx_hash).c_str(),
            prev_timeblock_tm_sec_, iter->first, latest_timeblock_height_,
            leader);
        return shard_statistic_tx->tx_ptr;
    }

    if (leader) {
        ZJC_DEBUG("failed get statistic tx");
    }
    return nullptr;
}

pools::TxItemPtr BlockManager::GetElectTx(uint32_t pool_index, const std::string& tx_hash) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        if (shard_elect_tx_[i] == nullptr) {
            ZJC_DEBUG("0 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i];
        if (!tx_hash.empty()) {
            if (shard_elect_tx->tx_ptr->unique_tx_hash == tx_hash) {
                ZJC_DEBUG("0 success get elect tx pool index: %u, tx hash: %s",
                    pool_index, common::Encode::HexEncode(tx_hash).c_str());
                return shard_elect_tx->tx_ptr;
            }

            ZJC_DEBUG("1 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        auto now_tm = common::TimeUtils::TimestampUs();
        if (shard_elect_tx->tx_ptr->time_valid > now_tm) {
            ZJC_DEBUG("2 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        ZJC_DEBUG("1 success get elect tx pool index: %u, tx hash: %s",
            pool_index, common::Encode::HexEncode(tx_hash).c_str());
        return shard_elect_tx->tx_ptr;
    }

    return nullptr;
}

bool BlockManager::ShouldStopConsensus() {
    return false;
}

void BlockManager::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    ZJC_DEBUG("new timeblock coming: %lu, %lu, lastest_time_block_tm: %lu",
        latest_timeblock_height_, latest_time_block_height, lastest_time_block_tm);
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    prev_timeblock_height_ = latest_timeblock_height_;
    latest_timeblock_height_ = latest_time_block_height;
    prev_timeblock_tm_sec_ = latest_timeblock_tm_sec_;
    latest_timeblock_tm_sec_ = lastest_time_block_tm;
    ZJC_INFO("success update timeblock height: %lu, %lu, tm: %lu, %lu",
        prev_timeblock_height_, latest_timeblock_height_,
        prev_timeblock_tm_sec_, latest_timeblock_tm_sec_);
}

}  // namespace block

}  // namespace shardora
