// Branch- and function-coverage tests for TxPool methods that were not yet
// exercised in test_tx_pool_inline.cc, test_tx_pool_methods.cc, or
// test_tx_pool_tx.cc:
//
//   TxItemPriOper::operator()
//   TxPool::TxKeyExists
//   TxPool::oldest_timestamp / latest_hash / latest_timestamp
//   TxPool::AddTx
//   TxPool::TxOver
//   TxPool::TempGetTxIdempotently / GetTxIdempotently
//   TxPool::GetTxSyncToLeader
//
// Uses #define private public to seed internal state directly.
// MinTxItem is in an anonymous namespace to avoid ODR violations with the
// same struct defined in other test TUs.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
#include "protos/prefix_db.h"
#include <protos/view_block.pb.h>

namespace shardora {
namespace pools {
namespace test {

namespace {

struct MinTxItem : public TxItem {
    MinTxItem(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, -1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

// Create a TxItemPtr.  Pubkey is intentionally left empty (size 0, not 64)
// so that SHARDORA_DEBUG branches involving security_->GetAddress() are skipped.
static TxItemPtr MakeTx(uint64_t addr_nonce, uint64_t tx_nonce,
                        pools::protobuf::StepType step,
                        const std::string& addr = "addr") {
    auto msg = std::make_shared<transport::TransportMessage>();
    auto ai  = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(addr_nonce);
    std::string padded = addr;
    if (padded.size() != common::kUnicastAddressLength &&
        padded.size() != common::kPreypamentAddressLength) {
        padded.resize(common::kUnicastAddressLength, '\0');
    }
    ai->set_addr(std::move(padded));
    auto* tx_proto = msg->header.mutable_tx_proto();
    tx_proto->set_nonce(tx_nonce);
    tx_proto->set_step(step);
    // System txs: TxPool::TempGetTxIdempotently asserts tx_info->to() == address_info->addr().
    if (!IsUserTransaction(static_cast<uint32_t>(step))) {
        tx_proto->set_to(ai->addr());
    }
    // pubkey left empty → size 0, never triggers security_->GetAddress
    return std::make_shared<MinTxItem>(msg, ai);
}

// tx_valid_func that always reports the tx as valid (returns 0)
static int AlwaysValid(const address::protobuf::AddressInfo&,
                       const pools::protobuf::TxMessage&,
                       uint64_t*) {
    return 0;
}

// tx_valid_func that always reports stale nonce (returns negative)
static int AlwaysInvalid(const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) {
    return -1;
}

}  // anonymous namespace

// ---- Fake KeyValueSync ----

static std::shared_ptr<sync::KeyValueSync> MakeFakeSync() {
    auto* raw = reinterpret_cast<sync::KeyValueSync*>(1uLL);
    return std::shared_ptr<sync::KeyValueSync>(raw, [](sync::KeyValueSync*) {});
}

// ============================================================
// Fixture: shared DB per test-suite

class TestTxPoolExtra : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_tx_pool_extra_db");
        db_ptr_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_ptr_->Init("./test_tx_pool_extra_db"));
    }

    static std::shared_ptr<db::Db> db_ptr_;

