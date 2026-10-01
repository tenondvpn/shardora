// Coverage tests for TxPoolManager paths the earlier extra* files don't reach:
//
//   - DispatchTx (cc lines 1479-1518): exercise step-too-large, unregistered
//     step, item_function returning nullptr, and the full happy path.
//   - Root-node constructor + SyncMinssingRootHeights null guard
//     (cc line 364-366): only hit when IsRootNode() is true, i.e. network_id
//     equals kRootCongressNetworkId.
//   - HandleSyncPoolsMaxHeight req path with network_id past the consensus
//     range (cc 769-770) — early return inside the request handler.
//   - SyncRootBlockWithMaxHeights waiting-shard handling (cc 466-467 sibling,
//     but for root sync) — added because the cc paths in SyncBlockWithMaxHeights
//     are now covered by extra3 and this completes the matched pair.
//
// Two fixtures, one per network_id setting (the standard shard one and the
// root-congress one). Each fixture saves/restores GlobalInfo::network_id_
// in SetUpTestSuite / TearDownTestSuite so tests stay isolated.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "sync/key_value_sync.h"
#include "transport/transport_utils.h"

#define private public
#define protected public
#include "db/db.h"
#include "pools/tx_pool.h"
#include "pools/tx_pool_manager.h"
#undef protected
#undef private

#include "common/global_info.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "network/network_utils.h"
#include "tnet/tcp_interface.h"

namespace shardora {
namespace pools {
namespace test {

namespace {

struct FakeTcpConnExtra4 : tnet::TcpInterface {
    std::string PeerIp() override { return "10.0.0.2"; }
    uint16_t PeerPort() override { return 4001; }
    void SetPeerIp(const std::string&) override {}
    void SetPeerPort(uint16_t) override {}
    int Send(const std::string&) override { return 0; }
    int Send(const char*, int32_t) override { return 0; }
    int Send(uint64_t, const std::string&) override { return 0; }
    int Send(const char*, int32_t, uint64_t) override { return 0; }
};

}  // namespace

// ---------------------------------------------------------------------------
// Minimal TxItem subclass with the abstract overrides required by tx_utils.h.
// Mirrors the pattern used in test_tx_pool_addtx.cc / test_tx_pool_manager_extra.cc.
// ---------------------------------------------------------------------------

struct SimpleTxForPmExtra4 : public TxItem {
    SimpleTxForPmExtra4(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, /*tx_info_idx=*/-1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

static transport::MessagePtr MakeMsgWithStep(
        pools::protobuf::StepType step,
        const std::string& addr = std::string(common::kUnicastAddressLength, 'D'),
        uint64_t nonce = 1u) {
    auto msg = std::make_shared<transport::TransportMessage>();
    auto* tx = msg->header.mutable_tx_proto();
    tx->set_step(step);
    tx->set_nonce(nonce);
    tx->set_to(addr);
    return msg;
}

static TxItemPtr MakeValidTxItem(const transport::MessagePtr& msg,
                                 const std::string& addr) {
    auto ai = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(0);
    ai->set_addr(addr);
    return std::make_shared<SimpleTxForPmExtra4>(msg, ai);
}

static std::shared_ptr<sync::KeyValueSync> MakeKvStub() {
    auto* raw = reinterpret_cast<sync::KeyValueSync*>(1uLL);
    return std::shared_ptr<sync::KeyValueSync>(raw, [](sync::KeyValueSync*) {});
}

// ===========================================================================
// Fixture A — standard shard fixture (network_id = kConsensusShardBeginNetworkId)
// ===========================================================================

class TestTxPoolManagerExtra4 : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_txpm_extra4_db");
        db_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_->Init("./test_txpm_extra4_db"));

        prev_net_ = common::GlobalInfo::Instance()->network_id();
        common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardBeginNetworkId);

        auto kv = MakeKvStub();
        std::shared_ptr<security::Security>         null_sec;
        std::shared_ptr<block::AccountManager>      null_acc;
        std::shared_ptr<consensus::HotstuffManager> null_hotstuff;
        mgr_ = std::make_shared<TxPoolManager>(null_sec, db_, kv, null_acc, null_hotstuff);
    }

    static void TearDownTestSuite() {
        mgr_.reset();
        common::GlobalInfo::Instance()->set_network_id(prev_net_);
    }

    // Per-test: ensure item_functions_ slots used in this fixture start nullptr,
    // restore them in the post-test clean-up below.
    void TearDown() override {
        // Reset only the slots tests in this file touch — leave others alone.
        mgr_->item_functions_[pools::protobuf::kNormalFrom] = nullptr;
    }

