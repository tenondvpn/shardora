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

namespace zjchain {

namespace block {

static const std::string kShardElectPrefix = common::Encode::HexDecode(
    "227a252b30589b8ed984cf437c475b069d0597fc6d51ec6570e95a681ffa9fe2");

BlockManager::BlockManager(transport::MultiThreadHandler& net_handler) : net_handler_(net_handler) {
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
        DbBlockCallback new_block_callback,
        block::BlockAggValidCallback block_agg_valid_func) {
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
    block_agg_valid_func_ = block_agg_valid_func;
    if (common::GlobalInfo::Instance()->for_ck_server()) {
        ck_client_ = std::make_shared<ck::ClickHouseClient>("127.0.0.1", "", "", db);
        ZJC_DEBUG("support ck");
    }

    consensus_block_queues_ = new common::ThreadSafeQueue<BlockToDbItemPtr>[
        common::GlobalInfo::Instance()->message_handler_thread_count()];
    network::Route::Instance()->RegisterMessage(
        common::kBlockMessage,
        std::bind(&BlockManager::HandleMessage, this, std::placeholders::_1));
    test_sync_block_tick_.CutOff(
        100000lu,
        std::bind(&BlockManager::ConsensusTimerMessage, this, std::placeholders::_1));
    bool genesis = false;
    return kBlockSuccess;
}

void BlockManager::ConsensusTimerMessage(uint8_t thread_idx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto now_tm = common::TimeUtils::TimestampUs();
    if (now_tm > prev_to_txs_tm_us_ + 5000000) {
        if (leader_to_txs_.size() >= 4) {
            leader_to_txs_.erase(leader_to_txs_.begin());
        }

        for (auto iter = leader_to_txs_.begin(); iter != leader_to_txs_.end(); ++iter) {
            auto msg_ptr = iter->second->to_txs_msg;
            if (msg_ptr == nullptr) {
                continue;
            }

            HandleToTxsMessage(msg_ptr, true);
        }

        prev_to_txs_tm_us_ = now_tm;
    }

    auto now_tm1 = common::TimeUtils::TimestampUs();
    HandleToTxMessage();
    HandleStatisticTxMessage();
    HandleAllNewBlock(thread_idx);
    auto tmp_to_tx_leader = to_tx_leader_;
    if (tmp_to_tx_leader != nullptr && local_id_ == tmp_to_tx_leader->id) {
        ZJC_DEBUG("now leader create to and statistic message.");
        CreateToTx(thread_idx);
        CreateStatisticTx(thread_idx);
    }

    if (!leader_statistic_txs_.empty() && prev_retry_create_statistic_tx_ms_ < now_tm_ms) {
        if (leader_statistic_txs_.size() >= 4) {
            leader_statistic_txs_.erase(leader_statistic_txs_.begin());
        }

        for (auto iter = leader_statistic_txs_.rbegin(); iter != leader_statistic_txs_.rend(); ++iter) {
            auto tmp_ptr = iter->second->statistic_msg;
            if (tmp_ptr != nullptr) {
                StatisticWithLeaderHeights(tmp_ptr, true);
                break;
            }
        }

        prev_retry_create_statistic_tx_ms_ = now_tm_ms + kRetryStatisticPeriod;
    }

    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 10) {
        ZJC_DEBUG("BlockManager handle message use time: %lu", (etime - now_tm_ms));
    }

    test_sync_block_tick_.CutOff(100000lu, std::bind(&BlockManager::ConsensusTimerMessage, this, std::placeholders::_1));
}

void BlockManager::OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, common::MembersPtr& members) {
    if (sharding_id > max_consensus_sharding_id_) {
        max_consensus_sharding_id_ = sharding_id;
    }

    if (sharding_id == common::GlobalInfo::Instance()->network_id() ||
            sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        if (elect_height <= latest_elect_height_) {
            return;
        }

        for (auto iter = members->begin(); iter != members->end(); ++iter) {
            if ((*iter)->pool_index_mod_num == 0) {
                to_tx_leader_.reset();
                to_tx_leader_ = *iter;
                ZJC_DEBUG("success get leader: %u, %s",
                    sharding_id,
                    common::Encode::HexEncode(to_tx_leader_->id).c_str());
                break;
            }
        }

        latest_members_ = members;
        latest_elect_height_ = elect_height;
        latest_cross_statistic_tx_ = nullptr;
        latest_shard_statistic_tx_ = nullptr;
        statistic_message_ = nullptr;
        leader_statistic_txs_.clear();
    }
}

void BlockManager::ChangeLeader(int32_t mod_num, common::BftMemberPtr& mem_ptr) {
    if (mod_num == 0) {
        to_tx_leader_.reset();
        to_tx_leader_ = mem_ptr;
        ZJC_DEBUG("success change leader: %u, %s",
            common::GlobalInfo::Instance()->network_id(),
            common::Encode::HexEncode(to_tx_leader_->id).c_str());
    }
}

void BlockManager::HandleToTxMessage() {
    std::shared_ptr<transport::TransportMessage> msg_ptr = nullptr;
    if (!to_tx_msg_queue_.pop(&msg_ptr)) {
        return;
    }

    if (latest_members_ == nullptr) {
        return;
    }

    if (!msg_ptr->header.has_sign()) {
        assert(false);
        return;
    }

    if (latest_members_->size() <= msg_ptr->header.block_proto().shard_to().leader_idx()) {
        return;
    }

    auto& mem_ptr = (*latest_members_)[msg_ptr->header.block_proto().shard_to().leader_idx()];
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
//         assert(false);
        return;
    }

    HandleToTxsMessage(msg_ptr, false);
}

