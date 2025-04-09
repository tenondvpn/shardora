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
    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&BlockManager::ConsensusTimerMessage, this, std::placeholders::_1));
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

void BlockManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // account_mgr_->GetAccountInfo("");
    // auto now_tm_ms = common::TimeUtils::TimestampMs();
    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (prev_timer_ms_ + 100lu > now_tm_ms) {
    //     return;
    // }

    // prev_timer_ms_ = now_tm_ms;
    // auto now_tm = common::TimeUtils::TimestampUs();
    // ZJC_DEBUG("now check CreateStatisticTx %lu, %lu",
    //     prev_create_statistic_tx_tm_us_, now_tm);
    // if (prev_create_statistic_tx_tm_us_ < now_tm) {
    //     prev_create_statistic_tx_tm_us_ = now_tm + 10000000lu;
    //     CreateStatisticTx();
    // }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // HandleAllConsensusBlocks();
    ADD_DEBUG_PROCESS_TIMESTAMP();
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

void BlockManager::ConsensusAddBlock(
        const std::shared_ptr<hotstuff::ViewBlockInfo>& block_item_info) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    auto block_item = block_item_info->view_block;
    consensus_block_queues_[thread_idx].push(block_item_info);
    ZJC_DEBUG("add new block thread: %d, size: %u, %u_%u_%lu", 
        thread_idx, consensus_block_queues_[thread_idx].size(),
        block_item->qc().network_id(),
        block_item->qc().pool_index(),
        block_item->qc().view());
}

void BlockManager::HandleAllConsensusBlocks() {
    common::GlobalInfo::Instance()->get_thread_index();
    while (!common::GlobalInfo::Instance()->global_stoped()) {
        auto now_tm = common::TimeUtils::TimestampUs();
        // ZJC_DEBUG("now check CreateStatisticTx %lu, %lu",
        //     prev_create_statistic_tx_tm_us_, now_tm);
        if (prev_create_statistic_tx_tm_us_ < now_tm) {
            prev_create_statistic_tx_tm_us_ = now_tm + 10000000lu;
            CreateStatisticTx();
        }

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
                    ZJC_DEBUG("from consensus new block coming sharding id: %u, pool: %d, height: %lu, "
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
                    ZJC_INFO(" %u, pool: %d, height: %lu, "
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
                    ZJC_INFO("pool index: %d, has block over from consensus new block coming sharding id:count: %u", i, consensus_block_queues_[i].size());
                }
            }

            // auto btime = common::TimeUtils::TimestampMs();
            // auto st = db_->Put(db_batch);
            // if (!st.ok()) {
            //     ZJC_FATAL("write to db faield!");
            // }

            // ZJC_DEBUG("write to db use time: %lu, size: %u", 
            //     (common::TimeUtils::TimestampMs() - btime), 
            //     db_batch.ApproximateSize());
        }
        
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(10));
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
    account_info->set_nonce(tx.nonce());
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
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& block_tx) {
    auto& block = view_block.block_info();
    uint32_t net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    if (!elect_statistic.ParseFromString(block_tx.storages(0).value())) {
        assert(false);
        return;
    }

    if (elect_statistic.sharding_id() != net_id) {
        ZJC_DEBUG("invalid sharding id %u, %u", elect_statistic.sharding_id(), net_id);
        return;
    }

    auto iter = shard_statistics_map_.find(elect_statistic.height_info().tm_height());
    if (iter != shard_statistics_map_.end()) {
        ZJC_DEBUG("success remove shard statistic block tm height: %lu", iter->first);
        shard_statistics_map_.erase(iter);
        CHECK_MEMORY_SIZE(shard_statistics_map_);
        auto tmp_ptr = std::make_shared<StatisticMap>(shard_statistics_map_);
        shard_statistics_map_ptr_queue_.push(tmp_ptr);
    }

    HandleStatisticBlock(view_block, block_tx, elect_statistic);
}

void BlockManager::ConsensusShardHandleRootCreateAddress(
        const view_block::protobuf::ViewBlockItem& view_block,
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
        tos->set_library_bytes(tx.contract_code());
        to_txs.mutable_to_heights()->set_sharding_id(des_sharding_and_pool[0]);
        ZJC_DEBUG("address: %s, amount: %lu, success handle root create address: %u, "
            "local net: %u, step: %d, %u_%u_%lu, block height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            tx.amount(),
            to_txs.to_heights().sharding_id(),
            common::GlobalInfo::Instance()->network_id(),
            tx.step(),
            view_block.qc().network_id(),
            view_block.qc().pool_index(),
            view_block.qc().view(),
            view_block.block_info().height());
        HandleLocalNormalToTx(view_block, to_txs, tx);
    }
}

