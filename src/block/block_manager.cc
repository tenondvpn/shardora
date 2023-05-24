#include "block/block_manager.h"

#include "block/block_utils.h"
#include "block/account_manager.h"
#include "block/block_proto.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "network/route.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"
#include "transport/processor.h"

namespace zjchain {

namespace block {

static const std::string kShardElectPrefix = common::Encode::HexDecode(
    "227a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe2");

BlockManager::BlockManager() {
}

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
        const std::string& local_id,
        DbBlockCallback new_block_callback) {
    account_mgr_ = account_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    local_id_ = local_id;
    new_block_callback_ = new_block_callback;
    statistic_mgr_ = statistic_mgr;
    security_ = security;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    to_txs_pool_ = std::make_shared<pools::ToTxsPools>(
        db_, local_id, max_consensus_sharding_id_, pools_mgr_);
    if (common::GlobalInfo::Instance()->for_ck_server()) {
        ck_client_ = std::make_shared<ck::ClickHouseClient>("127.0.0.1", "", "");
        ZJC_DEBUG("support ck");
    }

    consensus_block_queues_ = new common::ThreadSafeQueue<BlockToDbItemPtr>[
        common::GlobalInfo::Instance()->message_handler_thread_count()];
    network::Route::Instance()->RegisterMessage(
        common::kBlockMessage,
        std::bind(&BlockManager::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&BlockManager::ConsensusTimerMessage, this, std::placeholders::_1));
    bool genesis = false;
    return kBlockSuccess;
}

void BlockManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    if (to_txs_msg_ != nullptr) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (now_tm > prev_to_txs_tm_us_ + 1000000) {
            auto msg_ptr = to_txs_msg_;
            HandleToTxsMessage(msg_ptr, true);
            prev_to_txs_tm_us_ = now_tm;
        }
    }

    NetworkNewBlock(msg_ptr->thread_idx, nullptr);
    if (to_tx_leader_ == nullptr) {
        return;
    }

    if (local_id_ != to_tx_leader_->id) {
        return;
    }

    CreateToTx(msg_ptr->thread_idx);
    CreateStatisticTx(msg_ptr->thread_idx);
}

void BlockManager::OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members) {
    if (sharding_id > max_consensus_sharding_id_) {
        max_consensus_sharding_id_ = sharding_id;
    }

    if (sharding_id == common::GlobalInfo::Instance()->network_id()) {
        for (auto iter = members->begin(); iter != members->end(); ++iter) {
            if ((*iter)->pool_index_mod_num == 0) {
                to_tx_leader_ = *iter;
                ZJC_DEBUG("success get leader: %u, %s",
                    sharding_id,
                    common::Encode::HexEncode(to_tx_leader_->id).c_str());
                break;
            }
        }
    }
}

void BlockManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // verify signature valid and check leader valid
    if (msg_ptr->header.block_proto().to_txs_size() > 0) {
        HandleToTxsMessage(msg_ptr, false);
    }

    if (msg_ptr->header.block_proto().has_shard_statistic_tx()) {
        HandleStatisticMessage(msg_ptr);
    }

    if (msg_ptr->header.has_block()) {
        auto& header = msg_ptr->header;
        auto block_ptr = std::make_shared<block::protobuf::Block>(header.block());
        // (TODO): check block agg sign
        NetworkNewBlock(msg_ptr->thread_idx, block_ptr);
    }
}

void BlockManager::NetworkNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (block_item != nullptr) {
        db::DbWriteBatch db_batch;
        AddNewBlock(thread_idx, block_item, db_batch);
        if (!db_->Put(db_batch).ok()) {
            ZJC_FATAL("save db failed!");
            return;
        }
    }

    HandleAllConsensusBlocks(thread_idx);
}

void BlockManager::ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item) {
    consensus_block_queues_[thread_idx].push(block_item);
}

void BlockManager::NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
}