void BlockManager::HandleStatisticTxMessage() {
    std::shared_ptr<transport::TransportMessage> msg_ptr = nullptr;
    if (!statistic_tx_msg_queue_.pop(&msg_ptr)) {
        return;
    }

    ZJC_DEBUG("handle statistic message hash: %lu", msg_ptr->header.hash64());
    if (latest_members_ == nullptr) {
        ZJC_DEBUG("failed statistic message hash: %lu", msg_ptr->header.hash64());
        return;
    }

    if (!msg_ptr->header.has_sign()) {
        assert(false);
        return;
    }

    if (latest_members_->size() <= msg_ptr->header.block_proto().statistic_tx().leader_idx()) {
        ZJC_DEBUG("failed statistic message hash: %lu", msg_ptr->header.hash64());
        return;
    }

    auto& mem_ptr = (*latest_members_)[msg_ptr->header.block_proto().statistic_tx().leader_idx()];
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        return;
    }

    HandleStatisticMessage(msg_ptr);
}

void BlockManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    if (msg_ptr->header.block_proto().has_shard_to() > 0) {
        to_tx_msg_queue_.push(msg_ptr);
        ZJC_DEBUG("queue size to_tx_msg_queue_: %d", to_tx_msg_queue_.size());
    }

    if (msg_ptr->header.block_proto().has_statistic_tx()) {
        statistic_tx_msg_queue_.push(msg_ptr);
        ZJC_DEBUG("queue size statistic_tx_msg_queue_: %d", statistic_tx_msg_queue_.size());
    }

    if (msg_ptr->header.has_block()) {
        auto& header = msg_ptr->header;
        auto local_net = common::GlobalInfo::Instance()->network_id();
        if (local_net >= network::kConsensusShardEndNetworkId) {
            local_net -= network::kConsensusWaitingShardOffset;
        }

        if (header.block().network_id() == local_net) {
            ZJC_DEBUG("network block failed cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
                header.block().network_id(),
                header.block().pool_index(),
                header.block().height(),
                header.block().tx_list_size(),
                common::Encode::HexEncode(header.block().hash()).c_str());
            return;
        }

        auto block_ptr = std::make_shared<block::protobuf::Block>(header.block());
        if (block_agg_valid_func_(msg_ptr->thread_idx, *block_ptr)) {
            // just one thread
            block_from_network_queue_.push(block_ptr);
            ZJC_DEBUG("queue size add new block message hash: %lu, block_from_network_queue_ size: %d", msg_ptr->header.hash64(), block_from_network_queue_.size());
        }
    }
}

void BlockManager::HandleAllNewBlock(uint8_t thread_idx) {
    while (block_from_network_queue_.size() > 0) {
        std::shared_ptr<block::protobuf::Block> block_ptr = nullptr;
        if (block_from_network_queue_.pop(&block_ptr)) {
            db::DbWriteBatch db_batch;
            AddNewBlock(thread_idx, block_ptr, db_batch);
        }
    }

    HandleAllConsensusBlocks(thread_idx);
}

void BlockManager::GenesisNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    db::DbWriteBatch db_batch;
    AddNewBlock(thread_idx, block_item, db_batch);
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

void BlockManager::CheckWaitingBlocks(uint8_t thread_idx, uint32_t shard, uint64_t elect_height) {
    auto net_iter = waiting_check_sign_blocks_.find(shard);
    if (net_iter == waiting_check_sign_blocks_.end()) {
        return;
    }

    auto height_iter = net_iter->second.find(elect_height);
    if (height_iter == net_iter->second.end()) {
        return;
    }

    while (!height_iter->second.empty()) {
        auto& block_item = height_iter->second.front();
        height_iter->second.pop();
        if (block_agg_valid_func_ != nullptr && !block_agg_valid_func_(thread_idx, *block_item)) {
            ZJC_ERROR("verification agg sign failed hash: %s, signx: %s, "
                "net: %u, pool: %u, height: %lu",
                common::Encode::HexEncode(block_item->hash()).c_str(),
                common::Encode::HexEncode(block_item->bls_agg_sign_x()).c_str(),
                block_item->network_id(),
                block_item->pool_index(),
                block_item->height());
            continue;
        }

        block_from_network_queue_.push(block_item);
    }
}

int BlockManager::NetworkNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (block_item != nullptr) {
        if (!block_item->is_commited_block()) {
            ZJC_ERROR("not cross block coming: %s, signx: %s, net: %u, pool: %u, height: %lu",
                common::Encode::HexEncode(block_item->hash()).c_str(),
                common::Encode::HexEncode(block_item->bls_agg_sign_x()).c_str(),
                block_item->network_id(),
                block_item->pool_index(),
                block_item->height());
            assert(false);
            return kBlockError;
        }

        if (block_agg_valid_func_ != nullptr && !block_agg_valid_func_(thread_idx, *block_item)) {
            ZJC_ERROR("verification agg sign failed hash: %s, signx: %s, net: %u, pool: %u, height: %lu",
                common::Encode::HexEncode(block_item->hash()).c_str(),
                common::Encode::HexEncode(block_item->bls_agg_sign_x()).c_str(),
                block_item->network_id(),
                block_item->pool_index(),
                block_item->height());
            //assert(false);
            AddWaitingCheckSignBlock(block_item);
            return kBlockVerifyAggSignFailed;
        }

        CheckWaitingBlocks(thread_idx, block_item->network_id(), block_item->electblock_height());
        block_from_network_queue_.push(block_item);
    }

    return kBlockSuccess;
}

