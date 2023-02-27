#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#define private public
#include "contract/contract_manager.h"
#include "contract/call_parameters.h"
#include "common/encode.h"
#include "contract/contract_modexp.h"
#include "security/ecdsa/ecdsa.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace contract {

namespace test {

static std::shared_ptr<security::Security> security = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;

class TestContractManager : public testing::Test {
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
        system("rm -rf ./core.* ./test_db");
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./test_db");
        std::string config_path_ = "./";
        std::string conf_path = config_path_ + "/zjc.conf";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/zjc.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
        security = std::make_shared<security::Ecdsa>();
        contract::ContractManager::Instance()->Init(security);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestContractManager, modexpFermatTheorem) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000001");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpZeroBase) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpExtraByteIgnored) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "ffff"
        "8000000000000000000000000000000000000000000000000000000000000000"
        "07");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("3b01b01ac41f2d6e917c6d6a221ce793802469026d9ab7578fa2e79e4da6aaab");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpRightPadding) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "ffff"
        "8000000000000000000000000000000000000000000000000000000000000000"
        "07");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("3b01b01ac41f2d6e917c6d6a221ce793802469026d9ab7578fa2e79e4da6aaab");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpMissingValues) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpEmptyValue) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "8000000000000000000000000000000000000000000000000000000000000000");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000001");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpZeroPowerZero) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "00"
        "00"
        "80");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000001");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpZeroPowerZeroModZero) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "00"
        "00"
        "00");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexpModLengthZero) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "01"
        "01");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res;
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

// call gas price test
TEST_F(TestContractManager, modexpCostFermatTheorem) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 13056);
}

TEST_F(TestContractManager, modexpCostTooLarge) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000000"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 922337203685477120);
}

TEST_F(TestContractManager, modexpCostEmptyExponent) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000008" // length of B
        "0000000000000000000000000000000000000000000000000000000000000000" // length of E
        "0000000000000000000000000000000000000000000000000000000000000010" // length of M
        "998877665544332211" // B
        "" // E
        "998877665544332211998877665544332211" // M
        "9978");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 12);
}

TEST_F(TestContractManager, modexpCostZeroExponent) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000000" // length of B
        "0000000000000000000000000000000000000000000000000000000000000003" // length of E
        "000000000000000000000000000000000000000000000000000000000000000a" // length of M
        "" // B
        "000000" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 5);
}

TEST_F(TestContractManager, modexpCostApproximated) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003" // length of B
        "0000000000000000000000000000000000000000000000000000000000000021" // length of E
        "000000000000000000000000000000000000000000000000000000000000000a" // length of M
        "111111" // B
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 1315);
}

TEST_F(TestContractManager, modexpCostApproximatedPartialByte) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003" // length of B
        "0000000000000000000000000000000000000000000000000000000000000021" // length of E
        "000000000000000000000000000000000000000000000000000000000000000a" // length of M
        "111111" // B
        "02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 1285);
}

TEST_F(TestContractManager, modexpCostApproximatedGhost) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003" // length of B
        "0000000000000000000000000000000000000000000000000000000000000021" // length of E
        "000000000000000000000000000000000000000000000000000000000000000a" // length of M
        "111111" // B
        "000000000000000000000000000000000000000000000000000000000000000000" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, 40);
}

TEST_F(TestContractManager, modexpCostMidRange) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003" // length of B
        "0000000000000000000000000000000000000000000000000000000000000021" // length of E
        "000000000000000000000000000000000000000000000000000000000000004a" // length of M = 74
        "111111" // B
        "000000000000000000000000000000000000000000000000000000000000000000" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, ((74 * 74 / 4 + 96 * 74 - 3072) * 8) / 20);
}

TEST_F(TestContractManager, modexpCostHighRange) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003" // length of B
        "0000000000000000000000000000000000000000000000000000000000000021" // length of E
        "0000000000000000000000000000000000000000000000000000000000000401" // length of M = 1025
        "111111" // B
        "000000000000000000000000000000000000000000000000000000000000000000" // E
        "112233445566778899aa");
    contract::Modexp modexp("");
    uint64_t gas_used = modexp.GetGasPrice(params.data);
    ASSERT_EQ(gas_used, ((1025 * 1025 / 16 + 480 * 1025 - 199680) * 8) / 20);
}