void BlockManager::HandleAllConsensusBlocks(uint8_t thread_idx) {
    auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count();
    for (int32_t i = 0; i < thread_count; ++i) {
        while (consensus_block_queues_[i].size() > 0) {
            BlockToDbItemPtr db_item_ptr = nullptr;
            if (consensus_block_queues_[i].pop(&db_item_ptr)) {
                AddNewBlock(thread_idx, db_item_ptr->block_ptr, *db_item_ptr->db_batch);
            }
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
        auto& account_id = account_mgr_->GetTxValidAddress(tx_list[i]);
        auto account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(common::GetAddressPoolIndex(account_id));
        account_info->set_addr(account_id);
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(block_item->network_id());
        account_info->set_latest_height(block_item->height());
        account_info->set_balance(tx_list[i].balance());
        ZJC_DEBUG("genesis add new account %s : %lu",
            common::Encode::HexEncode(account_info->addr()).c_str(),
            account_info->balance());
        prefix_db_->AddAddressInfo(account_info->addr(), *account_info, db_batch);
    }
}

void BlockManager::HandleCrossTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        if (block_tx.storages(i).key() == protos::kShardCross) {
            if (cross_statistic_tx_ != nullptr) {
                if (block_tx.storages(i).val_hash() == cross_statistic_tx_->tx_hash) {
                    cross_statistic_tx_ = nullptr;
                }
            }

            ZJC_DEBUG("success handle cross tx block.");
            break;
        }
    }
}

void BlockManager::HandleStatisticTx(
        uint8_t thread_idx,
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
    pools::protobuf::ElectStatistic elect_statistic;
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        if (block_tx.storages(i).key() == protos::kShardStatistic) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                return;
            }

            if (!elect_statistic.ParseFromString(val)) {
                return;
            }

            if (shard_statistic_tx_ != nullptr) {
                if (block_tx.storages(i).val_hash() == shard_statistic_tx_->tx_hash) {
                    shard_statistic_tx_ = nullptr;
                }
            }

            break;
        }
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        HandleStatisticBlock(block, block_tx, elect_statistic, db_batch);
    }
}

void BlockManager::HandleNormalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
//     ZJC_DEBUG("new normal to block coming.");
    if (tx.storages_size() != 1) {
        ZJC_WARN("normal to txs storages invalid.");
        return;
    }

    std::string to_txs_str;
    if (!prefix_db_->GetTemporaryKv(tx.storages(0).val_hash(), &to_txs_str)) {
        ZJC_WARN("normal to get val hash failed: %s",
            common::Encode::HexEncode(tx.storages(0).val_hash()).c_str());
        return;
    }

    pools::protobuf::ToTxMessage to_txs;
    if (!to_txs.ParseFromString(to_txs_str)) {
        ZJC_WARN("parse to txs failed.");
        return;
    }

    to_txs_[to_txs.to_heights().sharding_id()] = nullptr;
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        if (to_txs.to_heights().sharding_id() != common::GlobalInfo::Instance()->network_id()) {
            ZJC_WARN("sharding invalid: %lu, %lu.",
                to_txs.to_heights().sharding_id(),
                common::GlobalInfo::Instance()->network_id());
            return;
        }

        HandleLocalNormalToTx(thread_idx, to_txs, tx.step(), tx.storages(0).val_hash());
    } else {
        RootHandleNormalToTx(thread_idx, block.height(), to_txs, db_batch);
    }
}

void BlockManager::RootHandleNormalToTx(
        uint8_t thread_idx,
        uint64_t height,
        pools::protobuf::ToTxMessage& to_txs,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto tos_item = to_txs.tos(i);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_step(pools::protobuf::kRootCreateAddress);
        if (tos_item.step() == pools::protobuf::kContractUserCreateCall) {
            // that's contract address, just add address
            auto account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(tos_item.pool_index());
            account_info->set_addr(tos_item.des());
            account_info->set_type(address::protobuf::kContract);
            account_info->set_sharding_id(tos_item.sharding_id());
            account_info->set_latest_height(height);
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
                ZJC_DEBUG("success handle kElectJoin tx: %s", common::Encode::HexEncode(tos_item.des()).c_str());
            }
            continue;
        }

        tx->set_pubkey("");
        tx->set_to(tos_item.des());
        auto gid = common::Hash::keccak256(
            tos_item.des() + "_" +
            std::to_string(height) + "_" +
            std::to_string(i));
        tx->set_gas_limit(0);
        tx->set_amount(tos_item.amount());
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        auto pool_index = common::Hash::Hash32(tos_item.des()) % common::kImmutablePoolSize;
        msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_DEBUG("create new address %s, amount: %lu",
            common::Encode::HexEncode(tos_item.des()).c_str(), tos_item.amount());
    }
}