void BlockManager::ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item) {
    consensus_block_queues_[thread_idx].push(block_item);
    ZJC_DEBUG("queue size thread_idx: %d consensus_block_queues_: %d", thread_idx, consensus_block_queues_[thread_idx].size());
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
        account_info->set_sharding_id(des_sharding_id);
        account_info->set_latest_height(block_item->height());
        account_info->set_balance(tx_list[i].balance());
        ZJC_DEBUG("genesis add new account %s : %lu, shard: %u",
            common::Encode::HexEncode(account_info->addr()).c_str(),
            account_info->balance(),
            des_sharding_id);
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
            std::string cross_val;
            if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &cross_val)) {
                assert(false);
                break;
            }

            if (latest_cross_statistic_tx_ != nullptr &&
                    latest_cross_statistic_tx_->tx_hash == block_tx.storages(i).val_hash()) {
                latest_cross_statistic_tx_ = nullptr;
            }

            pools::protobuf::CrossShardStatistic cross_statistic;
            if (!cross_statistic.ParseFromString(cross_val)) {
                assert(false);
                break;
            }

            auto iter = leader_statistic_txs_.find(cross_statistic.elect_height());
            if (iter != leader_statistic_txs_.end()) {
                iter->second->cross_statistic_tx = nullptr;
                ZJC_DEBUG("erase statistic elect height: %lu, hash: %s",
                    cross_statistic.elect_height(),
                    common::Encode::HexEncode(block_tx.storages(i).val_hash()).c_str());
                if (iter->second->shard_statistic_tx == nullptr) {
                    leader_statistic_txs_.erase(iter);
                }
            }

            for (int32_t i = 0; i < cross_statistic.crosses_size(); ++i) {
                ZJC_DEBUG("success handle cross tx block net: %u, pool: %u, height: %lu, "
                    "src shard: %u, src pool: %u, height: %lu, des shard: %lu",
                    block.network_id(), block.pool_index(), block.height(),
                    cross_statistic.crosses(i).src_shard(),
                    cross_statistic.crosses(i).src_pool(),
                    cross_statistic.crosses(i).height(),
                    cross_statistic.crosses(i).des_shard());
            }

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
    uint32_t net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    pools::protobuf::ElectStatistic elect_statistic;
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        if (block_tx.storages(i).key() == protos::kShardStatistic) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                continue;
            }

            if (!elect_statistic.ParseFromString(val)) {
                continue;
            }

            if (elect_statistic.sharding_id() != net_id) {
                continue;
            }

//             if (latest_timeblock_height_ == consensused_timeblock_height_) {
//                 ZJC_DEBUG("latest_timeblock_height_ == consensused_timeblock_height_ success erase statistic tx statistic elect height "
//                     ": %lu, net: %u, hash: %s, latest_shard_statistic_tx_ = null: %d",
//                     elect_statistic.elect_height(),
//                     net_id,
//                     common::Encode::HexEncode(block_tx.storages(i).val_hash()).c_str(),
//                     (latest_shard_statistic_tx_ == nullptr));
//                 latest_shard_statistic_tx_ = nullptr;
//                 statistic_message_ = nullptr;
//             }

            auto iter = leader_statistic_txs_.find(elect_statistic.elect_height());
            if (iter != leader_statistic_txs_.end()) {
                if (latest_shard_statistic_tx_ != nullptr &&
                        latest_shard_statistic_tx_.get() == iter->second->shard_statistic_tx.get()) {
                    latest_shard_statistic_tx_ = nullptr;
                    statistic_message_ = nullptr;
                }

                iter->second->shard_statistic_tx = nullptr;
                ZJC_DEBUG("success erase statistic tx statistic elect height "
                    ": %lu, net: %u, hash: %s, latest_shard_statistic_tx_ = null: %d",
                    elect_statistic.elect_height(),
                    net_id,
                    common::Encode::HexEncode(block_tx.storages(i).val_hash()).c_str(),
                    (latest_shard_statistic_tx_ == nullptr));
                if (iter->second->cross_statistic_tx == nullptr ||
                        net_id != network::kRootCongressNetworkId) {
                    leader_statistic_txs_.erase(iter);
                }
            }

            break;
        }
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        HandleStatisticBlock(thread_idx, block, block_tx, elect_statistic, db_batch);
    }
}

void BlockManager::HandleNormalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
//     ZJC_DEBUG("new normal to block coming.");
    std::string to_txs_str;
    ZJC_DEBUG("totx success add normal to tx gid coming: %s", common::Encode::HexEncode(tx.gid()).c_str());
    if (latest_to_tx_ != nullptr && tx.gid() == latest_to_tx_->to_tx->tx_hash) {
        ZJC_DEBUG("normal to tx gid coming: %s, des: %s",
            common::Encode::HexEncode(tx.gid()).c_str(),
            common::Encode::HexEncode(latest_to_tx_->to_tx->tx_hash).c_str());
        latest_to_tx_ = nullptr;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        ZJC_DEBUG("get normal to tx key: %s", tx.storages(i).key().c_str());
        if (tx.storages(i).key() != protos::kNormalToShards) {
            continue;
        }

        if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &to_txs_str)) {
            ZJC_WARN("normal to get val hash failed: %s",
                common::Encode::HexEncode(tx.storages(0).val_hash()).c_str());
            continue;
        }

        pools::protobuf::ToTxMessage to_txs;
        if (!to_txs.ParseFromString(to_txs_str)) {
            ZJC_WARN("parse to txs failed.");
            continue;
        }

        auto iter = leader_to_txs_.find(to_txs.elect_height());
        if (iter != leader_to_txs_.end()) {
            iter->second->to_tx = nullptr;
            leader_to_txs_.erase(iter);
        }

        if (tx.step() == pools::protobuf::kRootCreateAddressCrossSharding) {
            if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId ||
                    common::GlobalInfo::Instance()->network_id() ==
                    network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset) {
                ZJC_WARN("sharding step invalid: %u, %u, to hash: %s",
                    to_txs.to_heights().sharding_id(),
                    common::GlobalInfo::Instance()->network_id(),
                    common::Encode::HexEncode(to_txs.heights_hash()).c_str());
                continue;
            }
        }

        if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
            if (to_txs.to_heights().sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                ZJC_WARN("sharding invalid: %u, %u, to hash: %s",
                    to_txs.to_heights().sharding_id(),
                    common::GlobalInfo::Instance()->network_id(),
                    common::Encode::HexEncode(to_txs.heights_hash()).c_str());
    //             assert(false);
                continue;
            }

            ZJC_DEBUG("success add local transfer tx tos hash: %s", common::Encode::HexEncode(tx.storages(0).val_hash()).c_str());
            HandleLocalNormalToTx(thread_idx, to_txs, tx.step(), tx.storages(0).val_hash());
        } else {
            RootHandleNormalToTx(thread_idx, block.height(), to_txs, db_batch);
        }
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
        if (tos_item.step() == pools::protobuf::kContractCreate) {
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
        msg_ptr->thread_idx = thread_idx;
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("create new address %s, amount: %lu, gid: %s",
            common::Encode::HexEncode(tos_item.des()).c_str(),
            tos_item.amount(),
            common::Encode::HexEncode(gid).c_str());
    }
}

