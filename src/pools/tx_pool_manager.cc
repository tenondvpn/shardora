#include "pools/tx_pool_manager.h"

#include "block/account_manager.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "security/ecdsa/ecdsa.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"

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

    ZJC_EMPTY_DEBUG("TxPoolManager init success: %d", common::kInvalidPoolIndex);
    InitCrossPools();
    // 每 10ms 会共识一次时间块
    tools_tick_.CutOff(
        10000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this));
    // 注册 kPoolsMessage 的回调函数
    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::HandleMessage, this, std::placeholders::_1));
#ifdef USE_SERVER_TEST_TRANSACTION
    if (common::GlobalInfo::Instance()->test_pool_index() >= 0) {
        test_tx_thread_ = std::make_shared<std::thread>(
            &TxPoolManager::CreateTestTxs, 
            this, 
            common::GlobalInfo::Instance()->test_pool_index(), 
            common::GlobalInfo::Instance()->test_pool_index(), 
            common::GlobalInfo::Instance()->test_tx_tps());
        ZJC_WARN("success create test tx thread.");
    }
#endif

}

TxPoolManager::~TxPoolManager() {
    destroy_ = true;
    FlushHeightTree();
#ifdef USE_SERVER_TEST_TRANSACTION
    if (test_tx_thread_) {
        test_tx_thread_->join();
    }
#endif
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
    // ZJC_EMPTY_DEBUG("pools message fierwall coming.");
    // return transport::kFirewallCheckSuccess;
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    if (msg_ptr->header.has_sync_heights() && !msg_ptr->header.has_tx_proto()) {
        // TODO: check all message with valid signature
        ZJC_EMPTY_DEBUG("pools message fierwall coming is sync heights.");
        return transport::kFirewallCheckSuccess;
    }

    if (!tx_msg.has_sign() || !tx_msg.has_pubkey() ||
            tx_msg.sign().empty() || tx_msg.pubkey().empty()) {
        ZJC_EMPTY_DEBUG("pools check firewall message failed, invalid sign or pk. sign: %d, pk: %d, hash64: %lu", 
            tx_msg.sign().size(), tx_msg.pubkey().size(), header.hash64());
        return transport::kFirewallCheckError;
    }

    if (!account_tx_qps_check_.check(tx_msg.pubkey())) {
        ZJC_EMPTY_DEBUG("pools check firewall message failed, invalid qps limit pk: %d, hash64: %lu", 
            tx_msg.pubkey().size(), header.hash64());
        return transport::kFirewallCheckError;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (tx_msg.pubkey().size() == 64u) {
        security::GmSsl gmssl;
        if (gmssl.Verify(
                msg_ptr->msg_hash,
                tx_msg.pubkey(),
                tx_msg.sign()) != security::kSecuritySuccess) {
            ZJC_ERROR("verify signature failed!");
            return transport::kFirewallCheckError;
        }

        auto tmp_acc_ptr = acc_mgr_.lock();
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(gmssl.GetAddress(tx_msg.pubkey()));
        if (msg_ptr->address_info == nullptr) {
            ZJC_EMPTY_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(gmssl.GetAddress(tx_msg.pubkey())).c_str());
            return transport::kFirewallCheckError;
        }
    } else if (tx_msg.pubkey().size() > 128u) {
        security::Oqs oqs;
        if (oqs.Verify(
                msg_ptr->msg_hash,
                tx_msg.pubkey(),
                tx_msg.sign()) != security::kSecuritySuccess) {
            ZJC_ERROR("verify signature failed!");
            return transport::kFirewallCheckError;
        }

        auto tmp_acc_ptr = acc_mgr_.lock();
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(oqs.GetAddress(tx_msg.pubkey()));
        if (msg_ptr->address_info == nullptr) {
            ZJC_EMPTY_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(oqs.GetAddress(tx_msg.pubkey())).c_str());
            return transport::kFirewallCheckError;
        }
    } else {
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
            ZJC_EMPTY_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(security_->GetAddress(tx_msg.pubkey())).c_str());
            return transport::kFirewallCheckError;
        }
    }

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
                ZJC_EMPTY_DEBUG("now add sync height 1, %u_%u_%lu", 
                    network::kRootCongressNetworkId,
                    common::kImmutablePoolSize,
                    i);
                kv_sync_->AddSyncHeight(
                    network::kRootCongressNetworkId,
                    common::kImmutablePoolSize,
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

//     ZJC_EMPTY_DEBUG("success call FlushHeightTree");
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
        ZJC_EMPTY_DEBUG("TxPoolManager handle message use time: %lu", (etime - now_tm_ms));
    }

    tools_tick_.CutOff(
        100000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this));
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
            ZJC_EMPTY_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
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
            ZJC_EMPTY_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
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
            ZJC_EMPTY_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
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
            ZJC_EMPTY_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
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

    ZJC_EMPTY_DEBUG("now add sync height 1, %u_%u_%lu", 
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

    ZJC_EMPTY_DEBUG("now add sync height 1, %u_%u_%lu", 
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
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    // auto thread_idx = common::GlobalInfo::Instance()->get_thread_index(msg_ptr);
    // for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
    //     if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_idx) {
    //         tx_pool_[pool_idx].CheckPopedTxs();
    //     }
    // }

    auto& header = msg_ptr->header;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        if (IsUserTransaction(tx_msg.step())) {
            auto tmp_acc_ptr = acc_mgr_.lock();
            protos::AddressInfoPtr address_info = nullptr;
            if (tx_msg.pubkey().size() == 64u) {
                security::GmSsl gmssl;
                address_info = tmp_acc_ptr->GetAccountInfo(gmssl.GetAddress(tx_msg.pubkey()));
            } else if (tx_msg.pubkey().size() > 128u) {
                security::Oqs oqs;
                address_info = tmp_acc_ptr->GetAccountInfo(oqs.GetAddress(tx_msg.pubkey()));
            } else {
                address_info = tmp_acc_ptr->GetAccountInfo(security_->GetAddress(tx_msg.pubkey()));
            }

            if (!address_info) {
                return;
            }

            if (tx_pool_[address_info->pool_index()].all_tx_size() >= 
                    common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
                ZJC_EMPTY_DEBUG("add failed extend %u, %u, all valid: %u", 
                    tx_pool_[address_info->pool_index()].all_tx_size(), 
                    common::GlobalInfo::Instance()->each_tx_pool_max_txs(), 
                    tx_pool_[address_info->pool_index()].all_tx_size());
                return;
            }

            msg_ptr->address_info = address_info;
#ifndef NDEBUG
            auto now_tm = common::TimeUtils::TimestampMs();
            ++prev_tps_count_;
            uint64_t dur = 1000lu;
            if (now_tm > prev_show_tm_ms_ + dur) {
                ZJC_EMPTY_DEBUG("pools stored message size: %d, %d, pool index: %d, gid size: %u, tx all size: %u, tps: %lu", 
                        -1, pools_msg_queue_.size(),
                        address_info->pool_index(),
                        tx_pool_[address_info->pool_index()].all_tx_size(),
                        tx_pool_[address_info->pool_index()].all_tx_size(),
                        (prev_tps_count_/(dur / 1000)));
                prev_show_tm_ms_ = now_tm;
                prev_tps_count_ = 0;
            }
#endif
        }
    }

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_sync_heights()) {
        ZJC_EMPTY_DEBUG("header.has_sync_heights()");
        HandleSyncPoolsMaxHeight(msg_ptr);
        return;
    }

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    HandlePoolsMessage(msg_ptr);
    // pools_msg_queue_.push(msg_ptr);
    // pop_tx_con_.notify_one();
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

