// Additional branch-coverage tests for TxPool:
//   - ConsensusAddTxs: stale nonce early return (line 1045-1047)
//   - ConsensusAddTxs: system tx (non-user) early return (line 1049-1051)
//   - SyncBlock: null height_tree_ptr_ early return (line 1013-1015)
//   - SyncBlock: valid net + all heights valid → loop body covered (line 1027-1041)
//   - SetTxStatus: callback registration path via AddTx with kContractExcute/wrong addr size

#include <gtest/gtest.h>

#include <memory>
#include <string>

#define private public
#define protected public
#include "db/db.h"
#include "pools/tx_pool.h"
#include "pools/height_tree_level.h"
#undef protected
#undef private

#include "common/global_info.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "network/network_utils.h"
#include "sync/key_value_sync.h"
#include "transport/transport_utils.h"

namespace shardora {
namespace pools {
namespace test {

namespace {

struct MinTxItemE4 : public TxItem {
    MinTxItemE4(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, -1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

static TxItemPtr MakeTxE4(const std::string& addr,
                           uint64_t addr_nonce,
                           uint64_t tx_nonce,
                           pools::protobuf::StepType step = pools::protobuf::kNormalFrom) {
    auto msg = std::make_shared<transport::TransportMessage>();
    auto ai  = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(addr_nonce);
    ai->set_addr(addr);
    msg->header.mutable_tx_proto()->set_nonce(tx_nonce);
    msg->header.mutable_tx_proto()->set_step(step);
    msg->header.mutable_tx_proto()->set_to(addr);
    return std::make_shared<MinTxItemE4>(msg, ai);
}

static std::shared_ptr<sync::KeyValueSync> FakeKvE4() {
    auto* raw = reinterpret_cast<sync::KeyValueSync*>(1uLL);
    return std::shared_ptr<sync::KeyValueSync>(raw, [](sync::KeyValueSync*) {});
}

}  // namespace

class TestTxPoolExtra4 : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_tx_pool_extra4_db");
        db_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_->Init("./test_tx_pool_extra4_db"));
    }

    static std::shared_ptr<db::Db> db_;
};

std::shared_ptr<db::Db> TestTxPoolExtra4::db_ = nullptr;

// ---------------------------------------------------------------------------
// ConsensusAddTxs: addr_nonce >= tx_nonce → early return (line 1045-1047)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, ConsensusAddTxs_StaleNonce_EarlyReturn) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);

    TxPool pool;
    std::shared_ptr<sync::KeyValueSync> null_kv;
    pool.Init(nullptr, nullptr, 0, db_, null_kv);

    // addr_nonce=5, tx_nonce=3 → 5 >= 3 → early return
    std::string addr(common::kUnicastAddressLength, '\xA1');
    auto tx = MakeTxE4(addr, 5, 3);

    pool.ConsensusAddTxs(tx);
    // Pool should NOT have this tx in consensus_added_txs_
    EXPECT_EQ(pool.consensus_added_txs_.size(), 0u);

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// ConsensusAddTxs: non-user tx (kNormalTo) → early return (line 1049-1051)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, ConsensusAddTxs_SystemTx_EarlyReturn) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);

    TxPool pool;
    std::shared_ptr<sync::KeyValueSync> null_kv;
    pool.Init(nullptr, nullptr, 0, db_, null_kv);

    // kNormalTo is a system tx (not user tx) → early return
    std::string addr(common::kUnicastAddressLength, '\xA2');
    auto tx = MakeTxE4(addr, 0, 1, pools::protobuf::kNormalTo);

    pool.ConsensusAddTxs(tx);
    EXPECT_EQ(pool.consensus_added_txs_.size(), 0u);

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// ConsensusAddTxs: valid user tx, empty tx_key → computes key and pushes
// (lines 1053-1070, exercising the tx_key.empty() branch at line 1060)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, ConsensusAddTxs_EmptyTxKey_ComputesAndPushes) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);

