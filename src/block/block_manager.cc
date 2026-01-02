#include "block/block_manager.h"

#include "block/block_utils.h"
#include "block/account_manager.h"
#include "block/block_proto.h"
#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "consensus/hotstuff/hotstuff_manager.h"
#include "db/db.h"
#include "db/db_utils.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"
#include "protos/pools.pb.h"
#include "protos/tx_storage_key.h"
#include "transport/processor.h"

#include "zjcvm/execution.h"
#include <common/log.h>
#include <common/utils.h>
#include <protos/pools.pb.h>
#include <protos/tx_storage_key.h>
#include "db/db_utils.h"

namespace shardora {

namespace block {

static const std::string kShardElectPrefix = common::Encode::HexDecode(
    "227a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe2");
static const std::string kPoolStatisticTagPrefix = common::Encode::HexDecode(
    "7d501a4dda1b70eced7336fe49d6c1dbdf3dd2b8274981314cc959fe14552023");

BlockManager::BlockManager(
        transport::MultiThreadHandler& net_handler, 
        std::shared_ptr<ck::ClickHouseClient> ck_client) : net_handler_(net_handler), ck_client_(ck_client) {
}

BlockManager::~BlockManager() {
    if (handle_consensus_block_thread_) {
        handle_consensus_block_thread_->join();
    }

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
        std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr,
        const std::string& local_id,
        DbBlockCallback new_block_callback) {
    account_mgr_ = account_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    new_block_callback_ = new_block_callback;
    statistic_mgr_ = statistic_mgr;
    security_ = security;
    contract_mgr_ = contract_mgr;
    hotstuff_mgr_ = hotstuff_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    to_txs_pool_ = std::make_shared<pools::ToTxsPools>(
        db_, local_id, max_consensus_sharding_id_, pools_mgr_, account_mgr_);
    consensus_block_queues_ = new common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlockInfo>>[common::kMaxThreadCount];

    bool genesis = false;
    pop_tx_tick_.CutOff(200000lu, std::bind(&BlockManager::PopTxTicker, this));
    leader_prev_get_to_tx_tm_ = common::TimeUtils::TimestampMs();
    handle_consensus_block_thread_ = std::make_shared<std::thread>(
        std::bind(&BlockManager::HandleAllConsensusBlocks, this));
    return kBlockSuccess;
}

int BlockManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void BlockManager::CallNewElectBlock(uint32_t sharding_id) {
    if (sharding_id > max_consensus_sharding_id_) {
        max_consensus_sharding_id_ = sharding_id;
    }
}

void BlockManager::ConsensusAddBlock(
        const std::shared_ptr<hotstuff::ViewBlockInfo>& block_item_info) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    auto block_item = block_item_info->view_block;
    consensus_block_queues_[thread_idx].push(block_item_info);
    SHARDORA_DEBUG("add new block thread: %d, size: %u, %u_%u_%lu", 
        thread_idx, consensus_block_queues_[thread_idx].size(),
        block_item->qc().network_id(),
        block_item->qc().pool_index(),
        block_item->qc().view());
}

void BlockManager::HandleAllConsensusBlocks() {
    common::GlobalInfo::Instance()->get_thread_index();
    while (!common::GlobalInfo::Instance()->global_stoped()) {
        auto now_tm = common::TimeUtils::TimestampUs();
        bool no_sleep = true;
        while (no_sleep) {
            no_sleep = false;
            for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
                int32_t count = 0;
                while (count++ < kEachTimeHandleBlocksCount) {
                    std::shared_ptr<hotstuff::ViewBlockInfo> view_block_info_ptr = nullptr;
                    consensus_block_queues_[i].pop(&view_block_info_ptr);
                    if (view_block_info_ptr == nullptr) {
                        break;
                    }
    
                    auto view_block_ptr = view_block_info_ptr->view_block;
                    auto* block_ptr = &view_block_ptr->block_info();
                    SHARDORA_DEBUG("from consensus new block coming sharding id: %u, pool: %d, height: %lu, "
                        "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
                        view_block_ptr->qc().network_id(),
                        view_block_ptr->qc().pool_index(),
                        block_ptr->height(),
                        block_ptr->tx_list_size(),
                        common::Encode::HexEncode(view_block_ptr->qc().view_block_hash()).c_str(),
                        view_block_ptr->qc().elect_height(),
                        block_ptr->timeblock_height());
                    auto btime = common::TimeUtils::TimestampMs();
                    AddNewBlock(view_block_info_ptr);
                    auto use_time = (common::TimeUtils::TimestampMs() - btime);
                    if (use_time >= 200)
                    SHARDORA_DEBUG(" %u, pool: %d, height: %lu, "
                        "tx size: %u, hash: %s, elect height: %lu, tm height: %lu, use time: %lu, size: %u",
                        view_block_ptr->qc().network_id(),
                        view_block_ptr->qc().pool_index(),
                        block_ptr->height(),
                        block_ptr->tx_list_size(),
                        common::Encode::HexEncode(view_block_ptr->qc().view_block_hash()).c_str(),
                        view_block_ptr->qc().elect_height(),
                        block_ptr->timeblock_height(),
                        use_time,
                        0);
                }

                if (count >= kEachTimeHandleBlocksCount) {
                    no_sleep = true;
                    SHARDORA_DEBUG("pool index: %d, has block over from consensus new block coming sharding id:count: %u", i, consensus_block_queues_[i].size());
                }
            }

            // auto btime = common::TimeUtils::TimestampMs();
            // auto st = db_->Put(db_batch);
            // if (!st.ok()) {
            //     SHARDORA_FATAL("write to db faield!");
            // }

            // SHARDORA_DEBUG("write to db use time: %lu, size: %u", 
            //     (common::TimeUtils::TimestampMs() - btime), 
            //     db_batch.ApproximateSize());
        }
        
        if (prev_create_statistic_tx_tm_us_ < now_tm) {
            prev_create_statistic_tx_tm_us_ = now_tm + 10000000lu;
            CreateStatisticTx();
        }

        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

void BlockManager::HandleStatisticTx(const view_block::protobuf::ViewBlockItem& view_block) {
    auto& block = view_block.block_info();
    uint32_t net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    auto& elect_statistic = view_block.block_info().elect_statistic();
    if (elect_statistic.sharding_id() == net_id) {
        auto iter = shard_statistics_map_.find(elect_statistic.height_info().tm_height());
        if (iter != shard_statistics_map_.end()) {
            SHARDORA_DEBUG("success remove shard statistic block tm height: %lu", iter->first);
            shard_statistics_map_.erase(iter);
            CHECK_MEMORY_SIZE(shard_statistics_map_);
            auto tmp_ptr = std::make_shared<StatisticMap>(shard_statistics_map_);
            shard_statistics_map_ptr_queue_.push(tmp_ptr);
        }
    }

    HandleStatisticBlock(view_block, elect_statistic);
}

void BlockManager::ConsensusShardHandleRootCreateAddress(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    if (network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
        return;
    }

    // for (int32_t i = 0; i < tx.storages_size(); ++i) {
    //     SHARDORA_DEBUG("get normal to tx key: %s", tx.storages(i).key().c_str());
    //     uint32_t* des_sharding_and_pool = (uint32_t*)(tx.storages(i).value().c_str());
    //     if (des_sharding_and_pool[0] != common::GlobalInfo::Instance()->network_id()) {
    //         return;
    //     }

    //     if (des_sharding_and_pool[1] >= common::kInvalidPoolIndex) {
    //         return;
    //     }
        
    //     pools::protobuf::ToTxMessage to_txs;
    //     auto* tos = to_txs.add_tos();
    //     tos->set_amount(tx.amount());
    //     tos->set_des(tx.to());
    //     tos->set_sharding_id(des_sharding_and_pool[0]);
    //     tos->set_pool_index(des_sharding_and_pool[1]);
    //     tos->set_library_bytes(tx.contract_code());
    //     to_txs.mutable_to_heights()->set_sharding_id(des_sharding_and_pool[0]);
    //     SHARDORA_DEBUG("address: %s, amount: %lu, success handle root create address: %u, "
    //         "local net: %u, step: %d, %u_%u_%lu, block height: %lu",
    //         common::Encode::HexEncode(tx.to()).c_str(),
    //         tx.amount(),
    //         to_txs.to_heights().sharding_id(),
    //         common::GlobalInfo::Instance()->network_id(),
    //         tx.step(),
    //         view_block.qc().network_id(),
    //         view_block.qc().pool_index(),
    //         view_block.qc().view(),
    //         view_block.block_info().height());
    //     HandleNormalToTx(view_block, to_txs, tx);
    // }
}

void BlockManager::HandleNormalToTx(const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
    auto& view_block = *view_block_ptr;
    if (view_block.block_info().normal_to().to_tx_arr_size() <= 0) {
        SHARDORA_DEBUG("0 handle normale to message coming: %u_%u_%lu, des: %u, %s", 
            view_block.qc().network_id(), 
            view_block.qc().pool_index(), 
            view_block.qc().view(), 
            0, 
            ProtobufToJson(view_block).c_str());
        return;
    }

    SHARDORA_DEBUG("handle normale to message coming: %u_%u_%lu, des: %u, %s", 
        view_block.qc().network_id(), 
        view_block.qc().pool_index(), 
        view_block.qc().view(), 
        view_block.block_info().normal_to().to_tx_arr(0).des_shard(), 
        ProtobufToJson(view_block).c_str());
    if (network::IsSameToLocalShard(view_block.qc().network_id())) {
        auto tmp_latest_to_block_ptr_index = (latest_to_block_ptr_index_ + 1) % 2;
        latest_to_block_ptr_[tmp_latest_to_block_ptr_index].store(view_block_ptr);
        latest_to_block_ptr_index_ = tmp_latest_to_block_ptr_index;
        SHARDORA_DEBUG("success set latest to block ptr: %lu, tm: %lu", 
            view_block.block_info().height(), view_block.block_info().timestamp());
    }

    if (!view_block.block_info().has_normal_to()) {
        assert(false);
        return;
    }

    auto& to_txs = view_block.block_info().normal_to();
    for (uint32_t i = 0; i < to_txs.to_tx_arr_size(); ++i) {
        if (to_txs.to_tx_arr(i).des_shard() != common::GlobalInfo::Instance()->network_id()) {
            SHARDORA_WARN("sharding invalid: %u, %u",
                to_txs.to_heights().sharding_id(),
                common::GlobalInfo::Instance()->network_id());
            continue;
        }

        if (!network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
            HandleNormalToTx(view_block, to_txs.to_tx_arr(i));
        } else {
            RootHandleNormalToTx(view_block, to_txs.to_tx_arr(i));
        }
    }
}

void BlockManager::RootHandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const pools::protobuf::ToTxMessage& to_txs) {
    auto& block = view_block.block_info();
    // 将 NormalTo 中的多个 tx 拆分成多个 kRootCreateAddress tx
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto tos_item = to_txs.tos(i);
        SHARDORA_DEBUG("to tx new address %s, amount: %lu, prepayment: %lu, nonce: %lu",
            common::Encode::HexEncode(tos_item.des()).c_str(),
            tos_item.amount(),
            tos_item.prepayment(),
            0);

        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_step(pools::protobuf::kRootCreateAddress);
        // if (tos_item.sharding_id() >= network::kConsensusShardBeginNetworkId &&
        //         tos_item.sharding_id() < network::kConsensusShardEndNetworkId) {
        //     char data[4];
        //     uint32_t* uint_data = (uint32_t*)data;
        //     uint_data[0] = tos_item.sharding_id();
        // }
        
        auto pool_index = common::GetAddressPoolIndex(tos_item.des().substr(0, common::kUnicastAddressLength));
        msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_nonce(++step_with_nonce_[tx->step()]);
        tx->set_value(tos_item.SerializeAsString());
        auto unique_hash = common::Hash::keccak256(
            tx->to() + "_" +
            std::to_string(block.height()) + "_" +
            std::to_string(i));
        tx->set_key(unique_hash);
        SHARDORA_DEBUG("create new address %s, "
            "nonce: %lu, unique hash: %s, contract code: %s",
            ProtobufToJson(tos_item).c_str(),
            0,
            common::Encode::HexEncode(unique_hash).c_str(),
            common::Encode::HexEncode(tx->contract_code()).c_str());
        pools_mgr_->HandleMessage(msg_ptr);
    }
}

