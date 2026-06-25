#include "pools/tx_pool_manager.h"

#include <chrono>
#include <thread>

#include "block/account_manager.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "consensus/consensus_utils.h"
#include "consensus/hotstuff/hotstuff_manager.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "security/ecdsa/ecdsa.h"
#include "security/eth_verify.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"

#ifdef SHARDORA_UNITTEST
#include <functional>
#endif

namespace shardora {

namespace pools {

namespace {

bool TxGasLimitWithinBlockLimit(const pools::protobuf::TxMessage& tx_msg) {
    if (tx_msg.gas_limit() <= consensus::kBlockMaxGasLimit) {
        return true;
    }

    SHARDORA_WARN("tx gas limit exceeds block gas limit, step: %d, nonce: %lu, "
        "tx_gas_limit: %lu, block_gas_limit: %lu",
        (int32_t)tx_msg.step(),
        tx_msg.nonce(),
        tx_msg.gas_limit(),
        consensus::kBlockMaxGasLimit);
    return false;
}

}  // namespace

#ifdef SHARDORA_UNITTEST
namespace {
std::function<common::BftMemberPtr(uint32_t)> g_txpm_is_other_leader_test_hook;
}  // namespace

void TxPoolManager::SetIsOtherLeaderHookForTest(std::function<common::BftMemberPtr(uint32_t)> fn) {
    g_txpm_is_other_leader_test_hook = std::move(fn);
}

void TxPoolManager::ClearIsOtherLeaderHookForTest() {
    g_txpm_is_other_leader_test_hook = nullptr;
}
#endif

TxPoolManager::TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager>& acc_mgr,
        std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr) {
    security_ = security;
    db_ = db;
    acc_mgr_ = acc_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    hotstuff_mgr_ = hotstuff_mgr;
    // prefix_db_->InitGidManager();
    kv_sync_ = kv_sync;
    cross_block_mgr_ = std::make_shared<CrossBlockManager>(db_, kv_sync_);
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(this, security_, i, db, kv_sync);
    }

    SHARDORA_DEBUG("TxPoolManager init success: %d", common::kInvalidPoolIndex);
    InitCrossPools();
    // Time blocks are consensus every 10ms
    tools_tick_.CutOff(
        10000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this));
    // Register the callback function of kPoolsMessage
    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::TxPoolHandleMessage, this, std::placeholders::_1));
#ifdef USE_SERVER_TEST_TRANSACTION
    if (common::GlobalInfo::Instance()->test_pool_index() >= 0) {
        test_tx_thread_ = std::make_shared<std::thread>(
            &TxPoolManager::CreateTestTxs, 
            this, 
            common::GlobalInfo::Instance()->test_pool_index(), 
            common::GlobalInfo::Instance()->test_pool_index(), 
            common::GlobalInfo::Instance()->test_tx_tps());
        SHARDORA_WARN("success create test tx thread.");
    }
#endif
}

TxPoolManager::~TxPoolManager() {
    destroy_.store(true, std::memory_order_release);
    tools_tick_.Destroy();

    // Narrow race: tick thread may be between dequeuing this tick and invoking the callback.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    {
        std::unique_lock<std::mutex> lk(consensus_timer_shutdown_mutex_);
        const bool done = consensus_timer_cv_.wait_for(
            lk,
            std::chrono::seconds(30),
            [&] {
                return consensus_timer_in_flight_.load(std::memory_order_acquire) == 0;
            });
        if (!done) {
            SHARDORA_ERROR(
                "TxPoolManager::~TxPoolManager: timed out waiting for consensus timer (in_flight=%u)",
                static_cast<unsigned>(
                    consensus_timer_in_flight_.load(std::memory_order_acquire)));
        }
    }

    // FlushHeightTree();
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
    cross_pools_ = new CrossPool[network::kConsensusShardEndNetworkId]; // Number of shards
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
    return transport::kFirewallCheckSuccess;
}

