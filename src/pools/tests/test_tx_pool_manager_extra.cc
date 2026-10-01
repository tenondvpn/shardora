// Additional coverage tests for TxPoolManager private methods:
//   FlushHeightTree, ConsensusTimerMessage, SyncMinssingHeights,
//   SyncMinssingRootHeights, SyncCrossPool, SyncPoolsMaxHeight,
//   BackupConsensusAddTxs, TxPoolHandleMessage (sync_heights path),
//   HandleSyncPoolsMaxHeight.
//
// Uses #define private public to access private members.
// Uses kInvalidUint32 network_id so methods with network-id guards
// return early without touching security_, acc_mgr_, or hotstuff_mgr_.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "sync/key_value_sync.h"
#include "transport/transport_utils.h"

#define private public
#define protected public
#include "db/db.h"
#include "pools/tx_pool_manager.h"
#include "pools/tx_pool.h"
#undef protected
#undef private

#include "common/global_info.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "network/network_utils.h"

namespace shardora {
namespace pools {
namespace test {

// ---------------------------------------------------------------------------
// Minimal concrete TxItem for BackupConsensusAddTxs
// ---------------------------------------------------------------------------

struct SimpleTxForPmExtra : public TxItem {
    SimpleTxForPmExtra(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, -1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

static TxItemPtr MakePmExtraTx(const std::string& addr) {
    auto msg = std::make_shared<transport::TransportMessage>();
    auto ai  = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(0);
    ai->set_addr(addr);
    msg->header.mutable_tx_proto()->set_nonce(1);
    msg->header.mutable_tx_proto()->set_step(pools::protobuf::kNormalFrom);
    return std::make_shared<SimpleTxForPmExtra>(msg, ai);
}

static std::shared_ptr<sync::KeyValueSync> MakeKvStubPmExtra() {
    auto* raw = reinterpret_cast<sync::KeyValueSync*>(1uLL);
    return std::shared_ptr<sync::KeyValueSync>(raw, [](sync::KeyValueSync*) {});
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class TestTxPoolManagerExtra : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_txpm_extra_db");
        db_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_->Init("./test_txpm_extra_db"));

        prev_net_ = common::GlobalInfo::Instance()->network_id();
        common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);

        auto kv = MakeKvStubPmExtra();
        std::shared_ptr<security::Security>         null_sec;
        std::shared_ptr<block::AccountManager>      null_acc;
        std::shared_ptr<consensus::HotstuffManager> null_hotstuff;
        mgr_ = std::make_shared<TxPoolManager>(null_sec, db_, kv, null_acc, null_hotstuff);
    }

    static void TearDownTestSuite() {
        mgr_.reset();
        common::GlobalInfo::Instance()->set_network_id(prev_net_);
    }

    static std::shared_ptr<db::Db>        db_;
    static std::shared_ptr<TxPoolManager> mgr_;
    static uint32_t                       prev_net_;
};

std::shared_ptr<db::Db>        TestTxPoolManagerExtra::db_       = nullptr;
std::shared_ptr<TxPoolManager> TestTxPoolManagerExtra::mgr_      = nullptr;
uint32_t                       TestTxPoolManagerExtra::prev_net_ = common::kInvalidUint32;

// ---------------------------------------------------------------------------
// FlushHeightTree
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, FlushHeightTree_NoCrash) {
    mgr_->FlushHeightTree();
}

// ---------------------------------------------------------------------------
// SyncMinssingHeights — early return (kInvalidUint32)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, SyncMinssingHeights_EarlyReturn) {
    mgr_->SyncMinssingHeights(common::TimeUtils::TimestampMs());
}

// ---------------------------------------------------------------------------
// SyncMinssingRootHeights — root_cross_pools_ non-null, but kInvalidUint32
// → second early return
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, SyncMinssingRootHeights_EarlyReturn) {
    mgr_->SyncMinssingRootHeights(common::TimeUtils::TimestampMs());
}

// ---------------------------------------------------------------------------
// SyncCrossPool — now_valid_end_shard()=0 < kConsensusShardBeginNetworkId=3
// → loop never runs
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, SyncCrossPool_NoCrash) {
    mgr_->SyncCrossPool();
}