void BlockManager::HandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const pools::protobuf::ToTxMessage& to_txs) {
    std::unordered_map<std::string, std::shared_ptr<localToTxInfo>> addr_amount_map;
    SHARDORA_DEBUG("0 handle local to to_txs.tos_size(): %u, addr: %s, nonce: %lu, step: %d", 
        to_txs.tos_size(),
        "",
        0,
        0);
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto to_tx = to_txs.tos(i);
        if (to_tx.des_sharding_id() != common::GlobalInfo::Instance()->network_id()) {
            continue;
        }

        CreateLocalToTx(view_block, to_tx);
    }
}

void BlockManager::AddNewBlock(
        const std::shared_ptr<hotstuff::ViewBlockInfo>& view_block_info) {
    auto view_block_item = view_block_info->view_block;
    assert(!view_block_item->qc().sign_x().empty());
    auto* block_item = &view_block_item->block_info();
    auto btime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
        "tx size: %u, hash: %s, prehash: %s, elect height: %lu, tm height: %lu, %s, ck_client_: %d",
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        block_item->height(),
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block_item->parent_hash()).c_str(),
        view_block_item->qc().elect_height(),
        block_item->timeblock_height(),
        ProtobufToJson(*view_block_item).c_str(),
        (ck_client_ != nullptr));
    assert(view_block_item->qc().elect_height() >= 1);
    account_mgr_->AddNewBlock(*view_block_item);
    // 当前节点和 block 分配的 shard 不同，要跨分片交易
    if (!network::IsSameToLocalShard(view_block_item->qc().network_id())) {
        pools_mgr_->OnNewCrossBlock(view_block_item);
        SHARDORA_DEBUG("new cross block coming: %u, %u, %lu",
            view_block_item->qc().network_id(),
            view_block_item->qc().pool_index(), block_item->height());
    } else {
        if (statistic_mgr_) {
            statistic_mgr_->OnNewBlock(view_block_item);
        }

        to_txs_pool_->NewBlock(view_block_item);
    }

    if (block_item->has_elect_statistic()) {
       HandleStatisticTx(*view_block_item);
    }

    if (block_item->has_elect_block()) {
        SHARDORA_DEBUG("now call new elect block: %s", ProtobufToJson(*block_item).c_str());
        HandleElectTx(*view_block_item);
        CallNewElectBlock(block_item->elect_block().shard_network_id());
        if (statistic_mgr_) {
            statistic_mgr_->CallNewElectBlock(
                block_item->elect_block().shard_network_id(),
                block_item->height());
        }
    }

    if (block_item->has_normal_to()) {
        HandleNormalToTx(view_block_item);
    }

    if (block_item->has_timer_block()) {
        auto vss_random = block_item->timer_block().vss_random();
        CallTimeBlock(block_item->timer_block().timestamp(), block_item->height(), vss_random);
        SHARDORA_INFO("new time block called height: %lu, tm: %lu", block_item->height(), vss_random);
    }

    if (block_item->cross_shard_to_array_size() > 0) {
        SHARDORA_DEBUG("now handle root cross %u_%u_%lu, local net: %d,  block: %s",
            view_block_item->qc().network_id(),
            view_block_item->qc().pool_index(),
            view_block_item->qc().view(),
            common::GlobalInfo::Instance()->network_id(),
            ProtobufToJson(*block_item).c_str());
        if (view_block_item->qc().network_id() == network::kRootCongressNetworkId && 
                !network::IsSameShardOrSameWaitingPool(
                common::GlobalInfo::Instance()->network_id(), 
                network::kRootCongressNetworkId)) {
            HandleRootCrossShardTx(*view_block_item);
        }
    }

    if (ck_client_) {
        ck_client_->AddNewBlock(view_block_item);
    }
}