int TxPoolManager::TmpFirewallCheckMessage(const transport::MessagePtr& msg_ptr) {
    // SHARDORA_DEBUG("pools message fierwall coming.");
    // return transport::kFirewallCheckSuccess;
    auto firewall_begin_ms = common::TimeUtils::TimestampMs();
    auto& header = msg_ptr->header;
    auto& tx_msg = *header.mutable_tx_proto();
    if (!IsUserTransaction(tx_msg.step())) {
        if (!msg_ptr->system_message) {
            auto addr = security_->GetAddressWithPublicKey(tx_msg.pubkey());
            SHARDORA_DEBUG("pools message fierwall coming is system message, "
                "step: %u, from: %s, to: %s", (uint32_t)tx_msg.step(),
                common::Encode::HexEncode(addr).c_str(), 
                common::Encode::HexEncode(tx_msg.to()).c_str());
            return transport::kFirewallCheckError;
        }

        return transport::kFirewallCheckSuccess;
    }
    
    if (msg_ptr->header.has_sync_heights() && !msg_ptr->header.has_tx_proto()) {
        // TODO: check all message with valid signature
        SHARDORA_DEBUG("pools message fierwall coming is sync heights.");
        return transport::kFirewallCheckSuccess;
    }

    if (!tx_msg.has_sign() || !tx_msg.has_pubkey() ||
            tx_msg.sign().empty() || tx_msg.pubkey().empty()) {
        SHARDORA_DEBUG("pools check firewall message failed, invalid sign or pk. sign: %d, pk: %d, hash64: %lu", 
            tx_msg.sign().size(), tx_msg.pubkey().size(), header.hash64());
        msg_ptr->set_status(transport::kTxInvalidSignature);
        return transport::kFirewallCheckError;
    }

    if (!account_tx_qps_check_.check(tx_msg.pubkey())) {
        SHARDORA_DEBUG("pools check firewall message failed, invalid qps limit pk: %d, hash64: %lu", 
            tx_msg.pubkey().size(), header.hash64());
        msg_ptr->set_status(transport::kTxUserNonceInvalid);
        return transport::kFirewallCheckError;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    // Inject WS notify callback now that msg_hash is known.
    // Chain with any existing callback (e.g. the HTTP handle_status updater set by http_handler).
    if (tx_status_cb_) {
        auto cb = tx_status_cb_;
        auto existing_cb = msg_ptr->status_notify_cb;  // may be set by http_handler
        if (existing_cb) {
            // Chain: call both the existing callback and the WS callback
            msg_ptr->status_notify_cb = [cb, existing_cb](
                    const std::string& hash, transport::MessageHandleStatus s) {
                existing_cb(hash, s);
                cb(common::Encode::HexEncode(hash), s);
            };
        } else {
            msg_ptr->status_notify_cb = [cb](const std::string& hash, transport::MessageHandleStatus s) {
                cb(common::Encode::HexEncode(hash), s);
            };
        }
    }

    // ── ETH-format transaction (from eth_sendRawTransaction) ─────────────
    if (tx_msg.has_eth_raw_tx() && !tx_msg.eth_raw_tx().empty()) {
        // ETH transactions have already been verified in http_handler.cc during
        // RLP decoding and signature recovery. The address_info was also set there
        // (including auto-registration for new addresses).
        // Skip signature verification here to avoid redundant checks.
        
        if (tx_msg.pubkey().empty() || tx_msg.sign().empty()) {
            SHARDORA_ERROR("ETH tx missing pubkey or sign");
            msg_ptr->set_status(transport::kTxInvalidSignature);
            return transport::kFirewallCheckError;
        }

        // Use address_info that was already set in http_handler.cc
        if (msg_ptr->address_info == nullptr) {
            // Fallback: try to get from account manager
            auto tmp_acc_ptr = acc_mgr_.lock();
            msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(
                security_->GetAddressWithPublicKey(tx_msg.pubkey()));
            
            if (msg_ptr->address_info == nullptr) {
                SHARDORA_DEBUG("ETH tx: address not found: %s",
                    common::Encode::HexEncode(
                        security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str());
                msg_ptr->set_status(transport::kTxInvalidAddress);
                return transport::kFirewallCheckError;
            }
        }
        
        SHARDORA_DEBUG("ETH tx passed firewall check, from: %s",
            common::Encode::HexEncode(
                security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str());
        // Fall through to shard check + leader routing below
    } else if (tx_msg.pubkey().size() == 64u) {
        security::GmSsl gmssl;
        if (gmssl.Verify(
                msg_ptr->msg_hash,
                tx_msg.pubkey(),
                tx_msg.sign()) != security::kSecuritySuccess) {
            SHARDORA_ERROR("verify signature failed!");
            msg_ptr->set_status(transport::kTxInvalidSignature);
            return transport::kFirewallCheckError;
        }

        auto tmp_acc_ptr = acc_mgr_.lock();
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(gmssl.GetAddress(tx_msg.pubkey()));
        if (msg_ptr->address_info == nullptr) {
            SHARDORA_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(gmssl.GetAddress(tx_msg.pubkey())).c_str());
            msg_ptr->set_status(transport::kTxInvalidAddress);
            return transport::kFirewallCheckError;
        }
    } else if (tx_msg.pubkey().size() > 128u) {
        security::Oqs oqs;
        if (oqs.Verify(
                msg_ptr->msg_hash,
                tx_msg.pubkey(),
                tx_msg.sign()) != security::kSecuritySuccess) {
            SHARDORA_ERROR("verify signature failed msg hash: %s, pk: %s, sign: %s", 
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(),
                common::Encode::HexEncode(tx_msg.pubkey()).c_str(),
                common::Encode::HexEncode(tx_msg.sign()).c_str());
            msg_ptr->set_status(transport::kTxInvalidSignature);
            return transport::kFirewallCheckError;
        }

        auto tmp_acc_ptr = acc_mgr_.lock();
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(oqs.GetAddress(tx_msg.pubkey()));
        if (msg_ptr->address_info == nullptr) {
            SHARDORA_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(oqs.GetAddress(tx_msg.pubkey())).c_str());
            msg_ptr->set_status(transport::kTxInvalidAddress);
            return transport::kFirewallCheckError;
        }
    } else {
        if (security_->Verify(
                msg_ptr->msg_hash,
                tx_msg.pubkey(),
                tx_msg.sign()) != security::kSecuritySuccess) {
            SHARDORA_ERROR("verify signature failed!");
            msg_ptr->set_status(transport::kTxInvalidSignature);
            return transport::kFirewallCheckError;
        }

        auto tmp_acc_ptr = acc_mgr_.lock();
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(security_->GetAddressWithPublicKey(tx_msg.pubkey()));
        if (msg_ptr->address_info == nullptr) {
            SHARDORA_DEBUG("failed get account info: %s", 
                common::Encode::HexEncode(security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str());
            msg_ptr->set_status(transport::kTxInvalidAddress);
            return transport::kFirewallCheckError;
        }
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        network::Route::Instance()->Send(msg_ptr);
        return transport::kFirewallCheckError;
    }

    tx_msg.set_tx_hash(msg_ptr->msg_hash);
    auto firewall_end_ms = common::TimeUtils::TimestampMs();
    if (firewall_end_ms - firewall_begin_ms >= 2) {
    }
    return transport::kFirewallCheckSuccess;
}

void TxPoolManager::SyncCrossPool() {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    for (uint32_t shard_id = network::kConsensusShardBeginNetworkId;
            shard_id <= common::GlobalInfo::Instance()->now_valid_end_shard(); ++shard_id) {
        //assert(i < network::kServiceShardEndNetworkId);
        auto sync_count = cross_pools_[shard_id].SyncMissingBlocks(now_tm_ms);
        uint64_t ex_height = common::kInvalidUint64;
        if (cross_pools_[shard_id].latest_height() == common::kInvalidUint64) {
            ex_height = 1;
        } else {
            if (cross_pools_[shard_id].latest_height() < cross_synced_max_heights_[shard_id]) {
                ex_height = cross_pools_[shard_id].latest_height() + 1;
            }
        }

        if (ex_height != common::kInvalidUint64) {
            uint32_t count = 0;
            for (uint64_t height = ex_height;
                    height <= cross_synced_max_heights_[shard_id] && count < 64;
                    ++height, ++count) {
                SHARDORA_DEBUG("now add sync height 1, %u_%u_%lu",
                    shard_id,
                    common::kGlobalPoolIndex,
                    height);
                kv_sync_->AddSyncHeight(
                    shard_id,
                    common::kGlobalPoolIndex,
                    height,
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
        for (uint32_t i = network::kConsensusShardBeginNetworkId;
                i < network::kConsensusShardEndNetworkId && i <= now_max_sharding_id_;
                ++i) {
            cross_pools_[i].FlushHeightTree(db_batch);
        }
    }

//     SHARDORA_DEBUG("success call FlushHeightTree");
    auto st = db_->Put(db_batch);
    if (!st.ok()) {
        SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
    }
}

void TxPoolManager::OnConsensusTimerEnter() {
    consensus_timer_in_flight_.fetch_add(1, std::memory_order_acq_rel);
}

void TxPoolManager::OnConsensusTimerLeave() {
    const uint32_t prev = consensus_timer_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        std::lock_guard<std::mutex> lk(consensus_timer_shutdown_mutex_);
        consensus_timer_cv_.notify_all();
    }
}

void TxPoolManager::ConsensusTimerMessage() {
    OnConsensusTimerEnter();
    struct ConsensusTimerLeaveCaller {
        TxPoolManager* m;
        explicit ConsensusTimerLeaveCaller(TxPoolManager* mgr) : m(mgr) {}
        ~ConsensusTimerLeaveCaller() {
            m->OnConsensusTimerLeave();
        }
    } leave_guard{ this };

    if (destroy_.load(std::memory_order_acquire)) {
        return;
    }

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
        SHARDORA_DEBUG("TxPoolManager handle message use time: %lu", (etime - now_tm_ms));
    }

    if (!destroy_.load(std::memory_order_acquire)) {
        tools_tick_.CutOff(
            100000lu,
            std::bind(&TxPoolManager::ConsensusTimerMessage, this));
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
            SHARDORA_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                root_prev_synced_pool_index_,
                res, 
                root_cross_pools_[root_prev_synced_pool_index_].latest_height(),
                static_cast<uint64_t>(root_synced_max_heights_[root_prev_synced_pool_index_]));
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
            SHARDORA_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                root_prev_synced_pool_index_,
                res, root_cross_pools_[root_prev_synced_pool_index_].latest_height(),
                static_cast<uint64_t>(root_synced_max_heights_[root_prev_synced_pool_index_]));
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
            SHARDORA_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, 
                tx_pool_[prev_synced_pool_index_].latest_height(),
                static_cast<uint64_t>(synced_max_heights_[prev_synced_pool_index_]));
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
            SHARDORA_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                static_cast<uint64_t>(synced_max_heights_[prev_synced_pool_index_]));
        }
    }
}