std::shared_ptr<address::protobuf::AddressInfo> BlockManager::GetAccountInfo(
        const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
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

        auto account_info = GetAccountInfo(addr);
        if (account_info == nullptr) {
            if (step != pools::protobuf::kRootCreateAddressCrossSharding) {
//                 assert(false);
                ZJC_WARN("failed add local transfer tx tos heights_hash: %s, id: %s",
                    common::Encode::HexEncode(heights_hash).c_str(),
                    common::Encode::HexEncode(addr).c_str());
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
            ZJC_DEBUG("root create address coming %s, shard: %u, pool: %u",
                common::Encode::HexEncode(addr).c_str(), sharding_id, pool_index);
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
            ZJC_DEBUG("heights_hash: %s, ammount success add local transfer to %s, %lu",
                common::Encode::HexEncode(heights_hash).c_str(),
                common::Encode::HexEncode(iter->second.tos(i).des()).c_str(), amount);
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
        msg_ptr->thread_idx = thread_idx;
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("success add local transfer tx tos hash: %s, heights_hash: %s, gid: %s",
            common::Encode::HexEncode(tos_hash).c_str(),
            common::Encode::HexEncode(heights_hash).c_str(),
            common::Encode::HexEncode(gid).c_str());
    }
}

void BlockManager::AddNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch) {
    if (!block_item->is_commited_block()) {
        assert(false);
        return;
    }

    ZJC_DEBUG("new block coming sharding id: %u, pool: %d, height: %lu, "
        "tx size: %u, hash: %s, thread_idx: %d, elect height: %lu",
        block_item->network_id(),
        block_item->pool_index(),
        block_item->height(),
        block_item->tx_list_size(),
        common::Encode::HexEncode(block_item->hash()).c_str(),
        thread_idx,
        block_item->electblock_height());
    // TODO: check all block saved success
    assert(block_item->electblock_height() >= 1);
    if (!prefix_db_->SaveBlock(*block_item, db_batch)) {
        ZJC_DEBUG("block saved: %lu", block_item->height());
        return;
    }

    to_txs_pool_->NewBlock(block_item, db_batch);
    if (ck_client_ != nullptr) {
        ck_client_->AddNewBlock(block_item);
        ZJC_DEBUG("add to ck.");
    }

    if (block_item->pool_index() == common::kRootChainPoolIndex) {
        if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
                block_item->network_id() + network::kConsensusWaitingShardOffset !=
                common::GlobalInfo::Instance()->network_id()) {
            pools_mgr_->OnNewCrossBlock(thread_idx, block_item);
        }
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
        if (!new_block_callback_(thread_idx, block_item, db_batch)) {
            ZJC_DEBUG("block call back failed!");
            return;
        }
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
            ZJC_DEBUG("success handle kElectJoin tx: %s, net: %u, pool: %u, height: %lu",
                common::Encode::HexEncode(tx.from()).c_str(), block.network_id(), block.pool_index(), block.height());
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
        auto account_info = GetAccountInfo(id);
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
        msg_ptr->thread_idx = thread_idx;
        pools_mgr_->HandleMessage(msg_ptr);
        ZJC_INFO("success create kConsensusLocalTos gid: %s", common::Encode::HexEncode(gid).c_str());
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
                elect_block.shard_network_id() % common::kImmutablePoolSize,
                elect_block.elect_height(),
                block) == kBlockSuccess) {
            if (new_block_callback_ != nullptr) {
                new_block_callback_(thread_idx, elect_block_ptr, db_batch);
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

void BlockManager::StatisticWithLeaderHeights(const transport::MessagePtr& msg_ptr, bool retry) {
    auto& heights = msg_ptr->header.block_proto().statistic_tx().statistic();
    std::string height_str;
    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        height_str += std::to_string(heights.heights(i)) + " ";
    }

    ZJC_DEBUG("now statistic with leader heights retry: %d, "
        "elect height: %lu, local elect_height: %lu, leader idx: %u, to idx: %u, heights: %s",
        retry,
        msg_ptr->header.block_proto().statistic_tx().elect_height(),
        latest_elect_height_,
        msg_ptr->header.block_proto().statistic_tx().leader_idx(),
        msg_ptr->header.block_proto().statistic_tx().leader_to_idx(),
        height_str.c_str());
    if (msg_ptr->header.block_proto().statistic_tx().elect_height() < latest_elect_height_) {
        ZJC_DEBUG("elect height error: %lu, %lu", msg_ptr->header.block_proto().statistic_tx().elect_height(), latest_elect_height_);
        return;
    }

    if (create_statistic_tx_cb_ == nullptr) {
        return;
    }

    std::shared_ptr<LeaderWithStatisticTxItem> statistic_item = nullptr;
    auto iter = leader_statistic_txs_.find(msg_ptr->header.block_proto().statistic_tx().elect_height());
    if (iter != leader_statistic_txs_.end()) {
        statistic_item = iter->second;
    } else {
        statistic_item = std::make_shared<LeaderWithStatisticTxItem>();
        statistic_item->leader_idx = msg_ptr->header.block_proto().statistic_tx().leader_idx();
        statistic_item->leader_to_index = msg_ptr->header.block_proto().statistic_tx().leader_to_idx();
        statistic_item->statistic_msg = msg_ptr;
        leader_statistic_txs_[msg_ptr->header.block_proto().statistic_tx().elect_height()] = statistic_item;
    }

    if (msg_ptr->header.block_proto().statistic_tx().leader_idx() == statistic_item->leader_idx &&
            msg_ptr->header.block_proto().statistic_tx().leader_to_idx() > statistic_item->leader_to_index) {
        statistic_item->statistic_msg = msg_ptr;
    }

    if (statistic_item->shard_statistic_tx != nullptr &&
            statistic_item->leader_to_index >= msg_ptr->header.block_proto().statistic_tx().leader_to_idx()) {
        ZJC_DEBUG("leader index error: %u, %u", statistic_item->leader_to_index, msg_ptr->header.block_proto().statistic_tx().leader_to_idx());
        return;
    }

    if (msg_ptr->header.block_proto().statistic_tx().elect_height() != latest_elect_height_) {
        ZJC_DEBUG("elect height invalid and retry later local: %lu, leader: %lu",
            msg_ptr->header.block_proto().statistic_tx().elect_height(), latest_elect_height_);
        return;
    }

    auto now_time_ms = common::TimeUtils::TimestampMs();
    if (statistic_item->shard_statistic_tx != nullptr &&
            statistic_item->shard_statistic_tx->tx_ptr->in_consensus &&
            statistic_item->shard_statistic_tx->timeout > now_time_ms) {
        ZJC_DEBUG("statistic txs sharding not consensus yet: %u, %u, %lu, timeout: %lu, now: %lu",
            statistic_item->leader_idx,
            statistic_item->leader_to_index,
            msg_ptr->header.block_proto().statistic_tx().elect_height());
        return;
    }

    std::string statistic_hash;
    std::string cross_hash;
    if (statistic_mgr_->StatisticWithHeights(
            msg_ptr->header.block_proto().statistic_tx().elect_height(),
            heights,
            &statistic_hash,
            &cross_hash) != pools::kPoolsSuccess) {
        ZJC_DEBUG("error to txs sharding create statistic tx");
        statistic_item->shard_statistic_tx = nullptr;
        statistic_item->cross_statistic_tx = nullptr;
        return;
    }

    if (!statistic_hash.empty()) {
        if (statistic_item->shard_statistic_tx == nullptr ||
                statistic_item->shard_statistic_tx->tx_hash != statistic_hash) {
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
            tx_ptr->tx_ptr->time_valid += kStatisticValidTimeout;
            tx_ptr->tx_ptr->in_consensus = false;
            tx_ptr->tx_hash = statistic_hash;
            tx_ptr->timeout = common::TimeUtils::TimestampMs() + kStatisticTimeoutMs;
            tx_ptr->stop_consensus_timeout = tx_ptr->timeout + kStopConsensusTimeoutMs;
            statistic_item->shard_statistic_tx = tx_ptr;
            ZJC_INFO("success add statistic tx: %s, statistic elect height: %lu, "
                "heights: %s, timeout: %lu, kStatisticTimeoutMs: %lu, now: %lu, gid: %s",
                common::Encode::HexEncode(statistic_hash).c_str(),
                msg_ptr->header.block_proto().statistic_tx().elect_height(),
                height_str.c_str(), tx_ptr->timeout,
                kStatisticTimeoutMs, common::TimeUtils::TimestampMs(),
                common::Encode::HexEncode(gid).c_str());
        }
    }

    if (!cross_hash.empty()) {
        if (statistic_item->cross_statistic_tx == nullptr ||
                statistic_item->cross_statistic_tx->tx_hash != cross_hash) {
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
            tx_ptr->tx_ptr->time_valid += kStatisticValidTimeout;
            tx_ptr->tx_ptr->in_consensus = false;
            tx_ptr->tx_hash = cross_hash;
            tx_ptr->timeout = common::TimeUtils::TimestampMs() + kStatisticTimeoutMs;
            tx_ptr->stop_consensus_timeout = tx_ptr->timeout + kStopConsensusTimeoutMs;
            statistic_item->cross_statistic_tx = tx_ptr;
            ZJC_INFO("success add cross tx: %s, gid: %s",
                common::Encode::HexEncode(cross_hash).c_str(),
                common::Encode::HexEncode(gid).c_str());
        }
    }

    auto riter = leader_statistic_txs_.rbegin();
    if (riter != leader_statistic_txs_.rend() && riter->second->shard_statistic_tx != nullptr) {
        latest_shard_statistic_tx_ = riter->second->shard_statistic_tx;
        latest_cross_statistic_tx_ = riter->second->cross_statistic_tx;
        ZJC_DEBUG("success set statistic tx statistic elect height: %lu, statistic: %d, cross: %d, tx hash: %s",
            msg_ptr->header.block_proto().statistic_tx().elect_height(),
            (latest_shard_statistic_tx_ != nullptr),
            (latest_cross_statistic_tx_ != nullptr),
            common::Encode::HexEncode(latest_shard_statistic_tx_->tx_hash).c_str());
        if (statistic_item->elect_height == riter->second->elect_height) {
            assert(statistic_item->shard_statistic_tx->tx_hash == latest_shard_statistic_tx_->tx_hash);
        }
    }
}

void BlockManager::HandleStatisticMessage(const transport::MessagePtr& msg_ptr) {
    StatisticWithLeaderHeights(msg_ptr, false);
}

void BlockManager::RootCreateCrossTx(
        uint8_t thread_idx,
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
    msg_ptr->thread_idx = thread_idx;
    pools_mgr_->HandleMessage(msg_ptr);
    ZJC_INFO("create cross tx %s, gid: %s",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
        common::Encode::HexEncode(gid).c_str());
}

void BlockManager::HandleStatisticBlock(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& block_tx,
        const pools::protobuf::ElectStatistic& elect_statistic,
        db::DbWriteBatch& db_batch) {
    if (create_elect_tx_cb_ == nullptr) {
        ZJC_INFO("create_elect_tx_cb_ == nullptr");
        return;
    }
   
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
    assert(block.network_id() == elect_statistic.sharding_id());
    if (network::kRootCongressNetworkId == common::GlobalInfo::Instance()->network_id() &&
            block.network_id() != network::kRootCongressNetworkId &&
            elect_statistic.cross().crosses_size() > 0) {
        // add cross shard statistic to root pool
        RootCreateCrossTx(thread_idx, block, block_tx, elect_statistic, db_batch);
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
    shard_elect_tx->tx_ptr->time_valid += kElectValidTimeout;
    shard_elect_tx->timeout = common::TimeUtils::TimestampMs() + kElectTimeout;
    shard_elect_tx->stop_consensus_timeout = shard_elect_tx->timeout + kStopConsensusTimeoutMs;
    shard_elect_tx_[block.network_id()] = shard_elect_tx;
    ZJC_INFO("success add elect tx: %u, %lu, gid: %s, statistic elect height: %lu",
        block.network_id(), block.timeblock_height(),
        common::Encode::HexEncode(gid).c_str(),
        elect_statistic.elect_height());
}

void BlockManager::HandleToTxsMessage(const transport::MessagePtr& msg_ptr, bool recreate) {
    auto& shard_to = msg_ptr->header.block_proto().shard_to();
    auto& heights = shard_to.to_txs(0);
    std::string str_heights;
    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        str_heights += std::to_string(i) + "_" + std::to_string(heights.heights(i)) + " ";

    }

    ZJC_DEBUG("to tx message coming: %lu, elect height: %lu, heights: %s",
        msg_ptr->header.hash64(), shard_to.elect_height(), str_heights.c_str());
    if (create_to_tx_cb_ == nullptr || msg_ptr == nullptr) {
        return;
    }

    std::shared_ptr<LeaderWithToTxItem> leader_to_txs = nullptr;
    auto iter = leader_to_txs_.find(shard_to.elect_height());
    if (iter != leader_to_txs_.end()) {
        leader_to_txs = iter->second;
    } else {
        leader_to_txs = std::make_shared<LeaderWithToTxItem>();
        leader_to_txs->elect_height = shard_to.elect_height();
        leader_to_txs->leader_idx = shard_to.leader_idx();
        leader_to_txs_[shard_to.elect_height()] = leader_to_txs;
    }

    if (!recreate) {
        leader_to_txs->to_txs_msg = msg_ptr;
        ZJC_DEBUG("now handle to tx messages.");
    }

    auto tmp_tx = leader_to_txs->to_tx;
    ZJC_DEBUG("now handle to leader idx: %u, leader to index: %d, tmp_tx != nullptr: %d",
        shard_to.leader_idx(), shard_to.leader_to_idx(), (tmp_tx != nullptr));
    if (tmp_tx != nullptr && tmp_tx->success && tmp_tx->leader_to_index >= shard_to.leader_to_idx()) {
        ZJC_DEBUG("handled to leader idx: %u, leader to index: %d, tmp_tx != nullptr: %d, %u, %d",
            shard_to.leader_idx(), shard_to.leader_to_idx(),
            tmp_tx->success, (tmp_tx != nullptr), tmp_tx->leader_to_index);
        return;
    }

    auto now_time_ms = common::TimeUtils::TimestampMs();
    if (tmp_tx != nullptr &&
            tmp_tx->tx_ptr->in_consensus &&
            tmp_tx->timeout > now_time_ms) {
        ZJC_DEBUG("to txs sharding not consensus yet");
        return;
    }
    
    bool all_valid = true;
    if (!to_txs_pool_->StatisticTos(heights)) {
        ZJC_DEBUG("statistic tos failed!");
        return;
    }

    std::string tos_hashs;
    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id <= max_consensus_sharding_id_; ++sharding_id) {
        std::string tos_hash;
        if (to_txs_pool_->CreateToTxWithHeights(
                sharding_id,
                leader_to_txs->elect_height,
                heights,
                &tos_hash) != pools::kPoolsSuccess) {
            all_valid = false;
            continue;
        }

        tos_hashs += tos_hash;
    }

    if (tos_hashs.empty()) {
        return;
    }
    
    auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
    new_msg_ptr->address_info = account_mgr_->pools_address_info(0 % common::kImmutablePoolSize);
    auto* tx = new_msg_ptr->header.mutable_tx_proto();
    tx->set_key(protos::kNormalTos);
    tx->set_value(tos_hashs);
    tx->set_pubkey("");
    tx->set_to(new_msg_ptr->address_info->addr());
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        tx->set_step(pools::protobuf::kRootCreateAddressCrossSharding);
    } else {
        tx->set_step(pools::protobuf::kNormalTo);
    }

    auto gid = common::Hash::keccak256(tos_hashs);
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    auto to_txs_ptr = std::make_shared<BlockTxsItem>();
    to_txs_ptr->tx_ptr = create_to_tx_cb_(new_msg_ptr);
    to_txs_ptr->tx_ptr->time_valid += kToValidTimeout;
    to_txs_ptr->tx_hash = gid;
    to_txs_ptr->timeout = now_time_ms + kToValidTimeout + kToTimeoutMs;
    to_txs_ptr->stop_consensus_timeout = to_txs_ptr->timeout + kStopConsensusTimeoutMs;
    leader_to_txs->to_tx = to_txs_ptr;
    to_txs_ptr->success = true;
    to_txs_ptr->leader_to_index = shard_to.leader_to_idx();
    ZJC_DEBUG("totx success add txs: %s, leader idx: %u, leader to index: %d, gid: %s",
        common::Encode::HexEncode(tos_hashs).c_str(),
        shard_to.leader_idx(), shard_to.leader_to_idx(),
        common::Encode::HexEncode(gid).c_str());
    if (all_valid) {
        leader_to_txs->to_txs_msg = nullptr;
    }

    auto rbegin = leader_to_txs_.begin();
    if (rbegin != leader_to_txs_.end()) {
        latest_to_tx_ = rbegin->second;
        ZJC_DEBUG("set success add txs: %s, leader idx: %u, leader to index: %d, gid: %s, elect height: %lu",
            common::Encode::HexEncode(tos_hashs).c_str(),
            shard_to.leader_idx(), shard_to.leader_to_idx(),
            common::Encode::HexEncode(gid).c_str(),
            rbegin->first);
        assert(latest_to_tx_->to_tx != nullptr);
    }
}