void BlockManager::HandleLocalNormalToTx(
        uint8_t thread_idx,
        const pools::protobuf::ToTxMessage& to_txs,
        uint32_t step,
        const std::string& heights_hash) {
    std::unordered_map<std::string, std::pair<uint64_t, uint32_t>> addr_amount_map;
    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        // dispatch to txs to tx pool
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = common::kInvalidPoolIndex;
        auto addr = to_txs.tos(i).des();
        if (to_txs.tos(i).des().size() == security::kUnicastAddressLength * 2) {
            addr = to_txs.tos(i).des().substr(0, security::kUnicastAddressLength);
        }

        auto account_info = account_mgr_->GetAccountInfo(thread_idx, addr);
        if (account_info == nullptr) {
            if (step != pools::protobuf::kRootCreateAddressCrossSharding) {
//                 assert(false);
                continue;
            }

            if (!to_txs.tos(i).has_sharding_id() || !to_txs.tos(i).has_pool_index()) {
                assert(false);
                continue;
            }

            if (to_txs.tos(i).sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                assert(false);
                continue;
            }

            if (to_txs.tos(i).pool_index() >= common::kImmutablePoolSize) {
                assert(false);
                continue;
            }

            sharding_id = to_txs.tos(i).sharding_id();
            pool_index = to_txs.tos(i).pool_index();
        } else {
            sharding_id = account_info->sharding_id();
            pool_index = account_info->pool_index();
        }

        if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
            assert(false);
            continue;
        }

        auto iter = addr_amount_map.find(to_txs.tos(i).des());
        if (iter == addr_amount_map.end()) {
            addr_amount_map[to_txs.tos(i).des()] = std::make_pair(
                to_txs.tos(i).amount(),
                pool_index);
        } else {
            iter->second.first += to_txs.tos(i).amount();
        }
    }

    std::unordered_map<uint32_t, pools::protobuf::ToTxMessage> to_tx_map;
    for (auto iter = addr_amount_map.begin(); iter != addr_amount_map.end(); ++iter) {
        auto to_iter = to_tx_map.find(iter->second.second);
        if (to_iter == to_tx_map.end()) {
            pools::protobuf::ToTxMessage to_tx;
            to_tx_map[iter->second.second] = to_tx;
            to_iter = to_tx_map.find(iter->second.second);
        }

        auto to_item = to_iter->second.add_tos();
        to_item->set_pool_index(iter->second.second);
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second.first);
        ZJC_DEBUG("success add local transfer to %s, %lu",
            common::Encode::HexEncode(iter->first).c_str(), iter->second.first);
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
        tx->set_value(tos_hash);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        auto gid = common::Hash::keccak256(tos_hash + heights_hash);
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        pools_mgr_->HandleMessage(msg_ptr);
    }
}

void BlockManager::AddNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("new block coming sharding id: %u, pool: %d, height: %lu,"
        "tx size: %u, hash: %s, thread_idx: %d",
        block_item->network_id(),
        block_item->pool_index(),
        block_item->height(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(block_item->hash()).c_str(),
        thread_idx);
    if (!prefix_db_->SaveBlock(*block_item, db_batch)) {
        ZJC_DEBUG("block saved: %lu", block_item->height());
        return;
    }

    to_txs_pool_->NewBlock(*block_item, db_batch);
    if (ck_client_ != nullptr) {
        ck_client_->AddNewBlock(block_item);
        ZJC_DEBUG("add to ck.");
    }

    const auto& tx_list = block_item->tx_list();
    if (tx_list.empty()) {
        return;
    }

    for (int32_t i = 0; i < tx_list.size(); ++i) {
        switch (tx_list[i].step()) {
        case pools::protobuf::kRootCreateAddressCrossSharding:
        case pools::protobuf::kNormalTo:
            HandleNormalToTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kConsensusRootTimeBlock:
            prefix_db_->SaveLatestTimeBlock(block_item->height(), db_batch);
            break;
        case pools::protobuf::kStatistic:
            HandleStatisticTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kCross:
            HandleCrossTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kJoinElect:
            HandleJoinElectTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        default:
            break;
        }
    }

    if (new_block_callback_ != nullptr) {
        new_block_callback_(thread_idx, block_item, db_batch);
    }

    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        ZJC_FATAL("write block to db failed!");
    }
}

