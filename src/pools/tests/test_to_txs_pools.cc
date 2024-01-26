#include <stdlib.h>
#include <math.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>

#include <gtest/gtest.h>

#include "bzlib.h"
#include "common/random.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "protos/prefix_db.h"

#define private public
#include "pools/tx_pool_manager.h"
#include "pools/to_txs_pools.h"
#include "protos/block.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

namespace test {

static uint32_t *block_heihgts[1024];
static std::atomic<uint32_t> addr_index = 0;
static std::string kRandomString;
static const uint32_t kMaxShardingId = 10;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static std::shared_ptr<protos::PrefixDb> prefix_db = nullptr;
static const uint32_t kTestEachPoolBlockCount = 100;
static const uint32_t kTestEachPoolTxCount = 100;
static const uint32_t kTestNetworkId = 3;

class TestToTxsPools : public testing::Test {
public:
    static void SetUpTestCase() {
        kRandomString = common::Random::RandomString(20);
        common::GlobalInfo::Instance()->set_network_id(kTestNetworkId);
        for (uint32_t i = 0; i < 1024; ++i) {
            block_heihgts[i] = new uint32_t[common::kImmutablePoolSize];
            memset(block_heihgts[i], 0, common::kImmutablePoolSize * sizeof(uint32_t));
        }

        db_ptr = std::make_shared<db::Db>();
        system("rm -rf ./core.* ./testtotxs");
        ASSERT_TRUE(db_ptr->Init("./testtotxs"));
        prefix_db = std::make_shared<protos::PrefixDb>(db_ptr);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    static std::shared_ptr<address::protobuf::AddressInfo> CreateAddr(db::DbWriteBatch& db_batch) {
        auto addr = kRandomString;
        uint32_t* index_arr = (uint32_t*)addr.data();
        index_arr[0] = addr_index++;
        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        addr_info->set_addr(addr);
        addr_info->set_sharding_id(addr_index % kMaxShardingId);
        addr_info->set_pool_index(addr_index % 256);
        addr_info->set_balance(10000000000lu);
        addr_info->set_type(address::protobuf::kNormal);
        prefix_db->AddAddressInfo(addr, *addr_info, db_batch);
        return addr_info;
    }

    static void CreateBlock(ToTxsPools& to_txs_pool) {
        for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
            for (uint32_t j = 0; j < kTestEachPoolBlockCount; ++j) {
                db::DbWriteBatch db_batch;
                auto block_ptr = std::make_shared<block::protobuf::Block>();
                block_ptr->set_network_id(kTestNetworkId);
                block_ptr->set_pool_index(i);
                block_ptr->set_height(j);
                auto& tx_list = *block_ptr->mutable_tx_list();
                for (uint32_t k = 0; k < kTestEachPoolTxCount; ++k) {
                    auto& tx = *tx_list.Add();
                    auto addr_info = CreateAddr(db_batch);
                    tx.set_from("from");
                    tx.set_to(addr_info->addr());
                    tx.set_amount(1232324lu);
                    tx.set_step(pools::protobuf::kNormalFrom);
                }

                auto st = db_ptr->Put(db_batch);
                ASSERT_TRUE(st.ok());
                to_txs_pool.NewBlock(*block_ptr, db_batch);
            }
        }

        ASSERT_EQ(to_txs_pool.network_txs_pools_.size(), kMaxShardingId);
    }
};

TEST_F(TestToTxsPools, All) {
    std::shared_ptr<pools::TxPoolManager> pools_mgr = nullptr;
    ToTxsPools to_txs_pool(db_ptr, "local_id", 3, pools_mgr);
    CreateBlock(to_txs_pool);
    for (uint32_t i = 0; i < kMaxShardingId; ++i) {
        pools::protobuf::ToTxHeights lhs;
        ASSERT_EQ(to_txs_pool.LeaderCreateToHeights(i, lhs), kPoolsSuccess);
        std::string to_hash;
        ASSERT_EQ(to_txs_pool.CreateToTxWithHeights(i, lhs, &to_hash), kPoolsSuccess);
    }
}

}  // namespace test

}  // namespace pools

}  // namespace zjchain