void TxPoolManager::SyncRootBlockWithMaxHeights(uint32_t pool_idx, uint64_t height) {
    if (kv_sync_ == nullptr) {
        return;
    }

    SHARDORA_DEBUG("now add sync height 1, %u_%u_%lu", 
        network::kRootCongressNetworkId,
        pool_idx,
        height);
    kv_sync_->AddSyncHeight(
        network::kRootCongressNetworkId,
        pool_idx,
        height + 1,  // not commited
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

    SHARDORA_DEBUG("now add sync height 1, %u_%u_%lu", 
        net_id,
        pool_idx,
        height);
    kv_sync_->AddSyncHeight(
        net_id,
        pool_idx,
        height,
        sync::kSyncHigh);
}

void TxPoolManager::PoolTimerMessage() {
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        transport::MessagePtr msg_ptr;
        while (pools_msg_queue_[i].pop(&msg_ptr)) {
            TxPoolHandleMessage(msg_ptr);
        }
    }

    to_confirm_latency_tracker_.ProcessEvents();
}

void TxPoolManager::OnTxPoolAddTx(
        int32_t step,
        const std::string& to,
        const std::string& tx_value) {
    if (step == pools::protobuf::kNormalFrom) {
        if (ShouldRecordToConfirmStart(to)) {
            to_confirm_latency_tracker_.OnStart(
                ToConfirmLatencyTracker::NormalizeToKey(to));
        }
        return;
    }

    if (step == pools::protobuf::kConsensusLocalTos) {
        to_confirm_latency_tracker_.OnConfirmFromLocalToTx(to, tx_value);
    }
}

void TxPoolManager::OnCrossShardToStart(
        const std::string& des,
        uint64_t start_timestamp_us) {
    const std::string key = ToConfirmLatencyTracker::NormalizeToKey(des);
    if (key.empty()) {
        return;
    }

    to_confirm_latency_tracker_.OnStart(key, start_timestamp_us);
}

bool TxPoolManager::ShouldRecordToConfirmStart(const std::string& to) const {
    const std::string key = ToConfirmLatencyTracker::NormalizeToKey(to);
    if (key.empty()) {
        return false;
    }

    auto acc_mgr = acc_mgr_.lock();
    if (!acc_mgr) {
        return true;
    }

    auto to_info = acc_mgr->GetAccountInfo(key);
    if (to_info == nullptr) {
        return true;
    }

    return network::IsSameToLocalShard(to_info->sharding_id());
}

