#include "pools/tx_pool_manager.h"

#include "block/account_manager.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"
#include <common/time_utils.h>
#include <protos/pools.pb.h>

namespace shardora {

namespace pools {

TxPoolManager::TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager>& acc_mgr) {
    security_ = security;
    db_ = db;
    acc_mgr_ = acc_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    // prefix_db_->InitGidManager();
    kv_sync_ = kv_sync;
    cross_block_mgr_ = std::make_shared<CrossBlockManager>(db_, kv_sync_);
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(this, security_, i, db, kv_sync);
    }

    ZJC_INFO("TxPoolManager init success: %d", common::kInvalidPoolIndex);
    InitCrossPools();
    pop_message_thread_ = std::make_shared<std::thread>(
        &TxPoolManager::PopPoolsMessage, 
        this);
    // 每 10ms 会共识一次时间块
    tools_tick_.CutOff(
        10000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this));
    // 注册 kPoolsMessage 的回调函数
    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::HandleMessage, this, std::placeholders::_1));
}

TxPoolManager::~TxPoolManager() {
    destroy_ = true;
    pop_message_thread_->join();
    FlushHeightTree();
    if (tx_pool_ != nullptr) {
        delete []tx_pool_;
    }

    if (cross_pools_ != nullptr) {
        delete[] cross_pools_;
    }

    if (root_cross_pools_ ) {
        delete[] root_cross_pools_;
    }

    prefix_db_->Destroy();
}

void TxPoolManager::InitCrossPools() {
    cross_pools_ = new CrossPool[network::kConsensusShardEndNetworkId]; // shard 分片的个数
    for (uint32_t i = network::kConsensusShardBeginNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        cross_pools_[i].Init(i, db_, kv_sync_);
    }

    if (!IsRootNode()) {
        root_cross_pools_ = new RootCrossPool[common::kInvalidPoolIndex];
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            root_cross_pools_[i].Init(i, db_, kv_sync_);
        }
    }
}

int TxPoolManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    // ZJC_DEBUG("pools message fierwall coming.");
    // return transport::kFirewallCheckSuccess;
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    if (msg_ptr->header.has_sync_heights() && !msg_ptr->header.has_tx_proto()) {
        // TODO: check all message with valid signature
        ZJC_DEBUG("pools message fierwall coming is sync heights.");
        return transport::kFirewallCheckSuccess;
    }

    if (!tx_msg.has_sign() || !tx_msg.has_pubkey() ||
            tx_msg.sign().empty() || tx_msg.pubkey().empty()) {
        ZJC_DEBUG("pools check firewall message failed, invalid sign or pk. sign: %d, pk: %d, hash64: %lu", 
            tx_msg.sign().size(), tx_msg.pubkey().size(), header.hash64());
        return transport::kFirewallCheckError;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (security_->Verify(
            msg_ptr->msg_hash,
            tx_msg.pubkey(),
            tx_msg.sign()) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed!");
        return transport::kFirewallCheckError;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(security_->GetAddress(tx_msg.pubkey()));
    if (msg_ptr->address_info == nullptr) {
        ZJC_DEBUG("failed get account info: %s", 
            common::Encode::HexEncode(security_->GetAddress(tx_msg.pubkey())).c_str());
        return transport::kFirewallCheckError;
    }

    // ZJC_DEBUG("pools message fierwall coming success.");
    return transport::kFirewallCheckSuccess;
}

void TxPoolManager::SyncCrossPool() {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    for (uint32_t i = network::kConsensusShardBeginNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        auto sync_count = cross_pools_[i].SyncMissingBlocks(now_tm_ms);
        uint64_t ex_height = common::kInvalidUint64;
        if (cross_pools_[i].latest_height() == common::kInvalidUint64) {
            ex_height = 1;
        } else {
            if (cross_pools_[i].latest_height() < cross_synced_max_heights_[i]) {
                ex_height = cross_pools_[i].latest_height() + 1;
            }
        }

        if (ex_height != common::kInvalidUint64) {
            uint32_t count = 0;
            for (uint64_t i = ex_height; i < cross_synced_max_heights_[i] && count < 64; ++i, ++count) {
                ZJC_DEBUG("add sync block height net: %u, pool: %u, height: %lu",
                    network::kRootCongressNetworkId,
                    common::kRootChainPoolIndex,
                    i);
                ZJC_INFO("kvsync add sync block height net: %u, pool: %u, height: %lu",
                    network::kRootCongressNetworkId,
                    common::kRootChainPoolIndex,
                    i);
                kv_sync_->AddSyncHeight(
                    network::kRootCongressNetworkId,
                    common::kRootChainPoolIndex,
                    i,
                    sync::kSyncHigh);
            }
        }
    }
}

void TxPoolManager::FlushHeightTree() {
    db::DbWriteBatch db_batch;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].FlushHeightTree(db_batch);
    }

    if (cross_pools_ != nullptr) {
        for (uint32_t i = network::kRootCongressNetworkId; i <= now_max_sharding_id_; ++i) {
            cross_pools_[i].FlushHeightTree(db_batch);
        }
    }