pools::TxItemPtr BlockManager::GetCrossTx(uint32_t pool_index, bool leader) {
    auto& cross_statistic_tx = latest_cross_statistic_tx_;
    if (cross_statistic_tx != nullptr && !cross_statistic_tx->tx_ptr->in_consensus) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (leader && cross_statistic_tx->tx_ptr->time_valid > now_tm) {
            return nullptr;
        }

        cross_statistic_tx->tx_ptr->msg_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto* tx = cross_statistic_tx->tx_ptr->msg_ptr->header.mutable_tx_proto();
        tx->set_to(cross_statistic_tx->tx_ptr->msg_ptr->address_info->addr());
        cross_statistic_tx->tx_ptr->in_consensus = true;
        return cross_statistic_tx->tx_ptr;
    }

    return nullptr;
}

pools::TxItemPtr BlockManager::GetStatisticTx(uint32_t pool_index, bool leader) {
    auto shard_statistic_tx = latest_shard_statistic_tx_;
    if (shard_statistic_tx != nullptr) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (shard_statistic_tx->tx_ptr->in_consensus) {
            ZJC_DEBUG("get statistic tx failed is in consensus.");
            if (shard_statistic_tx->tx_ptr->timeout < now_tm) {
                shard_statistic_tx->tx_ptr->in_consensus = false;
            }
        }

        if (leader && shard_statistic_tx->tx_ptr->time_valid > now_tm) {
            return nullptr;
        }

        shard_statistic_tx->tx_ptr->msg_ptr->address_info =
            account_mgr_->pools_address_info(pool_index);
        auto* tx = shard_statistic_tx->tx_ptr->msg_ptr->header.mutable_tx_proto();
        tx->set_to(shard_statistic_tx->tx_ptr->msg_ptr->address_info->addr());
        shard_statistic_tx->tx_ptr->in_consensus = true;
        ZJC_DEBUG("success get statistic tx hash: %s",
            common::Encode::HexEncode(shard_statistic_tx->tx_hash).c_str());
        return shard_statistic_tx->tx_ptr;
    }

    return nullptr;
}