void BlockManager::HandleNormalToTx(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr,
        const block::protobuf::BlockTx& tx) {
    auto& view_block = *view_block_ptr;
    if (network::IsSameToLocalShard(view_block_ptr->qc().network_id())) {
        auto tmp_latest_to_block_ptr_index = (latest_to_block_ptr_index_ + 1) % 2;
        latest_to_block_ptr_[tmp_latest_to_block_ptr_index] = view_block_ptr;
        latest_to_block_ptr_index_ = tmp_latest_to_block_ptr_index;
        ZJC_DEBUG("success set latest to block ptr: %lu, tm: %lu", 
            view_block_ptr->block_info().height(), view_block_ptr->block_info().timestamp());
    }

    ZJC_DEBUG("success handle nonce: %lu", tx.nonce());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("get normal to tx key: %s", tx.storages(i).key().c_str());
        if (tx.storages(i).key() != protos::kNormalToShards) {
            ZJC_DEBUG("x.storages(i).key() != protos::kNormalToShards handle nonce: %lu", tx.nonce());
            continue;
        }

        pools::protobuf::ToTxMessage to_txs;
        if (!to_txs.ParseFromString(tx.storages(i).value())) {
            ZJC_WARN("parse to txs failed.");
            ZJC_DEBUG("parse to txs failed. handle nonce: %lu", tx.nonce());
            continue;
        }

        ZJC_DEBUG("success handle tox tx heights net: %u, local net: %u, step: %d, nonce: %lu",
            to_txs.to_heights().sharding_id(),
            common::GlobalInfo::Instance()->network_id(),
            tx.step(),
            tx.nonce());
        if (!network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
            if (to_txs.to_heights().sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                ZJC_WARN("sharding invalid: %u, %u",
                    to_txs.to_heights().sharding_id(),
                    common::GlobalInfo::Instance()->network_id());
    //             assert(false);
                continue;
            }

            for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
                ZJC_DEBUG("success add local transfer tx tos %u_%u_%lu, "
                    "view height: %lu, address: %s, amount: %lu",
                    view_block.qc().network_id(), 
                    view_block.qc().pool_index(), 
                    view_block.block_info().height(), 
                    view_block.qc().view(),
                    common::Encode::HexEncode(to_txs.tos(i).des()).c_str(),
                    to_txs.tos(i).amount());
            }

            HandleLocalNormalToTx(*view_block_ptr, to_txs, tx);
        } else {
            if (to_txs.to_heights().sharding_id() == network::kRootCongressNetworkId) {
                ZJC_DEBUG("root handle normal to tx to_txs size: %u", to_txs.tos_size());
                RootHandleNormalToTx(view_block, to_txs);
            }
        }
    }
}

void BlockManager::RootHandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        pools::protobuf::ToTxMessage& to_txs) {
    auto& block = view_block.block_info();
    // 将 NormalTo 中的多个 tx 拆分成多个 kRootCreateAddress tx
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto tos_item = to_txs.tos(i);
        ZJC_INFO("to tx step: %d, new address %s, amount: %lu, prepayment: %lu, nonce: %lu",
            tos_item.step(),
            common::Encode::HexEncode(tos_item.des()).c_str(),
            tos_item.amount(),
            tos_item.prepayment(),
            0);

        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_step(pools::protobuf::kRootCreateAddress);
        // 如果 shard 已经制定了 Contract Account 的 shard，直接创建，不需要 root 再分配
        // 如果没有，则需要 root 继续创建 kRootCreateAddress 交易
        // TODO: check address valid
        // if (tos_item.step() == pools::protobuf::kContractCreate) {
        //     // that's contract address, just add address
        //     auto account_info = std::make_shared<address::protobuf::AddressInfo>();
        //     account_info->set_pool_index(tos_item.pool_index());
        //     account_info->set_addr(tos_item.des());
        //     account_info->set_type(address::protobuf::kContract);
        //     account_info->set_sharding_id(tos_item.sharding_id());
        //     account_info->set_latest_height(block.height());
        //     account_info->set_balance(tos_item.amount());
        //     prefix_db_->AddAddressInfo(tos_item.des(), *account_info);
        //     ZJC_DEBUG("create add contract direct: %s, amount: %lu, sharding: %u, pool index: %u",
        //         common::Encode::HexEncode(tos_item.des()).c_str(),
        //         tos_item.amount(),
        //         tos_item.sharding_id(),
        //         tos_item.pool_index());
        //    continue;
        // }

        if (tos_item.step() == pools::protobuf::kJoinElect) {
            continue;
        }
        
        // for ContractCreateByRootFrom tx
        if (tos_item.step() == pools::protobuf::kCreateLibrary || 
                tos_item.step() == pools::protobuf::kContractCreate) {
            // assert(!tos_item.library_bytes().empty());
            tx->set_contract_code(tos_item.library_bytes());
            char data[8];
            uint32_t* uint_data = (uint32_t*)data;
            uint_data[0] = tos_item.sharding_id();
            uint_data[1] = tos_item.pool_index();
            tx->set_value(std::string(data, sizeof(data)));
        }
        
        auto pool_index = common::GetAddressPoolIndex(tos_item.des());
        msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
        tx->set_pubkey("");
        tx->set_to(tos_item.des());
        tx->set_gas_limit(0);
        tx->set_amount(tos_item.amount());
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_nonce(0);
        auto unique_hash = common::Hash::keccak256(
            tx->to() + "_" +
            std::to_string(block.height()) + "_" +
            std::to_string(i));
        tx->set_key(unique_hash);
        if (tos_item.prepayment() > 0 && tos_item.step() == pools::protobuf::kContractCreate) {
            auto tmp_msg_ptr = std::make_shared<transport::TransportMessage>();
            tmp_msg_ptr->address_info = msg_ptr->address_info;
            tmp_msg_ptr->header = msg_ptr->header;
            auto tmp_tx = tmp_msg_ptr->header.mutable_tx_proto();
            tmp_tx->set_to(tos_item.des() + tos_item.contract_from());
            auto unique_hash = common::Hash::keccak256(
                tmp_tx->to() + "_" +
                std::to_string(block.height()) + "_" +
                std::to_string(i));
            tmp_tx->set_key(unique_hash);
            tmp_tx->set_amount(tos_item.prepayment());
            tmp_tx->set_nonce(0);
            pools_mgr_->HandleMessage(msg_ptr);
            pools_mgr_->HandleMessage(tmp_msg_ptr);
            ZJC_INFO("create new contract address %s, user: %s, amount: %lu, "
                "prepayment: %lu, nonce: %lu, tmp nonce: %lu",
                common::Encode::HexEncode(tos_item.des()).c_str(),
                common::Encode::HexEncode(tos_item.contract_from()).c_str(),
                tos_item.amount(),
                tos_item.prepayment(),
                0, 0);
        } else {
            pools_mgr_->HandleMessage(msg_ptr);
            ZJC_INFO("create new address %s, amount: %lu, prepayment: %lu, nonce: %lu",
                common::Encode::HexEncode(tos_item.des()).c_str(),
                tos_item.amount(),
                tos_item.prepayment(),
                0);
        }
    }
}