void BlockManager::HandleJoinElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    prefix_db_->SaveElectNodeStoke(
        tx.from(),
        block.electblock_height(),
        tx.balance(),
        db_batch);
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                assert(false);
                break;
            }

            bls::protobuf::JoinElectInfo join_info;
            if (!join_info.ParseFromString(val)) {
                assert(false);
                break;
            }

            if (join_info.g2_req().verify_vec_size() <= 0) {
                ZJC_DEBUG("success handle kElectJoin tx: %s, not has verfications.",
                    common::Encode::HexEncode(tx.from()).c_str());
                break;
            }

            std::string str_for_hash;
            str_for_hash.reserve(join_info.g2_req().verify_vec_size() * 4 * 64 + 8);
            uint32_t shard_id = join_info.shard_id();
            uint32_t mem_idx = join_info.member_idx();
            str_for_hash.append((char*)&shard_id, sizeof(shard_id));
            str_for_hash.append((char*)&mem_idx, sizeof(mem_idx));
            for (int32_t i = 0; i < join_info.g2_req().verify_vec_size(); ++i) {
                auto& item = join_info.g2_req().verify_vec(i);
                str_for_hash.append(item.x_c0());
                str_for_hash.append(item.x_c1());
                str_for_hash.append(item.y_c0());
                str_for_hash.append(item.y_c1());
                str_for_hash.append(item.z_c0());
                str_for_hash.append(item.z_c1());
            }

            auto check_hash = common::Hash::keccak256(str_for_hash);
            if (check_hash != tx.storages(i).val_hash()) {
                assert(false);
                break;
            }

            prefix_db_->SaveNodeVerificationVector(tx.from(), join_info, db_batch);
            ZJC_DEBUG("success handle kElectJoin tx: %s", common::Encode::HexEncode(tx.from()).c_str());
            break;
        }
    }
}

void BlockManager::HandleElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kElectNodeAttrElectBlock) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                return;
            }

            elect::protobuf::ElectBlock elect_block;
            if (!elect_block.ParseFromString(val)) {
                return;
            }

            AddMiningToken(block.hash(), thread_idx, elect_block);
            if (shard_elect_tx_[elect_block.shard_network_id()] == nullptr) {
                return;
            }

            if (shard_elect_tx_[elect_block.shard_network_id()]->tx_ptr->gid == tx.gid()) {
                shard_elect_tx_[elect_block.shard_network_id()] = nullptr;
                ZJC_DEBUG("success erase elect tx: %u", elect_block.shard_network_id());
            }
        }
    }
}

void BlockManager::AddMiningToken(
        const std::string& block_hash,
        uint8_t thread_idx,
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
        auto account_info = account_mgr_->GetAccountInfo(thread_idx, id);
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
        tx->set_value(tos_hash);
        tx->set_pubkey("");
        tx->set_to(msg_ptr->address_info->addr());
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        auto gid = common::Hash::keccak256(tos_hash + block_hash);
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        pools_mgr_->HandleMessage(msg_ptr);
    }
}

