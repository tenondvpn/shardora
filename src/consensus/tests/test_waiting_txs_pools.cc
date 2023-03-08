#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#define private public
#include "common/random.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/zbft_waiting_txs_pools.h"
#include "consensus/zbft/zbft_utils.h"
#include "db/db.h"
#include "pools/tx_pool_manager.h"
#include "protos/pools.pb.h"
#include "security/ecdsa/ecdsa.h"

namespace zjchain {

namespace consensus {

namespace test {

static std::shared_ptr<security::Security> security = nullptr;
static std::shared_ptr<pools::TxPoolManager> pools_mgr = nullptr;
static std::shared_ptr<pools::TxPoolManager> pools_mgr_backup = nullptr;
static const uint32_t kTestTxCount = 256;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static std::shared_ptr<block::BlockManager> block_mgr = nullptr;

class TestWaitingTxsPools : public testing::Test {
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
        system("rm -rf ./core.* ./wtxp_db");
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./wtxp_db");
        std::string config_path_ = "./";
        std::string conf_path = config_path_ + "/zjc.conf";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/zjc.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
        security = std::make_shared<security::Ecdsa>();
        AddTxs();
    }

    static void AddTxs() {
        pools_mgr = std::make_shared<pools::TxPoolManager>(security, db_ptr);
        pools_mgr_backup = std::make_shared<pools::TxPoolManager>(security, db_ptr);
        std::string random_prefix = common::Random::RandomString(33);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            for (uint32_t j = 0; j < kTestTxCount; ++j) {
                auto msg_ptr = std::make_shared<transport::TransportMessage>();
                pools::protobuf::TxMessage& tx_info = *msg_ptr->header.mutable_tx_proto();
                tx_info.set_step(pools::protobuf::kNormalFrom);
                uint32_t* test_arr = (uint32_t*)random_prefix.data();
                test_arr[0] = i + j;
                auto pk = std::string((char*)test_arr, 33);
                tx_info.set_pubkey(pk);
                tx_info.set_to("");
                auto gid = std::string((char*)test_arr, 32);
                tx_info.set_gid(gid);
                tx_info.set_gas_limit(0llu);
                tx_info.set_amount(0);
                tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
                std::shared_ptr<block::AccountManager> acc_ptr = nullptr;
                pools::TxItemPtr tx_ptr = std::make_shared<FromTxItem>(msg_ptr, acc_ptr, security);
                tx_ptr->tx_hash = pools::GetTxMessageHash(tx_info);
                ASSERT_EQ(pools_mgr->AddTx(i, tx_ptr), pools::kPoolsSuccess);
                ASSERT_EQ(pools_mgr->tx_pool_[i].added_tx_map_.size(), j + 1);
                ASSERT_EQ(pools_mgr_backup->AddTx(i, tx_ptr), pools::kPoolsSuccess);
                ASSERT_EQ(pools_mgr_backup->tx_pool_[i].added_tx_map_.size(), j + 1);
            }
        }
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestWaitingTxsPools, GetValidTxs) {
    uint32_t invalid = 0;
    const uint32_t kTestCount = 10;
    uint32_t invalid_txs_count = 0;
    uint32_t all_count = 0;
    for (int32_t i = 0; i < kTestCount; ++i) {
        AddTxs();
        ZbftWaitingTxsPools leader_txs_pools(pools_mgr, block_mgr);
        ZbftWaitingTxsPools follower_txs_pools(pools_mgr_backup, block_mgr);
        for (int32_t j = 0; j < common::kInvalidPoolIndex; ++j) {
            auto ltxs = leader_txs_pools.LeaderGetValidTxs(false, j);
            if (ltxs == nullptr) {
                continue;
            }

            ASSERT_TRUE(ltxs != nullptr);
            auto btxs = follower_txs_pools.FollowerGetTxs(
                ltxs->pool_index,
                *ltxs->bloom_filter,
                0);
            ASSERT_TRUE(btxs != nullptr);
            all_count += ltxs->txs.size();
            ASSERT_EQ(btxs->txs.size(), ltxs->txs.size());
            if (ltxs->txs.size() != btxs->txs.size()) {
                invalid_txs_count += btxs->txs.size() - ltxs->txs.size();
            } else {
                EXPECT_EQ(ltxs->all_txs_hash, btxs->all_txs_hash);
            }
        }
    }
    
    std::cout << "ratio: " <<
        (float(all_count - invalid_txs_count) / float(all_count)) << std::endl;
    EXPECT_EQ(invalid_txs_count, 0);
}

}  // namespace test

}  // namespace consensus

}  // namespace zjchain
