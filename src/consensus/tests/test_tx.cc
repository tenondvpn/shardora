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
#include "consensus/zbft/from_tx_item.h"
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
static std::atomic<uint32_t> tx_index = 0;
static std::atomic<uint32_t> db_index = 0;
static std::string random_prefix;
static const uint32_t kTestShardingId = 3;
static std::unordered_map<std::string, std::string> addrs_map;
static std::vector<std::string> prikeys;
static std::vector<std::string> addrs;
static std::unordered_map<std::string, std::string> pri_pub_map;

class TestInit {
public:
    TestInit(BftManager* bft_mgr) {
        bft_mgr_ = bft_mgr;
        new_block_cache_callback_ = std::bind(
            &TestInit::AddBlockItemToCache,
            this,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3);
    }

private:
    void AddBlockItemToCache(
            uint8_t thread_idx,
            std::shared_ptr<block::protobuf::Block>& block,
            db::DbWriteBatch& db_batch) {
        const auto& tx_list = block->tx_list();
        if (tx_list.empty()) {
            return;
        }

        // one block must be one consensus pool
        for (int32_t i = 0; i < tx_list.size(); ++i) {
            switch (tx_list[i].step()) {
            case pools::protobuf::kNormalFrom:
            case pools::protobuf::kConsensusLocalTos:
                bft_mgr_->account_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
                break;
            case pools::protobuf::kConsensusRootTimeBlock:
                bft_mgr_->tm_block_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
                break;
            default:
                break;
            }
        }
    }

    BftManager* bft_mgr_ = nullptr; 
    BlockCacheCallback new_block_cache_callback_ = nullptr;
};

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

    void InitConsensus(
            BftManager& bft_mgr,
            const std::string& prikey,
            BlockCacheCallback new_block_cache_callback,
            uint8_t thread_count = 1) {
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        auto account_mgr = std::make_shared<block::AccountManager>();
        auto db_ptr = std::make_shared<db::Db>();
        auto tmp_prikey = std::string("db_") + 
            common::Encode::HexEncode(prikey) + std::to_string(db_index++);
        system((std::string("rm -rf ./") + tmp_prikey).c_str());
        system((std::string("cp -rf ../../src/consensus/tests/test_tx ./") + tmp_prikey).c_str());
        std::string db_path = std::string("./") + tmp_prikey;
        db_ptr->Init(db_path);
        auto block_mgr = std::make_shared<block::BlockManager>();
        auto bls_mgr = std::make_shared<bls::BlsManager>(security, db_ptr);
        auto pools_mgr = std::make_shared<pools::TxPoolManager>(security, db_ptr);
        account_mgr->Init(thread_count, db_ptr, pools_mgr);
        block_mgr->Init(account_mgr, db_ptr, pools_mgr, security->GetAddress());
        block_mgr->SetMaxConsensusShardingId(3);
        auto elect_mgr = std::make_shared<elect::ElectManager>(
            block_mgr, security, bls_mgr, db_ptr, nullptr);
        ASSERT_EQ(elect_mgr->Init(), 0);
        auto tm_block_mgr = std::make_shared<timeblock::TimeBlockManager>();
        tm_block_mgr->Init(pools_mgr, db_ptr);
        ASSERT_EQ(bft_mgr.Init(
            account_mgr,
            block_mgr,
            elect_mgr,
            pools_mgr,
            security,
            tm_block_mgr,
            db_ptr,
            nullptr,
            thread_count,
            new_block_cache_callback), kConsensusSuccess);
        auto members = elect_mgr->GetNetworkMembers(kTestShardingId);
        bft_mgr.OnNewElectBlock(kTestShardingId, members);
        block_mgr->OnNewElectBlock(kTestShardingId, members);
        common::GlobalInfo::Instance()->set_network_id(kTestShardingId);
    }

    void AddTxs(
            BftManager& bft_mgr,
            const pools::protobuf::TxMessage& tx_info) {
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto from_addr = bft_mgr.security_ptr_->GetAddress(tx_info.pubkey());
        msg_ptr->address_info = bft_mgr.account_mgr_->GetAccountInfo(0, from_addr);
        ASSERT_TRUE(msg_ptr->address_info->balance() > 0);
        *msg_ptr->header.mutable_tx_proto() = tx_info;
        pools::TxItemPtr tx_ptr = std::make_shared<FromTxItem>(
            msg_ptr, bft_mgr.account_mgr_, bft_mgr.security_ptr_);
        tx_ptr->tx_hash = pools::GetTxMessageHash(tx_info);
        ASSERT_EQ(
            bft_mgr.pools_mgr_->AddTx(msg_ptr->address_info->pool_index(), tx_ptr),
            pools::kPoolsSuccess);
        ASSERT_TRUE(bft_mgr.pools_mgr_->tx_pool_[
            msg_ptr->address_info->pool_index()].added_tx_map_.size() > 0);
    }

    void CreateTxInfo(
            const std::string& from_prikey,
            const std::string& to,
            pools::protobuf::TxMessage& tx_info) {
        tx_info.set_step(pools::protobuf::kNormalFrom);
        uint32_t* test_arr = (uint32_t*)random_prefix.data();
        test_arr[0] = tx_index++;
        tx_info.set_pubkey(pri_pub_map[from_prikey]);
        tx_info.set_to(to);
        tx_info.set_gid(std::string((char*)test_arr, 32));
        tx_info.set_gas_limit(10000000lu);
        tx_info.set_amount(100000lu);
        tx_info.set_gas_price(10lu);
    }

    static void TearDownTestCase() {
        //system("rm -rf ./db_*");
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestTx, TestTx) {
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_);
    pools::protobuf::TxMessage tx_info;
    CreateTxInfo(
        common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    transport::MessagePtr prepare_msg_ptr = nullptr;
    // 1.prepare
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
    // 2. precommit
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
    // 3. commit
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
};