pools::TxItemPtr BlockManager::GetElectTx(uint32_t pool_index, const std::string& tx_hash) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        if (i % common::kImmutablePoolSize != pool_index) {
            continue;
        }

        if (shard_elect_tx_[i] == nullptr) {
//             if (tx_hash.empty() && (pool_index == 2 || pool_index == 3)) {
//                 ZJC_DEBUG("failed get elect tx: %u", pool_index);
//             }
            continue;
        }

        auto shard_elect_tx = shard_elect_tx_[i];
        if (tx_hash.empty() && (pool_index == 2 || pool_index == 3)) {
            ZJC_DEBUG("now get elect tx valid check: %u, in consensus: %d",
                pool_index, shard_elect_tx->tx_ptr->in_consensus);
        }

        if (!shard_elect_tx->tx_ptr->in_consensus) {
            if (!tx_hash.empty()) {
                if (shard_elect_tx->tx_ptr->tx_hash == tx_hash) {
                    shard_elect_tx->tx_ptr->in_consensus = true;
                    return shard_elect_tx->tx_ptr;
                }

                continue;
            }

            auto now_tm = common::TimeUtils::TimestampUs();
            if (shard_elect_tx->tx_ptr->time_valid > now_tm) {
                if (tx_hash.empty() && (pool_index == 2 || pool_index == 3)) {
                    ZJC_DEBUG("now get elect tx time invalid: %u", pool_index);
                }
                continue;
            }

            shard_elect_tx->tx_ptr->in_consensus = true;
            return shard_elect_tx->tx_ptr;
        }
    }

    return nullptr;
}

