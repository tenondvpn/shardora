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
#include "consensus/zbft/waiting_txs_pools.h"
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
static std::shared_ptr<db::Db> db_ptr = nullptr;
static const uint32_t kTestShardingId = 2;
static std::atomic<uint32_t> tx_index = 0;
static std::atomic<uint32_t> db_index = 0;
static std::string random_prefix;
static std::unordered_map<std::string, std::string> addrs_map;
static std::vector<std::string> prikeys;
static std::vector<std::string> addrs;
static std::unordered_map<std::string, std::string> pri_pub_map;

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
            "log4cpp.rootCategory = DEBUG\n"
            "log4cpp.category.sub1 = DEBUG, programLog\n"
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
        random_prefix = common::Random::RandomString(33);
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

            std::string prikey = common::Encode::HexDecode(
                std::string(split[1], split.SubLen(1) - 1));
            std::string addr = common::Encode::HexDecode(split[0]);
            addrs_map[prikey] = addr;
            addrs.push_back(addr);
            prikeys.push_back(prikey);
            std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
            security->SetPrivateKey(prikey);
            pri_pub_map[prikey] = security->GetPublicKey();
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
        auto tmp_prikey = std::string("db_") + 
            common::Encode::HexEncode(prikey) + std::to_string(db_index++);
        system((std::string("rm -rf ./") + tmp_prikey).c_str());
        system((std::string("cp -rf ../../src/consensus/tests/test_tm ./") + tmp_prikey).c_str());
        std::string db_path = std::string("./") + tmp_prikey;
        db_ptr->Init(db_path);
        auto block_mgr = std::make_shared<block::BlockManager>();
        auto bls_mgr = std::make_shared<bls::BlsManager>(security, db_ptr);
        auto pools_mgr = std::make_shared<pools::TxPoolManager>(security, db_ptr);
        account_mgr->Init(1, db_ptr, pools_mgr);
        block_mgr->Init(account_mgr, db_ptr, pools_mgr, security->GetAddress());
        block_mgr->SetMaxConsensusShardingId(3);
        auto elect_mgr = std::make_shared<elect::ElectManager>(
            block_mgr, security, bls_mgr, db_ptr, nullptr);
        ASSERT_EQ(elect_mgr->Init(), 0);
        auto tm_block_mgr = std::make_shared<timeblock::TimeBlockManager>();
        tm_block_mgr->Init(pools_mgr, db_ptr);
        auto new_block_callback = [&](
            uint8_t thread_idx,
            std::shared_ptr<block::protobuf::Block>& block,
            db::DbWriteBatch& db_batch) {
            const auto& tx_list = block->tx_list();
            if (tx_list.empty()) {
                return;
            }

            // one block must be one consensus pool
            for (int32_t i = 0; i < tx_list.size(); ++i) {
                account_mgr->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            }
        };

        ASSERT_EQ(bft_mgr.Init(
            account_mgr,
            block_mgr,
            elect_mgr,
            pools_mgr,
            security,
            tm_block_mgr,
            db_ptr,
            nullptr,
            1,
            new_block_callback), kConsensusSuccess);
        auto members = elect_mgr->GetNetworkMembers(kTestShardingId);
        bft_mgr.OnNewElectBlock(kTestShardingId, members);
        block_mgr->OnNewElectBlock(kTestShardingId, members);
        common::GlobalInfo::Instance()->set_network_id(kTestShardingId);
    }

    void AddTxs(
            BftManager& bft_mgr,
            const pools::protobuf::TxMessage& tx_info) {
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
//         auto from_addr = bft_mgr.security_ptr_->GetAddress(tx_info.pubkey());
//         msg_ptr->address_info = bft_mgr.account_mgr_->GetAccountInfo(0, from_addr);
//         ASSERT_TRUE(msg_ptr->address_info->balance() > 0);
        *msg_ptr->header.mutable_tx_proto() = tx_info;
        pools::TxItemPtr tx_ptr = std::make_shared<FromTxItem>(
            msg_ptr, bft_mgr.account_mgr_, bft_mgr.security_ptr_);
        tx_ptr->tx_hash = pools::GetTxMessageHash(tx_info);
        ASSERT_EQ(
            bft_mgr.pools_mgr_->AddTx(common::kRootChainPoolIndex, tx_ptr),
            pools::kPoolsSuccess);
        ASSERT_TRUE(bft_mgr.pools_mgr_->tx_pool_[
            common::kRootChainPoolIndex].added_tx_map_.size() > 0);
    }

    void CreateTxInfo(
            const std::string& from_prikey,
            const std::string& to,
            pools::protobuf::TxMessage& tx_info) {
        tx_info.set_step(pools::protobuf::kConsensusRootTimeBlock);
        uint32_t* test_arr = (uint32_t*)random_prefix.data();
        test_arr[0] = tx_index++;
        tx_info.set_pubkey("");
        tx_info.set_to(to);
        tx_info.set_gid(std::string((char*)test_arr, 32));
        tx_info.set_gas_limit(0);
        tx_info.set_amount(0);
        tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
    }

    static void TearDownTestCase() {
        system("rm -rf ./db_*");
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestTimeBlock, TestTimeBlock) {
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
        "",
        common::kRootChainTimeBlockTxAddress,
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    transport::MessagePtr prepare_msg_ptr = nullptr;
    auto bft_ptr = leader_bft_mgr.Start(0, prepare_msg_ptr);
    ASSERT_TRUE(bft_ptr != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
    backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
    backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
};

}  // namespace test

}  // namespace consensus

}  // namespace zjchain