TEST_F(TestTx, TestMultiThread) {
    const uint8_t kThreadCount = 4;
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_,
        kThreadCount);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_,
        kThreadCount);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_,
        kThreadCount);
    auto to_addr = common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c");
    auto to_acc = leader_bft_mgr.account_mgr_->GetAccountInfo(0, to_addr);
    ASSERT_TRUE(to_acc != nullptr);
    uint64_t src_balance = to_acc->balance();
    uint64_t invalid_count = 0lu;
    const uint64_t kTestCount = 100000lu;
    for (uint32_t i = 0; i < kTestCount; ++i) {
        pools::protobuf::TxMessage tx_info;
        auto& from_prikey = prikeys[i % prikeys.size()];
        if (addrs_map[from_prikey] == to_addr) {
            ++invalid_count;
            continue;
        }

        CreateTxInfo(
            from_prikey,
            to_addr,
            tx_info);
        AddTxs(leader_bft_mgr, tx_info);
        AddTxs(backup_bft_mgr0, tx_info);
        AddTxs(backup_bft_mgr1, tx_info);
    }

    for (int32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        std::cout << leader_bft_mgr.pools_mgr_->tx_pool_[i].mem_queue_.size() << " ";
    }

    std::cout << std::endl;

    volatile bool over = false;
    auto leader_block_thread = [&]() {
        while (!over) {
            leader_bft_mgr.block_mgr_->HandleAllConsensusBlocks(0);
            backup_bft_mgr0.block_mgr_->HandleAllConsensusBlocks(0);
            backup_bft_mgr1.block_mgr_->HandleAllConsensusBlocks(0);
            usleep(100000);
        }
    };

    uint64_t all_b_time = common::TimeUtils::TimestampUs();
    auto block_thread = std::thread(leader_block_thread);
    uint64_t times[64] = { 0 };
    auto thread_call = [&](uint8_t thread_idx) {
        while (true) {
            // 1. prepare
            auto tm0 = common::TimeUtils::TimestampUs();
            transport::MessagePtr prepare_msg_ptr = nullptr;
            if (backup_bft_mgr0.now_msg_[thread_idx] == nullptr) {
                leader_bft_mgr.now_msg_[thread_idx] = nullptr;
                leader_bft_mgr.Start(thread_idx, prepare_msg_ptr);
                if (leader_bft_mgr.now_msg_[thread_idx] == nullptr) {
                    break;
                }
            } else {
                if (backup_bft_mgr0.now_msg_[thread_idx]->header.zbft().prepare_gid().empty() &&
                        backup_bft_mgr0.now_msg_[thread_idx]->header.zbft().precommit_gid().empty() &&
                        backup_bft_mgr0.now_msg_[thread_idx]->header.zbft().commit_gid().empty()) {
                    break;
                }

                backup_bft_mgr0.now_msg_[thread_idx]->thread_idx = thread_idx;
                backup_bft_mgr1.now_msg_[thread_idx]->thread_idx = thread_idx;
                leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[thread_idx]);
                leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[thread_idx]);
            }

            ASSERT_TRUE(leader_bft_mgr.now_msg_[thread_idx] != nullptr);
            leader_bft_mgr.now_msg_[thread_idx]->thread_idx = thread_idx;
            backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
            backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
            if (backup_bft_mgr0.now_msg_[thread_idx] == nullptr) {
                break;
            }

            ASSERT_TRUE(backup_bft_mgr1.now_msg_[thread_idx] != nullptr);
            backup_bft_mgr0.now_msg_[thread_idx]->thread_idx = thread_idx;
            backup_bft_mgr1.now_msg_[thread_idx]->thread_idx = thread_idx;
            leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[thread_idx]);
            leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[thread_idx]);
            // 2. precommit
            ASSERT_TRUE(leader_bft_mgr.now_msg_[thread_idx] != nullptr);
            leader_bft_mgr.now_msg_[thread_idx]->thread_idx = thread_idx;
            backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
            backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
            if (backup_bft_mgr0.now_msg_[thread_idx] == nullptr) {
                break;
            }

            ASSERT_TRUE(backup_bft_mgr1.now_msg_[thread_idx] != nullptr);
            backup_bft_mgr0.now_msg_[thread_idx]->thread_idx = thread_idx;
            backup_bft_mgr1.now_msg_[thread_idx]->thread_idx = thread_idx;
            leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[thread_idx]);
            leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[thread_idx]);
            // 3. commit
            ASSERT_TRUE(leader_bft_mgr.now_msg_[thread_idx] != nullptr);
            leader_bft_mgr.now_msg_[thread_idx]->thread_idx = thread_idx;
            backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
            backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[thread_idx]);
        }
    };

    std::vector<std::shared_ptr<std::thread>> call_threads;
    for (uint8_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        call_threads.push_back(std::make_shared<std::thread>(thread_call, thread_idx));
    }

    for (uint8_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        call_threads[thread_idx]->join();
    }

    uint64_t all_e_time = common::TimeUtils::TimestampUs();
    std::cout << kThreadCount << " threads tps: " <<
        (float(kTestCount - invalid_count) / (float(all_e_time - all_b_time) / 1000000.0)) <<
        std::endl;

    usleep(200000);
    over = true;
    block_thread.join();
    for (int32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        std::cout << leader_bft_mgr.pools_mgr_->tx_pool_[i].mem_queue_.size() << " ";
    }


    std::cout << std::endl;
};

