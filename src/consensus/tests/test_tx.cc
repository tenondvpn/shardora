#include <stdlib.h>
#include <math.h>

#include <atomic>
#include <iostream>
#include <vector>

#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#define private public
#include "block/account_manager.h"
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
static std::atomic<uint32_t> tx_index = 0;
static std::atomic<uint32_t> db_index = 0;
static std::string random_prefix = common::Random::RandomString(33);
static const uint32_t kTestShardingId = 3;
static std::unordered_map<std::string, std::string> addrs_map;
static std::vector<std::string> prikeys;
static std::vector<std::string> addrs;

class TestTx : public testing::Test {
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
        std::string config_path_ = "./";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/zjc.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
        common::GlobalInfo::Instance()->set_network_id(kTestShardingId);
        LoadAllAccounts();
    }

    static void LoadAllAccounts() {
        FILE* fd = fopen("../../src/consensus/tests/init_acc", "r");
        ASSERT_TRUE(fd != nullptr);
        bool res = true;
        std::string filed;
        const uint32_t kMaxLen = 1024;
        char* read_buf = new char[kMaxLen];
        while (true) {
            char* read_res = fgets(read_buf, kMaxLen, fd);
            if (read_res == NULL) {
                break;
            }

            common::Split<> split(read_buf, '\t');
            if (split.Count() != 2) {
                break;
            }

            std::string prikey = std::string(split[1], split.SubLen(1) - 1);
            std::string addr = split[0];
            addrs_map[prikey] = addr;
            addrs.push_back(common::Encode::HexDecode(addr));
            prikeys.push_back(common::Encode::HexDecode(prikey));
        }

        ASSERT_EQ(prikeys.size(), 256);
        fclose(fd);
        delete[]read_buf;
    }

    void InitConsensus(BftManager& bft_mgr, const std::string& prikey) {
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        auto account_mgr = std::make_shared<block::AccountManager>();
        auto db_ptr = std::make_shared<db::Db>();
        auto tmp_prikey = std::string("db_") + common::Encode::HexEncode(prikey) + std::to_string(db_index++);
        system((std::string("rm -rf ./") + tmp_prikey).c_str());
        system((std::string("cp -rf ../../src/consensus/tests/test_tx ./") + tmp_prikey).c_str());
        std::string db_path = std::string("./") + tmp_prikey;
        db_ptr->Init(db_path);
        account_mgr->Init(kTestShardingId, 1, db_ptr);
        auto block_mgr = std::make_shared<block::BlockManager>();
        auto bls_mgr = std::make_shared<bls::BlsManager>(security, db_ptr);
        auto pools_mgr = std::make_shared<pools::TxPoolManager>(security);
        block_mgr->Init(account_mgr, db_ptr, pools_mgr);
        auto elect_mgr = std::make_shared<elect::ElectManager>(block_mgr, security, bls_mgr, db_ptr);
        ASSERT_EQ(elect_mgr->Init(), 0);
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

    void AddTxs(
            BftManager& bft_mgr,
            const pools::protobuf::TxMessage& tx_info) {
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto from_addr = bft_mgr.security_ptr_->GetAddress(tx_info.pubkey());
        auto account_info = bft_mgr.account_mgr_->GetAcountInfo(0, from_addr);
        *msg_ptr->header.mutable_tx_proto() = tx_info;
        auto tx_ptr = std::make_shared<pools::TxItem>(msg_ptr);
        tx_ptr->tx_hash = pools::GetTxMessageHash(tx_info);
        ASSERT_EQ(bft_mgr.pools_mgr_->AddTx(account_info->pool_index(), tx_ptr), pools::kPoolsSuccess);
        ASSERT_TRUE(bft_mgr.pools_mgr_->tx_pool_[account_info->pool_index()].added_tx_map_.size() > 0);
    }

    void CreateTxInfo(
            const std::string& from_prikey,
            const std::string& to,
            pools::protobuf::TxMessage& tx_info) {
        tx_info.set_step(pools::protobuf::kNormalFrom);
        uint32_t* test_arr = (uint32_t*)random_prefix.data();
        test_arr[0] = tx_index++;
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(from_prikey);
        tx_info.set_pubkey(security->GetPublicKey());
        tx_info.set_to(to);
        auto gid = std::string((char*)test_arr, 32);
        tx_info.set_gid(gid);
        tx_info.set_gas_limit(10000000lu);
        tx_info.set_amount(100000lu);
        tx_info.set_gas_price(10lu);
    }

    static void TearDownTestCase() {
        system("rm -rf ./db_*");
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestTx, TestTx) {
    BftManager leader_bft_mgr;
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
    BftManager backup_bft_mgr0;
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
    BftManager backup_bft_mgr1;
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(
        common::Encode::HexDecode("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
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

TEST_F(TestTx, TestMoreTx) {
    BftManager leader_bft_mgr;
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
    BftManager backup_bft_mgr0;
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
    BftManager backup_bft_mgr1;
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    for (uint32_t i = 0; i < 100; ++i) {
        pools::protobuf::TxMessage tx_info;
        CreateTxInfo(
            prikeys[i % prikeys.size()],
            common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
            tx_info);
        AddTxs(leader_bft_mgr, tx_info);
        AddTxs(backup_bft_mgr0, tx_info);
        AddTxs(backup_bft_mgr1, tx_info);
    }

    while (true) {
        leader_bft_mgr.Start(0);
        if (leader_bft_mgr.leader_prepare_msg_ == nullptr) {
            break;
        }

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
        leader_bft_mgr.ResetTest();
        backup_bft_mgr0.ResetTest();
        backup_bft_mgr1.ResetTest();
    }
};

TEST_F(TestTx, TestTxOnePrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
    BftManager backup_bft_mgr0;
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
    BftManager backup_bft_mgr1;
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    CreateTxInfo(
        common::Encode::HexDecode("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_prepare_evil_ = true;
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

TEST_F(TestTx, TestTxOnePrecommitEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
    BftManager backup_bft_mgr0;
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
    BftManager backup_bft_mgr1;
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    CreateTxInfo(
        common::Encode::HexDecode("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_precommit_evil_ = true;
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

TEST_F(TestTx, TestTxTwoPrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
    BftManager backup_bft_mgr0;
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
    BftManager backup_bft_mgr1;
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    CreateTxInfo(
        common::Encode::HexDecode("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_prepare_evil_ = true;
    backup_bft_mgr1.test_for_prepare_evil_ = true;
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