void BlockManager::LoadLatestBlocks(uint8_t thread_idx) {
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
                new_block_callback_(thread_idx, tmblock_ptr, db_batch);
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
                common::kRootChainPoolIndex,
                elect_block.elect_height(),
                block) == kBlockSuccess) {
            if (new_block_callback_ != nullptr) {
                new_block_callback_(thread_idx, elect_block_ptr, db_batch);
            }

            ZJC_INFO("get block with height success: %u, %u, %lu",
                network::kRootCongressNetworkId,
                common::kRootChainPoolIndex,
                elect_block.elect_height());
        } else {
            ZJC_FATAL("get block with height failed: %u, %u, %lu",
                network::kRootCongressNetworkId,
                common::kRootChainPoolIndex,
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

void BlockManager::HandleStatisticMessage(const transport::MessagePtr& msg_ptr) {
    if (create_statistic_tx_cb_ == nullptr || msg_ptr == nullptr) {
        return;
    }

    auto& heights = msg_ptr->header.block_proto().shard_statistic_tx();
    std::string statistic_hash;
    std::string cross_hash;
    if (statistic_mgr_->StatisticWithHeights(
            heights,
            &statistic_hash,
            &cross_hash) != pools::kPoolsSuccess) {
        ZJC_DEBUG("error to txs sharding create statistic tx");
        return;
    }

    if (!statistic_hash.empty()) {
        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kShardStatistic);
        tx->set_value(statistic_hash);
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
        tx_ptr->tx_ptr->time_valid += 3000000lu;
        tx_ptr->tx_ptr->in_consensus = false;
        tx_ptr->tx_hash = statistic_hash;
        tx_ptr->timeout = common::TimeUtils::TimestampMs() + 20000lu;
        shard_statistic_tx_ = tx_ptr;
        ZJC_DEBUG("success add statistic tx: %s", common::Encode::HexEncode(statistic_hash).c_str());
    }

    if (!cross_hash.empty()) {
        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kShardCross);
        tx->set_value(cross_hash);
        tx->set_pubkey("");
        tx->set_step(pools::protobuf::kCross);
        auto gid = common::Hash::keccak256(
            cross_hash + std::to_string(common::GlobalInfo::Instance()->network_id()));
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        auto tx_ptr = std::make_shared<BlockTxsItem>();
        tx_ptr->tx_ptr = cross_tx_cb_(new_msg_ptr);
        tx_ptr->tx_ptr->time_valid += 3000000lu;
        tx_ptr->tx_ptr->in_consensus = false;
        tx_ptr->tx_hash = cross_hash;
        tx_ptr->timeout = common::TimeUtils::TimestampMs() + 20000lu;
        cross_statistic_tx_ = tx_ptr;
        ZJC_DEBUG("success add cross tx: %s", common::Encode::HexEncode(cross_hash).c_str());
    }
}

void BlockManager::RootCreateCrossTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        const pools::protobuf::ElectStatistic& elect_statistic,
        db::DbWriteBatch& db_batch) {
    if (elect_statistic.cross().crosses_size() <= 0) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto pool_index = block.network_id() % common::kImmutablePoolSize;
    msg_ptr->address_info = account_mgr_->pools_address_info(pool_index);
    auto tx = msg_ptr->header.mutable_tx_proto();
    tx->set_step(pools::protobuf::kRootCross);
    tx->set_pubkey("");
    tx->set_to(msg_ptr->address_info->addr());
    auto gid = common::Hash::keccak256(
        block_tx.gid() + "_" +
        std::to_string(block.height()) + "_" +
        std::to_string(block.pool_index()) + "_" +
        std::to_string(block.network_id()));
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    std::string cross_string_for_hash;
    cross_string_for_hash.reserve(elect_statistic.cross().crosses_size() * 48);
    for (int32_t i = 0; i < elect_statistic.cross().crosses_size(); ++i) {
        uint32_t src_shard = elect_statistic.cross().crosses(i).src_shard();
        uint32_t src_pool = elect_statistic.cross().crosses(i).src_pool();
        uint64_t height = elect_statistic.cross().crosses(i).height();
        uint32_t des_shard = elect_statistic.cross().crosses(i).des_shard();
        cross_string_for_hash.append((char*)&src_shard, sizeof(src_shard));
        cross_string_for_hash.append((char*)&src_pool, sizeof(src_pool));
        cross_string_for_hash.append((char*)&height, sizeof(height));
        cross_string_for_hash.append((char*)&des_shard, sizeof(des_shard));
    }

    auto hash = common::Hash::keccak256(cross_string_for_hash);
    prefix_db_->SaveTemporaryKv(hash, elect_statistic.cross().SerializeAsString());
    tx->set_key(protos::kRootCross);
    tx->set_value(hash);
    pools_mgr_->HandleMessage(msg_ptr);
    ZJC_DEBUG("create cross tx %s",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str());
}

