#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#include "common/encode.h"
#define private public
#include "zjcvm/execution.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace zjcvm {

namespace test {

class TestExecution : public testing::Test {
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

TEST_F(TestExecution, All) {
    zjcvm::Execution exec;
    std::string contract_address;
    std::string input;
    std::string from;
    std::string to;
    std::string origin_address;
    uint64_t value;
    uint64_t gas_limit;
    uint32_t depth = 0;
    bool is_create = false;
    evmc_result evmc_res = {};
    evmc::Result res{ evmc_res };
    zjcvm::ZjchainHost zjc_host;
    exec.execute(
        contract_address,
        input,
        from,
        to,
        origin_address,
        value,
        gas_limit,
        depth,
        is_create,
        zjc_host,
        &res);
}

}  // namespace test

}  // namespace zjcvm

}  // namespace zjchain