// TODO refactor needed!
void BlockManager::HandleLocalNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const pools::protobuf::ToTxMessage& to_txs,
        const block::protobuf::BlockTx& tx) {
    uint32_t step = tx.step();
    std::unordered_map<std::string, std::shared_ptr<localToTxInfo>> addr_amount_map;
    std::vector<std::shared_ptr<localToTxInfo>> contract_create_tx_infos;
    ZJC_DEBUG("0 handle local to to_txs.tos_size(): %u, addr: %s, nonce: %lu, step: %d", 
        to_txs.tos_size(),
        common::Encode::HexEncode(tx.to()).c_str(),
        tx.nonce(),
        step);
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = common::kInvalidPoolIndex;
        auto to_tx = to_txs.tos(i);
        auto addr = to_tx.des();
        if (to_tx.des().size() == security::kUnicastAddressLength * 2) { // gas_prepayment tx des = to + from
            addr = to_tx.des().substr(0, security::kUnicastAddressLength); // addr = to
        }
        
        protos::AddressInfoPtr account_info = account_mgr_->GetAccountInfo(addr);
        ZJC_DEBUG("1 handle local to to_txs.tos_size(): %u, nonce: %lu, "
            "step: %d, addr: %s, account_info == nullptr: %d", 
            to_txs.tos_size(),
            tx.nonce(),
            step,
            common::Encode::HexEncode(addr).c_str(),
            (account_info == nullptr));
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
            // ZJC_DEBUG("root create address coming %s, shard: %u, pool: %u",
            //     common::Encode::HexEncode(addr).c_str(), sharding_id, pool_index);
        } else {
            ZJC_DEBUG("1 handle local to to_txs.tos_size(): %u, nonce: %lu, step: %d, addr: %s, "
                "to_tx.sharding_id(): %d, account_info->sharding_id(): %d", 
                to_txs.tos_size(),
                tx.nonce(),
                step,
                common::Encode::HexEncode(addr).c_str(),
                to_tx.sharding_id(),
                account_info->sharding_id());
            if (to_tx.sharding_id() != account_info->sharding_id()) {
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
        ZJC_DEBUG("handle local to has_library_bytes: %d, des: %s, nonce: %lu", 
            to_tx.has_library_bytes(),
            common::Encode::HexEncode(to_tx.des()).c_str(),
            tx.nonce());
        if (to_tx.amount() > 0) {
            auto iter = addr_amount_map.find(to_tx.des());
            if (iter == addr_amount_map.end()) {
                addr_amount_map[to_tx.des()] = std::make_shared<localToTxInfo>(
                    to_tx.des(), to_tx.amount(), pool_index, "");
            } else {
                iter->second->amount += to_tx.amount();
            }
        }
    }

    createConsensusLocalToTxs(view_block, tx, addr_amount_map);
}

void BlockManager::createConsensusLocalToTxs(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& to_tx,
        std::unordered_map<std::string, std::shared_ptr<localToTxInfo>>& addr_amount_map) {
    // ZJC_DEBUG("addr_amount_map size: %lu", addr_amount_map.size());
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
        // 由于是异步的，因此需要持久化 kv 来传递数据，但是 to 需要填充以分配交易池
        // pool index 是指定好的，而不是 shard 分配的，所以需要将 to 设置为 pool addr
        auto val = iter->second.SerializeAsString();
        auto tos_hash = common::Hash::keccak256(val);
        prefix_db_->SaveTemporaryKv(tos_hash, val);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        msg_ptr->address_info = account_mgr_->pools_address_info(iter->first);
        auto tx = msg_ptr->header.mutable_tx_proto();
        // 将 tos_hash 存入 kv，用于 HandleTx 时获取 val
        std::string uinique_tx_str = common::Hash::keccak256(
            view_block.qc().view_block_hash() +
            view_block.qc().sign_x() + 
            view_block.qc().sign_y() +
            msg_ptr->address_info->addr());
        tx->set_key(uinique_tx_str);
        tx->set_value(val);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        tx->set_gas_limit(0);
        tx->set_amount(0); // 具体 amount 在 kv 中
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_nonce(0);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_DEBUG("success add local transfer tx tos hash: %s, nonce: %lu, src to tx nonce: %lu, val: %s",
            common::Encode::HexEncode(tos_hash).c_str(),
            msg_ptr->address_info->nonce(),
            0,
            common::Encode::HexEncode(val).c_str());
    }
}