void BlockManager::HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        const pools::protobuf::ElectStatistic& elect_statistic,
        db::DbWriteBatch& db_batch) {
    if (create_elect_tx_cb_ == nullptr) {
        return;
    }
   
    if (prefix_db_->ExistsStatisticedShardingHeight(
            block.network_id(),
            block.timeblock_height())) {
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
    assert(block.network_id() == elect_statistic.sharding_id());
    if (network::kRootCongressNetworkId == common::GlobalInfo::Instance()->network_id() &&
            block.network_id() != network::kRootCongressNetworkId &&
            elect_statistic.cross().crosses_size() > 0) {
        // add cross shard statistic to root pool
        RootCreateCrossTx(block, block_tx, elect_statistic, db_batch);
    }

    ZJC_DEBUG("success handle statistic block net: %u, sharding: %u, pool: %u, height: %lu",
        block.network_id(), elect_statistic.sharding_id(), block.pool_index(), block.timeblock_height());
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
    shard_elect_tx->tx_ptr->time_valid += 3000000lu;
//     shard_elect_tx->tx_hash = gid;
    shard_elect_tx->timeout = common::TimeUtils::TimestampMs() + 20000lu;
    shard_elect_tx_[block.network_id()] = shard_elect_tx;
    ZJC_DEBUG("success add elect tx: %u, %lu, gid: %s", block.network_id(), block.timeblock_height(), common::Encode::HexEncode(gid).c_str());
}

void BlockManager::HandleToTxsMessage(const transport::MessagePtr& msg_ptr, bool recreate) {
    if (create_to_tx_cb_ == nullptr || msg_ptr == nullptr) {
        return;
    }

    if (!recreate) {
        to_txs_msg_ = msg_ptr;
        ZJC_DEBUG("now handle to tx messages.");
    }

    bool all_valid = true;
    auto now_time_ms = common::TimeUtils::TimestampMs();
    for (int32_t i = 0; i < msg_ptr->header.block_proto().to_txs_size(); ++i) {
        auto& heights = msg_ptr->header.block_proto().to_txs(i);
        if (to_txs_[heights.sharding_id()] != nullptr) {
            ZJC_DEBUG("to txs sharding not consensus yet: %u", heights.sharding_id());
            continue;
        }

        std::string tos_hash;
        if (to_txs_pool_->CreateToTxWithHeights(
                heights.sharding_id(),
                heights,
                &tos_hash) != pools::kPoolsSuccess) {
            all_valid = false;
            ZJC_DEBUG("error to txs sharding create to txs: %u", heights.sharding_id());
            continue;
        }

        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        new_msg_ptr->address_info = account_mgr_->pools_address_info(
            heights.sharding_id() % common::kImmutablePoolSize);
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kNormalTos);
        tx->set_value(tos_hash);
        tx->set_pubkey("");
        tx->set_to(new_msg_ptr->address_info->addr());
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            tx->set_step(pools::protobuf::kRootCreateAddressCrossSharding);
        } else {
            tx->set_step(pools::protobuf::kNormalTo);
        }

        auto gid = common::Hash::keccak256(
            tos_hash + std::to_string(heights.sharding_id()));
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        auto to_txs_ptr = std::make_shared<BlockTxsItem>();
        to_txs_ptr->tx_ptr = create_to_tx_cb_(new_msg_ptr);
        to_txs_ptr->tx_ptr->time_valid += 3000000lu;
        to_txs_ptr->tx_hash = tos_hash;
        to_txs_ptr->timeout = now_time_ms + 30000lu;
        to_txs_[heights.sharding_id()] = to_txs_ptr;
        ZJC_DEBUG("success add txs: %s", common::Encode::HexEncode(tos_hash).c_str());
    }

    if (all_valid) {
        to_txs_msg_ = nullptr;
    }
}

