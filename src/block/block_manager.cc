#include "block/block_manager.h"

#include "block/block_utils.h"
#include "block/account_manager.h"
#include "block/block_proto.h"
#include "common/encode.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "network/route.h"
#include "protos/block.pb.h"
#include "protos/elect.pb.h"
#include "transport/processor.h"

namespace zjchain {

namespace block {

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
        const std::string& local_id) {
    account_mgr_ = account_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    local_id_ = local_id;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    to_txs_pool_ = std::make_shared<pools::ToTxsPools>(db_, local_id);
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
    NetworkNewBlock(msg_ptr->thread_idx, nullptr);
    if (to_tx_leader_ == nullptr) {
        return;
    }

    if (local_id_ != to_tx_leader_->id) {
        return;
    }

    CreateToTx(msg_ptr->thread_idx);
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
        HandleToTxsMessage(msg_ptr);
    }
}

void BlockManager::NetworkNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (block_item != nullptr) {
        db::DbWriteBatch db_batch;
        AddAllAccount(block_item, db_batch);
        AddNewBlock(thread_idx, block_item, db_batch);
    }

    HandleAllConsensusBlocks(thread_idx);
}

void BlockManager::ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item) {
    consensus_block_queues_[thread_idx].push(block_item);
}

void BlockManager::HandleAllConsensusBlocks(uint8_t thread_idx) {
    auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count();
    for (int32_t i = 0; i < thread_count; ++i) {
        while (consensus_block_queues_[i].size() > 0) {
            BlockToDbItemPtr db_item_ptr = nullptr;
            if (consensus_block_queues_[i].pop(&db_item_ptr)) {
                AddNewBlock(thread_idx, db_item_ptr->block_ptr, db_item_ptr->db_batch);
            }
        }
    }
}

void BlockManager::AddAllAccount(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch) {
    const auto& tx_list = block_item->tx_list();
    if (tx_list.empty()) {
        return;
    }

    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto account_info = account_mgr_->GetAcountInfo(block_item, tx_list[i]);
        ZJC_DEBUG("add new account %s : %lu",
            common::Encode::HexEncode(account_info->addr()).c_str(),
            account_info->balance());
        prefix_db_->AddAddressInfo(account_info->addr(), *account_info, db_batch);
    }
}


void BlockManager::HandleNormalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    if (tx.storages_size() != 1) {
        return;
    }

    std::string to_txs_str;
    if (!prefix_db_->GetTemporaryKv(tx.storages(0).val_hash(), &to_txs_str)) {
        return;
    }

    pools::protobuf::ToTxMessage to_txs;
    if (!to_txs.ParseFromString(to_txs_str)) {
        return;
    }

    to_txs_[to_txs.to_heights().sharding_id()] = nullptr;
    if (to_txs.to_heights().sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    std::unordered_map<std::string, std::pair<uint64_t, uint32_t>> addr_amount_map;
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
            if (to_txs.tos(i).amount() > 0) {
                // dispatch to txs to tx pool
                uint32_t sharding_id = common::kInvalidUint32;
                uint32_t pool_index = common::kInvalidUint32;
                auto account_info = account_mgr_->GetAcountInfo(thread_idx, to_txs.tos(i).des());
                if (account_info == nullptr) {
                    assert(false);
                    if (!to_txs.tos(i).has_sharding_id() || !to_txs.tos(i).has_pool_index()) {
                        continue;
                    }

                    if (to_txs.tos(i).sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                        continue;
                    }

                    if (to_txs.tos(i).pool_index() >= common::kImmutablePoolSize) {
                        continue;
                    }

                    sharding_id = to_txs.tos(i).sharding_id();
                    pool_index = to_txs.tos(i).pool_index();
                } else {
                    sharding_id = account_info->sharding_id();
                    pool_index = account_info->pool_index();
                }

                if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
                    continue;
                }

                auto iter = addr_amount_map.find(to_txs.tos(i).des());
                if (iter == addr_amount_map.end()) {
                    addr_amount_map[to_txs.tos(i).des()] = std::make_pair(to_txs.tos(i).amount(), pool_index);
                } else {
                    iter->second.first += to_txs.tos(i).amount();
                }
            }
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
    }

    for (auto iter = to_tx_map.begin(); iter != to_tx_map.end(); ++iter) {
        auto val = iter->second.SerializeAsString();
        auto tos_hash = common::Hash::keccak256(val);
        prefix_db_->SaveTemporaryKv(tos_hash, val);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto tx = msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kLocalNormalTos);
        tx->set_value(tos_hash);
        tx->set_pubkey("");
        tx->set_to("");
        tx->set_step(pools::protobuf::kConsensusLocalTos);
        auto gid = common::Hash::keccak256(tos_hash + std::to_string(iter->first));
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
//     ZJC_DEBUG("new block coming.");
    prefix_db_->SaveBlock(*block_item, db_batch);
    to_txs_pool_->NewBlock(*block_item, db_batch);
    if (ck_client_ != nullptr) {
        ck_client_->AddNewBlock(block_item);
        ZJC_DEBUG("add to ck.");
    }

    if (block_item->network_id() == common::GlobalInfo::Instance()->network_id()) {
        const auto& tx_list = block_item->tx_list();
        if (tx_list.empty()) {
            return;
        }

        // one block must be one consensus pool
        for (int32_t i = 0; i < tx_list.size(); ++i) {
            switch (tx_list[i].step()) {
            case pools::protobuf::kNormalTo:
                HandleNormalToTx(thread_idx, *block_item, tx_list[i], db_batch);
                break;
            default:
                break;
            }
        }
    }

    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        ZJC_FATAL("write block to db failed!");
    }
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