void BlockManager::GenesisNewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_item,
        db::DbWriteBatch& db_batch) {
    // TODO: fix
    // if (view_block_item->qc().view() >= 64) {
    //     return;
    // }

    assert(!view_block_item->qc().sign_x().empty());
    auto* block_item = &view_block_item->block_info();
    // TODO: check all block saved success
    auto btime = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
        "tx size: %u, hash: %s, prehash: %s, elect height: %lu, tm height: %lu, step: %d, status: %d",
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
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).step() : -1),
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).status() : -1));
    assert(view_block_item->qc().elect_height() >= 1);
    // block 两条信息持久化
    if (!prefix_db_->SaveBlock(*view_block_item, db_batch)) {
        ZJC_DEBUG("block saved: %lu", block_item->height());
        return;
    }

    // TODO: test
    const auto& tx_list = block_item->tx_list();
        auto btime10 = common::TimeUtils::TimestampMs();
        auto btime11 = common::TimeUtils::TimestampMs();
        auto btime1 = common::TimeUtils::TimestampMs();
#ifndef TEST_NO_CROSS
        // 当前节点和 block 分配的 shard 不同，要跨分片交易
        if (!network::IsSameToLocalShard(view_block_item->qc().network_id())) {
            pools_mgr_->OnNewCrossBlock(view_block_item);
            ZJC_DEBUG("new cross block coming: %u, %u, %lu",
                view_block_item->qc().network_id(), view_block_item->qc().pool_index(), block_item->height());
            btime10 = common::TimeUtils::TimestampMs();
        } else {
            if (statistic_mgr_) {
                statistic_mgr_->OnNewBlock(view_block_item);
            }

            to_txs_pool_->NewBlock(view_block_item);
            btime10 = common::TimeUtils::TimestampMs();
            // zjcvm::Execution::Instance()->NewBlock(*view_block_item, db_batch);
        }

        btime1 = common::TimeUtils::TimestampMs();
            // 处理交易信息
        for (int32_t i = 0; i < tx_list.size(); ++i) {
            // ZJC_DEBUG("0 new block coming sharding id: %u_%d_%lu, "
            //     "tx size: %u, hash: %s, phash: %s, elect height: %lu, "
            //     "tm height: %lu, from: %s, to: %s, nonce: %lu, status: %d, step: %d",
            //     view_block_item->qc().network_id(),
            //     view_block_item->qc().pool_index(),
            //     block_item->height(),
            //     block_item->tx_list_size(),
            //     common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
            //     common::Encode::HexEncode(view_block_item->parent_hash()).c_str(),
            //     view_block_item->qc().elect_height(),
            //     block_item->timeblock_height(),
            //     common::Encode::HexEncode(tx_list[i].from()).c_str(),
            //     common::Encode::HexEncode(tx_list[i].to()).c_str(),
            //     tx_list[i].nonce(),
            //     tx_list[i].status(),
            //     tx_list[i].step());
    //         // ADD_TX_DEBUG_INFO(const_cast<block::protobuf::Block*>(block_item)->mutable_tx_list(i));
            if (tx_list[i].step() != pools::protobuf::kConsensusCreateGenesisAcount &&
                    network::IsSameToLocalShard(view_block_item->qc().network_id())) {
                account_mgr_->NewBlockWithTx(*view_block_item, tx_list[i], db_batch);
            }
            
            if (tx_list[i].status() != consensus::kConsensusSuccess) {
                continue;
            }

            switch (tx_list[i].step()) {
            case pools::protobuf::kRootCreateAddress:
                // ZJC_DEBUG("success handle root create address tx.");
                ConsensusShardHandleRootCreateAddress(*view_block_item, tx_list[i]);
                break;
            case pools::protobuf::kNormalTo: {
                HandleNormalToTx(view_block_item, tx_list[i]);
                // ZJC_DEBUG("success handle to tx network: %u, pool: %u, height: %lu, "
                //     "nonce: %lu, bls: %s, %s",
                //     view_block_item->qc().network_id(),
                //     view_block_item->qc().pool_index(),
                //     block_item->height(),
                //     common::Encode::HexEncode(tx_list[i].gid()).c_str(),
                //     common::Encode::HexEncode(view_block_item->qc().sign_x()).c_str(),
                //     common::Encode::HexEncode(view_block_item->qc().sign_y()).c_str());
                break;
            }
            case pools::protobuf::kConsensusRootTimeBlock:
                prefix_db_->SaveLatestTimeBlock(block_item->height(), db_batch);
                break;
            case pools::protobuf::kStatistic:
                HandleStatisticTx(*view_block_item, tx_list[i]);
                break;
            case pools::protobuf::kCross:
                assert(false);
                break;
            case pools::protobuf::kConsensusRootElectShard:
                HandleElectTx(*view_block_item, tx_list[i], db_batch);
                break;
            case pools::protobuf::kJoinElect:
                HandleJoinElectTx(*view_block_item, tx_list[i], db_batch);
                break;
            default:
                break;
            }
        }