void BlockManager::HandleRootCrossShardTx(const view_block::protobuf::ViewBlockItem& view_block) {
    auto& block_item = view_block.block_info();
    std::unordered_map<std::string, std::shared_ptr<localToTxInfo>> addr_amount_map;
    for (int32_t i = 0; i < block_item.cross_shard_to_array_size(); ++i) {
        // dispatch to txs to tx pool
        auto to_tx = block_item.cross_shard_to_array(i);
        if (to_tx.des_sharding_id() != common::GlobalInfo::Instance()->network_id()) {
            continue;
        }

        CreateLocalToTx(view_block, to_tx);
    }
}

void BlockManager::CreateLocalToTx(
        const view_block::protobuf::ViewBlockItem& view_block, 
        const pools::protobuf::ToTxMessageItem& to_tx_item) {
    if (to_tx_item.des().size() != common::kUnicastAddressLength && 
            to_tx_item.des().size() != common::kPreypamentAddressLength) {
        SHARDORA_ERROR("invalid to tx item: %s", ProtobufToJson(to_tx_item).c_str());
        assert(false);
        return;
    }

    auto addr = to_tx_item.des().substr(0, common::kUnicastAddressLength);
    uint32_t pool_index = common::kInvalidPoolIndex;
    auto addr_info = prefix_db_->GetAddressInfo(addr);
    if (addr_info) {
        pool_index = addr_info->pool_index();
    } else {
        pool_index = common::GetAddressPoolIndex(addr);
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
    auto tx = msg_ptr->header.mutable_tx_proto();
    std::string uinique_tx_str = common::Hash::keccak256(
        view_block.qc().view_block_hash() +
        view_block.qc().sign_x() + 
        view_block.qc().sign_y() +
        to_tx_item.des());
    tx->set_key(uinique_tx_str);
    tx->set_value(to_tx_item.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to(msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kConsensusLocalTos);
    tx->set_gas_limit(0);
    tx->set_amount(0); // 具体 amount 在 kv 中
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_nonce(++step_with_nonce_[tx->step()]);
    pools_mgr_->HandleMessage(msg_ptr);
    SHARDORA_DEBUG("success add local transfer tx tos hash: %s, nonce: %lu, src to tx nonce: %lu, val: %s",
        common::Encode::HexEncode(uinique_tx_str).c_str(),
        msg_ptr->address_info->nonce(),
        0,
        ProtobufToJson(to_tx_item).c_str());
}

void BlockManager::HandleElectTx(const view_block::protobuf::ViewBlockItem& view_block) {
    auto& block = view_block.block_info();
    auto& elect_block = block.elect_block();
    AddMiningToken(view_block, elect_block);
    SHARDORA_DEBUG("success add elect block elect height: %lu, net: %u, "
        "pool: %u, height: %lu, common pk: %s, prev elect height: %lu", 
        view_block.qc().elect_height(),
        view_block.qc().network_id(),
        view_block.qc().pool_index(),
        block.height(),
        common::Encode::HexEncode(
        elect_block.prev_members().common_pubkey().SerializeAsString()).c_str(),
        elect_block.prev_members().prev_elect_height());
}

void BlockManager::AddMiningToken(
        const view_block::protobuf::ViewBlockItem& view_block,
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
        protos::AddressInfoPtr account_info = account_mgr_->GetAccountInfo(id);
        if (account_info == nullptr ||
                account_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
            continue;
        }

        auto pool_index = common::GetAddressPoolIndex(id);
        // auto to_iter = to_tx_map.find(pool_index);
        // if (to_iter == to_tx_map.end()) {
        //     pools::protobuf::ToTxMessage to_tx;
        //     to_tx_map[pool_index] = to_tx;
        //     to_iter = to_tx_map.find(pool_index);
        // }

        pools::protobuf::ToTxMessageItem to_item;
        to_item.set_pool_index(pool_index);
        to_item.set_des(id);
        to_item.set_amount(elect_block.in(i).mining_amount());
        SHARDORA_DEBUG("mining success add local transfer to %s, %lu",
            common::Encode::HexEncode(id).c_str(), elect_block.in(i).mining_amount());
        CreateLocalToTx(view_block, to_item);
    }

    // for (auto iter = to_tx_map.begin(); iter != to_tx_map.end(); ++iter) {
    //     std::string str_for_hash;
    //     str_for_hash.reserve(iter->second.tos_size() * 48);
    //     for (int32_t i = 0; i < iter->second.tos_size(); ++i) {
    //         str_for_hash.append(iter->second.tos(i).des());
    //         uint32_t pool_idx = iter->second.tos(i).pool_index();
    //         str_for_hash.append((char*)&pool_idx, sizeof(pool_idx));
    //         uint64_t amount = iter->second.tos(i).amount();
    //         str_for_hash.append((char*)&amount, sizeof(amount));
    //     }

    //     auto val = iter->second.SerializeAsString();
    //     auto tos_hash = common::Hash::keccak256(str_for_hash);
    //     prefix_db_->SaveTemporaryKv(tos_hash, val);
    //     auto msg_ptr = std::make_shared<transport::TransportMessage>();
    //     msg_ptr->address_info = account_mgr_->pools_address_info(iter->first);
    //     auto tx = msg_ptr->header.mutable_tx_proto();
    //     std::string uinique_tx_str = common::Hash::keccak256(
    //         view_block.qc().view_block_hash() +
    //         view_block.qc().sign_x() + 
    //         view_block.qc().sign_y() +
    //         msg_ptr->address_info->addr());
    //     tx->set_key(uinique_tx_str);
    //     tx->set_value(val);
    //     tx->set_pubkey("");
    //     tx->set_to(msg_ptr->address_info->addr());
    //     tx->set_step(pools::protobuf::kConsensusLocalTos);
    //     tx->set_gas_limit(0);
    //     tx->set_amount(0);
    //     tx->set_gas_price(common::kBuildinTransactionGasPrice);
    //     tx->set_nonce(0);
    //     pools_mgr_->HandleMessage(msg_ptr);
    //     SHARDORA_DEBUG("mining success create kConsensusLocalTos %s nonce: %lu",
    //         common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
    //         tx->nonce());
    // }
}

void BlockManager::LoadLatestBlocks() {
    SHARDORA_DEBUG("load latest block called!");
    timeblock::protobuf::TimeBlock tmblock;
    db::DbWriteBatch db_batch;
    if (prefix_db_->GetLatestTimeBlock(&tmblock)) {
        SHARDORA_DEBUG("load latest time block called!");
        auto tmblock_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto& block = *tmblock_ptr;
        if (GetBlockWithHeight(
                network::kRootCongressNetworkId,
                common::kImmutablePoolSize,
                tmblock.height(),
                block) == kBlockSuccess) {
            SHARDORA_DEBUG("load latest time block called!");
            if (new_block_callback_ != nullptr) {
                new_block_callback_(tmblock_ptr);
            }

            CallTimeBlock(tmblock.timestamp(), tmblock.height(), tmblock.vss_random());
        } else {
            SHARDORA_FATAL("load latest timeblock failed!");
        }
    }

    for (uint32_t load_idx = 0; load_idx < 2; ++load_idx) {
        for (uint32_t i = network::kRootCongressNetworkId;
                i < network::kConsensusShardEndNetworkId; ++i) {
            elect::protobuf::ElectBlock elect_block;
            if (load_idx == 0) {
                if (!prefix_db_->GetLatestElectBlock(i, &elect_block)) {
                    SHARDORA_ERROR("get elect latest block failed: %u", i);
                    break;
                }
            } else {
                if (!prefix_db_->GetHavePrevlatestElectBlock(i, &elect_block)) {
                    SHARDORA_ERROR("get elect latest block failed: %u", i);
                    break;
                }
            }

            auto elect_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
            auto& block = *elect_block_ptr;
            if (GetBlockWithHeight(
                    network::kRootCongressNetworkId,
                    elect_block.shard_network_id() % common::kImmutablePoolSize,
                    elect_block.elect_height(),
                    block) == kBlockSuccess) {
                if (new_block_callback_ != nullptr) {
                    new_block_callback_(elect_block_ptr);
                }

                CallNewElectBlock(block.block_info().elect_block().shard_network_id());
                if (statistic_mgr_) {
                    statistic_mgr_->CallNewElectBlock(
                        block.block_info().elect_block().shard_network_id(),
                        block.block_info().height());
                }

                AddMiningToken(block, elect_block);
                SHARDORA_DEBUG("get block with height success: %u, %u, %lu",
                    network::kRootCongressNetworkId,
                    elect_block.shard_network_id() % common::kImmutablePoolSize,
                    elect_block.elect_height());
            } else {
                SHARDORA_FATAL("get block with height failed: %u, %u, %lu",
                    network::kRootCongressNetworkId,
                    elect_block.shard_network_id() % common::kImmutablePoolSize,
                    elect_block.elect_height());
            }
        }
    }
    
    auto latest_to_tx_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
    auto& block = *latest_to_tx_block;
    if (prefix_db_->GetLatestToBlock(&block)) {
        auto tmp_latest_to_block_ptr_index = (latest_to_block_ptr_index_ + 1) % 2;
        latest_to_block_ptr_[tmp_latest_to_block_ptr_index].store(
            latest_to_tx_block);
        latest_to_block_ptr_index_ = tmp_latest_to_block_ptr_index;
        SHARDORA_DEBUG("success set latest to block ptr: %lu, tm: %lu",
            latest_to_tx_block->block_info().height(), latest_to_tx_block->block_info().timestamp());
    }

    db_->Put(db_batch);
}

int BlockManager::GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        view_block::protobuf::ViewBlockItem& block_item) {
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
        SHARDORA_DEBUG("create_statistic_tx_cb_ == nullptr");
        return;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    uint64_t timeblock_height = prev_timeblock_height_;
    SHARDORA_DEBUG("StatisticWithHeights called!");

    // Some nodes will receive statistic block ahead of timeblock.
    // This happens accasionally, making statistic tx failed to be found
    // So we should make sure that one timeblock can only gathered statistic info for once
    if (IsTimeblockHeightStatisticDone(timeblock_height)) {
        SHARDORA_DEBUG("repeat StatisticWithHeights, %lu, latest: %lu",
            timeblock_height, latest_statistic_timeblock_height_);
        return;
    }
    
    if (statistic_mgr_->StatisticWithHeights(
            elect_statistic,
            timeblock_height) != pools::kPoolsSuccess) {
        SHARDORA_DEBUG("failed StatisticWithHeights!");
        return;
    }

    // 对应 timeblock_height 的 elect_statistic 已经收集，不会进行重复收集
    MarkDoneTimeblockHeightStatistic(timeblock_height);
    auto unique_hash = common::Hash::keccak256(
        std::string("create_statistic_tx_") + 
        std::to_string(elect_statistic.sharding_id()) + "_" +
        std::to_string(elect_statistic.statistic_height()));
    SHARDORA_DEBUG("success create statistic message hash: %s, timeblock_height: %lu, statistic: %s", 
        common::Encode::HexEncode(unique_hash).c_str(), 
        timeblock_height, ProtobufToJson(elect_statistic).c_str());
    if (!unique_hash.empty()) {
        auto tm_statistic_iter = shard_statistics_map_.find(timeblock_height);
        if (tm_statistic_iter == shard_statistics_map_.end()) {
            auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
            auto* tx = new_msg_ptr->header.mutable_tx_proto();
            tx->set_key(unique_hash);
            tx->set_value(elect_statistic.SerializeAsString());
            tx->set_pubkey("");
            tx->set_step(pools::protobuf::kStatistic);
            tx->set_gas_limit(0);
            tx->set_amount(0);
            tx->set_gas_price(common::kBuildinTransactionGasPrice);
            new_msg_ptr->address_info = account_mgr_->pools_address_info(common::kImmutablePoolSize);
            tx->set_nonce(new_msg_ptr->address_info->nonce() + 1);
            auto tx_ptr = std::make_shared<BlockTxsItem>();
            tx_ptr->tx_ptr = create_statistic_tx_cb_(new_msg_ptr);
            tx_ptr->tx_ptr->time_valid += kStatisticValidTimeout;
            tx_ptr->tx_hash = unique_hash;
            tx_ptr->timeout = common::TimeUtils::TimestampMs() + kStatisticTimeoutMs;
            tx_ptr->stop_consensus_timeout = tx_ptr->timeout + kStopConsensusTimeoutMs;
            SHARDORA_DEBUG("success add statistic tx: %s, statistic elect height: %lu, "
                "heights: %s, timeout: %lu, kStatisticTimeoutMs: %lu, now: %lu, "
                "nonce: %lu, timeblock_height: %lu",
                common::Encode::HexEncode(unique_hash).c_str(),
                0,
                "", tx_ptr->timeout,
                0, common::TimeUtils::TimestampMs(),
                tx->nonce(),
                timeblock_height);
            shard_statistics_map_[timeblock_height] = tx_ptr;
            CHECK_MEMORY_SIZE(shard_statistics_map_);

            auto tmp_ptr = std::make_shared<StatisticMap>(shard_statistics_map_);
            shard_statistics_map_ptr_queue_.push(tmp_ptr);
        }
    }
}