//     ZJC_DEBUG("success call FlushHeightTree");
    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
    }
}

void TxPoolManager::ConsensusTimerMessage() {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_sync_height_tree_tm_ms_ < now_tm_ms) {
        FlushHeightTree();
        prev_sync_height_tree_tm_ms_ = now_tm_ms + kFlushHeightTreePeriod;
    }

    // if (prev_get_valid_tm_ms_ < now_tm_ms) {
    //     prev_get_valid_tm_ms_ = now_tm_ms + kGetMinPeriod;
    //     GetMinValidTxCount();
    // }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> tx_count_queue;
// #ifndef NDEBUG
//     // std::string test_str;
//     uint32_t max_count = 0;
//     for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
//         // if (tx_pool_[i].tx_size() > max_count) {
//         //     max_count = tx_pool_[i].tx_size();
//         // }
//         // test_str += std::to_string(tx_pool_[i].tx_size()) + ",";
//         tx_count_queue.push(tx_pool_[i].tx_size());
//         if (tx_count_queue.size() > 2) {
//             tx_count_queue.pop();
//         }
//     }

//     now_max_tx_count_ = max_count * 2 / 3;
//     // ZJC_DEBUG("set max txcount: %u, test str: %s", tx_count_queue.top(), test_str.c_str());
// #endif
    if (prev_sync_check_ms_ < now_tm_ms) {
        SyncMinssingHeights(now_tm_ms);
        SyncMinssingRootHeights(now_tm_ms);
        prev_sync_check_ms_ = now_tm_ms + kSyncMissingBlockPeriod;
    }

    if (prev_sync_heights_ms_ < now_tm_ms) {
        SyncPoolsMaxHeight();
        prev_sync_heights_ms_ = now_tm_ms + kSyncPoolsMaxHeightsPeriod;
    }

    if (prev_sync_cross_ms_ < now_tm_ms) {
        SyncCrossPool();
        prev_sync_cross_ms_ = now_tm_ms + kSyncCrossPeriod;
    }

    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 10) {
        ZJC_DEBUG("TxPoolManager handle message use time: %lu", (etime - now_tm_ms));
    }

    tools_tick_.CutOff(
        100000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this));
}

void TxPoolManager::CheckLeaderValid(
        const std::vector<double>& factors,
        std::vector<int32_t>* invalid_pools) {
    double average = 0.0;
    for (uint32_t i = 0; i < factors.size(); ++i) {
        average += factors[i];
    }

    if (average <= 0.0) {
        // all leader invalid
        if (latest_leader_count_ <= 2) {
            invalid_pools->push_back(-1);
        }

        return;
    }

    average /= factors.size();
    double variance = 0.0;
    for (uint32_t i = 0; i < factors.size(); ++i) {
        variance += (factors[i] - average) * (factors[i] - average);
    }

    variance = sqrt(variance / (factors.size() - 1));
    for (uint32_t i = 0; i < factors.size(); ++i) {
        double grubbs = abs(factors[i] - average) / variance;
        if (grubbs > kGrubbsValidFactor && factors[i] < kInvalidLeaderRatio) {
            // invalid leader
            ZJC_DEBUG("invalid pool found grubbs: %f, %d", grubbs, i);
            invalid_pools->push_back(i);
        }
    }
}