#endif
    auto btime2 = common::TimeUtils::TimestampMs();
    if (new_block_callback_ != nullptr) {
        // ZJC_DEBUG("new db callback called: %u_%u_%lu, %u_%u_%lu", 
        //     view_block_item->qc().network_id(),
        //     view_block_item->qc().pool_index(),
        //     block_item->height(),
        //     view_block_item->qc().network_id(),
        //     view_block_item->qc().pool_index(),
        //     view_block_item->qc().view());
        if (!new_block_callback_(view_block_item, db_batch)) {
            ZJC_DEBUG("block call back failed!");
            assert(false);
            return;
        }
    }

    prefix_db_->SaveValidViewBlockParentHash(
        view_block_item->parent_hash(), 
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        db_batch);
    auto btime3 = common::TimeUtils::TimestampMs();
    auto put_size = db_batch.ApproximateSize();
    // auto st = db_->Put(db_batch);
    // if (!st.ok()) {
    //     ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
    // }

    if (hotstuff_mgr_ && network::IsSameToLocalShard(view_block_item->qc().network_id())) {
        hotstuff_mgr_->UpdateStoredToDbView(
            view_block_item->qc().pool_index(), 
            view_block_item->qc().view());
    }

    auto etime = common::TimeUtils::TimestampMs();
    // if (etime - btime > 100000lu)
    ZJC_DEBUG("success new block coming sharding id UpdateStoredToDbView : %u_%u_%lu, "
        "tx size: %u, hash: %s, elect height: %lu, tm height: %lu, use time: %lu, t10: %lu, t11: %lu, t1: %lu, t2: %lu, t3: %lu",
        view_block_item->qc().network_id(),
        view_block_item->qc().pool_index(),
        view_block_item->qc().view(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(view_block_item->qc().view_block_hash()).c_str(),
        view_block_item->qc().elect_height(),
        block_item->timeblock_height(),
        (etime - btime),
        (btime10 - btime),
        (btime11 - btime10),
        (btime1 - btime11),
        (btime2 - btime1),
        (btime3 - btime2));

#ifndef NDEBUG
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto debug_len = tx_list[i].tx_debug_size();
        if (debug_len <= 0) {
            continue;
        }

        bool valid_delay = (tx_list[i].tx_debug(debug_len - 1).tx_debug_tm_ms() - tx_list[i].tx_debug(0).tx_debug_tm_ms()) > 15000lu ? true : false;
        if (!valid_delay) {
            continue;
        }

        for (int32_t debug_idx = 0; debug_idx < debug_len; ++debug_idx) {
            if (debug_idx == 0) {
                ZJC_INFO("tx delay debug step: %d, nonce: %lu, use time: %lu, all_time: %lu, pos: %s",
                    tx_list[i].step(),
                    tx_list[i].nonce(), 
                    tx_list[i].tx_debug(debug_idx).tx_debug_tm_ms(), 
                    (tx_list[i].tx_debug(debug_len - 1).tx_debug_tm_ms() - tx_list[i].tx_debug(0).tx_debug_tm_ms()),
                    tx_list[i].tx_debug(debug_idx).tx_debug_info().c_str());
            } else {
                // debug_idx -= 1;
                // if (debug_idx == 0) {
                //     ZJC_DEBUG("tx delay debug step: %d, nonce: %lu, use time: %lu, all_time: %lu, pos: %s",
                //         tx_list[i].step(),
                //         common::Encode::HexEncode(tx_list[i].gid()).c_str(), 
                //         tx_list[i].tx_debug(debug_idx).tx_debug_tm_ms(), 
                //         (tx_list[i].tx_debug(debug_len - 1).tx_debug_tm_ms() - tx_list[i].tx_debug(0).tx_debug_tm_ms()),
                //         tx_list[i].tx_debug(debug_idx).tx_debug_info().c_str());
                // } else {
                //     ZJC_DEBUG("tx delay debug step: %d, nonce: %lu, use time: %lu, all_time: %lu, pos: %s",
                //         tx_list[i].step(),
                //         common::Encode::HexEncode(tx_list[i].gid()).c_str(), 
                //         (tx_list[i].tx_debug(debug_idx).tx_debug_tm_ms() - tx_list[i].tx_debug(debug_idx - 1).tx_debug_tm_ms()), 
                //         (tx_list[i].tx_debug(debug_len - 1).tx_debug_tm_ms() - tx_list[i].tx_debug(0).tx_debug_tm_ms()),
                //         tx_list[i].tx_debug(debug_idx).tx_debug_info().c_str());
                // }
                
                // debug_idx += 1;
                ZJC_INFO("tx delay debug step: %d, nonce: %lu, use time: %lu, all_time: %lu, pos: %s",
                    tx_list[i].step(),
                    tx_list[i].nonce(),
                    (tx_list[i].tx_debug(debug_idx).tx_debug_tm_ms() - tx_list[i].tx_debug(debug_idx - 1).tx_debug_tm_ms()), 
                    (tx_list[i].tx_debug(debug_len - 1).tx_debug_tm_ms() - tx_list[i].tx_debug(0).tx_debug_tm_ms()),
                    tx_list[i].tx_debug(debug_idx).tx_debug_info().c_str());
            }
        }
    }
#endif

    if (ck_client_ != nullptr) {
        ck_client_->AddNewBlock(view_block_item);
    }
}