// Only for root
void BlockManager::HandleStatisticBlock(
        const view_block::protobuf::ViewBlockItem& view_block,
        const pools::protobuf::ElectStatistic& elect_statistic) {
    auto& block = view_block.block_info();
    if (create_elect_tx_cb_ == nullptr) {
        SHARDORA_DEBUG("create_elect_tx_cb_ == nullptr");
        return;
    }

    if (elect_statistic.statistics_size() <= 0) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return;
    }
#ifdef NDEBUG
    for (int32_t i = 0; i < elect_statistic.join_elect_nodes_size(); ++i) {
        SHARDORA_DEBUG("sharding: %u, new elect node: %s, balance: %lu, shard: %u, pos: %u", 
            elect_statistic.sharding_id(), 
            common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
            elect_statistic.join_elect_nodes(i).stoke(),
            elect_statistic.join_elect_nodes(i).shard(),
            elect_statistic.join_elect_nodes(i).elect_pos());
    }

    assert(view_block.qc().network_id() == elect_statistic.sharding_id());
    SHARDORA_DEBUG("success handle statistic block net: %u, sharding: %u, "
        "pool: %u, height: %lu, elect height: %lu",
        view_block.qc().network_id(), elect_statistic.sharding_id(), view_block.qc().pool_index(), 
        block.timeblock_height(), elect_statistic.statistics(elect_statistic.statistics_size() - 1).elect_height());