    void SetUpPool(TxPool& pool) {
        pool.db_          = db_ptr_;
        pool.pool_index_  = 0;
        pool.prefix_db_   = std::make_shared<protos::PrefixDb>(db_ptr_);
        pool.kv_sync_     = MakeFakeSync();
        pool.latest_height_      = common::kInvalidUint64;
        pool.synced_height_      = 0;
        pool.prev_synced_height_ = 0;
    }
};

std::shared_ptr<db::Db> TestTxPoolExtra::db_ptr_ = nullptr;

// ============================================================
// TxItemPriOper::operator()

TEST_F(TestTxPoolExtra, TxItemPriOper_LowerGasPrice_ReturnsTrue) {
    TxItemPriOper op;
    auto a = MakeTx(0, 1, pools::protobuf::kNormalFrom);
    auto b = MakeTx(0, 1, pools::protobuf::kNormalFrom);
    a->tx_info->set_gas_price(100);
    b->tx_info->set_gas_price(200);
    EXPECT_TRUE(op(a, b));
}

TEST_F(TestTxPoolExtra, TxItemPriOper_EqualGasPrice_ReturnsFalse) {
    TxItemPriOper op;
    auto a = MakeTx(0, 1, pools::protobuf::kNormalFrom);
    auto b = MakeTx(0, 1, pools::protobuf::kNormalFrom);
    a->tx_info->set_gas_price(100);
    b->tx_info->set_gas_price(100);
    EXPECT_FALSE(op(a, b));
}

// ============================================================
// TxPool::TxKeyExists

// Branch: addr not in tx_map_ → false
TEST_F(TestTxPoolExtra, TxKeyExists_AddrNotInMap_ReturnsFalse) {
    TxPool pool;
    EXPECT_FALSE(pool.TxKeyExists("nosuchaddr", 0, "key"));
}

// Branch: addr found, nonce not found → false
TEST_F(TestTxPoolExtra, TxKeyExists_NonceNotFound_ReturnsFalse) {
    TxPool pool;
    auto tx = MakeTx(0, 5, pools::protobuf::kNormalFrom, "addr1");
    pool.tx_map_["addr1"][5] = tx;
    EXPECT_FALSE(pool.TxKeyExists("addr1", 99, "key"));
}

// Branch: addr+nonce found, key matches → true
TEST_F(TestTxPoolExtra, TxKeyExists_KeyMatches_ReturnsTrue) {
    TxPool pool;
    auto tx = MakeTx(0, 5, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_info->set_key("mykey");
    pool.tx_map_["addr1"][5] = tx;
    EXPECT_TRUE(pool.TxKeyExists("addr1", 5, "mykey"));
}

// Branch: addr+nonce found, key does NOT match → false
TEST_F(TestTxPoolExtra, TxKeyExists_KeyMismatch_ReturnsFalse) {
    TxPool pool;
    auto tx = MakeTx(0, 5, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_info->set_key("mykey");
    pool.tx_map_["addr1"][5] = tx;
    EXPECT_FALSE(pool.TxKeyExists("addr1", 5, "otherkey"));
}

// ============================================================
// TxPool::oldest_timestamp

TEST_F(TestTxPoolExtra, OldestTimestamp_ReturnsStoredValue) {
    TxPool pool;
    pool.oldest_timestamp_.store(12345u);
    EXPECT_EQ(pool.oldest_timestamp(), 12345u);
}

// ============================================================
// TxPool::latest_hash

// Branch: non-empty → return directly without calling InitLatestInfo
TEST_F(TestTxPoolExtra, LatestHash_NonEmpty_ReturnsDirectly) {
    TxPool pool;
    pool.latest_hash_ = "somehash";
    // network_id left at default (kInvalidUint32) to ensure InitLatestInfo
    // would be a no-op if accidentally called
    EXPECT_EQ(pool.latest_hash(), "somehash");
}

// Branch: empty → calls InitLatestInfo which returns early (kInvalidUint32 net_id)
// → latest_hash_ stays empty
TEST_F(TestTxPoolExtra, LatestHash_Empty_TriggersInitLatestInfo_StaysEmpty) {
    TxPool pool;
    SetUpPool(pool);
    pool.latest_hash_ = "";
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);
    EXPECT_EQ(pool.latest_hash(), "");
    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ============================================================
// TxPool::latest_timestamp

// Branch: non-zero → return directly
TEST_F(TestTxPoolExtra, LatestTimestamp_NonZero_ReturnsDirectly) {
    TxPool pool;
    pool.latest_timestamp_ = 9999u;
    EXPECT_EQ(pool.latest_timestamp(), 9999u);
}

// Branch: zero → calls InitLatestInfo (returns early) → stays 0
TEST_F(TestTxPoolExtra, LatestTimestamp_Zero_TriggersInitLatestInfo_StaysZero) {
    TxPool pool;
    SetUpPool(pool);
    pool.latest_timestamp_ = 0;
    auto prev = common::GlobalInfo::Instance()->network_id();
    common::GlobalInfo::Instance()->set_network_id(common::kInvalidUint32);
    EXPECT_EQ(pool.latest_timestamp(), 0u);
    common::GlobalInfo::Instance()->set_network_id(prev);
}

// ============================================================
// TxPool::AddTx

// Branch: kContractExcute with wrong address size → kPoolsError
TEST_F(TestTxPoolExtra, AddTx_ContractExcute_WrongAddrSize_Error) {
    TxPool pool;
    // addr "shortaddr" has size 9, not kPreypamentAddressLength(40)
    auto tx = MakeTx(0, 1, pools::protobuf::kContractExcute, "shortaddr");
    EXPECT_EQ(pool.AddTx(tx), kPoolsError);
}

// Branch: kContractRefund with wrong address size → kPoolsError
TEST_F(TestTxPoolExtra, AddTx_ContractRefund_WrongAddrSize_Error) {
    TxPool pool;
    auto tx = MakeTx(0, 1, pools::protobuf::kContractRefund, "shortaddr");
    EXPECT_EQ(pool.AddTx(tx), kPoolsError);
}

// Branch: normal user tx with empty tx_key → key computed, pushed, kPoolsSuccess
TEST_F(TestTxPoolExtra, AddTx_NormalTx_EmptyKey_ComputedAndPushed) {
    TxPool pool;
    auto tx = MakeTx(0, 1, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_key = "";
    EXPECT_EQ(pool.AddTx(tx), kPoolsSuccess);
    EXPECT_FALSE(tx->tx_key.empty());  // key was computed
    EXPECT_TRUE(pool.tx_pool_dirty_);
}

// Branch: normal user tx with non-empty tx_key → pushed directly, kPoolsSuccess
TEST_F(TestTxPoolExtra, AddTx_NormalTx_NonEmptyKey_PushedDirectly) {
    TxPool pool;
    auto tx = MakeTx(0, 1, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_key = "presetkey";
    EXPECT_EQ(pool.AddTx(tx), kPoolsSuccess);
    EXPECT_EQ(tx->tx_key, "presetkey");
}

// ============================================================
// TxPool::TxOver

// Branch: empty view_block → over_addr_map_queue_ gets a new empty map
TEST_F(TestTxPoolExtra, TxOver_EmptyViewBlock_NoTxRemoved) {
    TxPool pool;
    pool.tx_pool_dirty_ = false;
    view_block::protobuf::ViewBlockItem vb;
    pool.TxOver(vb);
    EXPECT_TRUE(pool.tx_pool_dirty_);  // always set true at end of TxOver
}

// Branch: kNormalFrom tx in tx_map_ (IsUserTransaction=true, nonce matches) → removed
TEST_F(TestTxPoolExtra, TxOver_UserTx_RemovedFromTxMap) {
    TxPool pool;
    auto stored = MakeTx(0, 5, pools::protobuf::kNormalFrom, "from_addr");
    pool.tx_map_["from_addr"][5] = stored;
    ASSERT_EQ(pool.tx_map_.size(), 1u);

    view_block::protobuf::ViewBlockItem vb;
    auto* tx_info = vb.mutable_block_info()->add_tx_list();
    tx_info->set_step(pools::protobuf::kNormalFrom);
    tx_info->set_from("from_addr");
    tx_info->set_nonce(5);

    pool.TxOver(vb);
    EXPECT_TRUE(pool.tx_map_.empty());
}

// Branch: kNormalTo (system tx, IsUserTransaction=false) — key must match unique_hash
TEST_F(TestTxPoolExtra, TxOver_SystemTx_RemovedOnKeyMatch) {
    TxPool pool;
    const std::string kKey = "unique_key";
    auto stored = MakeTx(0, 0, pools::protobuf::kNormalTo, "to_addr");
    stored->tx_info->set_key(kKey);
    pool.tx_map_["to_addr"][0] = stored;

    view_block::protobuf::ViewBlockItem vb;
    auto* tx_info = vb.mutable_block_info()->add_tx_list();
    tx_info->set_step(pools::protobuf::kNormalTo);
    tx_info->set_to("to_addr");
    tx_info->set_unique_hash(kKey);
    tx_info->set_nonce(0);

    pool.TxOver(vb);
    EXPECT_TRUE(pool.tx_map_.empty());
}

// ============================================================
// TxPool::TempGetTxIdempotently

// Branch: tx_pool_dirty_ == false AND both queues empty → early return, res_map stays empty
TEST_F(TestTxPoolExtra, TempGetTxIdempotently_NotDirty_EarlyReturn) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = false;
    // No txs in any queue
    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.TempGetTxIdempotently(msg, res, 10, AlwaysValid);
    EXPECT_TRUE(res.empty());
}

// Branch: user tx with valid nonce in added_txs_ → added to tx_map_ then res_map
TEST_F(TestTxPoolExtra, TempGetTxIdempotently_ValidUserTx_AddedToResult) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = true;
    auto tx = MakeTx(0, 1, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_key = "k1";
    pool.added_txs_.push(tx);

    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.TempGetTxIdempotently(msg, res, 10, AlwaysValid);
    EXPECT_EQ(res.size(), 1u);
}

// Branch: user tx with stale addr_nonce (addr_nonce >= tx_nonce) → skipped
TEST_F(TestTxPoolExtra, TempGetTxIdempotently_StaleNonce_TxSkipped) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = true;
    // addr_nonce(5) >= tx_nonce(3) → skip
    auto tx = MakeTx(5, 3, pools::protobuf::kNormalFrom, "addr1");
    tx->tx_key = "k1";
    pool.added_txs_.push(tx);

    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.TempGetTxIdempotently(msg, res, 10, AlwaysValid);
    EXPECT_TRUE(res.empty());
    EXPECT_FALSE(pool.tx_pool_dirty_);  // set to false when res_map empty
}

// Branch: system tx (kNormalTo): to == addr, key not in unique hash store → added to tx_map_
TEST_F(TestTxPoolExtra, TempGetTxIdempotently_SystemTx_AddedToTxMap) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = true;
    auto tx = MakeTx(0, 0, pools::protobuf::kNormalTo, "sys_addr");
    const std::string map_key = tx->address_info->addr();
    tx->tx_key = "syskey";
    pool.added_txs_.push(tx);

    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.TempGetTxIdempotently(msg, res, 10, AlwaysValid);
    // System tx goes into tx_map_, not res_map (AlwaysInvalid not called for system tx)
    EXPECT_GT(pool.tx_map_.count(map_key), 0u);
}

// ============================================================
// TxPool::GetTxIdempotently

// Branch: count=1 → loop body breaks after first call
TEST_F(TestTxPoolExtra, GetTxIdempotently_CountOne_BreaksAfterFirstCall) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = false;
    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.GetTxIdempotently(msg, res, 1, AlwaysValid);
    EXPECT_TRUE(res.empty());  // no txs, but no crash
}

// Branch: added_txs_ empty → inner break exits loop
TEST_F(TestTxPoolExtra, GetTxIdempotently_EmptyQueue_BreaksImmediately) {
    TxPool pool;
    SetUpPool(pool);
    pool.tx_pool_dirty_ = false;
    transport::MessagePtr msg;
    std::vector<pools::TxItemPtr> res;
    pool.GetTxIdempotently(msg, res, 10, AlwaysValid);
    EXPECT_TRUE(res.empty());
}

// ============================================================
// TxPool::GetTxSyncToLeader

// Branch: empty pool → txs stays empty, no crash
TEST_F(TestTxPoolExtra, GetTxSyncToLeader_EmptyPool_NoTxsAdded) {
    TxPool pool;
    SetUpPool(pool);
    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> txs;
    std::unordered_map<std::string, uint64_t> leader_nonce_map;
    pool.GetTxSyncToLeader(0, 10, &txs, AlwaysValid, leader_nonce_map);
    EXPECT_EQ(txs.size(), 0);
}

// Branch: one user tx in added_txs_ with valid nonce → tx added to txs
TEST_F(TestTxPoolExtra, GetTxSyncToLeader_ValidUserTx_AddedToTxs) {
    TxPool pool;
    SetUpPool(pool);
    auto tx = MakeTx(0, 1, pools::protobuf::kNormalFrom, "user_addr");
    tx->tx_key = "txkey1";
    pool.added_txs_.push(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> txs;
    std::unordered_map<std::string, uint64_t> leader_nonce_map;
    pool.GetTxSyncToLeader(0, 10, &txs, AlwaysValid, leader_nonce_map);
    EXPECT_EQ(txs.size(), 1);
}

// Branch: user tx with stale nonce in added_txs_ → skipped via addr_nonce >= tx_nonce
TEST_F(TestTxPoolExtra, GetTxSyncToLeader_StaleNonceTx_Skipped) {
    TxPool pool;
    SetUpPool(pool);
    // addr_nonce(5) >= tx_nonce(3) → skip
    auto tx = MakeTx(5, 3, pools::protobuf::kNormalFrom, "user_addr");
    tx->tx_key = "txkey_stale";
    pool.added_txs_.push(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> txs;
    std::unordered_map<std::string, uint64_t> leader_nonce_map;
    pool.GetTxSyncToLeader(0, 10, &txs, AlwaysValid, leader_nonce_map);
    EXPECT_EQ(txs.size(), 0);
}

// Branch: tx_valid_func returns negative → break out of nonce loop
TEST_F(TestTxPoolExtra, GetTxSyncToLeader_InvalidTx_BreaksLoop) {
    TxPool pool;
    SetUpPool(pool);
    auto tx = MakeTx(0, 1, pools::protobuf::kNormalFrom, "user_addr");
    tx->tx_key = "txkey1";
    pool.added_txs_.push(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> txs;
    std::unordered_map<std::string, uint64_t> leader_nonce_map;
    pool.GetTxSyncToLeader(0, 10, &txs, AlwaysInvalid, leader_nonce_map);
    EXPECT_EQ(txs.size(), 0);
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