TEST_F(TestTx, TestMoreTx) {
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_);
    auto to_addr = common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c");
    auto to_acc = leader_bft_mgr.account_mgr_->GetAccountInfo(0, to_addr);
    ASSERT_TRUE(to_acc != nullptr);
    uint64_t src_balance = to_acc->balance();
    uint64_t invalid_count = 0lu;
    const uint64_t kTestCount = 100000lu;
    for (uint32_t i = 0; i < kTestCount; ++i) {
        pools::protobuf::TxMessage tx_info;
        auto& from_prikey = prikeys[i % prikeys.size()];
        if (addrs_map[from_prikey] == to_addr) {
            ++invalid_count;
            continue;
        }

        CreateTxInfo(
            from_prikey,
            to_addr,
            tx_info);
        AddTxs(leader_bft_mgr, tx_info);
        AddTxs(backup_bft_mgr0, tx_info);
        AddTxs(backup_bft_mgr1, tx_info);
    }

    volatile bool over = false;
    auto leader_block_thread = [&]() {
        while (!over) {
            leader_bft_mgr.block_mgr_->HandleAllConsensusBlocks(0);
            backup_bft_mgr0.block_mgr_->HandleAllConsensusBlocks(0);
            backup_bft_mgr1.block_mgr_->HandleAllConsensusBlocks(0);
            usleep(100000);
        }
    };

    uint64_t all_b_time = common::TimeUtils::TimestampUs();
    auto block_thread = std::thread(leader_block_thread);
    uint64_t times[64] = { 0 };
    uint32_t consensus_count = 0;
    while (true) {
        // 1. prepare
        auto tm0 = common::TimeUtils::TimestampUs();
        transport::MessagePtr prepare_msg_ptr = nullptr;
        if (backup_bft_mgr0.now_msg_[0] == nullptr) {
            ZJC_DEBUG("now leader start.");
            leader_bft_mgr.now_msg_[0] = nullptr;
            leader_bft_mgr.Start(0, prepare_msg_ptr);
            if (leader_bft_mgr.now_msg_[0] == nullptr) {
                break;
            }
        } else {
            ZJC_DEBUG("now leader pipeline start prepare gid: %s, precommit gid: %s, commit gid: %s",
                common::Encode::HexEncode(
                    backup_bft_mgr0.now_msg_[0]->header.zbft().prepare_gid()).c_str(),
                common::Encode::HexEncode(
                    backup_bft_mgr0.now_msg_[0]->header.zbft().precommit_gid()).c_str(),
                common::Encode::HexEncode(
                    backup_bft_mgr0.now_msg_[0]->header.zbft().commit_gid()).c_str());
            if (backup_bft_mgr0.now_msg_[0]->header.zbft().prepare_gid().empty() &&
                    backup_bft_mgr0.now_msg_[0]->header.zbft().precommit_gid().empty() &&
                    backup_bft_mgr0.now_msg_[0]->header.zbft().commit_gid().empty()) {
                break;
            }

            backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
            backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
            leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
            leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
        }

        ZJC_DEBUG("now backup prepare start.");
        ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
        auto tm = common::TimeUtils::TimestampUs();
        times[0] += tm - tm0;
        tm0 = tm;
        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ZJC_DEBUG("now backup prepare end.");
        if (backup_bft_mgr0.now_msg_[0] == nullptr) {
            break;
        }

        ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
        tm = common::TimeUtils::TimestampUs();
        times[1] += tm - tm0;
        tm0 = tm;

        backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
        ZJC_DEBUG("now leader precommit.");
        leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
        leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
        tm = common::TimeUtils::TimestampUs();
        times[2] += tm - tm0;
        tm0 = tm;
        ZJC_DEBUG("leader precommit end.");
        ZJC_DEBUG("now backup precommit.");
        // 2. precommit
        ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ZJC_DEBUG("backup precommit end.");
        if (backup_bft_mgr0.now_msg_[0] == nullptr) {
            break;
        }

        ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
        tm = common::TimeUtils::TimestampUs();
        times[3] += tm - tm0;
        tm0 = tm;
        backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
        leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
        leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
        tm = common::TimeUtils::TimestampUs();
        times[4] += tm - tm0;
        tm0 = tm;
        // 3. commit
        ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
        tm = common::TimeUtils::TimestampUs();
        times[5] += tm - tm0;
        ++consensus_count;
    }

    uint64_t all_e_time = common::TimeUtils::TimestampUs();
    std::cout << ", consensus_count: " << consensus_count << ", tps: " <<
        (float(kTestCount - invalid_count) / (float(all_e_time - all_b_time) / 1000000.0)) <<
        std::endl;
    for (uint32_t i = 0; i < 7; ++i) {
        std::cout << (i > 6 ? (i - 6) : i) << " : " << (times[i] / consensus_count) << std::endl;
    }

    usleep(200000);
    over = true;
    block_thread.join();
    // batch transfer to txs
    leader_bft_mgr.block_mgr_->CreateToTx(0);
    ASSERT_TRUE(leader_bft_mgr.block_mgr_->leader_to_txs_msg_ != nullptr);
    leader_bft_mgr.block_mgr_->leader_to_txs_msg_->thread_idx = 0;
    leader_bft_mgr.block_mgr_->HandleMessage(leader_bft_mgr.block_mgr_->leader_to_txs_msg_);
    backup_bft_mgr0.block_mgr_->HandleMessage(leader_bft_mgr.block_mgr_->leader_to_txs_msg_);
    backup_bft_mgr1.block_mgr_->HandleMessage(leader_bft_mgr.block_mgr_->leader_to_txs_msg_);

    // cross shard
    while (true) {
        // 1. prepare
        transport::MessagePtr prepare_msg_ptr = nullptr;
        leader_bft_mgr.now_msg_[0] = nullptr;
        leader_bft_mgr.Start(0, prepare_msg_ptr);
        if (leader_bft_mgr.now_msg_[0] == nullptr) {
            break;
        }

        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
        backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
        leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
        leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);

        // 2. precommit
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

        // 3. commit
        ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    }

    leader_bft_mgr.block_mgr_->HandleAllConsensusBlocks(0);
    backup_bft_mgr0.block_mgr_->HandleAllConsensusBlocks(0);
    backup_bft_mgr1.block_mgr_->HandleAllConsensusBlocks(0);
    // local shard to txs
    while (true) {
        // 1. prepare
        transport::MessagePtr prepare_msg_ptr = nullptr;
        leader_bft_mgr.now_msg_[0] = nullptr;
        leader_bft_mgr.Start(0, prepare_msg_ptr);
        if (leader_bft_mgr.now_msg_[0] == nullptr) {
            break;
        }

        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
        ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
        backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
        leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
        leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);

        // 2. precommit
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

        // 3. commit
        ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
        leader_bft_mgr.now_msg_[0]->thread_idx = 0;
        backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
        backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    }

    auto to_acc_b1 = backup_bft_mgr0.account_mgr_->GetAccountInfo(0, to_addr);
    ASSERT_TRUE(to_acc_b1 != nullptr);
    std::cout << src_balance << ":" << to_acc_b1->balance() << ", " << kTestCount << ", " << invalid_count << std::endl;
    ASSERT_EQ(src_balance + (kTestCount - invalid_count) * 100000lu, to_acc_b1->balance());

    auto to_acc_b2 = backup_bft_mgr1.account_mgr_->GetAccountInfo(0, to_addr);
    ASSERT_TRUE(to_acc_b2 != nullptr);
    std::cout << src_balance << ":" << to_acc_b2->balance() << ", " << kTestCount << ", " << invalid_count << std::endl;
    ASSERT_EQ(src_balance + (kTestCount - invalid_count) * 100000lu, to_acc_b2->balance());

    auto to_acc_leader = leader_bft_mgr.account_mgr_->GetAccountInfo(0, to_addr);
    ASSERT_TRUE(to_acc_leader != nullptr);
    std::cout << src_balance << ":" << to_acc_leader->balance() << ", " << kTestCount << ", " << invalid_count << std::endl;
    ASSERT_EQ(src_balance + (kTestCount - invalid_count) * 100000lu, to_acc_leader->balance());
};