void TxPoolManager::SyncMinssingRootHeights(uint64_t now_tm_ms) {
    if (root_cross_pools_ == nullptr) {
        return;
    }
    
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    root_prev_synced_pool_index_ %= common::kInvalidPoolIndex;
    auto begin_pool = root_prev_synced_pool_index_;
    for (; root_prev_synced_pool_index_ < common::kInvalidPoolIndex; ++root_prev_synced_pool_index_) {
        auto res = root_cross_pools_[root_prev_synced_pool_index_].SyncMissingBlocks(now_tm_ms);
        if (root_cross_pools_[root_prev_synced_pool_index_].latest_height() == common::kInvalidUint64 ||
                root_cross_pools_[root_prev_synced_pool_index_].latest_height() <
                root_synced_max_heights_[root_prev_synced_pool_index_]) {
            SyncRootBlockWithMaxHeights(
                root_prev_synced_pool_index_, root_synced_max_heights_[root_prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                root_prev_synced_pool_index_,
                res, 
                root_cross_pools_[root_prev_synced_pool_index_].latest_height(),
                root_synced_max_heights_[root_prev_synced_pool_index_]);
        }
    }

    for (root_prev_synced_pool_index_ = 0;
            root_prev_synced_pool_index_ < begin_pool; ++root_prev_synced_pool_index_) {
        auto res = root_cross_pools_[root_prev_synced_pool_index_].SyncMissingBlocks(now_tm_ms);
        if (root_cross_pools_[root_prev_synced_pool_index_].latest_height() == common::kInvalidUint64 || 
                root_cross_pools_[root_prev_synced_pool_index_].latest_height() <
                root_synced_max_heights_[root_prev_synced_pool_index_]) {
            SyncRootBlockWithMaxHeights(
                root_prev_synced_pool_index_, root_synced_max_heights_[root_prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                root_prev_synced_pool_index_,
                res, root_cross_pools_[root_prev_synced_pool_index_].latest_height(),
                root_synced_max_heights_[root_prev_synced_pool_index_]);
        }
    }
}

void TxPoolManager::SyncMinssingHeights(uint64_t now_tm_ms) {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    prev_synced_pool_index_ %= common::kInvalidPoolIndex;
    auto begin_pool = prev_synced_pool_index_;
    for (; prev_synced_pool_index_ < common::kInvalidPoolIndex; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(now_tm_ms);
        if (tx_pool_[prev_synced_pool_index_].latest_height() == common::kInvalidUint64 ||
                tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, 
                tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }
    }

    for (prev_synced_pool_index_ = 0;
            prev_synced_pool_index_ < begin_pool; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(now_tm_ms);
        if (tx_pool_[prev_synced_pool_index_].latest_height() == common::kInvalidUint64 || 
                tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }
    }
}

void TxPoolManager::SyncRootBlockWithMaxHeights(uint32_t pool_idx, uint64_t height) {
    if (kv_sync_ == nullptr) {
        return;
    }

    ZJC_DEBUG("add sync block height net: %u, pool: %u, height: %lu",
        network::kRootCongressNetworkId,
        pool_idx,
        height);
    kv_sync_->AddSyncHeight(
        network::kRootCongressNetworkId,
        pool_idx,
        height,
        sync::kSyncHigh);
}

void TxPoolManager::SyncBlockWithMaxHeights(uint32_t pool_idx, uint64_t height) {
    if (kv_sync_ == nullptr) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    ZJC_DEBUG("add sync block height net: %u, pool: %u, height: %lu",
        net_id,
        pool_idx,
        height);
    ZJC_INFO("kvsync add sync block height net: %u, pool: %u, height: %lu",
        net_id,
        pool_idx,
        height);
    kv_sync_->AddSyncHeight(
        net_id,
        pool_idx,
        height,
        sync::kSyncHigh);
}

void TxPoolManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index(msg_ptr);
    // just one thread
    ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_DEBUG("success add message hash64: %lu, thread idx: %u, msg size: %u, max: %u, gid: %s",
        msg_ptr->header.hash64(),
        thread_idx,
        pools_msg_queue_.size(),
        common::GlobalInfo::Instance()->pools_each_thread_max_messages(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().gid()).c_str());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (pools_msg_queue_.size() > common::GlobalInfo::Instance()->pools_each_thread_max_messages()) {
        return;
    }

    auto& header = msg_ptr->header;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        if (IsUserTransaction(tx_msg.step())) {
            auto tmp_acc_ptr = acc_mgr_.lock();
            auto address_info = tmp_acc_ptr->GetAccountInfo(security_->GetAddress(tx_msg.pubkey()));
            if (!address_info) {
                return;
            }

            if (tx_pool_[address_info->pool_index()].all_tx_size() >= 
                    common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
                ZJC_DEBUG("add failed extend %u, %u, all valid: %u", 
                    tx_pool_[address_info->pool_index()].all_tx_size(), 
                    common::GlobalInfo::Instance()->each_tx_pool_max_txs(), 
                    tx_pool_[address_info->pool_index()].tx_size());
                return;
            }

            msg_ptr->address_info = address_info;
// #ifndef NDEBUG
            auto now_tm = common::TimeUtils::TimestampMs();
            ++prev_tps_count_;
            uint64_t dur = 1000lu;
            if (now_tm > prev_show_tm_ms_ + dur) {
                ZJC_INFO("pools stored message size: %d, %d, pool index: %d, gid size: %u, tx all size: %u, tps: %lu", 
                        thread_idx, pools_msg_queue_.size(),
                        address_info->pool_index(),
                        tx_pool_[address_info->pool_index()].all_tx_size(),
                        tx_pool_[address_info->pool_index()].tx_size(),
                        (prev_tps_count_/(dur / 1000)));
                prev_show_tm_ms_ = now_tm;
                prev_tps_count_ = 0;
            }
// #endif
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_sync_heights()) {
        ZJC_DEBUG("header.has_sync_heights()");
        HandleSyncPoolsMaxHeight(msg_ptr);
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    pools_msg_queue_.push(msg_ptr);
    pop_tx_con_.notify_one();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}


int TxPoolManager::BackupConsensusAddTxs(
        transport::MessagePtr msg_ptr,
        uint32_t pool_index, 
        const std::vector<pools::TxItemPtr>& valid_txs) {
    ZJC_DEBUG("success add consensus tx size: %u", valid_txs.size());
    tx_pool_[pool_index].ConsensusAddTxs(valid_txs);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    return kPoolsSuccess;
}

int TxPoolManager::BackupConsensusAddTxs(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index, 
        const pools::TxItemPtr& valid_tx) {
    if (tx_pool_[pool_index].all_tx_size() >= 
            common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_DEBUG("add failed extend %u, %u, all valid: %u", 
            tx_pool_[pool_index].all_tx_size(), 
            common::GlobalInfo::Instance()->each_tx_pool_max_txs(), 
            tx_pool_[pool_index].tx_size());
    } else {
        tx_pool_[pool_index].ConsensusAddTxs(valid_tx);
    }

    return kPoolsSuccess;
}

void TxPoolManager::ConsensusAddTxs(uint32_t pool_index, const std::vector<pools::TxItemPtr>& txs) {
    std::vector<pools::TxItemPtr> valid_txs;
    for (uint32_t i = 0; i < txs.size(); ++i) {
        auto tx_ptr = txs[i];
        if (tx_ptr->tx_info->pubkey().empty() || tx_ptr->tx_info->sign().empty()) {
            valid_txs.push_back(tx_ptr);
            continue;
        }

        if (security_->Verify(
                tx_ptr->unique_tx_hash,
                tx_ptr->tx_info->pubkey(),
                tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
            ZJC_DEBUG("verify signature failed address balance: %lu, transfer amount: %lu, "
                "prepayment: %lu, default call contract gas: %lu, txid: %s, step: %d",
                tx_ptr->address_info->balance(),
                tx_ptr->tx_info->amount(),
                tx_ptr->tx_info->contract_prepayment(),
                consensus::kCallContractDefaultUseGas,
                common::Encode::HexEncode(tx_ptr->tx_info->gid()).c_str(),
                tx_ptr->tx_info->step());
            assert(false);
            continue;
        }

        valid_txs.push_back(tx_ptr);
    }
    
    tx_pool_[pool_index].ConsensusAddTxs(valid_txs);
}

void TxPoolManager::PopPoolsMessage() {
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        while (!destroy_) {
            transport::MessagePtr msg_ptr = nullptr;
            if (!pools_msg_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
                break;
            }

            auto now_tm = common::TimeUtils::TimestampMs();
            ++add_prev_tps_count_;
            uint64_t dur = 1000lu;
            if (now_tm > add_prev_tps_time_ms_ + dur) {
                ZJC_INFO("pools add tx tps: %lu", 
                    (prev_tps_count_/(dur / 1000lu)));
                add_prev_tps_time_ms_ = now_tm;
                add_prev_tps_count_ = 0;
            }
            
            ZJC_DEBUG("success handle message hash64: %lu", msg_ptr->header.hash64());
            HandlePoolsMessage(msg_ptr);
        }

        std::unique_lock<std::mutex> lock(pop_tx_mu_);
        pop_tx_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

void TxPoolManager::HandlePoolsMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    uint32_t pool_index = common::kInvalidPoolIndex;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        ADD_TX_DEBUG_INFO(header.mutable_tx_proto());
        ZJC_DEBUG("success handle message hash64: %lu, from: %s, to: %s, type: %d, gid: %s",
            msg_ptr->header.hash64(),
            common::Encode::HexEncode(tx_msg.pubkey()).c_str(),
            common::Encode::HexEncode(tx_msg.to()).c_str(),
            tx_msg.step(),
            common::Encode::HexEncode(tx_msg.gid()).c_str());
        switch (tx_msg.step()) {
        case pools::protobuf::kJoinElect:
            HandleElectTx(msg_ptr);
            break;
        case pools::protobuf::kNormalFrom:
            HandleNormalFromTx(msg_ptr);
            break;
        case pools::protobuf::kCreateLibrary:
        case pools::protobuf::kContractCreate:
        case pools::protobuf::kContractCreateByRootFrom:
            HandleCreateContractTx(msg_ptr);
            break;
        case pools::protobuf::kContractGasPrepayment:
            HandleSetContractPrepayment(msg_ptr);
            break;
        case pools::protobuf::kRootCreateAddress: {
            if (tx_msg.to().size() != security::kUnicastAddressLength) {
                return;
            }

            // must not coming from network
            if (msg_ptr->conn != nullptr) {
                return;
            }
            
            msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
            ZJC_DEBUG("get local tokRootCreateAddress tx message hash: %s, gid: %s", 
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(),
                common::Encode::HexEncode(tx_msg.gid()).c_str());
            pool_index = common::GetAddressPoolIndex(tx_msg.to()) % common::kImmutablePoolSize;
            break;
        }
        case pools::protobuf::kContractExcute:
            HandleContractExcute(msg_ptr);
            break;
        case pools::protobuf::kContractCreateByRootTo: 
        case pools::protobuf::kConsensusLocalTos: {
			// 如果要指定 pool index, tx_msg.to() 必须是 pool addr，否则就随机分配 pool index 了
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
            ZJC_DEBUG("get local to tx message hash: %s, gid: %s",
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(), 
                common::Encode::HexEncode(msg_ptr->header.tx_proto().gid()).c_str());
            break;
        }
        case pools::protobuf::kRootCross: {
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            break;
        }
        case pools::protobuf::kPoolStatisticTag: {
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            break;
        }
        default:
            ZJC_DEBUG("invalid tx step: %d", tx_msg.step());
            assert(false);
            break;
        }

        if (pool_index == common::kInvalidPoolIndex) {
            if (!msg_ptr->address_info) {
                ZJC_DEBUG("invalid tx step: %d", tx_msg.step());
                assert(false);
                return;
            }

            pool_index = msg_ptr->address_info->pool_index();
        }

        DispatchTx(pool_index, msg_ptr);
    }
}

void TxPoolManager::SyncPoolsMaxHeight() {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto net_id = common::GlobalInfo::Instance()->network_id();
    msg_ptr->header.set_src_sharding_id(net_id);
    for (uint32_t i = network::kRootCongressNetworkId; i <= now_max_sharding_id_; ++i) {
        dht::DhtKeyManager dht_key(i);
        msg_ptr->header.set_des_dht_key(dht_key.StrKey());
        msg_ptr->header.set_type(common::kPoolsMessage);
        auto* sync_heights = msg_ptr->header.mutable_sync_heights();
        sync_heights->set_req(true);
        transport::TcpTransport::Instance()->SetMessageHash(msg_ptr->header);
        ZJC_DEBUG("sync net data from network: %u, hash64: %lu, src sharding id: %u",
            i, msg_ptr->header.hash64(), msg_ptr->header.src_sharding_id());
        network::Route::Instance()->Send(msg_ptr);
    }
}

void TxPoolManager::HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr) {
    if (tx_pool_ == nullptr) {
        ZJC_DEBUG("tx_pool_ == nullptr");
        return;
    }

    if (!msg_ptr->header.has_sync_heights()) {
        ZJC_DEBUG("!msg_ptr->header.has_sync_heights()");
        return;
    }

    uint32_t src_net_id = msg_ptr->header.src_sharding_id();
    if (msg_ptr->header.sync_heights().req()) {
        if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
            return;
        }
        
        transport::protobuf::Header msg;
        msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
        dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kPoolsMessage);
        auto* sync_heights = msg.mutable_sync_heights();
        uint32_t pool_idx = common::kInvalidPoolIndex;
        std::string sync_debug;
        std::string cross_debug;
        if (src_net_id >= network::kConsensusWaitingShardBeginNetworkId) {
            src_net_id -= network::kConsensusWaitingShardOffset;
        }

        if (src_net_id == common::GlobalInfo::Instance()->network_id()) {
            for (uint32_t i = 0; i < pool_idx; ++i) {
                sync_heights->add_heights(tx_pool_[i].latest_height());
                sync_debug += std::to_string(tx_pool_[i].latest_height()) + " ";
            }
        } else {
            if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
                for (uint32_t i = 0; i < pool_idx; ++i) {
                    sync_heights->add_heights(tx_pool_[i].latest_height());
                    sync_debug += std::to_string(tx_pool_[i].latest_height()) + " ";
                }
            } else {
                sync_heights->add_cross_heights(tx_pool_[common::kInvalidPoolIndex - 1].latest_height());
                cross_debug += std::to_string(tx_pool_[common::kInvalidPoolIndex - 1].latest_height()) + " ";
            }
        }

        transport::TcpTransport::Instance()->SetMessageHash(msg);
        transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
        ZJC_DEBUG("response pool heights: %s, cross pool heights: %s, "
            "now_max_sharding_id_: %u, src sharding id: %u, src hash64: %lu, des hash64: %lu",
            sync_debug.c_str(), cross_debug.c_str(),
            now_max_sharding_id_, msg_ptr->header.src_sharding_id(),
            msg_ptr->header.hash64(), msg.hash64());
    } else {
        if (src_net_id >= network::kConsensusShardEndNetworkId) {
            return;
        }

        if (src_net_id != common::GlobalInfo::Instance()->network_id()) {
            if (src_net_id != network::kRootCongressNetworkId) {
                auto sharding_id = msg_ptr->header.src_sharding_id();
                auto& cross_heights = msg_ptr->header.sync_heights().cross_heights();
                uint64_t update_height = cross_pools_[sharding_id].latest_height();
                do {
                    if (cross_heights.empty()) {
                        break;
                    }

                    if (cross_pools_[sharding_id].latest_height() == common::kInvalidUint64 &&
                            cross_synced_max_heights_[sharding_id] < cross_heights[0]) {
                        update_height = cross_heights[0];
                        break;
                    }

                    if (cross_heights[0] > cross_pools_[sharding_id].latest_height() + 64) {
                        update_height = cross_pools_[sharding_id].latest_height() + 64;
                        break;
                    }

                    if (cross_heights[0] > cross_pools_[sharding_id].latest_height()) {
                        update_height = cross_heights[0];
                        break;
                    }
                
                    ZJC_DEBUG("net: %u, get response pool heights, cross pool heights: %lu, update_height: %lu, "
                        "cross_synced_max_heights_[i]: %lu, cross_pools_[i].latest_height(): %lu, cross_heights[i]: %lu",
                        sharding_id, update_height, update_height,
                        cross_synced_max_heights_[sharding_id], cross_pools_[sharding_id].latest_height(),
                        cross_heights[0]);
                    cross_synced_max_heights_[sharding_id] = cross_heights[0];
                } while (0);
                cross_block_mgr_->UpdateMaxHeight(sharding_id, update_height);
            } else {
                auto& heights = msg_ptr->header.sync_heights().heights();
                if (heights.size() != common::kInvalidPoolIndex) {
                    return;
                }

                std::string sync_debug;
                for (int32_t i = 0; i < heights.size(); ++i) {
                    sync_debug += std::to_string(i) + "_" + std::to_string(heights[i]) + " ";
                    if (heights[i] != common::kInvalidUint64) {
                        if (root_cross_pools_[i].latest_height() == common::kInvalidUint64 &&
                                root_synced_max_heights_[i] < heights[i]) {
                            root_synced_max_heights_[i] = heights[i];
                            continue;
                        }

                        if (heights[i] > root_cross_pools_[i].latest_height() + 64) {
                            root_synced_max_heights_[i] = root_cross_pools_[i].latest_height() + 64;
                            continue;
                        }

                        if (heights[i] > root_cross_pools_[i].latest_height()) {
                            root_synced_max_heights_[i] = heights[i];
                        }
                    }
                }

                ZJC_DEBUG("get root response pool heights: %s", sync_debug.c_str());
            }
            return;
        }

        auto& heights = msg_ptr->header.sync_heights().heights();
        if (heights.size() != common::kInvalidPoolIndex) {
            return;
        }

        std::string sync_debug;
        std::string cross_debug;
        for (int32_t i = 0; i < heights.size(); ++i) {
            sync_debug += std::to_string(i) + "_" + std::to_string(heights[i]) + " ";
            if (heights[i] != common::kInvalidUint64) {
                if (tx_pool_[i].latest_height() == common::kInvalidUint64 &&
                        synced_max_heights_[i] < heights[i]) {
                    synced_max_heights_[i] = heights[i];
                    continue;
                }

                if (heights[i] > tx_pool_[i].latest_height() + 64) {
                    synced_max_heights_[i] = tx_pool_[i].latest_height() + 64;
                    continue;
                }

                if (heights[i] > tx_pool_[i].latest_height()) {
                    synced_max_heights_[i] = heights[i];
                }
            }
        }

        ZJC_DEBUG("get response pool heights: %s, cross pool heights: %s", sync_debug.c_str(), cross_debug.c_str());
    }
}