bool BlockManager::ShouldStopConsensus() {
    return false;
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto tmp_to_txs = latest_to_tx_;
    if (tmp_to_txs != nullptr && tmp_to_txs->to_tx != nullptr) {
        if (tmp_to_txs->to_tx->stop_consensus_timeout < now_tm_ms) {
            ZJC_DEBUG("to tx stop consensus timeout: %lu, %lu", tmp_to_txs->to_tx->stop_consensus_timeout, now_tm_ms);
            return true;
        }
    }

    auto& cross_statistic_tx = latest_cross_statistic_tx_;
    if (cross_statistic_tx != nullptr) {
        if (cross_statistic_tx->stop_consensus_timeout < now_tm_ms) {
            ZJC_DEBUG("shard cross tx stop consensus timeout: %lu, %lu", cross_statistic_tx->stop_consensus_timeout, now_tm_ms);
            return true;
        }
    }

    auto shard_statistic_tx = latest_shard_statistic_tx_;
    if (shard_statistic_tx != nullptr) {
        if (shard_statistic_tx->stop_consensus_timeout < now_tm_ms) {
            ZJC_DEBUG("shard statistic tx stop consensus timeout: %lu, %lu", shard_statistic_tx->stop_consensus_timeout, now_tm_ms);
            return true;
        }
    }

    for (uint32_t i = network::kRootCongressNetworkId; i <= max_consensus_sharding_id_; ++i) {
        auto shard_elect_tx = shard_elect_tx_[i];
        if (shard_elect_tx != nullptr) {
            if (shard_elect_tx->stop_consensus_timeout < now_tm_ms) {
                ZJC_DEBUG("shard elect tx stop consensus timeout: %lu, %lu", shard_statistic_tx->stop_consensus_timeout, now_tm_ms);
                return true;
            }
        }
    }


    return false;
}

pools::TxItemPtr BlockManager::GetToTx(uint32_t pool_index, bool leader) {
    if (!leader) {
        ZJC_DEBUG("backup get to tx coming!");
    }

    if (latest_to_tx_ == nullptr) {
        if (!leader) {
            ZJC_DEBUG("backup get to tx failed, latest_to_tx_ == nullptr!");
        }

        return nullptr;
    }

    if (pool_index != 0) {
        if (!leader) {
            ZJC_DEBUG("backup get to tx failed, pool_index != 0!");
        }

        return nullptr;
    }

    auto now_tm = common::TimeUtils::TimestampUs();
    auto latest_to_tx = latest_to_tx_;
    auto tmp_to_txs = latest_to_tx->to_tx;
    if (tmp_to_txs != nullptr && !tmp_to_txs->tx_ptr->in_consensus) {
        if (leader && tmp_to_txs->tx_ptr->time_valid > now_tm) {
            return nullptr;
        }

        tmp_to_txs->tx_ptr->in_consensus = true;
        return tmp_to_txs->tx_ptr;
    }

    if (tmp_to_txs != nullptr) {
        ZJC_DEBUG("get to tx failed in_consensus: %d", tmp_to_txs->tx_ptr->in_consensus);
    }

    if (!leader) {
        ZJC_DEBUG("backup get to tx failed elect height: %lu", latest_to_tx_->elect_height);
    }
    return nullptr;
}

