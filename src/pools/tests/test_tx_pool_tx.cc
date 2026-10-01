// Branch-coverage tests for TxPool::SyncBlock (non-trivial paths) and
// TxPool::ConsensusAddTxs (early-return branches).
//
// Requires a concrete TxItem subclass (MinTxItem) to create TxItemPtrs
// without the full Init / validator chain.  The linker stub lives in
// test_pools_stubs.cc.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "sync/key_value_sync.h"
#include "transport/transport_utils.h"

#define private public
#define protected public
#include "db/db.h"
#include "pools/tx_pool.h"
#undef protected
#undef private

#include "common/global_info.h"
#include "common/utils.h"
#include "network/network_utils.h"

namespace shardora {
namespace pools {
namespace test {

// Minimal concrete TxItem: overrides pure virtuals with no-op bodies.
// Constructed with tx_info_idx=-1 so TxItem sets tx_info from msg header.
struct MinTxItem : public TxItem {
    MinTxItem(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, -1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

static std::shared_ptr<sync::KeyValueSync> MakeFakeSync() {
    auto* raw = reinterpret_cast<sync::KeyValueSync*>(1uLL);
    return std::shared_ptr<sync::KeyValueSync>(raw, [](sync::KeyValueSync*) {});
}

// Helper: create a TxItemPtr whose tx_info has the given nonce and step,
// and whose address_info has the given nonce.
static TxItemPtr MakeTxItem(uint64_t addr_nonce, uint64_t tx_nonce,
                             pools::protobuf::StepType step) {
    auto msg  = std::make_shared<transport::TransportMessage>();
    auto ai   = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(addr_nonce);
    ai->set_addr(std::string(common::kUnicastAddressLength, 'A'));
    msg->header.mutable_tx_proto()->set_nonce(tx_nonce);
    msg->header.mutable_tx_proto()->set_step(step);
    return std::make_shared<MinTxItem>(msg, ai);
}

class TestTxPoolTx : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_tx_pool_tx_db");
        db_ptr_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_ptr_->Init("./test_tx_pool_tx_db"));
    }

    static std::shared_ptr<db::Db> db_ptr_;
};

std::shared_ptr<db::Db> TestTxPoolTx::db_ptr_ = nullptr;

// ---- SyncBlock additional branches ----

// Branch: valid net_id but prev_synced_height_ >= to_sync_max_height_ → loop skipped
TEST_F(TestTxPoolTx, SyncBlock_NoWorkNeeded_LoopSkipped) {
    TxPool pool;
    pool.db_          = db_ptr_;
    pool.pool_index_  = 0;
    pool.height_tree_ptr_ = std::make_shared<HeightTreeLevel>(
        network::kConsensusShardBeginNetworkId, 0, common::kInvalidUint64, db_ptr_);
    pool.kv_sync_     = MakeFakeSync();
    pool.prev_synced_height_  = 10;
    pool.to_sync_max_height_  = 10;  // equal → loop condition false
    pool.synced_height_       = 10;
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);
    pool.SyncBlock();  // no-op because prev_synced_height_ == to_sync_max_height_
    EXPECT_EQ(pool.prev_synced_height_, 10u);
    common::GlobalInfo::Instance()->set_network_id(prev);
}

// Branch: out-of-shard-range net_id → early return before loop
TEST_F(TestTxPoolTx, SyncBlock_OutOfRangeNetId_EarlyReturn) {
    TxPool pool;
    pool.height_tree_ptr_ = std::make_shared<HeightTreeLevel>(
        network::kConsensusShardBeginNetworkId, 0, common::kInvalidUint64, db_ptr_);
    pool.kv_sync_            = MakeFakeSync();
    pool.prev_synced_height_ = 0;
    pool.to_sync_max_height_ = 10;
    pool.synced_height_      = 10;
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kRootCongressNetworkId - 1);
    pool.SyncBlock();
    EXPECT_EQ(pool.prev_synced_height_, 0u);  // unchanged: returned early
    common::GlobalInfo::Instance()->set_network_id(prev);
}

// Branch: loop runs and calls AddSyncHeight for heights not in tree
TEST_F(TestTxPoolTx, SyncBlock_LoopCallsAddSyncHeight) {
    TxPool pool;
    pool.db_         = db_ptr_;
    pool.pool_index_ = 0;
    pool.height_tree_ptr_ = std::make_shared<HeightTreeLevel>(
        network::kConsensusShardBeginNetworkId, 0, common::kInvalidUint64, db_ptr_);
    pool.height_tree_ptr_->Set(0);
    pool.kv_sync_            = MakeFakeSync();
    pool.prev_synced_height_ = 0;
    pool.to_sync_max_height_ = 3;
    pool.synced_height_      = 10;   // > prev+64 would cap, but here 0+64 > 3 so full range
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);
    pool.SyncBlock();
    // Loop should advance prev_synced_height_ towards to_sync_max_height_
    EXPECT_EQ(pool.prev_synced_height_, pool.to_sync_max_height_);
    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ---- ConsensusAddTxs early-return branches ----

// Branch: address_info->nonce() >= tx_info->nonce() → early return
TEST_F(TestTxPoolTx, ConsensusAddTxs_AddrNonceGe_EarlyReturn) {
    TxPool pool;
    // addr_nonce(5) >= tx_nonce(3) → return
    auto tx = MakeTxItem(5, 3, pools::protobuf::kNormalFrom);
    size_t before = pool.consensus_added_txs_.size();
    pool.ConsensusAddTxs(tx);
    EXPECT_EQ(pool.consensus_added_txs_.size(), before);  // queue unchanged
}

// Branch: !IsUserTransaction(step) → early return
TEST_F(TestTxPoolTx, ConsensusAddTxs_SystemTx_EarlyReturn) {
    TxPool pool;
    // kNormalTo is a system tx step, so IsUserTransaction returns false
    auto tx = MakeTxItem(0, 5, pools::protobuf::kNormalTo);
    pool.ConsensusAddTxs(tx);
    EXPECT_EQ(pool.consensus_added_txs_.size(), 0u);
}

// Branch: valid user tx → pushed to consensus_added_txs_
TEST_F(TestTxPoolTx, ConsensusAddTxs_ValidUserTx_PushedToQueue) {
    TxPool pool;
    // addr_nonce(0) < tx_nonce(5), kNormalFrom is user tx
    auto tx = MakeTxItem(0, 5, pools::protobuf::kNormalFrom);
    pool.ConsensusAddTxs(tx);
    EXPECT_EQ(pool.consensus_added_txs_.size(), 1u);
    EXPECT_TRUE(pool.tx_pool_dirty_);
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