#endif
    // create elect transaction now for block.network_id
    auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
    new_msg_ptr->address_info = account_mgr_->pools_address_info(elect_statistic.sharding_id());
    auto* tx = new_msg_ptr->header.mutable_tx_proto();
    std::string unique_hash = common::Hash::keccak256(
        std::string("root_create_elect_tx_") + 
        std::to_string(elect_statistic.sharding_id()) + "_" +
        std::to_string(block.timeblock_height()));
    tx->set_key(unique_hash);
    tx->set_value(elect_statistic.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to(new_msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kConsensusRootElectShard);
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_nonce(++step_with_nonce_[tx->step()]);
    auto shard_elect_tx = std::make_shared<BlockTxsItem>();
    shard_elect_tx->tx_ptr = create_elect_tx_cb_(new_msg_ptr);
    shard_elect_tx->tx_ptr->time_valid += kElectValidTimeout;
    shard_elect_tx->timeout = common::TimeUtils::TimestampMs() + kElectTimeout;
    shard_elect_tx->stop_consensus_timeout = shard_elect_tx->timeout + kStopConsensusTimeoutMs;
    shard_elect_tx_[view_block.qc().network_id()].store(shard_elect_tx);
    SHARDORA_DEBUG("success add elect tx: %u, %lu, nonce: %lu, tx key: %s, "
        "statistic elect height: %lu, unique hash: %s",
        view_block.qc().network_id(), block.timeblock_height(),
        tx->nonce(),
        common::Encode::HexEncode(shard_elect_tx->tx_ptr->tx_key).c_str(),
        0,
        common::Encode::HexEncode(unique_hash).c_str());
}