pools::TxItemPtr BlockManager::GetCrossTx(uint32_t pool_index, bool leader) {
    if (cross_statistic_tx_ != nullptr && !cross_statistic_tx_->tx_ptr->in_consensus) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (leader && cross_statistic_tx_->tx_ptr->time_valid > now_tm) {
            return nullptr;
        }

        cross_statistic_tx_->tx_ptr->msg_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto* tx = cross_statistic_tx_->tx_ptr->msg_ptr->header.mutable_tx_proto();
        tx->set_to(cross_statistic_tx_->tx_ptr->msg_ptr->address_info->addr());
        cross_statistic_tx_->tx_ptr->in_consensus = true;
        return cross_statistic_tx_->tx_ptr;
    }

    return nullptr;
}

pools::TxItemPtr BlockManager::GetStatisticTx(uint32_t pool_index, bool leader) {
    if (shard_statistic_tx_ != nullptr && !shard_statistic_tx_->tx_ptr->in_consensus) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (leader && shard_statistic_tx_->tx_ptr->time_valid > now_tm) {
            return nullptr;
        }

        shard_statistic_tx_->tx_ptr->msg_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto* tx = shard_statistic_tx_->tx_ptr->msg_ptr->header.mutable_tx_proto();
        tx->set_to(shard_statistic_tx_->tx_ptr->msg_ptr->address_info->addr());
        shard_statistic_tx_->tx_ptr->in_consensus = true;
        return shard_statistic_tx_->tx_ptr;
    }

    return nullptr;
}

pools::TxItemPtr BlockManager::GetElectTx(uint32_t pool_index, const std::string& tx_hash) {
    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        if (shard_elect_tx_[i] == nullptr) {
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i];
        ZJC_DEBUG("success get elect tx pool: %u, net: %d", pool_index, i);
        if (shard_elect_tx != nullptr && !shard_elect_tx->tx_ptr->in_consensus) {
            if (!tx_hash.empty()) {
                if (shard_elect_tx->tx_ptr->tx_hash == tx_hash) {
                    shard_elect_tx->tx_ptr->in_consensus = true;
                    return shard_elect_tx->tx_ptr;
                }

                continue;
            }

            auto now_tm = common::TimeUtils::TimestampUs();
            if (shard_elect_tx->tx_ptr->time_valid > now_tm) {
                continue;
            }

            shard_elect_tx->tx_ptr->in_consensus = true;
            return shard_elect_tx->tx_ptr;
        }
    }

    return nullptr;
}


pools::TxItemPtr BlockManager::GetToTx(uint32_t pool_index, bool leader) {
    auto now_tm = common::TimeUtils::TimestampUs();
    for (uint32_t i = prev_pool_index_; i <= max_consensus_sharding_id_; ++i) {
        uint32_t mod_idx = i % common::kImmutablePoolSize;
        if (mod_idx == pool_index) {
            auto tmp_to_txs = to_txs_[i];
            if (tmp_to_txs != nullptr && !tmp_to_txs->tx_ptr->in_consensus) {
                if (leader && tmp_to_txs->tx_ptr->time_valid > now_tm) {
                    continue;
                }

                tmp_to_txs->tx_ptr->in_consensus = true;
                prev_pool_index_ = i + 1;
                return tmp_to_txs->tx_ptr;
            }
        }
    }

    for (uint32_t i = network::kRootCongressNetworkId; i < prev_pool_index_; ++i) {
        uint32_t mod_idx = i % common::kImmutablePoolSize;
        if (mod_idx == pool_index) {
            auto tmp_to_txs = to_txs_[i];
            if (tmp_to_txs != nullptr && !tmp_to_txs->tx_ptr->in_consensus) {
                if (leader && tmp_to_txs->tx_ptr->time_valid > now_tm) {
                    continue;
                }

                tmp_to_txs->tx_ptr->in_consensus = true;
                prev_pool_index_ = i + 1;
                return tmp_to_txs->tx_ptr;
            }
        }
    }

    return nullptr;
}