void BlockManager::AddNewBlock(
        const std::shared_ptr<hotstuff::ViewBlockInfo>& view_block_info) {
    auto view_block_item = view_block_info->view_block;
    assert(!view_block_item->qc().sign_x().empty());
    auto* block_item = &view_block_item->block_info();
    // TODO: check all block saved success
    auto btime = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
        "tx size: %u, hash: %s, prehash: %s, elect height: %lu, tm height: %lu, step: %d, status: %d",
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
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).step() : -1),
        (view_block_item->block_info().tx_list_size() > 0 ? view_block_item->block_info().tx_list(0).status() : -1));
    assert(view_block_item->qc().elect_height() >= 1);
    // 当前节点和 block 分配的 shard 不同，要跨分片交易
    if (!network::IsSameToLocalShard(view_block_item->qc().network_id())) {
        pools_mgr_->OnNewCrossBlock(view_block_item);
        ZJC_DEBUG("new cross block coming: %u, %u, %lu",
            view_block_item->qc().network_id(), view_block_item->qc().pool_index(), block_item->height());
    } else {
        if (statistic_mgr_) {
            statistic_mgr_->OnNewBlock(view_block_item);
        }

        to_txs_pool_->NewBlock(view_block_item);
        // zjcvm::Execution::Instance()->NewBlock(*view_block_item, db_batch);
    }

    if (view_block_info->zjc_host_ptr->normal_to_tx_ != nullptr) {
        HandleNormalToTx(view_block_item, *view_block_info->zjc_host_ptr->normal_to_tx_);
    }

    if (view_block_info->zjc_host_ptr->root_create_address_tx_ != nullptr) {
        ConsensusShardHandleRootCreateAddress(
            *view_block_item, 
            *view_block_info->zjc_host_ptr->root_create_address_tx_);
    }

    if (view_block_info->zjc_host_ptr->statisitc_tx_ != nullptr) {
        HandleStatisticTx(*view_block_item, *view_block_info->zjc_host_ptr->statisitc_tx_);
    }
}

// HandleJoinElectTx 持久化 JoinElect 交易相关信息
void BlockManager::HandleJoinElectTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    // 存放了一个 from => balance 的映射
    prefix_db_->SaveElectNodeStoke(
        tx.from(),
        view_block.qc().elect_height(),
        tx.balance(),
        db_batch);
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            // 解析参与选举的信息
            bls::protobuf::JoinElectInfo join_info;
            // ZJC_DEBUG("now parse join elect info: %u", tx.storages(i).value().size());
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
                view_block.qc().network_id(), 
                view_block.qc().pool_index(), 
                block.height(),
                common::GlobalInfo::Instance()->network_id());
            break;
        }
    }
}

void BlockManager::HandleElectTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    ZJC_DEBUG("handle elect tx storage size: %u, %u_%u_%lu, elect height: %lu",
        tx.storages_size(), view_block.qc().network_id(),
        view_block.qc().pool_index(), block.height(), 
        view_block.qc().elect_height());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        // ZJC_DEBUG("handle elect tx storage index: %u, key: %s, protos::kElectNodeAttrElectBlock: %s",
        //     i, tx.storages(i).key().c_str(), protos::kElectNodeAttrElectBlock.c_str());
        if (tx.storages(i).key() == protos::kElectNodeAttrElectBlock) {
            elect::protobuf::ElectBlock elect_block;
            if (!elect_block.ParseFromString(tx.storages(i).value())) {
                assert(false);
                return;
            }

            AddMiningToken(view_block, elect_block);
            if (shard_elect_tx_[elect_block.shard_network_id()] != nullptr) {
                if (shard_elect_tx_[elect_block.shard_network_id()]->tx_ptr->tx_info->nonce() == tx.nonce()) {
                    shard_elect_tx_[elect_block.shard_network_id()] = nullptr;
                    ZJC_DEBUG("success erase elect tx: %u", elect_block.shard_network_id());
                }
            }

            ZJC_DEBUG("success add elect block elect height: %lu, net: %u, "
                "pool: %u, height: %lu, common pk: %s, prev elect height: %lu", 
                view_block.qc().elect_height(),
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
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
        std::string uinique_tx_str = common::Hash::keccak256(
            view_block.qc().view_block_hash() +
            view_block.qc().sign_x() + 
            view_block.qc().sign_y() +
            msg_ptr->address_info->addr());
        tx->set_key(uinique_tx_str);
        tx->set_value(val);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_nonce(0);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("mining success create kConsensusLocalTos %s nonce: %lu",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            tx->nonce());
    }
}