std::shared_ptr<address::protobuf::AddressInfo> TxPoolManager::GetAddressInfo(const std::string& address) {
    auto tmp_acc_ptr = acc_mgr_.lock();
    return tmp_acc_ptr->GetAccountInfo(address);
}

void TxPoolManager::HandleElectTx(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = *header.mutable_tx_proto();
    auto addr = security_->GetAddress(tx_msg.pubkey());
    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(addr);
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info: %s", common::Encode::HexEncode(addr).c_str());
        return;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        assert(false);
        return;
    }

    if (msg_ptr->address_info->balance() < consensus::kJoinElectGas) {
        ZJC_WARN("address info join elect gas invalid: %s %lu %lu", 
            common::Encode::HexEncode(addr).c_str(), 
            msg_ptr->address_info->balance(), 
            consensus::kJoinElectGas);
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (!tx_msg.has_key() || tx_msg.key() != protos::kJoinElectVerifyG2) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return;
    }

    auto msg_hash = pools::GetTxMessageHash(tx_msg);
    if (security_->Verify(
            msg_hash,
            tx_msg.pubkey(),
            tx_msg.sign()) != security::kSecuritySuccess) {
        ZJC_WARN("kElectJoin verify signature failed!");
        return;
    }

    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(tx_msg.value())) {
        ZJC_WARN("join_info parse failed address info: %s", common::Encode::HexEncode(addr).c_str());
        return;
    }

    uint32_t tmp_shard = join_info.shard_id();
    if (tmp_shard != network::kRootCongressNetworkId) {
        if (tmp_shard != msg_ptr->address_info->sharding_id()) {
            ZJC_DEBUG("join des shard error: %d,  %d.",
                tmp_shard, msg_ptr->address_info->sharding_id());
            return;
        }
    }

    // std::string new_hash;
    // if (!SaveNodeVerfiyVec(msg_ptr->address_info->addr(), join_info, &new_hash)) {
    //     assert(false);
    //     return;
    // }
    // tx_msg.set_key(protos::kJoinElectVerifyG2);
    // tx_msg.set_value(new_hash);
    
    ZJC_DEBUG("elect tx msg hash is %s", 
        common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
    msg_ptr->msg_hash = msg_hash;
}