pools::TxItemPtr BlockManager::GetToTx(
        uint32_t pool_index, 
        const std::string& heights_str) {
    if (network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
        return nullptr;
    }

    if (pool_index != common::kImmutablePoolSize) {
        return nullptr;
    }

    pools::protobuf::ShardToTxItem heights;
    if (heights_str.empty()) {
        auto cur_time = common::TimeUtils::TimestampMs();
        if (leader_prev_get_to_tx_tm_ > cur_time) {
            return nullptr;
        }

        SHARDORA_DEBUG("now leader get to to tx.");
        leader_prev_get_to_tx_tm_ = cur_time + 3000lu;
        auto latest_to_block_ptr = latest_to_block_ptr_[latest_to_block_ptr_index_].load();
        if (latest_to_block_ptr != nullptr &&
                latest_to_block_ptr->block_info().timestamp() + 10000lu >= cur_time) {
            SHARDORA_DEBUG("now leader get to to tx timestamp error");
            return nullptr;
        }

        if (to_txs_pool_->LeaderCreateToHeights(heights) != pools::kPoolsSuccess) {
            SHARDORA_DEBUG("now leader get to to tx leader get error");
            return nullptr;
        }
    } else {
        if (!heights.ParseFromString(heights_str)) {
            assert(false);
            return nullptr;
        }
    }

    heights.set_sharding_id(network::GetLocalConsensusNetworkId());
    auto tx_ptr = HandleToTxsMessage(heights);
    if (tx_ptr != nullptr) {
        // heights_str_map_[height_hash] = tx_ptr;
        // CHECK_MEMORY_SIZE(heights_str_map_);
        SHARDORA_DEBUG("success get to tx tx info: %s, nonce: %lu, val: %s, heights: %s",
            ProtobufToJson(*tx_ptr->tx_info).c_str(),
            tx_ptr->tx_info->nonce(), 
            "common::Encode::HexEncode(tx_ptr->tx_info.value()).c_str()",
            ProtobufToJson(heights).c_str());
    } else {
        SHARDORA_DEBUG("failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
    }

    return tx_ptr;
}

pools::TxItemPtr BlockManager::HandleToTxsMessage(
        const pools::protobuf::ShardToTxItem& heights) {
    if (create_to_tx_cb_ == nullptr) {
        return nullptr;
    }

    pools::protobuf::AllToTxMessage all_to_txs;
    pools::protobuf::ShardToTxItem prev_heights;
    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id <= max_consensus_sharding_id_; ++sharding_id) {
        auto& to_tx = *all_to_txs.add_to_tx_arr();
        if (to_txs_pool_->CreateToTxWithHeights(
                sharding_id,
                0,
                &prev_heights,
                heights,
                to_tx) != pools::kPoolsSuccess) {
            all_to_txs.mutable_to_tx_arr()->RemoveLast();
            SHARDORA_DEBUG("1 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        }
    }

    if (all_to_txs.to_tx_arr_size() == 0) {
        SHARDORA_DEBUG("2 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        return nullptr;
    }
    
    *all_to_txs.mutable_to_heights() = heights;
    auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
    new_msg_ptr->address_info = account_mgr_->pools_address_info(common::kImmutablePoolSize);
    auto* tx = new_msg_ptr->header.mutable_tx_proto();
    std::string unique_str;
    for (uint32_t i = 0; i < prev_heights.heights_size(); ++i) {
        unique_str += std::to_string(prev_heights.heights(i)) + "_";
    }

    tx->set_key(common::Hash::keccak256(unique_str));
    tx->set_value(all_to_txs.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to(new_msg_ptr->address_info->addr());
    tx->set_step(pools::protobuf::kNormalTo);
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_nonce(++step_with_nonce_[tx->step()]);
    auto tx_ptr = create_to_tx_cb_(new_msg_ptr);
    tx_ptr->time_valid += kToValidTimeout;
    SHARDORA_DEBUG("success get to tx unique hash: %s, heights: %s",
        common::Encode::HexEncode(tx->key()).c_str(), 
        ProtobufToJson(prev_heights).c_str());
    return tx_ptr;
}

bool BlockManager::HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (HasToTx(pool_index, tx_valid_func)) {
        // SHARDORA_DEBUG("success check has to tx.");
        return true;
    }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (HasStatisticTx(pool_index, tx_valid_func)) {
    //     // SHARDORA_DEBUG("success check has statistic tx.");
    //     return true;
    // }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (HasElectTx(pool_index, tx_valid_func)) {
    //     // SHARDORA_DEBUG("success check has elect tx.");
    //     return true;
    // }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    return false;
}

void BlockManager::PopTxTicker() {
    std::shared_ptr<StatisticMap> static_tmp_map = nullptr;
    while (shard_statistics_map_ptr_queue_.pop(&static_tmp_map)) {}
    if (static_tmp_map != nullptr) {
        for (auto iter = static_tmp_map->begin(); iter != static_tmp_map->end(); ++iter) {
            SHARDORA_DEBUG("now pop statistic tx tx nonce: %lu, tm height: %lu",
                iter->second->tx_ptr->tx_info->nonce(), iter->first);
        }

        auto valid_got_latest_statistic_map_ptr_index_tmp = (valid_got_latest_statistic_map_ptr_index_ + 1) % 2;
        got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_tmp].store(
            static_tmp_map);
        valid_got_latest_statistic_map_ptr_index_ = valid_got_latest_statistic_map_ptr_index_tmp;
    }

    pop_tx_tick_.CutOff(50000lu, std::bind(&BlockManager::PopTxTicker, this));
}

bool BlockManager::HasToTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func) {
    if (network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
        return false;
    }
    
    if (pool_index != common::kImmutablePoolSize) {
        return false;
    }

    auto cur_time = common::TimeUtils::TimestampMs();
    auto latest_to_block_ptr = latest_to_block_ptr_[latest_to_block_ptr_index_].load();
    if (latest_to_block_ptr != nullptr &&
            latest_to_block_ptr->block_info().timestamp() + 10000lu >= cur_time) {
        SHARDORA_DEBUG("invalid latest_to_block_ptr: %d", (latest_to_block_ptr != nullptr));
        return false;
    }

    return true;
}

bool BlockManager::HasStatisticTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func) {
    if (pool_index != common::kImmutablePoolSize) {
        return false;
    }

    auto statistic_map_ptr = got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_].load();
    if (statistic_map_ptr == nullptr) {
        return false;
    }

    if (statistic_map_ptr->empty()) {
        return false;
    }

    auto iter = statistic_map_ptr->begin();
    auto shard_statistic_tx = iter->second;
    if (shard_statistic_tx == nullptr) {
        SHARDORA_DEBUG("shard_statistic_tx == nullptr");
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

        if (tx_valid_func(
                *iter->second->tx_ptr->address_info, 
                *iter->second->tx_ptr->tx_info) != 0) {
            return false;
        }

        // SHARDORA_DEBUG("has statistic %u, tx nonce: %lu", 
        //     pool_index, 
        //     common::Encode::HexEncode(iter->second->tx_ptr->tx_info.gid()).c_str());
        return true;
    }

    return false;
}

