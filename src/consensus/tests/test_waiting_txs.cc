#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#define private public
#include "common/random.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/waiting_txs.h"
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

class TestWaitingTxs : public testing::Test {
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
        system("rm -rf ./core.* ./wtx_db");
        db_ptr->Init("./wtx_db");
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

TEST_F(TestWaitingTxs, GetValidTxs) {
    uint32_t invalid = 0;
    const uint32_t kTestCount = 10;
    for (int32_t i = 0; i < kTestCount; ++i) {
        AddTxs();
        WaitingTxs wtxs[common::kInvalidPoolIndex];
        WaitingTxs wtxs_backup[common::kInvalidPoolIndex];
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            wtxs[i].Init(i, pools_mgr);
            wtxs_backup[i].Init(i, pools_mgr_backup);
            auto ltxs = wtxs[i].LeaderGetValidTxs(false);
            ASSERT_TRUE(ltxs != nullptr);

            auto& tx_map = ltxs->txs;
            uint32_t bitcount = ((kBitcountWithItemCount * tx_map.size()) / 64) * 64;
            if (((kBitcountWithItemCount * tx_map.size()) % 64) > 0) {
                bitcount += 64;
            }

            ltxs->bloom_filter = std::make_shared<common::BloomFilter>(bitcount, kHashCount);
            for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
                ltxs->bloom_filter->Add(common::Hash::Hash64(iter->first));
            }

            // pass by bloomfilter
            auto error_bloomfilter_txs = wtxs[i].FollowerGetTxs(*ltxs->bloom_filter);
            for (auto iter = error_bloomfilter_txs->txs.begin();
                    iter != error_bloomfilter_txs->txs.end(); ++iter) {
                tx_map[iter->first] = iter->second;
            }

            auto& all_txs_hash = ltxs->all_txs_hash;
            all_txs_hash.reserve(tx_map.size() * 32);
            for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
                all_txs_hash.append(iter->first);
            }

            all_txs_hash = common::Hash::keccak256(all_txs_hash);
            ASSERT_TRUE(ltxs->bloom_filter != nullptr);
            auto btxs = wtxs_backup[i].FollowerGetTxs(*ltxs->bloom_filter);
            if (ltxs->txs.size() != btxs->txs.size()) {
                invalid += btxs->txs.size() - ltxs->txs.size();
            } else {
                EXPECT_EQ(ltxs->all_txs_hash, btxs->all_txs_hash);
            }
        }
    }
    
    std::cout << "ratio: " << (float(kTestCount * common::kInvalidPoolIndex * kTestTxCount - invalid) / float(kTestCount * common::kInvalidPoolIndex * kTestTxCount)) << std::endl;
    EXPECT_EQ(invalid, 0);
}

}  // namespace test

}  // namespace consensus

}  // namespace zjchain