bool TxPoolManager::SaveNodeVerfiyVec(
        const std::string& id,
        const bls::protobuf::JoinElectInfo& join_info,
        std::string* new_hash) {
    int32_t t = common::GetSignerCount(common::GlobalInfo::Instance()->each_shard_max_members());
    if (join_info.g2_req().verify_vec_size() > 0 && join_info.g2_req().verify_vec_size() != t) {
        return false;
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

    *new_hash = common::Hash::keccak256(str_for_hash);
    auto str = join_info.SerializeAsString();
    prefix_db_->SaveTemporaryKv(*new_hash, str);
    return true;
}

void TxPoolManager::HandleContractExcute(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    // if (tx_msg.has_key() && tx_msg.key().size() > 0) {
    //     ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    //     return;
    // }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->destructed()) {
        ZJC_ERROR("contract destructed: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    auto from = security_->GetAddress(tx_msg.pubkey());
    if (msg_ptr->address_info->addr() == from) {
        ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    uint64_t height = 0;
    uint64_t prepayment = 0;
    // TODO: test check
    // if (!prefix_db_->GetContractUserPrepayment(
    //         tx_msg.to(),
    //         from,
    //         &height,
    //         &prepayment)) {
    //     ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    //     return;
    // }

    // if (prepayment < tx_msg.amount() + tx_msg.gas_limit() * tx_msg.gas_price()) {
    //     ZJC_ERROR("failed add contract call. %s, prepayment: %lu, tx_msg.amount(): %lu, "
    //         "tx_msg.gas_limit(): %lu, tx_msg.gas_price(): %lu, all: %lu",
    //         common::Encode::HexEncode(tx_msg.to()).c_str(),
    //         prepayment,
    //         tx_msg.amount(),
    //         tx_msg.gas_limit(),
    //         tx_msg.gas_price(),
    //         (tx_msg.amount() + tx_msg.gas_limit() * tx_msg.gas_price()));
    //     return;
    // }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    auto pool_index = msg_ptr->address_info->pool_index();
    if (security_->Verify(
            msg_ptr->msg_hash,
            tx_msg.pubkey(),
            tx_msg.sign()) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu, txid: %s",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas,
            common::Encode::HexEncode(tx_msg.gid()).c_str());
        assert(false);
        return;
    }
}

void TxPoolManager::HandleSetContractPrepayment(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    // user can't direct call contract, pay contract prepayment and call contract direct
    if (!tx_msg.contract_input().empty() ||
            tx_msg.contract_prepayment() < consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("call contract not has valid contract input"
            "and contract prepayment invalid.");
        return;
    }

    if (!UserTxValid(msg_ptr)) {
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kCallContractDefaultUseGas * tx_msg.gas_price()) {
        ZJC_DEBUG("address %s balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu, from: %s, to: %s",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas,
            common::Encode::HexEncode(security_->GetAddress(
            msg_ptr->header.tx_proto().pubkey())).c_str(),
            common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
        return;
    }
}

bool TxPoolManager::UserTxValid(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(security_->GetAddress(tx_msg.pubkey()));
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info.");
        return false;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        assert(false);
        return false;
    }

    auto local_shard = common::GlobalInfo::Instance()->network_id();
    if (local_shard >= network::kConsensusShardEndNetworkId) {
        local_shard -= network::kConsensusWaitingShardOffset;
    }

    if (msg_ptr->address_info->sharding_id() != local_shard) {
        ZJC_WARN("sharding error id: %s, shard: %d, local shard: %d",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        assert(false);
        return false;
    }

    if (tx_msg.has_key() && tx_msg.key().size() > kTxStorageKeyMaxSize) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return false;
    }

    return true;
}

void TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!UserTxValid(msg_ptr)) {
//         assert(false);
        return;
    }

    // 验证账户余额是否足够
    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kTransferGas * tx_msg.gas_price()) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas);
        assert(false);
        return;
    }

    ADD_TX_DEBUG_INFO(msg_ptr->header.mutable_tx_proto());
}