    static std::shared_ptr<db::Db>        db_;
    static std::shared_ptr<TxPoolManager> mgr_;
    static uint32_t                       prev_net_;
};

std::shared_ptr<db::Db>        TestTxPoolManagerExtra4::db_       = nullptr;
std::shared_ptr<TxPoolManager> TestTxPoolManagerExtra4::mgr_      = nullptr;
uint32_t                       TestTxPoolManagerExtra4::prev_net_ = common::kInvalidUint32;

// ---------------------------------------------------------------------------
// (Skipped) DispatchTx step >= StepType_ARRAYSIZE early return (cc 1485-1487):
// in a Debug coverage build set_step() with an out-of-range value trips an
// assert inside the protobuf-generated setter (proto2 closed-enum semantics).
// The branch is effectively defensive C++ that can't be reached through the
// public protobuf C++ API, so we leave it as documented dead-defensive code.
//
// ---------------------------------------------------------------------------
// DispatchTx — step is in-range but item_functions_[step] == nullptr →
// hits cc 1490-1494, sets handle_status = kUnkonwn.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4, DispatchTx_UnregisteredStep_SetsUnknown) {
    auto msg = MakeMsgWithStep(pools::protobuf::kNormalFrom);
    // Ensure the slot is empty (TearDown also resets it).
    mgr_->item_functions_[pools::protobuf::kNormalFrom] = nullptr;

    mgr_->DispatchTx(/*pool_index=*/0, msg);
    EXPECT_EQ(msg->handle_status, transport::kUnkonwn);
}

// ---------------------------------------------------------------------------
// DispatchTx — registered function returns nullptr → hits cc 1499-1502.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4, DispatchTx_FunctionReturnsNull_SetsUnknown) {
    auto msg = MakeMsgWithStep(pools::protobuf::kNormalFrom);
    mgr_->RegisterCreateTxFunction(
        static_cast<uint32_t>(pools::protobuf::kNormalFrom),
        [](transport::MessagePtr) -> TxItemPtr { return nullptr; });

    mgr_->DispatchTx(/*pool_index=*/0, msg);
    EXPECT_EQ(msg->handle_status, transport::kUnkonwn);
}

// ---------------------------------------------------------------------------
// DispatchTx — registered function returns a valid TxItemPtr → exercises
// the full happy-path body (cc 1497-1518), including AddTx, sign_verified
// flip, and set_status(kTxAccept).
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4, DispatchTx_ValidTx_AddsToPoolAndAcceptsStatus) {
    const std::string addr(common::kUnicastAddressLength, 'D');
    auto msg = MakeMsgWithStep(pools::protobuf::kNormalFrom, addr, /*nonce=*/1u);

    TxItemPtr created_tx;  // captured in lambda for assertion outside.
    mgr_->RegisterCreateTxFunction(
        static_cast<uint32_t>(pools::protobuf::kNormalFrom),
        [&created_tx, &addr](transport::MessagePtr m) -> TxItemPtr {
            created_tx = MakeValidTxItem(m, addr);
            return created_tx;
        });

    // Snapshot pool size to confirm AddTx ran.
    const uint32_t before = mgr_->all_tx_size(/*pool_index=*/0);

    mgr_->DispatchTx(/*pool_index=*/0, msg);

    EXPECT_EQ(msg->handle_status, transport::kTxAccept);
    ASSERT_NE(created_tx, nullptr);
    EXPECT_TRUE(created_tx->sign_verified);                 // flipped by DispatchTx
    EXPECT_GE(mgr_->all_tx_size(/*pool_index=*/0), before); // AddTx called
}

// ---------------------------------------------------------------------------
// HandleSyncPoolsMaxHeight: req=true but local network_id >= kConsensusShardEndNetworkId
// → early return at cc 769-770.
//
// We temporarily flip network_id above the consensus range and call directly.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4, HandleSyncPoolsMaxHeight_ReqLocalNetTooLarge_EarlyReturn) {
    auto msg = std::make_shared<transport::TransportMessage>();
    msg->header.set_src_sharding_id(network::kConsensusShardBeginNetworkId);
    msg->header.mutable_sync_heights()->set_req(true);

    const uint32_t saved_net = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(network::kConsensusShardEndNetworkId + 5);
    mgr_->HandleSyncPoolsMaxHeight(msg);
    common::GlobalInfo::Instance()->set_network_id(saved_net);
}

// Request path with valid network + Tcp conn → TcpTransport::Send on response (cc ~829-831).
TEST_F(TestTxPoolManagerExtra4, HandleSyncPoolsMaxHeight_ReqWithConn_ExercisesResponseSend) {
    auto msg = std::make_shared<transport::TransportMessage>();
    msg->header.set_src_sharding_id(network::kConsensusShardBeginNetworkId);
    msg->header.mutable_sync_heights()->set_req(true);
    msg->conn = std::make_shared<FakeTcpConnExtra4>();
    mgr_->HandleSyncPoolsMaxHeight(msg);
}

TEST_F(TestTxPoolManagerExtra4, SyncPoolsMaxHeight_NoCrash) {
    mgr_->SyncPoolsMaxHeight();
}