void BlockManager::ToTxsTimeout(uint32_t sharding_id) {
    to_txs_[sharding_id] = nullptr;
}

void BlockManager::HandleToTxsMessage(const transport::MessagePtr& msg_ptr) {
    if (create_to_tx_cb_ == nullptr) {
        return;
    }

    for (int32_t i = 0; i < msg_ptr->header.block_proto().to_txs_size(); ++i) {
        auto& heights = msg_ptr->header.block_proto().to_txs(i);
        pools::protobuf::ToTxHeights to_heights;
        if (to_txs_pool_->BackupCreateToTx(
                heights.sharding_id(),
                heights,
                &to_heights) != pools::kPoolsSuccess) {
            continue;
        }

        if (to_heights.tos_hash().empty()) {
            continue;
        }

        auto old_to_txs = to_txs_[heights.sharding_id()];
        if (old_to_txs != nullptr && old_to_txs->to_txs_hash == to_heights.tos_hash()) {
            continue;
        }

        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kNormalTos);
        tx->set_value(to_heights.tos_hash());
        tx->set_pubkey("");
        tx->set_to("");
        tx->set_step(pools::protobuf::kNormalTo);
        auto gid = common::Hash::keccak256(
            to_heights.tos_hash() + std::to_string(heights.sharding_id()));
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        auto to_txs_ptr = std::make_shared<ToTxsItem>();
        to_txs_ptr->tx_ptr = create_to_tx_cb_(new_msg_ptr);
        to_txs_ptr->to_txs_hash = to_heights.tos_hash();
        to_txs_ptr->tx_count = to_heights.tx_count();
        to_txs_[heights.sharding_id()] = to_txs_ptr;
        ZJC_DEBUG("follower success add txs");
    }
}

pools::TxItemPtr BlockManager::GetToTx(uint32_t pool_index) {
    for (uint32_t i = prev_pool_index_; i <= max_consensus_sharding_id_; ++i) {
        uint32_t mod_idx = i % common::kImmutablePoolSize;
        if (mod_idx == pool_index) {
            auto old_to_txs = to_txs_[i];
            if (old_to_txs != nullptr && !old_to_txs->in_consensus) {
                old_to_txs->in_consensus = true;
                prev_pool_index_ = i + 1;
                return old_to_txs->tx_ptr;
            }
        }
    }

    for (uint32_t i = network::kRootCongressNetworkId; i < prev_pool_index_; ++i) {
        uint32_t mod_idx = i % common::kImmutablePoolSize;
        if (mod_idx == pool_index) {
            auto old_to_txs = to_txs_[i];
            if (old_to_txs != nullptr && !old_to_txs->in_consensus) {
                old_to_txs->in_consensus = true;
                prev_pool_index_ = i + 1;
                return old_to_txs->tx_ptr;
            }
        }
    }

    return nullptr;
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
    for (uint32_t i = network::kRootCongressNetworkId;
            i <= max_consensus_sharding_id_; ++i) {
        auto old_to_txs = to_txs_[i];
        if (old_to_txs != nullptr && old_to_txs->in_consensus) {
            continue;
        }

        pools::protobuf::ToTxHeights& to_heights = *block_msg.add_to_txs();
        if (to_txs_pool_->LeaderCreateToTx(i, to_heights) != pools::kPoolsSuccess) {
            block_msg.mutable_to_txs()->RemoveLast();
            continue;
        }

        if (to_heights.tos_hash().empty()) {
            block_msg.mutable_to_txs()->RemoveLast();
            continue;
        }

        if (old_to_txs != nullptr && old_to_txs->to_txs_hash == to_heights.tos_hash()) {
            block_msg.mutable_to_txs()->RemoveLast();
            continue;
        }

        auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
        auto* tx = new_msg_ptr->header.mutable_tx_proto();
        tx->set_key(protos::kNormalTos);
        tx->set_value(to_heights.tos_hash());
        tx->set_pubkey("");
        tx->set_to("");
        tx->set_step(pools::protobuf::kNormalTo);
        auto gid = common::Hash::keccak256(to_heights.tos_hash() + std::to_string(i));
        tx->set_gas_limit(0);
        tx->set_amount(0);
        tx->set_gas_price(common::kBuildinTransactionGasPrice);
        tx->set_gid(gid);
        auto to_txs_ptr = std::make_shared<ToTxsItem>();
        to_txs_ptr->tx_ptr = create_to_tx_cb_(new_msg_ptr);
        to_txs_ptr->to_txs_hash = to_heights.tos_hash();
        to_txs_ptr->tx_count = to_heights.tx_count();
        to_txs_[i] = to_txs_ptr;
    }

    if (block_msg.to_txs_size() <= 0) {
        return;
    }
    
    // send to other nodes
    auto& broadcast = *msg.mutable_broadcast();
    broadcast.set_hop_limit(10);
    msg_ptr->thread_idx = thread_idx;
#ifndef ZJC_UNITTEST
    network::Route::Instance()->Send(msg_ptr);
#else
    // for test
    leader_to_txs_msg_ = msg_ptr;
#endif
}

}  // namespace block

}  // namespace zjchain