void BlockManager::LoadLatestBlocks() {
    ZJC_DEBUG("load latest block called!");
    timeblock::protobuf::TimeBlock tmblock;
    db::DbWriteBatch db_batch;
    if (prefix_db_->GetLatestTimeBlock(&tmblock)) {
        ZJC_DEBUG("load latest time block called!");
        auto tmblock_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto& block = *tmblock_ptr;
        if (GetBlockWithHeight(
                network::kRootCongressNetworkId,
                common::kImmutablePoolSize,
                tmblock.height(),
                block) == kBlockSuccess) {
            ZJC_DEBUG("load latest elect block called!");
            if (new_block_callback_ != nullptr) {
                new_block_callback_(tmblock_ptr, db_batch);
            }
        } else {
            ZJC_FATAL("load latest timeblock failed!");
        }
    }

    for (uint32_t load_idx = 0; load_idx < 2; ++load_idx) {
        for (uint32_t i = network::kRootCongressNetworkId;
                i < network::kConsensusShardEndNetworkId; ++i) {
            elect::protobuf::ElectBlock elect_block;
            if (load_idx == 0) {
                if (!prefix_db_->GetLatestElectBlock(i, &elect_block)) {
                    ZJC_ERROR("get elect latest block failed: %u", i);
                    break;
                }
            } else {
                if (!prefix_db_->GetHavePrevlatestElectBlock(i, &elect_block)) {
                    ZJC_ERROR("get elect latest block failed: %u", i);
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
                    new_block_callback_(elect_block_ptr, db_batch);
                }

                AddMiningToken(block, elect_block);
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
    }
    
    auto latest_to_tx_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
    auto& block = *latest_to_tx_block;
    if (prefix_db_->GetLatestToBlock(&block)) {
        auto tmp_latest_to_block_ptr_index = (latest_to_block_ptr_index_ + 1) % 2;
        latest_to_block_ptr_[tmp_latest_to_block_ptr_index] = latest_to_tx_block;
        latest_to_block_ptr_index_ = tmp_latest_to_block_ptr_index;
        ZJC_DEBUG("success set latest to block ptr: %lu, tm: %lu",
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
        ZJC_DEBUG("create_statistic_tx_cb_ == nullptr");
        return;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    uint64_t timeblock_height = prev_timeblock_height_;
    ZJC_DEBUG("StatisticWithHeights called!");

    // Some nodes will receive statistic block ahead of timeblock.
    // This happens accasionally, making statistic tx failed to be found
    // So we should make sure that one timeblock can only gathered statistic info for once
    if (IsTimeblockHeightStatisticDone(timeblock_height)) {
        ZJC_DEBUG("repeat StatisticWithHeights, %lu, latest: %lu",
            timeblock_height, latest_statistic_timeblock_height_);
        return;
    }
    
    if (statistic_mgr_->StatisticWithHeights(
            elect_statistic,
            timeblock_height) != pools::kPoolsSuccess) {
        ZJC_DEBUG("failed StatisticWithHeights!");
        return;
    }

    // 对应 timeblock_height 的 elect_statistic 已经收集，不会进行重复收集
    MarkDoneTimeblockHeightStatistic(timeblock_height);

    // TODO: fix invalid hash
    auto unique_hash = common::Hash::keccak256(
        std::string("create_statistic_tx_") + 
        std::to_string(elect_statistic.sharding_id()) + "_" +
        std::to_string(elect_statistic.statistic_height()));
    ZJC_DEBUG("success create statistic message hash: %s, timeblock_height: %lu, statistic: %s", 
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
            ZJC_INFO("success add statistic tx: %s, statistic elect height: %lu, "
                "heights: %s, timeout: %lu, kStatisticTimeoutMs: %lu, now: %lu, "
                "nonce: %lu, timeblock_height: %lu",
                common::Encode::HexEncode(unique_hash).c_str(),
                0,
                "", tx_ptr->timeout,
                kStatisticTimeoutMs, common::TimeUtils::TimestampMs(),
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
        const block::protobuf::BlockTx& block_tx,
        const pools::protobuf::ElectStatistic& elect_statistic) {
    auto& block = view_block.block_info();
    if (create_elect_tx_cb_ == nullptr) {
        ZJC_INFO("create_elect_tx_cb_ == nullptr");
        return;
    }

    // 时间粒度一个 epoch, 10min 一个选举块
    if (prefix_db_->ExistsStatisticedShardingHeight(
            view_block.qc().network_id(),
            block.timeblock_height())) {
        ZJC_INFO("prefix_db_->ExistsStatisticedShardingHeight net: %u, tm height: %lu",
            view_block.qc().network_id(), block.timeblock_height());
        return;
    }

    if (elect_statistic.statistics_size() <= 0) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        for (int32_t i = 0; i < elect_statistic.join_elect_nodes_size(); ++i) {
            ZJC_DEBUG("sharding: %u, new elect node: %s, balance: %lu, shard: %u, pos: %u", 
                elect_statistic.sharding_id(), 
                common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
                elect_statistic.join_elect_nodes(i).stoke(),
                elect_statistic.join_elect_nodes(i).shard(),
                elect_statistic.join_elect_nodes(i).elect_pos());
        }

        assert(view_block.qc().network_id() == elect_statistic.sharding_id());
        ZJC_DEBUG("success handle statistic block net: %u, sharding: %u, "
            "pool: %u, height: %lu, elect height: %lu",
            view_block.qc().network_id(), elect_statistic.sharding_id(), view_block.qc().pool_index(), 
            block.timeblock_height(), elect_statistic.statistics(elect_statistic.statistics_size() - 1).elect_height());
        // create elect transaction now for block.network_id
        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        new_msg_ptr->address_info = account_mgr_->pools_address_info(elect_statistic.sharding_id());
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        std::string unique_hash = common::Hash::keccak256(
            std::string("root_create_elect_tx_") + 
            std::to_string(elect_statistic.sharding_id()) + "_" +
            std::to_string(block.timeblock_height()));
        tx->set_key(unique_hash);
        char data[16];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = view_block.qc().network_id();
        tmp[1] = block.timeblock_height();
        tx->set_value(std::string(data, sizeof(data)));
        tx->set_pubkey("");
        tx->set_to(new_msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusRootElectShard);
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_nonce(new_msg_ptr->address_info->nonce() + 1);
        auto shard_elect_tx = std::make_shared<BlockTxsItem>();
        shard_elect_tx->tx_ptr = create_elect_tx_cb_(new_msg_ptr);
        shard_elect_tx->tx_ptr->time_valid += kElectValidTimeout;
        shard_elect_tx->timeout = common::TimeUtils::TimestampMs() + kElectTimeout;
        shard_elect_tx->stop_consensus_timeout = shard_elect_tx->timeout + kStopConsensusTimeoutMs;
        shard_elect_tx_[view_block.qc().network_id()] = shard_elect_tx;
        ZJC_INFO("success add elect tx: %u, %lu, nonce: %lu, tx key: %s, statistic elect height: %lu",
            view_block.qc().network_id(), block.timeblock_height(),
            tx->nonce(),
            common::Encode::HexEncode(shard_elect_tx->tx_ptr->tx_key).c_str(),
            0);
    }
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

        ZJC_DEBUG("now leader get to to tx.");
        leader_prev_get_to_tx_tm_ = cur_time + 3000lu;
        auto latest_to_block_ptr = latest_to_block_ptr_[latest_to_block_ptr_index_];
        if (latest_to_block_ptr != nullptr &&
                latest_to_block_ptr->block_info().timestamp() + 10000lu >= cur_time) {
            ZJC_DEBUG("now leader get to to tx timestamp error");
            return nullptr;
        }

        if (to_txs_pool_->LeaderCreateToHeights(heights) != pools::kPoolsSuccess) {
            ZJC_DEBUG("now leader get to to tx leader get error");
            return nullptr;
        }
    } else {
        if (!heights.ParseFromString(heights_str)) {
            assert(false);
            return nullptr;
        }
    }

    auto tx_ptr = HandleToTxsMessage(heights);
    if (tx_ptr != nullptr) {
        // heights_str_map_[height_hash] = tx_ptr;
        // CHECK_MEMORY_SIZE(heights_str_map_);
        ZJC_INFO("success get to tx tx info: %s, nonce: %lu, val: %s, heights: %s",
            ProtobufToJson(*tx_ptr->tx_info).c_str(),
            tx_ptr->tx_info->nonce(), 
            "common::Encode::HexEncode(tx_ptr->tx_info.value()).c_str()",
            ProtobufToJson(heights).c_str());
    } else {
        ZJC_DEBUG("failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
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
            ZJC_DEBUG("1 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        }
    }

    if (all_to_txs.to_tx_arr_size() == 0) {
        ZJC_DEBUG("2 failed get to tx tx info: %s", ProtobufToJson(heights).c_str());
        return nullptr;
    }
    
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
    tx->set_nonce(0);
    auto tx_ptr = create_to_tx_cb_(new_msg_ptr);
    tx_ptr->time_valid += kToValidTimeout;
    return tx_ptr;
}

bool BlockManager::HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (HasToTx(pool_index, tx_valid_func)) {
        // ZJC_DEBUG("success check has to tx.");
        return true;
    }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (HasStatisticTx(pool_index, tx_valid_func)) {
    //     // ZJC_DEBUG("success check has statistic tx.");
    //     return true;
    // }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (HasElectTx(pool_index, tx_valid_func)) {
    //     // ZJC_DEBUG("success check has elect tx.");
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
            ZJC_DEBUG("now pop statistic tx tx nonce: %lu, tm height: %lu",
                iter->second->tx_ptr->tx_info->nonce(), iter->first);
        }

        auto valid_got_latest_statistic_map_ptr_index_tmp = (valid_got_latest_statistic_map_ptr_index_ + 1) % 2;
        got_latest_statistic_map_ptr_[valid_got_latest_statistic_map_ptr_index_tmp] = static_tmp_map;
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
    auto latest_to_block_ptr = latest_to_block_ptr_[latest_to_block_ptr_index_];
    if (latest_to_block_ptr != nullptr &&
            latest_to_block_ptr->block_info().timestamp() + 10000lu >= cur_time) {
        ZJC_DEBUG("invalid latest_to_block_ptr: %d", (latest_to_block_ptr != nullptr));
        return false;
    }

    return true;
}

bool BlockManager::HasStatisticTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func) {
    if (pool_index != common::kImmutablePoolSize) {
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

        if (tx_valid_func(
                *iter->second->tx_ptr->address_info, 
                *iter->second->tx_ptr->tx_info) != 0) {
            return false;
        }

        // ZJC_DEBUG("has statistic %u, tx nonce: %lu", 
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

        if (shard_elect_tx_[i] == nullptr) {
            continue;
        }

        if (tx_valid_func(
                *shard_elect_tx_[i]->tx_ptr->address_info, 
                *shard_elect_tx_[i]->tx_ptr->tx_info) != 0) {
            return false;
        }
        
        ZJC_DEBUG("has elect %u, tx nonce: %lu", 
            pool_index, 
            shard_elect_tx_[i]->tx_ptr->tx_info->nonce());
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

        if (iter->second->tx_ptr->tx_info->key() == unqiue_hash) {
            shard_statistic_tx = iter->second;
            break;
        }
    }

    if (shard_statistic_tx == nullptr) {
        ZJC_DEBUG("shard_statistic_tx == nullptr, unqiue_hash: %s, is leader: %d",
            common::Encode::HexEncode(unqiue_hash).c_str(),
            leader);
        if (pool_index == common::kImmutablePoolSize) {
            assert(false); // 长时间压测下，有的节点 pool: 16 找不到 statistic tx 导致共识卡死
        }
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
        tx->set_to(shard_statistic_tx->tx_ptr->address_info->addr());
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
            // ZJC_DEBUG("0 failed get elect tx pool index: %u, tx hash: %s",
            //     pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i];
        if (!tx_hash.empty()) {
            if (shard_elect_tx->tx_ptr->tx_key == tx_hash) {
                // ZJC_DEBUG("0 success get elect tx pool index: %u, tx hash: %s",
                //     pool_index, common::Encode::HexEncode(tx_hash).c_str());
                return shard_elect_tx->tx_ptr;
            }

            // ZJC_DEBUG("1 failed get elect tx pool index: %u, tx hash: %s",
            //     pool_index, common::Encode::HexEncode(tx_hash).c_str());
            continue;
        }

        auto now_tm = common::TimeUtils::TimestampUs();
        if (shard_elect_tx->tx_ptr->time_valid > now_tm) {
            // ZJC_DEBUG("2 failed get elect tx pool index: %u, tx hash: %s",
            //     pool_index, common::Encode::HexEncode(tx_hash).c_str());
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