void TxPoolManager::HandleCreateContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!tx_msg.has_contract_code()) {
        ZJC_DEBUG("create contract not has valid contract code: %s",
            common::Encode::HexEncode(tx_msg.contract_code()).c_str());
        return;
    }

    uint64_t default_gas = consensus::kCallContractDefaultUseGas +
        tx_msg.value().size() * consensus::kKeyValueStorageEachBytes;
    if (tx_msg.step() == pools::protobuf::kContractCreate) {
        if (memcmp(
                tx_msg.contract_code().c_str(),
                protos::kContractBytesStartCode.c_str(),
                protos::kContractBytesStartCode.size()) != 0) {
            return;
        }
    } else {
        // all shards will save the library
        default_gas = consensus::kCreateLibraryDefaultUseGas +
            network::kConsensusWaitingShardOffset *
            (tx_msg.value().size() + tx_msg.key().size()) *
            consensus::kKeyValueStorageEachBytes;
    }

    if (!UserTxValid(msg_ptr)) {
        ZJC_ERROR("create contract error!");
        return;
    }

    ZJC_INFO("create contract address: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    auto tmp_acc_ptr = acc_mgr_.lock();
    auto contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info != nullptr) {
        ZJC_WARN("contract address exists: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            default_gas * tx_msg.gas_price()) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu, gas price: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            default_gas,
            tx_msg.gas_price());
        return;
    }
}

