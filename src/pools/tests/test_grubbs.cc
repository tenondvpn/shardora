#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <unordered_set>
#include <vector>

#define private public
#include "pools/tx_pool_manager.h"
#include "db/db.h"

namespace zjchain {

namespace pools {

namespace test {

static std::shared_ptr<db::Db> db_ptr = nullptr;
class TestGrubbs : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }


private:

};

TEST_F(TestGrubbs, All) {
    std::shared_ptr<security::Security> security = nullptr;
    system("rm -rf ./test_grubbs");
    std::shared_ptr<db::Db> db_ptr = std::make_shared<db::Db>();
    db_ptr->Init("./test_grubbs");
    std::shared_ptr<sync::KeyValueSync> kv_sync = nullptr;
    for (int32_t valid_fac = 70; valid_fac < 100; valid_fac += 10) {
        for (int32_t invalid_fac = 0; invalid_fac < 90; invalid_fac += 10) {
            std::string res;
            pools::TxPoolManager pool_mgr(security, db_ptr, kv_sync);
            std::vector<double> factors(256);
            for (int32_t i = 0; i < 256; ++i) {
                factors[i] = double(90 + rand() % 10) / 100.0;
                if (rand() % 100 < 10) {
                    factors[i] = double(invalid_fac + rand() % 20) / 100.0;
                    res += std::to_string(i) + " ";
                }
            }

            res += "\n";
            std::vector<uint32_t> invalid_pools;
            pool_mgr.CheckLeaderValid(factors, &invalid_pools);
            for (int32_t i = 0; i < invalid_pools.size(); ++i) {
                res += std::to_string(invalid_pools[i]) + " ";
            }

            std::cout << valid_fac << ", " << invalid_fac << ", " << res << std::endl;
        }
    }
    
}

}  // namespace test

}  // namespace pools

}  // namespace zjchain
