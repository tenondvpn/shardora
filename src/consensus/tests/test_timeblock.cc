#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#define private public
#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/random.h"
#include "consensus/waiting_txs_pools.h"
#include "consensus/zbft/zbft_utils.h"
#include "consensus/zbft/bft_manager.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/pools.pb.h"
#include "security/ecdsa/ecdsa.h"

namespace zjchain {

namespace consensus {

namespace test {

static const uint32_t kTestTxCount = 256;
static std::string random_prefix = common::Random::RandomString(33);
static std::shared_ptr<db::Db> db_ptr = nullptr;
static const uint32_t kTestShardingId = 2;

class TestTimeBlock : public testing::Test {
public:
    static void WriteDefaultLogConf(
        const std::string& log_conf_path,
        const std::string& log_path) {
        FILE* file = NULL;
        file = fopen(log_conf_path.c_str(), "w");
        if (file == NULL) {
            return;
        }
        std::string log_str = ("# log4cpp.properties\n"
            "log4cpp.rootCategory = WARN\n"
            "log4cpp.category.sub1 = WARN, programLog\n"
            "log4cpp.appender.rootAppender = ConsoleAppender\n"
            "log4cpp.appender.rootAppender.layout = PatternLayout\n"
            "log4cpp.appender.rootAppender.layout.ConversionPattern = %d [%p] %m%n\n"
            "log4cpp.appender.programLog = RollingFileAppender\n"
            "log4cpp.appender.programLog.fileName = ") + log_path + "\n" +
            std::string("log4cpp.appender.programLog.maxFileSize = 1073741824\n"
                "log4cpp.appender.programLog.maxBackupIndex = 1\n"
                "log4cpp.appender.programLog.layout = PatternLayout\n"
                "log4cpp.appender.programLog.layout.ConversionPattern = %d [%p] %m%n\n");
        fwrite(log_str.c_str(), log_str.size(), 1, file);
        fclose(file);
    }

    static void SetUpTestCase() {
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("../../src/consensus/tests/test_tm");
        std::string config_path_ = "./";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/zjc.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
        common::GlobalInfo::Instance()->set_network_id(kTestShardingId);
    }

    void AddTxs(
            BftManager& bft_mgr,
            const std::string& prikey,
            uint32_t pool_index,
            const pools::protobuf::TxMessage& tx_info) {
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        auto account_mgr = std::make_shared<block::AccountManager>();
        account_mgr->Init(kTestShardingId, 1, db_ptr);
        auto block_mgr = std::make_shared<block::BlockManager>();
        auto bls_mgr = std::make_shared<bls::BlsManager>(security, db_ptr);
        auto pools_mgr = std::make_shared<pools::TxPoolManager>(security);
        block_mgr->Init(account_mgr, db_ptr, pools_mgr);
        auto elect_mgr = std::make_shared<elect::ElectManager>(block_mgr, security, bls_mgr, db_ptr);
        ASSERT_EQ(elect_mgr->Init(), 0);
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        *msg_ptr->header.mutable_tx_proto() = tx_info;
        auto tx_ptr = std::make_shared<pools::TxItem>(msg_ptr);
        tx_ptr->tx_hash = pools::GetTxMessageHash(tx_info);
        ASSERT_EQ(pools_mgr->AddTx(pool_index, tx_ptr), pools::kPoolsSuccess);
        ASSERT_EQ(pools_mgr->tx_pool_[pool_index].added_tx_map_.size(), 1);
        ASSERT_EQ(bft_mgr.Init(
            account_mgr,
            block_mgr,
            elect_mgr,
            pools_mgr,
            security,
            nullptr,
            1), kConsensusSuccess);
        ASSERT_EQ(bft_mgr.OnNewElectBlock(0, 1), kConsensusSuccess);
    }