TEST_F(TestTx, TestTxOnePrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_);
    CreateTxInfo(
        common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_prepare_evil_ = true;
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

    ZJC_DEBUG("leader 0");
    auto leader_ptr = leader_bft_mgr.GetBft(0, bft_ptr->gid(), true);
    ASSERT_TRUE(leader_ptr != nullptr);
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
    leader_ptr = leader_bft_mgr.GetBft(0, bft_ptr->gid(), true);
    ASSERT_TRUE(leader_ptr == nullptr);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;

    ZJC_DEBUG("backup 0");
    auto b0_ptr = backup_bft_mgr0.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b0_ptr == nullptr);
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    b0_ptr = backup_bft_mgr0.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b0_ptr == nullptr);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] == nullptr);
    auto b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr == nullptr);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] == nullptr);
};

TEST_F(TestTx, TestTxOnePrecommitEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_);
    CreateTxInfo(
        common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_precommit_evil_ = true;
    transport::MessagePtr prepare_msg_ptr = nullptr;
    // prepare
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

    // precommit
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    ZJC_DEBUG("backup 0");
    auto b0_ptr = backup_bft_mgr0.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b0_ptr != nullptr);
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
    b0_ptr = backup_bft_mgr0.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b0_ptr == nullptr);

    auto b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
    b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr != nullptr);

    backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
    auto l_ptr = leader_bft_mgr.GetBft(0, bft_ptr->gid(), true);
    ASSERT_TRUE(l_ptr != nullptr);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
    ASSERT_TRUE(leader_bft_mgr.now_msg_[0] != nullptr);
    l_ptr = leader_bft_mgr.GetBft(0, bft_ptr->gid(), true);
    ASSERT_TRUE(l_ptr == nullptr);

    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    b0_ptr = backup_bft_mgr0.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b0_ptr == nullptr);
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    b1_ptr = backup_bft_mgr1.GetBft(0, bft_ptr->gid(), false);
    ASSERT_TRUE(b1_ptr == nullptr);
};