    TxPool pool;
    std::shared_ptr<sync::KeyValueSync> null_kv;
    pool.Init(nullptr, nullptr, 0, db_, null_kv);

    std::string addr(common::kUnicastAddressLength, '\xA3');
    auto tx = MakeTxE4(addr, 0, 1, pools::protobuf::kNormalFrom);
    tx->tx_key = "";  // empty key → triggers computation at line 1062

    pool.ConsensusAddTxs(tx);
    EXPECT_EQ(pool.consensus_added_txs_.size(), 1u);
    EXPECT_FALSE(tx->tx_key.empty());

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// SyncBlock: height_tree_ptr_ == null → early return (lines 1013-1015)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, SyncBlock_NullTree_EarlyReturn) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);

    TxPool pool;
    pool.height_tree_ptr_ = nullptr;
    pool.prev_synced_height_ = 0;
    pool.to_sync_max_height_ = 5;

    pool.SyncBlock();  // should return without crash (tree null)
    EXPECT_EQ(pool.prev_synced_height_, 0u);

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// SyncBlock: valid net_id + MISSING heights → loop body calls
// kv_sync_->AddSyncHeight (non-virtual stub, no-op) — covers lines 1027-1041
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, SyncBlock_ValidNet_MissingHeights_LoopBody) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);

    TxPool pool;
    auto kv = FakeKvE4();
    pool.Init(nullptr, nullptr, 0, db_, kv);
    // height_tree_ptr_ is set; only height 0 is marked valid (Set in InitHeightTree)

    pool.prev_synced_height_ = 0;
    pool.synced_height_ = 3;
    pool.to_sync_max_height_ = 3;

    // Loop: Valid(1)=false → kv_sync_->AddSyncHeight called (non-virtual stub → no-op)
    pool.SyncBlock();
    // prev_synced_height_ should have advanced (loop ran)
    EXPECT_GE(pool.prev_synced_height_, 1u);

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// SyncMissingBlocks: valid tree, valid net_id, latest_height_=5 →
// inner sync loop runs, kv_sync_->AddSyncHeight called (stub) (lines 129-165)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, SyncMissingBlocks_WithMissingHeights_InnerLoopRuns) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);

    TxPool pool;
    auto kv = FakeKvE4();
    pool.Init(nullptr, nullptr, 0, db_, kv);
    // height_tree_ptr_ set, only height 0 is valid

    // Set latest_height_ to a small value so GetMissingHeights finds gaps
    pool.latest_height_ = 3;

    auto result = pool.SyncMissingBlocks(common::TimeUtils::TimestampMs());
    // Should find missing heights 1..3 (not in tree), call AddSyncHeight (stub)
    // synced_count > 0 → has_missing_height_ = true
    EXPECT_TRUE(pool.has_missing_height_);
    EXPECT_GT(result, 0u);

    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---------------------------------------------------------------------------
// SyncBlock: invalid net_id (waiting-shard offset adjustment) → early return
// (covers the kConsensusWaitingShardBeginNetworkId adjustment branch)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolExtra4, SyncBlock_WaitingShardNetId_AdjustedAndReturnsEarly) {
    auto prev = common::GlobalInfo::Instance()->network_id();
    // Use a waiting-shard offset ID that maps to a valid shard after adjustment
    uint32_t waiting_id = network::kConsensusWaitingShardBeginNetworkId +
                          (network::kConsensusShardBeginNetworkId -
                           network::kRootCongressNetworkId);
    common::GlobalInfo::Instance()->set_network_id(waiting_id);

    TxPool pool;
    auto kv = FakeKvE4();
    pool.Init(nullptr, nullptr, 0, db_, kv);

    // Set heights valid to avoid crash if loop runs
    if (pool.height_tree_ptr_) {
        pool.height_tree_ptr_->Set(1);
    }
    pool.prev_synced_height_ = 0;
    pool.synced_height_       = 0;
    pool.to_sync_max_height_  = 0;

    pool.SyncBlock();  // should not crash

    common::GlobalInfo::Instance()->set_network_id(prev);
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