void TxPoolManager::TxPoolHandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    // auto thread_idx = common::GlobalInfo::Instance()->get_thread_index(msg_ptr);
    // for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
    //     if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_idx) {
    //         tx_pool_[pool_idx].CheckPopedTxs();
    //     }
    // }
    auto& header = msg_ptr->header;
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_sync_heights()) {
        SHARDORA_DEBUG("header.has_sync_heights()");
        HandleSyncPoolsMaxHeight(msg_ptr);
        msg_ptr->set_status(transport::kRequestInvalid);
        return;
    }

    if (TmpFirewallCheckMessage(msg_ptr) != transport::kFirewallCheckSuccess) {
        msg_ptr->set_status(transport::kRequestInvalid);
        return;
    }

    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        if (IsUserTransaction(tx_msg.step())) {
            auto tmp_acc_ptr = acc_mgr_.lock();
            protos::AddressInfoPtr address_info = nullptr;
            std::string addr = security_->GetAddressWithPublicKey(tx_msg.pubkey());
            if (tx_msg.step() == pools::protobuf::kContractExcute || 
                    tx_msg.step() == pools::protobuf::kContractRefund) {
                addr = tx_msg.to() + addr;
            }

            address_info = tmp_acc_ptr->GetAccountInfo(addr);
            if (!address_info) {
                msg_ptr->set_status(transport::kTxInvalidAddress);
                return;
            }

            if (!NewTxValid(address_info->pool_index(), address_info->addr(), tx_msg.nonce())) {
                SHARDORA_DEBUG("add failed extend %u, %u, all valid: %u", 
                    tx_pool_[address_info->pool_index()].all_tx_size(), 
                    common::GlobalInfo::Instance()->each_tx_pool_max_txs(), 
                    tx_pool_[address_info->pool_index()].all_tx_size());
                msg_ptr->set_status(transport::kTxUserNonceInvalid);
                return;
            }

            msg_ptr->address_info = address_info;
// #ifndef NDEBUG
//             auto now_tm = common::TimeUtils::TimestampMs();
//             ++prev_tps_count_;
//             uint64_t dur = 1000lu;
//             if (now_tm > prev_show_tm_ms_ + dur) {
//                 SHARDORA_DEBUG("pools stored message size: %d, pool index: %d, tx all size: %u, tps: %lu", 
//                         -1,
//                         address_info->pool_index(),
//                         tx_pool_[address_info->pool_index()].all_tx_size(),
//                         (prev_tps_count_/(dur / 1000)));
//                 prev_show_tm_ms_ = now_tm;
//                 prev_tps_count_ = 0;
//             }
// #endif
        }
    }

    
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    HandlePoolsMessage(msg_ptr);
    // pop_tx_con_.notify_one();
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_tx_proto() && IsUserTransaction(header.tx_proto().step()) && 
            (msg_ptr->handle_status == transport::kMessageHandle || 
            msg_ptr->handle_status == transport::kTxAccept)) {
        auto& tx_msg = header.tx_proto();
        common::BftMemberPtr leader = nullptr;
#if defined(SHARDORA_UNITTEST)
        if (g_txpm_is_other_leader_test_hook) {
            leader = g_txpm_is_other_leader_test_hook(msg_ptr->address_info->pool_index());
        } else
#endif
        if (hotstuff_mgr_) {
            leader = hotstuff_mgr_->is_other_leader(msg_ptr->address_info->pool_index());
        }
        if (leader) {
            auto network_id = network::GetLocalConsensusNetworkId();
            auto dht = network::DhtManager::Instance()->GetDht(network_id);
            if (dht == nullptr) {
                dht = network::UniversalManager::Instance()->GetUniversal(network::kNodeNetworkId);
            }

            auto dht_vec = dht->readonly_hash_sort_dht();
            auto node_it = std::find_if(dht_vec->begin(), dht_vec->end(), [&](const auto& item) {
                return item->id == leader->id;
            });

            if (node_it != dht_vec->end()) {
                auto found_node = *node_it; 
                transport::TcpTransport::Instance()->Send(
                    found_node->public_ip, 
                    found_node->public_port, 
                    msg_ptr->header);
                SHARDORA_DEBUG("send tx message to leader, leader id: %s, ip: %s, port: %d, hash64: %lu, from: %s, to: %s, nonce: %lu", 
                    common::Encode::HexEncode(leader->id).c_str(),
                    found_node->public_ip.c_str(),
                    found_node->public_port,
                    header.hash64(),
                    common::Encode::HexEncode(security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str(),
                    common::Encode::HexEncode(tx_msg.to()).c_str(),
                    tx_msg.nonce());
            } else {
                network::Route::Instance()->Send(msg_ptr);
                SHARDORA_DEBUG("send tx message to leader, leader id: %s, by route, hash64: %lu, from: %s, to: %s, nonce: %lu", 
                    common::Encode::HexEncode(leader->id).c_str(),
                    header.hash64(),
                    common::Encode::HexEncode(security_->GetAddressWithPublicKey(tx_msg.pubkey())).c_str(),
                    common::Encode::HexEncode(tx_msg.to()).c_str(),
                    tx_msg.nonce());
            }
            
        }
    }
}

