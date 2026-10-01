// Additional branch-coverage tests for TxPool::AddTx, GetTxSyncToLeader,
// and TxOver paths not yet hit by earlier test files.
//
// Uses the same MinTxItem helper pattern as test_tx_pool_tx.cc.
// Linker stub for KeyValueSync::AddSyncHeight lives in test_pools_stubs.cc.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

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

// Minimal concrete TxItem: same pattern as test_tx_pool_tx.cc.
struct MinTxItem2 : public TxItem {
    MinTxItem2(transport::MessagePtr msg, protos::AddressInfoPtr ai)
        : TxItem(msg, -1, ai) {}
    int HandleTx(uint32_t, view_block::protobuf::ViewBlockItem&,
                 shardoravm::ShardorahainHost&, hotstuff::BalanceAndNonceMap&,
                 block::protobuf::BlockTx&) override { return 0; }
    int TxToBlockTx(const pools::protobuf::TxMessage&,
                    block::protobuf::BlockTx*) override { return 0; }
};

static TxItemPtr MakeTxItem2(
        uint64_t addr_nonce, uint64_t tx_nonce,
        pools::protobuf::StepType step,
        const std::string& addr = std::string(common::kUnicastAddressLength, 'B')) {
    auto msg = std::make_shared<transport::TransportMessage>();
    auto ai  = std::make_shared<address::protobuf::AddressInfo>();
    ai->set_nonce(addr_nonce);
    ai->set_addr(addr);
    msg->header.mutable_tx_proto()->set_nonce(tx_nonce);
    msg->header.mutable_tx_proto()->set_step(step);
    msg->header.mutable_tx_proto()->set_to(addr);
    return std::make_shared<MinTxItem2>(msg, ai);
}

class TestTxPoolAddTx : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_tx_pool_addtx_db");
        db_ptr_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_ptr_->Init("./test_tx_pool_addtx_db"));
    }

    static std::shared_ptr<db::Db> db_ptr_;

    void SetUpPool(TxPool& pool) {
        pool.db_         = db_ptr_;
        pool.pool_index_ = 0;
        pool.pools_mgr_  = nullptr;
        pool.prefix_db_  = std::make_shared<protos::PrefixDb>(db_ptr_);
    }
};

std::shared_ptr<db::Db> TestTxPoolAddTx::db_ptr_ = nullptr;

// ============================================================
// AddTx — kContractExcute step with wrong address length → kPoolsError

TEST_F(TestTxPoolAddTx, AddTx_ContractExcute_WrongAddrLen_ReturnsError) {
    TxPool pool;
    SetUpPool(pool);

    // addr length != kPreypamentAddressLength (40) → error path
    std::string short_addr(common::kUnicastAddressLength, 'X');  // 20 bytes
    ASSERT_NE(short_addr.size(), common::kPreypamentAddressLength);

    auto tx = MakeTxItem2(0, 1, pools::protobuf::kContractExcute, short_addr);
    EXPECT_EQ(pool.AddTx(tx), kPoolsError);
}

// AddTx — kContractRefund step with wrong address length → kPoolsError
TEST_F(TestTxPoolAddTx, AddTx_ContractRefund_WrongAddrLen_ReturnsError) {
    TxPool pool;
    SetUpPool(pool);

    std::string short_addr(common::kUnicastAddressLength, 'Y');
    auto tx = MakeTxItem2(0, 1, pools::protobuf::kContractRefund, short_addr);
    EXPECT_EQ(pool.AddTx(tx), kPoolsError);
}

// AddTx — kContractExcute step with correct address length → kPoolsSuccess
TEST_F(TestTxPoolAddTx, AddTx_ContractExcute_CorrectAddrLen_ReturnsSuccess) {
    TxPool pool;
    SetUpPool(pool);

    std::string prepay_addr(common::kPreypamentAddressLength, 'P');
    auto tx = MakeTxItem2(0, 1, pools::protobuf::kContractExcute, prepay_addr);
    // tx_key is set via GetTxKey in the constructor
    EXPECT_EQ(pool.AddTx(tx), kPoolsSuccess);
}