int TxPoolManager::BackupConsensusAddTxs(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index, 
        const pools::TxItemPtr& valid_tx) {
    if (tx_pool_[pool_index].all_tx_size() >= 
            common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_EMPTY_DEBUG("add failed extend %u, %u, all valid: %u", 
            tx_pool_[pool_index].all_tx_size(), 
            common::GlobalInfo::Instance()->each_tx_pool_max_txs(), 
            tx_pool_[pool_index].all_tx_size());
    } else {
        tx_pool_[pool_index].ConsensusAddTxs(valid_tx);
    }

    return kPoolsSuccess;
}

void TxPoolManager::HandlePoolsMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    uint32_t pool_index = common::kInvalidPoolIndex;
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        ADD_TX_DEBUG_INFO(header.mutable_tx_proto());
        ZJC_EMPTY_DEBUG("success handle message hash64: %lu, from: %s, to: %s, type: %d, nonce: %lu",
            msg_ptr->header.hash64(),
            common::Encode::HexEncode(tx_msg.pubkey()).c_str(),
            common::Encode::HexEncode(tx_msg.to()).c_str(),
            tx_msg.step(),
            tx_msg.nonce());
        switch (tx_msg.step()) {
        case pools::protobuf::kJoinElect:
            HandleElectTx(msg_ptr);
            break;
        case pools::protobuf::kNormalFrom:
            HandleNormalFromTx(msg_ptr);
            break;
        case pools::protobuf::kCreateLibrary:
        case pools::protobuf::kContractCreate:
            HandleCreateContractTx(msg_ptr);
            break;
        case pools::protobuf::kContractGasPrepayment:
            HandleSetContractPrepayment(msg_ptr);
            break;
        case pools::protobuf::kRootCreateAddress: {
            if (tx_msg.to().size() != common::kUnicastAddressLength &&
                    tx_msg.to().size() != common::kUnicastAddressLength * 2) {
                return;
            }

            // must not coming from network
            if (msg_ptr->conn != nullptr) {
                return;
            }
            
            msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
            ZJC_EMPTY_DEBUG("get local tokRootCreateAddress tx message hash: %s, to: %s, amount: %lu nonce: %lu", 
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(),
                common::Encode::HexEncode(tx_msg.to()).c_str(),
                tx_msg.amount(),
                tx_msg.nonce());
            pool_index = common::GetAddressPoolIndex(
                tx_msg.to().substr(0, common::kUnicastAddressLength)) % common::kImmutablePoolSize;
            break;
        }
        case pools::protobuf::kContractExcute:
            HandleContractExcute(msg_ptr);
            break;
        case pools::protobuf::kConsensusLocalTos: {
			// 如果要指定 pool index, tx_msg.to() 必须是 pool addr，否则就随机分配 pool index 了
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
            ZJC_EMPTY_DEBUG("get local to tx message hash: %s, nonce: %lu",
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(), 
                msg_ptr->header.tx_proto().nonce());
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
            ZJC_EMPTY_DEBUG("invalid tx step: %d", tx_msg.step());
            assert(false);
            break;
        }

        if (pool_index == common::kInvalidPoolIndex) {
            if (!msg_ptr->address_info) {
                ZJC_EMPTY_DEBUG("invalid tx step: %d", tx_msg.step());
                return;
            }

            pool_index = msg_ptr->address_info->pool_index();
        }

        TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
        DispatchTx(pool_index, msg_ptr);
        TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
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
        ZJC_EMPTY_DEBUG("sync net data from network: %u, hash64: %lu, src sharding id: %u",
            i, msg_ptr->header.hash64(), msg_ptr->header.src_sharding_id());
        network::Route::Instance()->Send(msg_ptr);
    }
}