void BlockManager::OnTimeBlock(
        uint8_t thread_idx,
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    ZJC_DEBUG("new timeblock coming: %lu, %lu", latest_timeblock_height_, latest_time_block_height);
    if (latest_timeblock_height_ >= latest_time_block_height) {
        return;
    }

    latest_timeblock_height_ = latest_time_block_height;
    latest_timeblock_tm_sec_ = lastest_time_block_tm;
    CreateStatisticTx(thread_idx);
}

void BlockManager::CreateStatisticTx(uint8_t thread_idx) {
    // check this node is leader
    auto tmp_to_tx_leader = to_tx_leader_;
    if (tmp_to_tx_leader == nullptr) {
        ZJC_DEBUG("leader null");
        return;
    }

    if (local_id_ != tmp_to_tx_leader->id) {
        ZJC_DEBUG("not leader local_id_: %s, to tx leader: %s",
            common::Encode::HexEncode(local_id_).c_str(),
            common::Encode::HexEncode(tmp_to_tx_leader->id).c_str());
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_create_statistic_tx_ms_ >= now_tm_ms) {
        return;
    }

    if (latest_timeblock_height_ <= consensused_timeblock_height_) {
//         ZJC_DEBUG("latest_timeblock_height_ <= consensused_timeblock_height_: %lu, %lu",
//             latest_timeblock_height_, consensused_timeblock_height_);
        return;
    }

    prev_create_statistic_tx_ms_ = now_tm_ms + kCreateToTxPeriodMs;
    if (statistic_message_ == nullptr) {
        statistic_message_ = std::make_shared<transport::TransportMessage>();
        auto& msg = statistic_message_->header;
        msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
        dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kBlockMessage);
        auto& block_msg = *msg.mutable_block_proto();
        block::protobuf::StatisticTxMessage& statistic_msg = *block_msg.mutable_statistic_tx();
        auto& to_heights = *statistic_msg.mutable_statistic();
        int res = statistic_mgr_->LeaderCreateStatisticHeights(to_heights);
        if (res != pools::kPoolsSuccess || to_heights.heights_size() <= 0) {
            ZJC_WARN("leader create statistic heights failed!");
            return;
        }

        statistic_msg.set_elect_height(latest_elect_height_);
        statistic_msg.set_leader_idx(tmp_to_tx_leader->index);
        // send to other nodes
    }
    
    statistic_message_->header.release_broadcast();
    statistic_message_->thread_idx = thread_idx;
    auto& msg = statistic_message_->header;
    auto& broadcast = *msg.mutable_broadcast();
    auto& block_msg = *msg.mutable_block_proto();
    block::protobuf::StatisticTxMessage& statistic_msg = *block_msg.mutable_statistic_tx();
    statistic_msg.set_leader_to_idx(leader_create_statistic_heights_index_++);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    if (security_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    statistic_message_->header.set_sign(sign);
    network::Route::Instance()->Send(statistic_message_);
    HandleStatisticMessage(statistic_message_);
    ZJC_DEBUG("leader success broadcast statistic heights elect height: %lu, leader_create_statistic_heights_index_: %d",
        latest_elect_height_, leader_create_statistic_heights_index_);
}

void BlockManager::CreateToTx(uint8_t thread_idx) {
#ifndef ZJC_UNITTEST
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) <
            common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
        return;
    }
#endif

    if (create_to_tx_cb_ == nullptr) {
        return;
    }

    // check this node is leader
    auto tmp_to_tx_leader = to_tx_leader_;
    if (tmp_to_tx_leader == nullptr) {
        ZJC_DEBUG("leader null");
        return;
    }

    if (local_id_ != tmp_to_tx_leader->id) {
        ZJC_DEBUG("not leader");
        return;
    }

    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_create_to_tx_ms_ >= now_tm_ms) {
        return;
    }

    if (latest_to_tx_ != nullptr &&
            latest_to_tx_->to_tx != nullptr &&
            latest_to_tx_->to_tx->timeout > now_tm_ms) {
        return;
    }

    ZJC_DEBUG("now create new to tx: %lu, now tm: %lu", prev_create_to_tx_ms_, now_tm_ms);
    prev_create_to_tx_ms_ = now_tm_ms + kCreateToTxPeriodMs;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kBlockMessage);
    auto& block_msg = *msg.mutable_block_proto();
    auto& shard_to = *block_msg.mutable_shard_to();
    auto iter = leader_to_txs_.find(latest_elect_height_);
    if (iter != leader_to_txs_.end()) {
        auto tmp_to_txs = iter->second->to_tx;
        if (tmp_to_txs != nullptr && tmp_to_txs->tx_ptr->in_consensus) {
            return;
        }

        if (tmp_to_txs != nullptr && tmp_to_txs->timeout >= now_tm_ms) {
            iter->second->to_tx = nullptr;
        }
    }

    auto& to_heights = *shard_to.add_to_txs();
    int res = to_txs_pool_->LeaderCreateToHeights(to_heights);
    if (res != pools::kPoolsSuccess || to_heights.heights_size() <= 0) {
        shard_to.mutable_to_txs()->RemoveLast();
    } else {
        shard_to.set_leader_idx(tmp_to_tx_leader->index);
    }

    if (shard_to.to_txs_size() <= 0) {
        return;
    }
    
    shard_to.set_leader_to_idx(leader_create_to_heights_index_++);
    // send to other nodes
    auto& broadcast = *msg.mutable_broadcast();
    msg_ptr->thread_idx = thread_idx;
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string sign;
    if (security_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    msg_ptr->header.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    HandleToTxsMessage(msg_ptr, false);
}

}  // namespace block

}  // namespace zjchain