// AddTx — normal tx (kNormalFrom) with non-empty tx_key → kPoolsSuccess
TEST_F(TestTxPoolAddTx, AddTx_NormalTx_WithTxKey_ReturnsSuccess) {
    TxPool pool;
    SetUpPool(pool);

    auto tx = MakeTxItem2(0, 1, pools::protobuf::kNormalFrom);
    EXPECT_EQ(pool.AddTx(tx), kPoolsSuccess);
    EXPECT_EQ(pool.added_txs_.size(), 1u);
}

// AddTx — empty tx_key gets filled from GetTxMessageHash
TEST_F(TestTxPoolAddTx, AddTx_EmptyTxKey_GetsFilled) {
    TxPool pool;
    SetUpPool(pool);

    auto tx = MakeTxItem2(0, 2, pools::protobuf::kNormalFrom);
    tx->tx_key.clear();  // force empty key
    EXPECT_EQ(pool.AddTx(tx), kPoolsSuccess);
    EXPECT_FALSE(tx->tx_key.empty());  // should have been filled
}

// ============================================================
// GetTxSyncToLeader — drain queue: non-user tx, hash already committed → skip

TEST_F(TestTxPoolAddTx, GetTxSyncToLeader_NonUserTx_AlreadyOvered_Skipped) {
    TxPool pool;
    SetUpPool(pool);

    auto tx = MakeTxItem2(0, 1, pools::protobuf::kStatistic);
    // Set a non-empty key so the DB lookup works correctly
    const std::string test_key = "test_overed_key_abc123";
    tx->tx_info->set_key(test_key);
    tx->tx_key = test_key;
    // Store a key in the over-unique-hash db so ExistsOverUniqueHash returns true
    db::DbWriteBatch batch;
    pool.prefix_db_->SaveOverUniqueHash(test_key, batch);
    db_ptr_->Put(batch);

    pool.AddTx(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> out;
    auto null_valid = [](const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) { return 0; };
    const std::unordered_map<std::string, uint64_t> empty_nonces;
    pool.GetTxSyncToLeader(0, 10, &out, null_valid, empty_nonces);
    // Tx was skipped (already overed), so output is empty
    EXPECT_EQ(out.size(), 0);
}

// GetTxSyncToLeader — non-user tx, to != addr → skipped
TEST_F(TestTxPoolAddTx, GetTxSyncToLeader_NonUserTx_ToMismatch_Skipped) {
    TxPool pool;
    SetUpPool(pool);

    auto tx = MakeTxItem2(0, 1, pools::protobuf::kStatistic);
    // Set to != addr to hit the mismatch branch
    tx->tx_info->set_to("different_addr_xxxxxxxxxxxxxxxxxx");

    pool.AddTx(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> out;
    auto null_valid = [](const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) { return 0; };
    const std::unordered_map<std::string, uint64_t> empty_nonces;
    pool.GetTxSyncToLeader(0, 10, &out, null_valid, empty_nonces);
    EXPECT_EQ(out.size(), 0);
}

// GetTxSyncToLeader — non-user tx, to == addr, not overed → added to tx_map
TEST_F(TestTxPoolAddTx, GetTxSyncToLeader_NonUserTx_Valid_AddedToTxMap) {
    TxPool pool;
    SetUpPool(pool);

    std::string addr(common::kUnicastAddressLength, 'S');
    auto tx = MakeTxItem2(0, 1, pools::protobuf::kStatistic, addr);
    tx->tx_info->set_to(addr);  // to == addr

    pool.AddTx(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> out;
    auto null_valid = [](const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) { return 0; };
    const std::unordered_map<std::string, uint64_t> empty_nonces;
    pool.GetTxSyncToLeader(0, 10, &out, null_valid, empty_nonces);
    // tx_map entry added; GetTxSyncToLeader reads from it (up to count)
    EXPECT_FALSE(pool.tx_map_.empty());
}

// GetTxSyncToLeader — user tx, addr_nonce >= tx_nonce → skipped
TEST_F(TestTxPoolAddTx, GetTxSyncToLeader_UserTx_NonceTooLow_Skipped) {
    TxPool pool;
    SetUpPool(pool);

    // addr_nonce=5 >= tx_nonce=3 → nonce check fails
    auto tx = MakeTxItem2(5, 3, pools::protobuf::kNormalFrom);

    pool.AddTx(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> out;
    auto null_valid = [](const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) { return 0; };
    const std::unordered_map<std::string, uint64_t> empty_nonces;
    pool.GetTxSyncToLeader(0, 10, &out, null_valid, empty_nonces);
    EXPECT_TRUE(pool.tx_map_.empty());
}

// GetTxSyncToLeader — user tx in consensus_tx_map → skipped (nonce collision)
TEST_F(TestTxPoolAddTx, GetTxSyncToLeader_UserTx_ConsensusNonceCollision_Skipped) {
    TxPool pool;
    SetUpPool(pool);

    std::string addr(common::kUnicastAddressLength, 'C');
    // addr_nonce=0 < tx_nonce=1 → passes nonce check
    auto tx = MakeTxItem2(0, 1, pools::protobuf::kNormalFrom, addr);

    // Pre-seed consensus_tx_map_ with the same addr+nonce
    auto existing_tx = MakeTxItem2(0, 1, pools::protobuf::kNormalFrom, addr);
    pool.consensus_tx_map_[addr][1] = existing_tx;

    pool.AddTx(tx);

    ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage> out;
    auto null_valid = [](const address::protobuf::AddressInfo&,
                         const pools::protobuf::TxMessage&,
                         uint64_t*) { return 0; };
    const std::unordered_map<std::string, uint64_t> empty_nonces;
    pool.GetTxSyncToLeader(0, 10, &out, null_valid, empty_nonces);
    // Tx skipped because nonce already in consensus map
    EXPECT_TRUE(pool.tx_map_.empty());
}

// ============================================================
// TxOver — kContractExcute addr = to + from concatenation

TEST_F(TestTxPoolAddTx, TxOver_ContractExcute_UsesToPlusFrom) {
    TxPool pool;
    SetUpPool(pool);

    std::string from_addr(common::kUnicastAddressLength, 'F');
    std::string to_addr(common::kUnicastAddressLength, 'T');

    // Seed tx_map_ with compound key (to+from)
    auto tx = MakeTxItem2(0, 1, pools::protobuf::kContractExcute, from_addr);
    std::string compound = to_addr + from_addr;
    pool.tx_map_[compound][1] = tx;

    view_block::protobuf::ViewBlockItem vb;
    auto* tx_item = vb.mutable_block_info()->add_tx_list();
    tx_item->set_step(pools::protobuf::kContractExcute);
    tx_item->set_from(from_addr);
    tx_item->set_to(to_addr);
    tx_item->set_nonce(1);

    pool.TxOver(vb);  // should not crash; compound-key tx removed from tx_map_
    // tx_map_ entry for compound key removed after TxOver
    EXPECT_TRUE(pool.tx_map_.find(compound) == pool.tx_map_.end());
}

// TxOver — tx with empty addr (to and from both empty) → continue (line 231+)
TEST_F(TestTxPoolAddTx, TxOver_EmptyAddr_Skipped) {
    TxPool pool;
    SetUpPool(pool);

    view_block::protobuf::ViewBlockItem vb;
    auto* tx_item = vb.mutable_block_info()->add_tx_list();
    tx_item->set_step(pools::protobuf::kNormalFrom);
    // from and to are empty strings → addr = "" → empty addr branch
    tx_item->set_from("");
    tx_item->set_to("");
    tx_item->set_nonce(1);

    pool.TxOver(vb);  // should not crash
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