void BlockManager::OnTimeBlock(
        uint8_t thread_idx,
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    latest_timeblock_height_ = latest_time_block_height;
    CreateStatisticTx(thread_idx);
}

void BlockManager::CreateStatisticTx(uint8_t thread_idx) {
    // check this node is leader
    if (to_tx_leader_ == nullptr) {
        ZJC_DEBUG("leader null");
        return;
    }

    if (local_id_ != to_tx_leader_->id) {
        ZJC_DEBUG("not leader local_id_: %s, to tx leader: %s",
            common::Encode::HexEncode(local_id_).c_str(),
            common::Encode::HexEncode(to_tx_leader_->id).c_str());
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_create_statistic_tx_ms_ >= now_tm_ms) {
        return;
    }

    if (latest_timeblock_height_ <= consensused_timeblock_height_) {
        return;
    }

    if (shard_statistic_tx_ != nullptr && shard_statistic_tx_->tx_ptr->in_consensus) {
        return;
    }

    if (cross_statistic_tx_ != nullptr && cross_statistic_tx_->tx_ptr->in_consensus) {
        return;
    }

    if (shard_statistic_tx_ != nullptr && shard_statistic_tx_->timeout >= now_tm_ms) {
        shard_statistic_tx_ = nullptr;
    }

    if (cross_statistic_tx_ != nullptr && cross_statistic_tx_->timeout >= now_tm_ms) {
        cross_statistic_tx_ = nullptr;
    }

    prev_create_statistic_tx_ms_ = now_tm_ms + kCreateToTxPeriodMs;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kBlockMessage);
    auto& block_msg = *msg.mutable_block_proto();
    pools::protobuf::ToTxHeights& to_heights = *block_msg.mutable_shard_statistic_tx();
    int res = statistic_mgr_->LeaderCreateStatisticHeights(to_heights);
    if (res != pools::kPoolsSuccess || to_heights.heights_size() <= 0) {
        ZJC_WARN("leader create statistic heights failed!");
        return;
    }

    // send to other nodes
    auto& broadcast = *msg.mutable_broadcast();
    msg_ptr->thread_idx = thread_idx;
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    network::Route::Instance()->Send(msg_ptr);
    HandleStatisticMessage(msg_ptr);
    ZJC_DEBUG("leader success broadcast statistic heights.");
}

void BlockManager::CreateToTx(uint8_t thread_idx) {
    if (create_to_tx_cb_ == nullptr) {
        return;
    }

    // check this node is leader
    if (to_tx_leader_ == nullptr) {
        ZJC_DEBUG("leader null");
        return;
    }

    if (local_id_ != to_tx_leader_->id) {
        ZJC_DEBUG("not leader");
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_create_to_tx_ms_ >= now_tm_ms) {
        return;
    }

    prev_create_to_tx_ms_ = now_tm_ms + kCreateToTxPeriodMs;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kBlockMessage);
    auto& block_msg = *msg.mutable_block_proto();
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        auto tmp_to_txs = to_txs_[i];
        if (tmp_to_txs != nullptr && tmp_to_txs->tx_ptr->in_consensus) {
            continue;
        }

        if (tmp_to_txs != nullptr && tmp_to_txs->timeout >= now_tm_ms) {
            to_txs_[i] = nullptr;
        }

        pools::protobuf::ToTxHeights& to_heights = *block_msg.add_to_txs();
        int res = to_txs_pool_->LeaderCreateToHeights(i, to_heights);
        if (res != pools::kPoolsSuccess || to_heights.heights_size() <= 0) {
            block_msg.mutable_to_txs()->RemoveLast();
        }
    }

    if (block_msg.to_txs_size() <= 0) {
        return;
    }
    
    // send to other nodes
    auto& broadcast = *msg.mutable_broadcast();
    msg_ptr->thread_idx = thread_idx;
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    network::Route::Instance()->Send(msg_ptr);
    HandleToTxsMessage(msg_ptr, false);
}

}  // namespace block

}  // namespace zjchain