void TxPoolManager::BftCheckInvalidGids(
        uint32_t pool_index,
        std::vector<std::shared_ptr<InvalidGidItem>>& items) {
    while (true) {
        std::shared_ptr<InvalidGidItem> gid_ptr = nullptr;
        invalid_gid_queues_[pool_index].pop(&gid_ptr);
        if (gid_ptr == nullptr) {
            break;
        }

        items.push_back(gid_ptr);
    }
}

void TxPoolManager::PopTxs(uint32_t pool_index, bool pop_all, bool* has_user_tx, bool* has_system_tx) {
    // uint32_t count = 0;
    // while (!destroy_) {
    //     transport::MessagePtr msg_ptr = nullptr;
    //     if (!msg_queues_[pool_index].pop(&msg_ptr)) {
    //         break;
    //     }

    //     ZJC_DEBUG("success pop tx gid: %s, step: %d",
    //         common::Encode::HexEncode(msg_ptr->header.tx_proto().gid()).c_str(),
    //         msg_ptr->header.tx_proto().step());
    //     if (pools::IsUserTransaction(msg_ptr->header.tx_proto().step())) {
    //         if (has_user_tx != nullptr) {
    //             *has_user_tx = true;
    //         }
    //     } else {
    //         if (has_system_tx != nullptr) {
    //             *has_system_tx = true;
    //         }
    //     }

    //     // auto now_tm_ms = common::TimeUtils::TimestampMs();
    //     DispatchTx(pool_index, msg_ptr);
    //     if (!pop_all && ++count >= 1024) {
    //         break;
    //     }
    // }
    assert(false);
}