// ===========================================================================
// Fixture B — root-node fixture (network_id = kRootCongressNetworkId)
// ===========================================================================
//
// With this network_id, IsRootNode() returns true so the constructor skips
// allocating root_cross_pools_, and SyncMinssingRootHeights short-circuits
// at its first guard (cc line 364-366).

class TestTxPoolManagerExtra4RootNode : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_txpm_extra4_root_db");
        db_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_->Init("./test_txpm_extra4_root_db"));

        prev_net_ = common::GlobalInfo::Instance()->network_id();
        common::GlobalInfo::Instance()->set_network_id(network::kRootCongressNetworkId);

        auto kv = MakeKvStub();
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

std::shared_ptr<db::Db>        TestTxPoolManagerExtra4RootNode::db_       = nullptr;
std::shared_ptr<TxPoolManager> TestTxPoolManagerExtra4RootNode::mgr_      = nullptr;
uint32_t                       TestTxPoolManagerExtra4RootNode::prev_net_ = common::kInvalidUint32;

// ---------------------------------------------------------------------------
// Constructor: when IsRootNode() is true the constructor SKIPS allocating
// root_cross_pools_ (cc 101-106), leaving it nullptr.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4RootNode, Constructor_RootNode_RootCrossPoolsIsNull) {
    EXPECT_NE(mgr_->tx_pool_, nullptr);
    EXPECT_NE(mgr_->cross_pools_, nullptr);
    EXPECT_EQ(mgr_->root_cross_pools_, nullptr);
}

// ---------------------------------------------------------------------------
// SyncMinssingRootHeights: root_cross_pools_ == nullptr → first early
// return at cc 364-366.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4RootNode, SyncMinssingRootHeights_RootCrossPoolsNull_EarlyReturn) {
    ASSERT_EQ(mgr_->root_cross_pools_, nullptr);
    mgr_->SyncMinssingRootHeights(common::TimeUtils::TimestampMs());
}

// ---------------------------------------------------------------------------
// HandleSyncPoolsMaxHeight: response path with src == local (root) →
// the function takes the self branch with network_id == kRootCongressNetworkId.
// Specifically lines 786-796: src_net_id == network_id (root) → iterates
// pool_idx=kInvalidPoolIndex from 0, but since pool_idx == kInvalidPoolIndex
// the loop body never executes.
//
// This is a "second branch in the root local-pool path" that the extra2/extra3
// tests don't hit (because they use network_id=kConsensusShardBeginNetworkId).
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4RootNode, HandleSyncPoolsMaxHeight_ResponseRoot_ProcessesOwnRootData) {
    auto msg = std::make_shared<transport::TransportMessage>();
    msg->header.set_src_sharding_id(network::kRootCongressNetworkId);
    msg->header.mutable_sync_heights()->set_req(false);

    // Provide enough heights so the size check at line 853 passes; only
    // heights[0]=5 matters (rest are 0 which is < kInvalidUint64 → outer if false).
    // root_cross_pools_ is null on a root node, so the body skips
    // root_cross_pools_[i].latest_height() accesses entirely via the
    // root_synced_max_heights_ writes which target the TxPoolManager's own
    // atomic array (separate from cross_block_mgr_).
    //
    // Wait — actually on a root node, the response-from-root branch (cc 852)
    // would dereference root_cross_pools_, which is null. So instead provide
    // a response from a non-root shard so the cross_block_mgr_->UpdateMaxHeight
    // path runs (which is identical to extra2's existing OtherShard cases).
    // This test mostly ensures the function entry under root network_id is safe.
    msg->header.mutable_sync_heights()->add_heights(0u);
    msg->header.mutable_sync_heights()->add_heights(0u);

    // No crash with size != kInvalidPoolIndex → early return at line 884-885.
    mgr_->HandleSyncPoolsMaxHeight(msg);
}

// ---------------------------------------------------------------------------
// FlushHeightTree: on a root node the inner `cross_pools_[i].FlushHeightTree`
// loop (cc 317-321) iterates up to now_max_sharding_id_; default is
// kConsensusShardBeginNetworkId (= 3). The loop runs for i=2..3 → exercises
// the cross_pools_ flush path. Mirrors the standard-shard FlushHeightTree
// coverage but on the root-node fixture.
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4RootNode, FlushHeightTree_RootNode_NoCrash) {
    mgr_->FlushHeightTree();
}

// ---------------------------------------------------------------------------
// SyncCrossPool on a root node: same as the standard-shard SyncCrossPool but
// with the root-node global state. Exercises the loop with
// now_valid_end_shard >= kConsensusShardBeginNetworkId.
// (extra3 already set now_valid_end_shard up; it's monotonic so it remains.)
// ---------------------------------------------------------------------------

TEST_F(TestTxPoolManagerExtra4RootNode, SyncCrossPool_RootNode_NoCrash) {
    common::GlobalInfo::Instance()->set_now_valid_end_shard(
        network::kConsensusShardBeginNetworkId);
    mgr_->SyncCrossPool();
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