void TxPoolManager::HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr) {
    if (tx_pool_ == nullptr) {
        ZJC_EMPTY_DEBUG("tx_pool_ == nullptr");
        return;
    }

    if (!msg_ptr->header.has_sync_heights()) {
        ZJC_EMPTY_DEBUG("!msg_ptr->header.has_sync_heights()");
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
        ZJC_EMPTY_DEBUG("response pool heights: %s, cross pool heights: %s, "
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
                
                    ZJC_EMPTY_DEBUG("net: %u, get response pool heights, cross pool heights: %lu, update_height: %lu, "
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

                ZJC_EMPTY_DEBUG("get root response pool heights: %s", sync_debug.c_str());
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

        ZJC_EMPTY_DEBUG("get response pool heights: %s, cross pool heights: %s", sync_debug.c_str(), cross_debug.c_str());
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
        ZJC_EMPTY_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return;
    }

    auto msg_hash = pools::GetTxMessageHash(tx_msg);
    // if (security_->Verify(
    //         msg_hash,
    //         tx_msg.pubkey(),
    //         tx_msg.sign()) != security::kSecuritySuccess) {
    //     ZJC_WARN("kElectJoin verify signature failed!");
    //     return;
    // }

    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(tx_msg.value())) {
        ZJC_WARN("join_info parse failed address info: %s", common::Encode::HexEncode(addr).c_str());
        return;
    }

    uint32_t tmp_shard = join_info.shard_id();
    if (tmp_shard != network::kRootCongressNetworkId) {
        if (tmp_shard != msg_ptr->address_info->sharding_id()) {
            ZJC_EMPTY_DEBUG("join des shard error: %d,  %d.",
                tmp_shard, msg_ptr->address_info->sharding_id());
            return;
        }
    }
    
    ZJC_EMPTY_DEBUG("elect tx msg hash is %s", 
        common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
    msg_ptr->msg_hash = msg_hash;
}

void TxPoolManager::HandleContractExcute(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    // if (tx_msg.has_key() && tx_msg.key().size() > 0) {
    //     ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    //     return;
    // }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kCallContractDefaultUseGas) {
        ZJC_EMPTY_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    auto from = security_->GetAddress(tx_msg.pubkey());
    auto contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info == nullptr) {
        ZJC_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (contract_info->destructed()) {
        ZJC_ERROR("contract destructed: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    auto prepayment_id = tx_msg.to() + from;
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(prepayment_id);
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        ZJC_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        msg_ptr->address_info = nullptr;
        return;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    ZJC_EMPTY_DEBUG("success add tx contract execute prepyament id: %s, prepayment: %lu, nonce: %lu",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(), 
        msg_ptr->address_info->balance(), 
        msg_ptr->address_info->nonce());
}

void TxPoolManager::HandleSetContractPrepayment(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    // user can't direct call contract, pay contract prepayment and call contract direct
    if (!tx_msg.contract_input().empty() ||
            tx_msg.contract_prepayment() < consensus::kCallContractDefaultUseGas) {
        ZJC_EMPTY_DEBUG("call contract not has valid contract input"
            "and contract prepayment invalid.");
        return;
    }

    if (!UserTxValid(msg_ptr)) {
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kCallContractDefaultUseGas * tx_msg.gas_price()) {
        ZJC_EMPTY_DEBUG("address %s balance invalid: %lu, transfer amount: %lu, "
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
    if (msg_ptr->address_info == nullptr) {
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(
            security_->GetAddress(tx_msg.pubkey()));
    }

    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info.");
        return false;
    }

    if (msg_ptr->address_info->type() == address::protobuf::kWaitingRootConfirm) {
        ZJC_WARN("address invalid and waiting root confirm.");
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
        ZJC_EMPTY_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return false;
    }

    return true;
}

void TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!UserTxValid(msg_ptr)) {
//         assert(false);
        return;
    }

    // 验证账户余额是否足够
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kTransferGas * tx_msg.gas_price()) {
        ZJC_EMPTY_DEBUG("address: %s balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas);
        return;
    }

    ADD_TX_DEBUG_INFO(msg_ptr->header.mutable_tx_proto());
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
}

void TxPoolManager::HandleCreateContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!tx_msg.has_contract_code()) {
        ZJC_EMPTY_DEBUG("create contract not has valid contract code: %s",
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

    ZJC_EMPTY_DEBUG("create contract address: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    auto tmp_acc_ptr = acc_mgr_.lock();
    protos::AddressInfoPtr contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info != nullptr) {
        ZJC_WARN("contract address exists: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            default_gas * tx_msg.gas_price()) {
        ZJC_EMPTY_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
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

static transport::MessagePtr CreateTransactionWithAttr(
        std::shared_ptr<security::Security>& security,
        uint64_t nonce,
        const std::string& from_prikey,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    // auto* brd = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(security->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx); // cout 输出信息
    std::string sign;
    if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

    new_tx->set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}


static std::unordered_map<std::string, std::string> g_pri_addrs_map;
static std::vector<std::string> g_prikeys;
static std::vector<std::string> g_addrs;
static std::unordered_map<std::string, std::string> g_pri_pub_map;
static std::vector<std::string> g_oqs_prikeys;
static std::unordered_map<std::string, std::string> g_oqs_pri_pub_map;
static std::unordered_map<std::string, uint64_t> prikey_with_nonce;

static void LoadAllAccounts(int32_t shardnum=3) {
    FILE* fd = fopen((std::string("/root/shardora/init_accounts") + std::to_string(shardnum)).c_str(), "r");
    if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    bool res = true;
    std::string filed;
    const uint32_t kMaxLen = 1024;
    char* read_buf = new char[kMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        std::string prikey = common::Encode::HexDecode(std::string(read_res, 64));
        g_prikeys.push_back(prikey);
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        g_pri_pub_map[prikey] = security->GetPublicKey();
        std::string addr = security->GetAddress();
        g_pri_addrs_map[prikey] = addr;
        g_addrs.push_back(addr);
        if (g_pri_addrs_map.size() >= common::kImmutablePoolSize) {
            break;
        }
        std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(addr) << std::endl;
    }

    assert(!g_prikeys.empty());
    while (g_prikeys.size() < common::kImmutablePoolSize) {
        g_prikeys.push_back(g_prikeys[0]);
    }

    fclose(fd);
    delete[]read_buf;
}

void TxPoolManager::CreateTestTxs(uint32_t pool_begin, uint32_t pool_end, uint32_t tps) {
    LoadAllAccounts(3);
    std::shared_ptr<security::Security> pool_sec[pool_end + 1];
    for (auto i = pool_begin; i <= pool_end; ++i) {
        auto from_prikey = g_prikeys[i];
        for (uint32_t tmp_idx = 0; tmp_idx < g_prikeys.size(); ++tmp_idx) {
            from_prikey = g_prikeys[i];
            std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
            thread_security->SetPrivateKey(from_prikey);
            if (common::GetAddressPoolIndex(thread_security->GetAddress()) == i) {
                break;
            }
        }

        std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
        thread_security->SetPrivateKey(from_prikey);
        pool_sec[i] = thread_security;
        auto iter = prikey_with_nonce.find(from_prikey);
        if (iter == prikey_with_nonce.end()) {
            prikey_with_nonce[from_prikey] = 1;
        }
    }

    std::string to = common::Encode::HexDecode("27d4c39244f26c157b5a87898569ef4ce5807413");
    while (!common::GlobalInfo::Instance()->global_stoped()) {
        for (auto i = pool_begin; i <= pool_end; ++i) {
            auto from_prikey = g_prikeys[i];
            auto tx_msg_ptr = CreateTransactionWithAttr(
                pool_sec[i],
                ++prikey_with_nonce[from_prikey],
                from_prikey,
                to,
                "",
                "",
                1980,
                10000,
                1,
                3);
            pools::TxItemPtr tx_ptr = item_functions_[0](tx_msg_ptr);
            if (tx_ptr == nullptr) {
                assert(false);
                return;
            }
        
            tx_pool_[i].AddTx(tx_ptr);
            ZJC_DEBUG("success create test tx thread: %s, nonce: %lu",
                common::Encode::HexEncode(pool_sec[i]->GetAddress()).c_str(), 
                prikey_with_nonce[from_prikey]);
        }
        
        usleep(1000000lu);
    }
}

void TxPoolManager::DispatchTx(uint32_t pool_index, const transport::MessagePtr& msg_ptr) {
#ifdef USE_SERVER_TEST_TRANSACTION
    return;
#endif

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->header.tx_proto().step() >= pools::protobuf::StepType_ARRAYSIZE) {
        assert(false);
        return;
    }

    if (item_functions_[msg_ptr->header.tx_proto().step()] == nullptr) {
        ZJC_EMPTY_DEBUG("not registered step : %d", msg_ptr->header.tx_proto().step());
        assert(false);
        return;
    }

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    if (tx_ptr == nullptr) {
        assert(false);
        return;
    }

    // 交易池增加 msg 中的交易
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    tx_pool_[pool_index].AddTx(tx_ptr);
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ZJC_EMPTY_DEBUG("trace tx success add local transfer to tx pool: %u, "
        "step: %d, addr: %s, nonce: %lu, from pk: %s, to: %s",
        pool_index,
        msg_ptr->header.tx_proto().step(),
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
        tx_ptr->tx_info->nonce(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().pubkey()).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
}

void TxPoolManager::GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t pool_index,
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    tx_pool_[pool_index].GetTxSyncToLeader(leader_idx, count, txs, tx_valid_func);    
}

void TxPoolManager::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index,
        uint32_t count,
        std::vector<pools::TxItemPtr>& res_map,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    tx_pool_[pool_index].GetTxIdempotently(msg_ptr, res_map, count, tx_valid_func);    
}

}  // namespace pools

}  // namespace shardora