void TxPoolManager::DispatchTx(uint32_t pool_index, const transport::MessagePtr& msg_ptr) {
    if (!tx_pool_[msg_ptr->address_info->pool_index()].GidValid(msg_ptr->header.tx_proto().gid())) {
        ZJC_DEBUG("gid invalid pop tx gid: %s, step: %d",
            common::Encode::HexEncode(msg_ptr->header.tx_proto().gid()).c_str(),
            msg_ptr->header.tx_proto().step());
        return;
    }

    if (msg_ptr->header.tx_proto().step() >= pools::protobuf::StepType_ARRAYSIZE) {
        assert(false);
        return;
    }

    if (item_functions_[msg_ptr->header.tx_proto().step()] == nullptr) {
        ZJC_DEBUG("not registered step : %d", msg_ptr->header.tx_proto().step());
        assert(false);
        return;
    }

    pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    if (tx_ptr == nullptr) {
        assert(false);
        return;
    }

    tx_ptr->unique_tx_hash = msg_ptr->msg_hash;
    // 交易池增加 msg 中的交易
    tx_pool_[pool_index].AddTx(tx_ptr);
    ZJC_DEBUG("success add local transfer to tx pool: %u, step: %d, %s, gid: %s, from pk: %s, to: %s",
        pool_index,
        msg_ptr->header.tx_proto().step(),
        common::Encode::HexEncode(tx_ptr->unique_tx_hash).c_str(),
        common::Encode::HexEncode(tx_ptr->tx_info->gid()).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().pubkey()).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
}

void TxPoolManager::GetTxSyncToLeader(
        uint32_t pool_index,
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckGidValidFunction gid_vlid_func) {
    tx_pool_[pool_index].GetTxSyncToLeader(count, txs, gid_vlid_func);    
}

void TxPoolManager::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map,
        pools::CheckGidValidFunction gid_vlid_func) {
    // TODO: check latency
    if (tx_pool_[pool_index].tx_size() < now_max_tx_count_) {
        return;
    }

    tx_pool_[pool_index].GetTxIdempotently(msg_ptr, res_map, count, gid_vlid_func);    
}

void TxPoolManager::GetTxByGids(
        uint32_t pool_index, 
        std::vector<std::string> gids, 
        std::map<std::string, pools::TxItemPtr>& res_map) {
    tx_pool_[pool_index].GetTxByIds(gids, res_map);
}

void TxPoolManager::TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxRecover(recover_txs);
}

void TxPoolManager::TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxOver(tx_list);
}

void TxPoolManager::GetMinValidTxCount() {
    std::priority_queue<PoolsCountPrioItem> count_queue_;
    std::priority_queue<PoolsTmPrioItem> tm_queue_;
    uint64_t min_tm = common::kInvalidUint64;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        if (tx_pool_[i].tx_size() > 0) {
            count_queue_.push(PoolsCountPrioItem(i, tx_pool_[i].tx_size()));
        }

        if (count_queue_.size() > kMinPoolsValidCount) {
            count_queue_.pop();
        }

        if (tx_pool_[i].oldest_timestamp() > 0) {
            tm_queue_.push(PoolsTmPrioItem(i, tx_pool_[i].oldest_timestamp()));
            if (tx_pool_[i].oldest_timestamp() < min_tm) {
                min_tm = tx_pool_[i].oldest_timestamp();
            }
        }

        if (tm_queue_.size() > kMinPoolsValidCount) {
            tm_queue_.pop();
        }
    }

    if (min_tm != common::kInvalidUint64) {
        min_timestamp_ = min_tm;
    }

    if (count_queue_.size() < kMinPoolsValidCount) {
        return;
    }

    min_valid_tx_count_ = count_queue_.top().count;
    min_valid_timestamp_ = tm_queue_.top().max_timestamp;
}

}  // namespace pools

}  // namespace shardora