    void CreateTxInfo(uint32_t pool_index, pools::protobuf::TxMessage& tx_info) {
        tx_info.set_step(pools::protobuf::kConsensusRootTimeBlock);
        uint32_t* test_arr = (uint32_t*)random_prefix.data();
        test_arr[0] = common::kRootChainPoolIndex;
        tx_info.set_pubkey("");
        tx_info.set_to(common::kRootChainTimeBlockTxAddress);
        auto gid = std::string((char*)test_arr, 32);
        tx_info.set_gid(gid);
        tx_info.set_gas_limit(0llu);
        tx_info.set_amount(0);
        tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestTimeBlock, TestTimeBlock) {
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(common::kRootChainPoolIndex, tx_info);
    BftManager leader_bft_mgr;
    AddTxs(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"),
        common::kRootChainPoolIndex, 
        tx_info);
    BftManager backup_bft_mgr0;
    AddTxs(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"),
        common::kRootChainPoolIndex, 
        tx_info);
    BftManager backup_bft_mgr1;
    AddTxs(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"),
        common::kRootChainPoolIndex,
        tx_info);
    ASSERT_EQ(leader_bft_mgr.Start(0), kConsensusSuccess);
    leader_bft_mgr.leader_prepare_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_prepare_msg_ != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_prepare_msg_ != nullptr);
    backup_bft_mgr0.backup_prepare_msg_->thread_idx = 0;
    backup_bft_mgr1.backup_prepare_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.backup_prepare_msg_);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_prepare_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_precommit_msg_ != nullptr);
    leader_bft_mgr.leader_precommit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_precommit_msg_ != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_precommit_msg_ != nullptr);
    backup_bft_mgr0.backup_precommit_msg_->thread_idx = 0;
    backup_bft_mgr1.backup_precommit_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.backup_precommit_msg_);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_precommit_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_commit_msg_ != nullptr);
    leader_bft_mgr.leader_commit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_commit_msg_);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_commit_msg_);
};

TEST_F(TestTimeBlock, TestTimeBlockOnePrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(common::kRootChainPoolIndex, tx_info);
    BftManager leader_bft_mgr;
    AddTxs(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr0;
    backup_bft_mgr0.test_for_prepare_evil_ = true;
    AddTxs(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr1;
    AddTxs(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"),
        common::kRootChainPoolIndex,
        tx_info);
    ASSERT_EQ(leader_bft_mgr.Start(0), kConsensusSuccess);
    leader_bft_mgr.leader_prepare_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_prepare_msg_ == nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_prepare_msg_ != nullptr);
    backup_bft_mgr1.backup_prepare_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_prepare_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_precommit_msg_ != nullptr);
    leader_bft_mgr.leader_precommit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_precommit_msg_ == nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_precommit_msg_ != nullptr);
    backup_bft_mgr1.backup_precommit_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_precommit_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_commit_msg_ != nullptr);
    leader_bft_mgr.leader_commit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_commit_msg_);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_commit_msg_);
};

TEST_F(TestTimeBlock, TestTimeBlockOnePrecommitEvil) {
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(common::kRootChainPoolIndex, tx_info);
    BftManager leader_bft_mgr;
    AddTxs(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr0;
    backup_bft_mgr0.test_for_precommit_evil_ = true;
    AddTxs(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr1;
    AddTxs(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"),
        common::kRootChainPoolIndex,
        tx_info);
    ASSERT_EQ(leader_bft_mgr.Start(0), kConsensusSuccess);
    leader_bft_mgr.leader_prepare_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_prepare_msg_ != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_prepare_msg_ != nullptr);
    backup_bft_mgr0.backup_prepare_msg_->thread_idx = 0;
    backup_bft_mgr1.backup_prepare_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.backup_prepare_msg_);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_prepare_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_precommit_msg_ != nullptr);
    leader_bft_mgr.leader_precommit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_precommit_msg_ == nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_precommit_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_precommit_msg_ != nullptr);
    backup_bft_mgr1.backup_precommit_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.backup_precommit_msg_);
    ASSERT_TRUE(leader_bft_mgr.leader_commit_msg_ != nullptr);
    leader_bft_mgr.leader_commit_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_commit_msg_);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_commit_msg_);
};

TEST_F(TestTimeBlock, TestTimeBlockTwoPrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(common::kRootChainPoolIndex, tx_info);
    BftManager leader_bft_mgr;
    AddTxs(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr0;
    backup_bft_mgr0.test_for_prepare_evil_ = true;
    AddTxs(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"),
        common::kRootChainPoolIndex,
        tx_info);
    BftManager backup_bft_mgr1;
    backup_bft_mgr1.test_for_prepare_evil_ = true;
    AddTxs(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"),
        common::kRootChainPoolIndex,
        tx_info);
    ASSERT_EQ(leader_bft_mgr.Start(0), kConsensusSuccess);
    leader_bft_mgr.leader_prepare_msg_->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr0.backup_prepare_msg_ == nullptr);
    ASSERT_TRUE(backup_bft_mgr0.bk_prepare_op_msg_ != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.leader_prepare_msg_);
    ASSERT_TRUE(backup_bft_mgr1.backup_prepare_msg_ == nullptr);
    ASSERT_TRUE(backup_bft_mgr1.bk_prepare_op_msg_ != nullptr);
    backup_bft_mgr0.bk_prepare_op_msg_->thread_idx = 0;
    backup_bft_mgr1.bk_prepare_op_msg_->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.bk_prepare_op_msg_);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.bk_prepare_op_msg_);
};

}  // namespace test

}  // namespace consensus

}  // namespace zjchain