TEST_F(TestTx, TestTxTwoPrepareEvil) {
    pools::protobuf::TxMessage tx_info;
    BftManager leader_bft_mgr;
    TestInit t1(&leader_bft_mgr);
    InitConsensus(leader_bft_mgr, common::Encode::HexDecode(
        "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"),
        t1.new_block_cache_callback_);
    BftManager backup_bft_mgr0;
    TestInit t2(&backup_bft_mgr0);
    InitConsensus(backup_bft_mgr0, common::Encode::HexDecode(
        "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"),
        t2.new_block_cache_callback_);
    BftManager backup_bft_mgr1;
    TestInit t3(&backup_bft_mgr1);
    InitConsensus(backup_bft_mgr1, common::Encode::HexDecode(
        "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"),
        t3.new_block_cache_callback_);
    CreateTxInfo(
        common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"),
        common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c"),
        tx_info);
    AddTxs(leader_bft_mgr, tx_info);
    AddTxs(backup_bft_mgr0, tx_info);
    AddTxs(backup_bft_mgr1, tx_info);
    backup_bft_mgr0.test_for_prepare_evil_ = true;
    backup_bft_mgr1.test_for_prepare_evil_ = true;
    transport::MessagePtr prepare_msg_ptr = nullptr;
    auto bft_ptr = leader_bft_mgr.Start(0, prepare_msg_ptr);
    ASSERT_TRUE(bft_ptr != nullptr);
    leader_bft_mgr.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr0.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
    ASSERT_TRUE(backup_bft_mgr0.now_msg_[0] != nullptr);
    backup_bft_mgr1.HandleMessage(leader_bft_mgr.now_msg_[0]);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
    ASSERT_TRUE(backup_bft_mgr1.now_msg_[0] != nullptr);
    backup_bft_mgr0.now_msg_[0]->thread_idx = 0;
    backup_bft_mgr1.now_msg_[0]->thread_idx = 0;
    leader_bft_mgr.HandleMessage(backup_bft_mgr0.now_msg_[0]);
    leader_bft_mgr.HandleMessage(backup_bft_mgr1.now_msg_[0]);
};

}  // namespace test

}  // namespace consensus

}  // namespace zjchain