// ---------------------------------------------------------------------------
// SyncPoolsMaxHeight — now_max_sharding_id_=0 < kRootCongressNetworkId=2
// → loop never runs, no Route::Send
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, SyncPoolsMaxHeight_EmptyLoop_NoCrash) {
    mgr_->SyncPoolsMaxHeight();
}

// ---------------------------------------------------------------------------
// ConsensusTimerMessage — all prev_*_ms_ = 0 → all four branches fire
// (each sub-method has early return for kInvalidUint32)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, ConsensusTimerMessage_AllBranchesFire_NoCrash) {
    mgr_->prev_sync_height_tree_tm_ms_ = 0;
    mgr_->prev_sync_check_ms_          = 0;
    mgr_->prev_sync_heights_ms_        = 0;
    mgr_->prev_sync_cross_ms_          = 0;
    mgr_->ConsensusTimerMessage();
}

// ---------------------------------------------------------------------------
// ConsensusTimerMessage — none of the thresholds are exceeded (no-op path)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, ConsensusTimerMessage_NoThresholdsMet_NoCrash) {
    auto far_future = common::TimeUtils::TimestampMs() + 9999999lu;
    mgr_->prev_sync_height_tree_tm_ms_ = far_future;
    mgr_->prev_sync_check_ms_          = far_future;
    mgr_->prev_sync_heights_ms_        = far_future;
    mgr_->prev_sync_cross_ms_          = far_future;
    mgr_->ConsensusTimerMessage();
}

// ---------------------------------------------------------------------------
// BackupConsensusAddTxs — pool not full → ConsensusAddTxs called
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, BackupConsensusAddTxs_PoolNotFull_ReturnsSuccess) {
    std::string addr(common::kUnicastAddressLength, '\x10');
    auto tx = MakePmExtraTx(addr);
    int rc = mgr_->BackupConsensusAddTxs(nullptr, 0, tx);
    EXPECT_EQ(rc, kPoolsSuccess);
}

// ---------------------------------------------------------------------------
// TxPoolHandleMessage: sync_heights req=true → early return in HandleSync
// (kInvalidUint32 >= kConsensusShardEndNetworkId)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, TxPoolHandleMessage_SyncHeightsReq_NoCrash) {
    auto msg = std::make_shared<transport::TransportMessage>();
    msg->header.set_src_sharding_id(network::kConsensusShardBeginNetworkId);
    msg->header.mutable_sync_heights()->set_req(true);
    mgr_->TxPoolHandleMessage(msg);
}

// ---------------------------------------------------------------------------
// HandleSyncPoolsMaxHeight: no sync_heights field → second guard exits
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, HandleSyncPoolsMaxHeight_NoSyncHeights_EarlyReturn) {
    auto msg = std::make_shared<transport::TransportMessage>();
    // no sync_heights → early return at line 762
    mgr_->HandleSyncPoolsMaxHeight(msg);
}

// ---------------------------------------------------------------------------
// HandleSyncPoolsMaxHeight: sync_heights present but req=false and
// src_net_id >= kConsensusShardEndNetworkId (kInvalidUint32 = large) → early return
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, HandleSyncPoolsMaxHeight_ResponseMsg_LargeSrcNet_EarlyReturn) {
    auto msg = std::make_shared<transport::TransportMessage>();
    // src_sharding_id = kConsensusShardEndNetworkId (large) and req=false
    msg->header.set_src_sharding_id(network::kConsensusShardEndNetworkId);
    msg->header.mutable_sync_heights()->set_req(false);
    mgr_->HandleSyncPoolsMaxHeight(msg);
}

// ---------------------------------------------------------------------------
// PoolTimerMessage: push a sync_heights message, drain without crash
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra, PoolTimerMessage_WithSyncMessage_DrainQueue) {
    auto msg = std::make_shared<transport::TransportMessage>();
    msg->header.set_src_sharding_id(network::kConsensusShardBeginNetworkId);
    msg->header.mutable_sync_heights()->set_req(true);
    mgr_->pools_msg_queue_[0].push(msg);
    mgr_->PoolTimerMessage();
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