bool BlockManager::HasElectTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i].load();
        if (shard_elect_tx == nullptr) {
            continue;
        }

        if (tx_valid_func(
                *shard_elect_tx->tx_ptr->address_info, 
                *shard_elect_tx->tx_ptr->tx_info) != 0) {
            return false;
        }
        
        SHARDORA_DEBUG("has elect %u, tx nonce: %lu", 
            pool_index, 
            shard_elect_tx->tx_ptr->tx_info->nonce());
        return true;
    }

    return false;
}

pools::TxItemPtr BlockManager::GetStatisticTx(
        uint32_t pool_index, 
        const std::string& unqiue_hash) {
    bool leader = unqiue_hash.empty();
    while (shard_statistics_map_ptr_queue_.size() > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(50000ull));
    }

    auto statistic_map_ptr = got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_].load(
        std::memory_order_acquire);;
    if (statistic_map_ptr == nullptr) {
        SHARDORA_DEBUG("statistic_map_ptr == nullptr");
        return nullptr;
    }

    if (statistic_map_ptr->empty()) {
        SHARDORA_DEBUG("statistic_map_ptr->empty()");
        return nullptr;
    }

    std::shared_ptr<BlockTxsItem> shard_statistic_tx = nullptr;
    auto iter = statistic_map_ptr->begin();
    for (; iter != statistic_map_ptr->end(); ++iter) {
        if (leader) {
            shard_statistic_tx = iter->second;
            break;
        }

        if (iter->second->tx_ptr->tx_info->key() == unqiue_hash) {
            shard_statistic_tx = iter->second;
            break;
        }
    }

    if (shard_statistic_tx == nullptr) {
        SHARDORA_DEBUG("shard_statistic_tx == nullptr, unqiue_hash: %s, is leader: %d",
            common::Encode::HexEncode(unqiue_hash).c_str(),
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
            SHARDORA_DEBUG("leader get tx failed: %lu, %lu", shard_statistic_tx->tx_ptr->time_valid, now_tm);
            return nullptr;
        }

        if (iter->first >= latest_timeblock_height_) {
            if (leader) {
                SHARDORA_DEBUG("iter->first >= latest_timeblock_height_: %lu, %lu",
                    iter->first, latest_timeblock_height_);
            }

            return nullptr;
        }

        if (prev_timeblock_tm_sec_ + (common::kRotationPeriod / (1000lu * 1000lu)) > (now_tm / 1000000lu)) {
            static uint64_t prev_get_tx_tm1 = common::TimeUtils::TimestampMs();
            if (now_tx_tm > prev_get_tx_tm1 + 10000) {
                SHARDORA_DEBUG("failed get statistic tx: %lu, %lu, %lu", 
                    prev_timeblock_tm_sec_, 
                    (common::kRotationPeriod / 1000000lu), 
                    (now_tm / 1000000lu));
                prev_get_tx_tm1 = now_tx_tm;
            }
            
            return nullptr;
        }

        if (leader && shard_statistic_tx->tx_ptr->time_valid > now_tm) {
            SHARDORA_DEBUG("time_valid invalid!");
            return nullptr;
        }

        shard_statistic_tx->tx_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto& tx = shard_statistic_tx->tx_ptr->tx_info;
        tx->set_to(shard_statistic_tx->tx_ptr->address_info->addr());
        SHARDORA_INFO("success get statistic tx hash: %s, prev_timeblock_tm_sec_: %lu, "
            "height: %lu, latest time block height: %lu, is leader: %d",
            common::Encode::HexEncode(shard_statistic_tx->tx_hash).c_str(),
            prev_timeblock_tm_sec_, iter->first, latest_timeblock_height_,
            leader);
        return shard_statistic_tx->tx_ptr;
    }

    if (leader) {
        SHARDORA_DEBUG("failed get statistic tx");
    }
    return nullptr;
}