int TxPoolManager::BackupConsensusAddTxs(
        transport::MessagePtr msg_ptr, 
        uint32_t pool_index, 
        const pools::TxItemPtr& valid_tx) {
    if (tx_pool_[pool_index].all_tx_size() >= 
            common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        SHARDORA_DEBUG("add failed extend %u, %u, all valid: %u", 
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
        SHARDORA_DEBUG("success handle message hash64: %lu, from: %s, to: %s, type: %d, nonce: %lu",
            msg_ptr->header.hash64(),
            common::Encode::HexEncode(tx_msg.pubkey()).c_str(),
            common::Encode::HexEncode(tx_msg.to()).c_str(),
            (int32_t)tx_msg.step(),
            tx_msg.nonce());
        int32_t handle_status = transport::kMessageHandle;
        if (IsUserTransaction(tx_msg.step()) && !TxGasLimitWithinBlockLimit(tx_msg)) {
            handle_status = consensus::kConsensusUserSetGasLimitError;
        }
        switch (tx_msg.step()) {
        case pools::protobuf::kJoinElect:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleElectTx(msg_ptr);
            }
            break;
        case pools::protobuf::kNormalFrom:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleNormalFromTx(msg_ptr);
            }
            break;
        case pools::protobuf::kCreateLibrary:
        case pools::protobuf::kCreateContract:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleCreateContractTx(msg_ptr);
            }
            break;
        case pools::protobuf::kContractGasPrefund:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleSetContractPrefund(msg_ptr);
            }
            break;
        case pools::protobuf::kContractRefund:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleContractRefund(msg_ptr);
            }
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
            SHARDORA_DEBUG("get local tokRootCreateAddress tx message hash: %s, to: %s, amount: %lu nonce: %lu", 
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(),
                common::Encode::HexEncode(tx_msg.to()).c_str(),
                tx_msg.amount(),
                tx_msg.nonce());
            pool_index = common::GetAddressPoolIndex(
                tx_msg.to().substr(0, common::kUnicastAddressLength)) % common::kImmutablePoolSize;
            break;
        }
        case pools::protobuf::kContractExcute:
            if (handle_status == transport::kMessageHandle) {
                handle_status = HandleContractExcute(msg_ptr);
            }
            break;
        case pools::protobuf::kConsensusLocalTos: {
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
            SHARDORA_DEBUG("get local to tx message hash: %s, nonce: %lu",
                common::Encode::HexEncode(msg_ptr->msg_hash).c_str(), 
                msg_ptr->header.tx_proto().nonce());
            break;
        }
        case pools::protobuf::kRootCross: {
            pool_index = common::GetAddressPoolIndex(tx_msg.to());
            break;
        }
        case pools::protobuf::kPoolStatisticTag: {
            pool_index = msg_ptr->address_info->pool_index();
            break;
        }
        case pools::protobuf::kStatistic: {
            pool_index = msg_ptr->address_info->pool_index();
            break;
        }
        case pools::protobuf::kConsensusRootElectShard: {
            pool_index = msg_ptr->address_info->pool_index();
            break;
        }
        default:
            SHARDORA_DEBUG("invalid tx step: %d", (int32_t)tx_msg.step());
            //assert(false);
            break;
        }

        if (handle_status != transport::kMessageHandle) {
            msg_ptr->set_status((transport::MessageHandleStatus)handle_status);
            return;
        }

        if (pool_index == common::kInvalidPoolIndex) {
            if (msg_ptr->address_info == nullptr) {
                SHARDORA_DEBUG("invalid tx step: %d, address invalid.", (int32_t)tx_msg.step());
                if (handle_status == transport::kMessageHandle) {
                    msg_ptr->set_status(transport::kTxInvalidAddress);
                }
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
    const uint32_t local_net = common::GlobalInfo::Instance()->network_id();
    if (local_net == common::kInvalidUint32) {
        return;
    }
    if (network::kConsensusShardEndNetworkId <= network::kRootCongressNetworkId) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->header.set_src_sharding_id(local_net);

    uint32_t last = now_max_sharding_id_;
    const uint32_t cap = network::kConsensusShardEndNetworkId - 1;
    if (last > cap) {
        last = cap;
    }
    if (last < network::kRootCongressNetworkId) {
        return;
    }

    for (uint32_t i = network::kRootCongressNetworkId; i <= last; ++i) {
        dht::DhtKeyManager dht_key(i);
        msg_ptr->header.set_des_dht_key(dht_key.StrKey());
        msg_ptr->header.set_type(common::kPoolsMessage);
        auto* sync_heights = msg_ptr->header.mutable_sync_heights();
        sync_heights->set_req(true);
        transport::TcpTransport::Instance()->SetMessageHash(msg_ptr->header);
        SHARDORA_DEBUG("sync net data from network: %u, hash64: %lu, src sharding id: %u",
            i, msg_ptr->header.hash64(), msg_ptr->header.src_sharding_id());
        network::Route::Instance()->Send(msg_ptr);
    }
}

void TxPoolManager::HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr) {
    if (tx_pool_ == nullptr) {
        SHARDORA_DEBUG("tx_pool_ == nullptr");
        return;
    }

    if (!msg_ptr->header.has_sync_heights()) {
        SHARDORA_DEBUG("!msg_ptr->header.has_sync_heights()");
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
        // transport::TcpTransport::Instance()->Send(msg_ptr->conn, msg);
        transport::TcpTransport::Instance()->Send(msg_ptr->conn->PeerIp(), msg_ptr->conn->PeerPort(), msg);
        SHARDORA_DEBUG("response pool heights: %s, cross pool heights: %s, "
            "now_max_sharding_id_: %u, src sharding id: %u, src hash64: %lu, des hash64: %lu",
            sync_debug.c_str(), cross_debug.c_str(),
            now_max_sharding_id_, msg_ptr->header.src_sharding_id(),
            msg_ptr->header.hash64(), msg.hash64());
    } else {
        if (src_net_id >= network::kConsensusShardEndNetworkId) {
            return;
        }

        auto local_des_shard_id = network::GetLocalConsensusNetworkId();
        if (src_net_id != local_des_shard_id) {
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
                
                    SHARDORA_DEBUG("net: %u, get response pool heights, cross pool heights: %lu, update_height: %lu, "
                        "cross_synced_max_heights_[i]: %lu, cross_pools_[i].latest_height(): %lu, cross_heights[i]: %lu",
                        sharding_id, update_height, update_height,
                        static_cast<uint64_t>(cross_synced_max_heights_[sharding_id]), cross_pools_[sharding_id].latest_height(),
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

                SHARDORA_DEBUG("get root response pool heights: %s", sync_debug.c_str());
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

        SHARDORA_DEBUG("get response pool heights: %s, cross pool heights: %s", sync_debug.c_str(), cross_debug.c_str());
    }
}

std::shared_ptr<address::protobuf::AddressInfo> TxPoolManager::GetAddressInfo(const std::string& address) {
    auto tmp_acc_ptr = acc_mgr_.lock();
    return tmp_acc_ptr->GetAccountInfo(address);
}

int32_t TxPoolManager::HandleElectTx(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = *header.mutable_tx_proto();
    auto addr = security_->GetAddressWithPublicKey(tx_msg.pubkey());
    auto tmp_acc_ptr = acc_mgr_.lock();
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(addr);
    if (msg_ptr->address_info == nullptr) {
        SHARDORA_WARN("no address info: %s", common::Encode::HexEncode(addr).c_str());
        return transport::kTxInvalidAddress;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        //assert(false);
        return transport::kTxInvalidAddress;
    }

    if (msg_ptr->address_info->balance() < consensus::kJoinElectGas) {
        SHARDORA_WARN("address info join elect gas invalid: %s %lu %lu", 
            common::Encode::HexEncode(addr).c_str(), 
            msg_ptr->address_info->balance(), 
            consensus::kJoinElectGas);
        return transport::kTxInvalidAddress;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        SHARDORA_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return transport::kTxInvalidAddress;
    }

    if (!tx_msg.has_key() || tx_msg.key() != protos::kJoinElectVerifyG2) {
        SHARDORA_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return transport::kRequestInvalid;
    }

    auto msg_hash = pools::GetTxMessageHash(tx_msg);
    // if (security_->Verify(
    //         msg_hash,
    //         tx_msg.pubkey(),
    //         tx_msg.sign()) != security::kSecuritySuccess) {
    //     SHARDORA_WARN("kElectJoin verify signature failed!");
    //     return;
    // }

    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(tx_msg.value())) {
        SHARDORA_WARN("join_info parse failed address info: %s", common::Encode::HexEncode(addr).c_str());
        return transport::kRequestInvalid;
    }

    uint32_t tmp_shard = join_info.shard_id();
    if (tmp_shard != network::kRootCongressNetworkId) {
        if (tmp_shard != msg_ptr->address_info->sharding_id()) {
            SHARDORA_DEBUG("join des shard error: %d,  %d.",
                tmp_shard, msg_ptr->address_info->sharding_id());
        return transport::kRequestInvalid;
        }
    }

    auto n = common::GlobalInfo::Instance()->each_shard_max_members();
    auto t = common::GetSignerCount(n);
    if (join_info.g2_req().verify_vec_size() != t) {
        SHARDORA_DEBUG("join des shard error: %d,  %d, "
            "join_info.g2_req().verify_vec_size() != t %u : %u",
            tmp_shard, msg_ptr->address_info->sharding_id(),
            join_info.g2_req().verify_vec_size(), t);
        return transport::kRequestInvalid;
    }
    
    SHARDORA_DEBUG("elect tx msg hash is %s", 
        common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
    msg_ptr->msg_hash = msg_hash;
    return transport::kMessageHandle;
}

int32_t TxPoolManager::HandleContractExcute(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    // if (tx_msg.has_key() && tx_msg.key().size() > 0) {
    //     SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    //     return;
    // }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kCallContractDefaultUseGas) {
        SHARDORA_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusOutOfGas;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    auto from = security_->GetAddressWithPublicKey(tx_msg.pubkey());
    auto contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info == nullptr) {
        SHARDORA_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractNotExists;
    }

    if (contract_info->destructed()) {
        SHARDORA_ERROR("contract destructed: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractDestructed;
    }

    auto prefund_id = tx_msg.to() + from;
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(prefund_id);
    if (msg_ptr->address_info == nullptr) {
        SHARDORA_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractPrefundNotExists;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        SHARDORA_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        msg_ptr->address_info = nullptr;
        return transport::kTxInvalidAddress;
    }

    if (msg_ptr->address_info->balance() <= (tx_msg.amount() + consensus::kCallContractDefaultUseGas * tx_msg.gas_price())) {
        SHARDORA_WARN("address %s balance invalid: %lu, transfer amount: %lu, "
            "default call contract gas: %lu, from: %s, to: %s",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            consensus::kCallContractDefaultUseGas,
            common::Encode::HexEncode(security_->GetAddressWithPublicKey(
            msg_ptr->header.tx_proto().pubkey())).c_str(),
            common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
        msg_ptr->address_info = nullptr;
        return consensus::kConsensusOutOfPrefund;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    SHARDORA_DEBUG("success add tx contract execute prepyament id: %s, prefund: %lu, nonce: %lu",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(), 
        msg_ptr->address_info->balance(), 
        msg_ptr->address_info->nonce());
    return transport::kMessageHandle;
}

int32_t TxPoolManager::HandleSetContractPrefund(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    // user can't direct call contract, pay contract prefund and call contract direct
    if (!tx_msg.contract_input().empty() ||
            tx_msg.contract_prefund() < consensus::kCallContractDefaultUseGas) {
        SHARDORA_DEBUG("call contract not has valid contract input"
            "and contract prefund invalid.");
        return transport::kRequestInvalid;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    auto contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info == nullptr) {
        msg_ptr->address_info = nullptr;
        SHARDORA_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractNotExists;
    }

    if (!UserTxValid(msg_ptr)) {
        SHARDORA_DEBUG("address %s balance invalid: %lu, transfer amount: %lu, "
            "prefund: %lu, default call contract gas: %lu, from: %s, to: %s",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prefund(),
            consensus::kCallContractDefaultUseGas,
            common::Encode::HexEncode(security_->GetAddressWithPublicKey(
            msg_ptr->header.tx_proto().pubkey())).c_str(),
            common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
        return transport::kTxInvalidAddress;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prefund() +
            consensus::kCallContractDefaultUseGas * tx_msg.gas_price()) {
        SHARDORA_WARN("address %s balance invalid: %lu, transfer amount: %lu, "
            "prefund: %lu, default call contract gas: %lu, from: %s, to: %s",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prefund(),
            consensus::kCallContractDefaultUseGas,
            common::Encode::HexEncode(security_->GetAddressWithPublicKey(
            msg_ptr->header.tx_proto().pubkey())).c_str(),
            common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
        return consensus::kConsensusAccountBalanceError;
    }

    SHARDORA_DEBUG("success add tx contract prefund id: %s, prefund: %lu, nonce: %lu",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(), 
        msg_ptr->address_info->balance(), 
        msg_ptr->address_info->nonce());
    return transport::kMessageHandle;
}


int32_t TxPoolManager::HandleContractRefund(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    // if (tx_msg.has_key() && tx_msg.key().size() > 0) {
    //     SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    //     return;
    // }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kTransferGas) {
        SHARDORA_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusOutOfGas;
    }

    auto tmp_acc_ptr = acc_mgr_.lock();
    auto from = security_->GetAddressWithPublicKey(tx_msg.pubkey());
    auto contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info == nullptr) {
        SHARDORA_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractNotExists;
    }

    if (contract_info->destructed()) {
        SHARDORA_ERROR("contract destructed: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractDestructed;
    }

    auto prefund_id = tx_msg.to() + from;
    msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(prefund_id);
    if (msg_ptr->address_info == nullptr) {
        SHARDORA_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractPrefundNotExists;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        SHARDORA_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        SHARDORA_ERROR("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        msg_ptr->address_info = nullptr;
        return transport::kTxInvalidAddress;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    SHARDORA_DEBUG("success add tx contract execute prepyament id: %s, prefund: %lu, nonce: %lu",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(), 
        msg_ptr->address_info->balance(), 
        msg_ptr->address_info->nonce());
    return transport::kMessageHandle;
}

bool TxPoolManager::UserTxValid(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    auto tmp_acc_ptr = acc_mgr_.lock();
    if (msg_ptr->address_info == nullptr) {
        msg_ptr->address_info = tmp_acc_ptr->GetAccountInfo(
            security_->GetAddressWithPublicKey(tx_msg.pubkey()));
    }

    if (msg_ptr->address_info == nullptr) {
        SHARDORA_WARN("no address info.");
        return false;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        //assert(false);
        return false;
    }

    auto local_shard = common::GlobalInfo::Instance()->network_id();
    if (local_shard >= network::kConsensusShardEndNetworkId) {
        local_shard -= network::kConsensusWaitingShardOffset;
    }

    if (msg_ptr->address_info->sharding_id() != local_shard) {
        SHARDORA_WARN("sharding error id: %s, shard: %d, local shard: %d",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        //assert(false);
        return false;
    }

    if (tx_msg.has_key() && tx_msg.key().size() > kTxStorageKeyMaxSize) {
        SHARDORA_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return false;
    }

    return true;
}

int32_t TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (!UserTxValid(msg_ptr)) {
//         //assert(false);
        return transport::kTxInvalidAddress;
    }

    // Verify that the account balance is sufficient
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prefund() +
            consensus::kTransferGas * tx_msg.gas_price()) {
        SHARDORA_WARN("address: %s balance invalid: %lu, transfer amount: %lu, "
            "prefund: %lu, default call contract gas: %lu",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prefund(),
            consensus::kCallContractDefaultUseGas);
        return consensus::kConsensusAccountBalanceError;
    }

    ADD_TX_DEBUG_INFO(msg_ptr->header.mutable_tx_proto());
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    return transport::kMessageHandle;
}

int32_t TxPoolManager::HandleCreateContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!tx_msg.has_contract_code() || tx_msg.contract_code().empty()) {
        SHARDORA_DEBUG("create contract not has valid contract code: %s",
            common::Encode::HexEncode(tx_msg.contract_code()).c_str());
        return transport::kRequestInvalid;
    }

    uint64_t default_gas = consensus::kCallContractDefaultUseGas
        + consensus::CalcKvStorageGas(0, tx_msg.value().size(), true);
    if (tx_msg.step() == pools::protobuf::kCreateContract) {
        if (common::IsContractBytescodeValid(tx_msg.contract_code()) != common::ValidationStatus::SUCCESS) {
            return consensus::kConsensusContractBytesCodeError;
        }
    } else {
        // all shards will save the library.
        default_gas = consensus::kCreateLibraryDefaultUseGas
            + consensus::CalcKvStorageGas(tx_msg.key().size(), tx_msg.value().size(), true);
    }

    if (!UserTxValid(msg_ptr)) {
        SHARDORA_ERROR("create contract error!");
        return transport::kTxInvalidAddress;
    }

    SHARDORA_DEBUG("create contract address: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
    auto tmp_acc_ptr = acc_mgr_.lock();
    protos::AddressInfoPtr contract_info = tmp_acc_ptr->GetAccountInfo(tx_msg.to());
    if (contract_info != nullptr) {
        SHARDORA_WARN("contract address exists: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return consensus::kConsensusContractAddressLocked;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prefund() +
            default_gas * tx_msg.gas_price()) {
        SHARDORA_WARN("address balance invalid: %lu, transfer amount: %lu, "
            "prefund: %lu, default call contract gas: %lu, gas price: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prefund(),
            default_gas,
            tx_msg.gas_price());
        return consensus::kConsensusAccountBalanceError;
    }

    return transport::kMessageHandle;
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

// static transport::MessagePtr CreateTransactionWithAttr(
//         std::shared_ptr<security::Security>& security,
//         uint64_t nonce,
//         const std::string& from_prikey,
//         const std::string& to,
//         const std::string& key,
//         const std::string& val,
//         uint64_t amount,
//         uint64_t gas_limit,
//         uint64_t gas_price,
//         int32_t des_net_id) {
//     auto msg_ptr = std::make_shared<transport::TransportMessage>();
//     transport::protobuf::Header& msg = msg_ptr->header;
//     dht::DhtKeyManager dht_key(des_net_id);
//     msg.set_src_sharding_id(des_net_id);
//     msg.set_des_dht_key(dht_key.StrKey());
//     msg.set_type(common::kPoolsMessage);
//     // auto* brd = msg.mutable_broadcast();
//     auto new_tx = msg.mutable_tx_proto();
//     new_tx->set_nonce(nonce);
//     new_tx->set_pubkey(security->GetPublicKeyUnCompressed());
//     new_tx->set_step(pools::protobuf::kNormalFrom);
//     new_tx->set_to(to);
//     new_tx->set_amount(amount);
//     new_tx->set_gas_limit(gas_limit);
//     new_tx->set_gas_price(gas_price);
//     if (!key.empty()) {
//         if (key == "create_contract") {
//             new_tx->set_step(pools::protobuf::kCreateContract);
//             new_tx->set_contract_code(val);
//             new_tx->set_contract_prefund(9000000000lu);
//         } else if (key == "prefund") {
//             new_tx->set_step(pools::protobuf::kContractGasPrefund);
//             new_tx->set_contract_prefund(9000000000lu);
//         } else if (key == "call") {
//             new_tx->set_step(pools::protobuf::kContractExcute);
//             new_tx->set_contract_input(val);
//         } else {
//             new_tx->set_key(key);
//             if (!val.empty()) {
//                 new_tx->set_value(val);
//             }
//         }
//     }

//     transport::TcpTransport::Instance()->SetMessageHash(msg);
//     auto tx_hash = pools::GetTxMessageHash(*new_tx); // cout output info
//     std::string sign;
//     if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
//         //assert(false);
//         return nullptr;
//     }

//     new_tx->set_sign(sign);
//     //assert(new_tx->gas_price() > 0);
//     return msg_ptr;
// }


// static std::unordered_map<std::string, std::string> g_pri_addrs_map;
// static std::vector<std::string> g_prikeys;
// static std::vector<std::string> g_addrs;
// static std::unordered_map<std::string, std::string> g_pri_pub_map;
// static std::vector<std::string> g_oqs_prikeys;
// static std::unordered_map<std::string, std::string> g_oqs_pri_pub_map;
// static std::unordered_map<std::string, uint64_t> prikey_with_nonce;
// static std::unordered_map<std::string, std::shared_ptr<address::protobuf::AddressInfo>> address_map;

// static void LoadAllAccounts(int32_t shardnum=3) {
//     FILE* fd = fopen((common::GlobalInfo::Instance()->RootPathFile("init_accounts") + std::to_string(shardnum)).c_str(), "r");
//     if (fd == nullptr) {
//         std::cout << "invalid init acc file." << std::endl;
//         exit(1);
//     }

//     bool res = true;
//     std::string filed;
//     const uint32_t kMaxLen = 1024;
//     char* read_buf = new char[kMaxLen];
//     while (true) {
//         char* read_res = fgets(read_buf, kMaxLen, fd);
//         if (read_res == NULL) {
//             break;
//         }

//         std::string prikey = common::Encode::HexDecode(std::string(read_res, 64));
//         g_prikeys.push_back(prikey);
//         std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
//         security->SetPrivateKey(prikey);
//         g_pri_pub_map[prikey] = security->GetPublicKey();
//         std::string addr = security->GetAddress();
//         g_pri_addrs_map[prikey] = addr;
//         g_addrs.push_back(addr);
//         if (g_pri_addrs_map.size() >= common::kImmutablePoolSize) {
//             break;
//         }
//         std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(addr) << std::endl;
//     }

//     //assert(!g_prikeys.empty());
//     while (g_prikeys.size() < common::kImmutablePoolSize) {
//         g_prikeys.push_back(g_prikeys[0]);
//     }

//     fclose(fd);
//     delete[]read_buf;
// }

// void TxPoolManager::CreateTestTxs(uint32_t pool_begin, uint32_t pool_end, uint32_t tps) {
    // LoadAllAccounts(3);
    // std::shared_ptr<address::protobuf::AddressInfo> address_[pool_end + 1];
    // std::shared_ptr<security::Security> pool_sec[pool_end + 1];
    // for (auto i = pool_begin; i <= pool_end; ++i) {
    //     auto from_prikey = g_prikeys[i];
    //     std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
    //     for (uint32_t tmp_idx = 0; tmp_idx < g_prikeys.size(); ++tmp_idx) {
    //         from_prikey = g_prikeys[i];
    //         thread_security->SetPrivateKey(from_prikey);
    //         if (common::GetAddressPoolIndex(thread_security->GetAddress()) == i) {
    //             break;
    //         }
    //     }

    //     pool_sec[i] = thread_security;
    //     address_map[from_prikey] = prefix_db_->GetAddressInfo(thread_security->GetAddress());
    //     prikey_with_nonce[from_prikey] = address_map[from_prikey]->nonce();
    //     SHARDORA_WARN("success get pool: %d, prikey: %s", i, common::Encode::HexEncode(from_prikey).c_str());
    // }

    // std::string to = common::Encode::HexDecode("27d4c39244f26c157b5a87898569ef4ce5807413");
    // static const uint64_t kSleepTimeMs = 100lu;
    // common::GlobalInfo::Instance()->get_thread_index();
    // usleep(120000000lu);
    // uint32_t send_out_tps = common::GlobalInfo::Instance()->test_tx_tps() / (1000lu / kSleepTimeMs);
    // while (!common::GlobalInfo::Instance()->global_stoped()) {
    //     if (item_functions_[0] == nullptr) {
    //         usleep(1000000lu);
    //         continue;
    //     }

    //     for (auto i = pool_begin; i <= pool_end; ++i) {
    //         for (uint32_t tx_idx = 0; tx_idx < send_out_tps; ++tx_idx) {
    //             if (tx_pool_[i].all_tx_size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
    //                 break;
    //             }

    //             auto from_prikey = pool_sec[i]->GetPrikey();
    //             auto tx_msg_ptr = CreateTransactionWithAttr(
    //                 pool_sec[i],
    //                 ++prikey_with_nonce[from_prikey],
    //                 from_prikey,
    //                 to,
    //                 "",
    //                 "",
    //                 1980,
    //                 10000,
    //                 1,
    //                 3);
    //             tx_msg_ptr->address_info = address_map[from_prikey];
    //             pools::TxItemPtr tx_ptr = item_functions_[0](tx_msg_ptr);
    //             if (tx_ptr == nullptr) {
    //                 //assert(false);
    //                 return;
    //             }
            
    //             tx_pool_[i].AddTx(tx_ptr);
    //             SHARDORA_DEBUG("success create test tx thread: %s, nonce: %lu",
    //                 common::Encode::HexEncode(pool_sec[i]->GetAddress()).c_str(), 
    //                 prikey_with_nonce[from_prikey]);
    //         }
    //     }
        
    //     usleep(kSleepTimeMs * 1000lu);
    // }
// }

void TxPoolManager::DispatchTx(uint32_t pool_index, const transport::MessagePtr& msg_ptr) {
#ifdef USE_SERVER_TEST_TRANSACTION
    return;
#endif

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->header.tx_proto().step() >= pools::protobuf::StepType_ARRAYSIZE) {
        //assert(false);
        return;
    }

    if (item_functions_[msg_ptr->header.tx_proto().step()] == nullptr) {
        SHARDORA_DEBUG("not registered step : %d", (int32_t)msg_ptr->header.tx_proto().step());
        //assert(false);
        msg_ptr->set_status(transport::kUnkonwn);
        return;
    }

    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    if (tx_ptr == nullptr) {
        //assert(false);
        msg_ptr->set_status(transport::kUnkonwn);
        return;
    }

    tx_ptr->sign_verified = true;
    // The transaction pool adds transactions in msg
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    tx_pool_[pool_index].AddTx(tx_ptr);
    TMP_ADD_DEBUG_PROCESS_TIMESTAMP();
    SHARDORA_DEBUG("trace tx success add local transfer to tx pool: %u, "
        "step: %d, addr: %s, nonce: %lu, from pk: %s, to: %s",
        pool_index,
        (int32_t)msg_ptr->header.tx_proto().step(),
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
        tx_ptr->tx_info->nonce(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().pubkey()).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
    msg_ptr->set_status(transport::kTxAccept);
}

void TxPoolManager::GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t pool_index,
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func,
        const std::unordered_map<std::string, uint64_t>& leader_nonce_map) {
    tx_pool_[pool_index].GetTxSyncToLeader(leader_idx, count, txs, tx_valid_func, leader_nonce_map);    
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