TEST_F(TestContractManager, ecrecover) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractEcrecover;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e000000000000000000000000000000000000000000000000000000000000001b38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e789d1dd423d25f0772d2748d60f7e4b81bb14d086eba8e8e8efb6dcff8a4ae02");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("ceaccac640adf55b2028469bd36ba501f28b699d");
    auto sec = security::Ecdsa();
    std::string output = sec.UnicastAddress(std::string((char*)raw_result->output_data, raw_result->output_size));
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, modexp) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractModexp;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000001");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, bn256Add) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractAlt_bn128_G1_add;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "18b18acfb4c2c30276db5411368e7185b311dd124691610c5d3b74034e093dc9063c909c4720840cb5134cb9f59fa749755796819658d32efc0d288198f3726607c2b7f58a84bd6145f00c9c2bc0bb1a187f20ff2c92963a88019e7c6a014eed06614e20c147e940f2d70da3f74c9a17df361706a4485c742bd6788478fa17d7");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("2243525c5efd4b9c3d3c45ac0ca3fe4dd85e830a4ce6b65fa1eeaee202839703301d1d33be6da8e509df21cc35964723180eed7532537db9ae5e7d48f195c915");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, bn256ScalarMul) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractAlt_bn128_G1_mul;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "2bd3e6d0f3b142924f5ca7b49ce5b9d54c4703d7ae5648e61d02268b1a0a9fb721611ce0a6af85915e2f1d70300909ce2e49dfad4a4619c8390cae66cefdb20400000000000000000000000000000000000000000000000011138ce750fa15c2");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("070a8d6a982153cae4be29d434e8faef8a47b274a053f5a4ee2a6c9c13c31e5c031b8ce914eba3a9ffb989f9cdd5b0f01943074bf4f0f315690ec3cec6981afc");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, bn256Pairing) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractAlt_bn128_pairing_product;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "1c76476f4def4bb94541d57ebba1193381ffa7aa76ada664dd31c16024c43f593034dd2920f673e204fee2811c678745fc819b55d3e9d294e45c9b03a76aef41209dd15ebff5d46c4bd888e51a93cf99a7329636c63514396b4a452003a35bf704bf11ca01483bfa8b34b43561848d28905960114c8ac04049af4b6315a416782bb8324af6cfc93537a2ad1a445cfd0ca2a71acd7ac41fadbf933c2a51be344d120a2a4cf30c1bf9845f20c6fe39e07ea2cce61f0c9bb048165fe5e4de877550111e129f1cf1097710d41c4ac70fcdfa5ba2023c6ff1cbeac322de49d1b6df7c2032c61a830e3c17286de9462bf242fca2883585b93870a73853face6a6bf411198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c21800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000001");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, blake2compression) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractBlake2_compression;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "0000000048c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3"
        "e2b8c68059b6bbd41fbabd9831f79217e1319cde05b61626300000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000300000000000000000000000000000001");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractSuccess);
    std::string res = common::Encode::HexDecode(
        "08c9bcf367e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d282e6ad7f520e511f6c3e2b8c"
        "68059b9442be0454267ce079217e1319cde05b");
    std::string output = std::string((char*)raw_result->output_data, raw_result->output_size);
    ASSERT_EQ(res, output);
}

TEST_F(TestContractManager, blake2compressionFail) {
    contract::CallParameters params;
    params.gas = 100000000;
    params.apparent_value = 0;
    params.value = params.apparent_value;
    params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
    params.code_address = contract::kContractBlake2_compression;
    params.to = params.code_address;
    params.data = common::Encode::HexDecode(
        "00000c48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2"
        "b8c68059b6bbd41fbabd9831f79217e1319cde05b6162630000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000300000000000000000000000000000001");
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = params.gas;
    ASSERT_EQ(contract::ContractManager::Instance()->call(
        params,
        10000000,
        "",
        raw_result), contract::kContractError);
}

}  // namespace test

}  // namespace bignum

}  // namespace zjchain