pools::TxItemPtr BlockManager::GetElectTx(uint32_t pool_index, const std::string& tx_hash) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i].load();
        if (shard_elect_tx == nullptr) {
            SHARDORA_DEBUG("0 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        if (!tx_hash.empty()) {
            if (shard_elect_tx->tx_ptr->tx_info->key() == tx_hash) {
                SHARDORA_DEBUG("0 success get elect tx pool index: %u, tx hash: %s",
                    pool_index, common::Encode::HexEncode(tx_hash).c_str());
                return shard_elect_tx->tx_ptr;
            }

            SHARDORA_DEBUG("1 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        auto now_tm = common::TimeUtils::TimestampUs();
        if (shard_elect_tx->tx_ptr->time_valid > now_tm) {
            SHARDORA_DEBUG("2 failed get elect tx pool index: %u, tx hash: %s",
                pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        SHARDORA_DEBUG("1 success get elect tx pool index: %u, unique hash: %s",
            pool_index, 
            common::Encode::HexEncode(shard_elect_tx->tx_ptr->tx_info->key()).c_str());
        return shard_elect_tx->tx_ptr;
    }

    return nullptr;
}

bool BlockManager::ShouldStopConsensus() {
    return false;
}

void BlockManager::CallTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    SHARDORA_DEBUG("new timeblock coming: %lu, %lu, lastest_time_block_tm: %lu",
        latest_timeblock_height_, latest_time_block_height, lastest_time_block_tm);
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    prev_timeblock_height_ = latest_timeblock_height_;
    latest_timeblock_height_ = latest_time_block_height;
    prev_timeblock_tm_sec_ = latest_timeblock_tm_sec_;
    latest_timeblock_tm_sec_ = lastest_time_block_tm;
    SHARDORA_DEBUG("success update timeblock height: %lu, %lu, tm: %lu, %lu",
        prev_timeblock_height_, latest_timeblock_height_,
        prev_timeblock_tm_sec_, latest_timeblock_tm_sec_);

    if (statistic_mgr_) {
        statistic_mgr_->CallTimeBlock(lastest_time_block_tm, latest_time_block_height, vss_random);
    }
}

}  // namespace block

}  // namespace shardora
