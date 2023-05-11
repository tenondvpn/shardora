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
    system("rm -rf ./test_height_tree_db");
    std::shared_ptr<db::Db> db_ptr = std::make_shared<db::Db>();
    db_ptr->Init("./test_grubbs");
    std::shared_ptr<sync::KeyValueSync> kv_sync = nullptr;
    pools::TxPoolManager pool_mgr(security, db_ptr, kv_sync);
}

}  // namespace test

}  // namespace pools

}  // namespace zjchain
