#include <evmc/evmc.hpp>
#include <gtest/gtest.h>

#include "contract/contract_manager.h"
#include "contract/call_parameters.h"
#include "common/encode.h"
#include "security/ecdsa/ecdsa.h"

namespace shardora {

namespace contract {

namespace test {

class TestContractManager : public testing::Test {
public:
    static void SetUpTestSuite() {
        security_ = std::make_shared<security::Ecdsa>();
        contract_manager_.Init(security_);
    }

protected:
    static int CallRaw(
        const std::string& code_addr,
        const std::string& data_raw,
        std::string* output_hex,
        int64_t* gas_left = nullptr) {
        CallParameters params;
        params.gas = 100000000;
        params.apparent_value = 0;
        params.value = 0;
        params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
        params.code_address = code_addr;
        params.to = code_addr;
        params.data = data_raw;
        params.on_op = {};
        evmc_result call_result = {};
        evmc::Result evmc_res{ call_result };
        evmc_result* raw_result = reinterpret_cast<evmc_result*>(&evmc_res);
        raw_result->gas_left = params.gas;
        const int ret = contract_manager_.call(params, 10000000, "", raw_result);
        if (gas_left != nullptr) {
            *gas_left = raw_result->gas_left;
        }
        if (output_hex != nullptr) {
            const std::string out(reinterpret_cast<const char*>(raw_result->output_data), raw_result->output_size);
            *output_hex = common::Encode::HexEncode(out);
        }
        return ret;
    }

    static int Call(
        const std::string& code_addr,
        const std::string& data_hex,
        std::string* output_hex,
        int64_t* gas_left = nullptr) {
        CallParameters params;
        params.gas = 100000000;
        params.apparent_value = 0;
        params.value = 0;
        params.from = common::Encode::HexDecode("b8ce9ab6943e0eced004cde8e3bbed6568b2fa01");
        params.code_address = code_addr;
        params.to = code_addr;
        params.data = common::Encode::HexDecode(data_hex);
        params.on_op = {};
        evmc_result call_result = {};
        evmc::Result evmc_res{ call_result };
        evmc_result* raw_result = reinterpret_cast<evmc_result*>(&evmc_res);
        raw_result->gas_left = params.gas;
        const int ret = contract_manager_.call(params, 10000000, "", raw_result);
        if (gas_left != nullptr) {
            *gas_left = raw_result->gas_left;
        }
        if (output_hex != nullptr) {
            const std::string out(reinterpret_cast<const char*>(raw_result->output_data), raw_result->output_size);
            *output_hex = common::Encode::HexEncode(out);
        }
        return ret;
    }

    static std::shared_ptr<security::Security> security_;
    static ContractManager contract_manager_;
};

std::shared_ptr<security::Security> TestContractManager::security_;
ContractManager TestContractManager::contract_manager_;

TEST_F(TestContractManager, NotExistsContract) {
    std::string output_hex;
    ASSERT_EQ(Call(common::Encode::HexDecode("ffffffffffffffffffffffffffffffffffffffff"), "", &output_hex), kContractNotExists);
}

TEST_F(TestContractManager, ModexpFermatTheorem) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractModexp,
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f",
        &output_hex), kContractSuccess);
    ASSERT_EQ(output_hex, "0000000000000000000000000000000000000000000000000000000000000001");
}

TEST_F(TestContractManager, EcrecoverRoundTrip) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractEcrecover,
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "000000000000000000000000000000000000000000000000000000000000001b"
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "789d1dd423d25f0772d2748d60f7e4b81bb14d086eba8e8e8efb6dcff8a4ae02",
        &output_hex), kContractSuccess);
    security::Ecdsa sec;
    const std::string output = common::Encode::HexDecode(output_hex);
    ASSERT_EQ(sec.UnicastAddress(output), common::Encode::HexDecode("ceaccac640adf55b2028469bd36ba501f28b699d"));
}

TEST_F(TestContractManager, AltBn128G1Add) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractAlt_bn128_G1_add,
        "18b18acfb4c2c30276db5411368e7185b311dd124691610c5d3b74034e093dc9"
        "063c909c4720840cb5134cb9f59fa749755796819658d32efc0d288198f37266"
        "07c2b7f58a84bd6145f00c9c2bc0bb1a187f20ff2c92963a88019e7c6a014eed"
        "06614e20c147e940f2d70da3f74c9a17df361706a4485c742bd6788478fa17d7",
        &output_hex), kContractSuccess);
    ASSERT_EQ(
        output_hex,
        "2243525c5efd4b9c3d3c45ac0ca3fe4dd85e830a4ce6b65fa1eeaee202839703"
        "301d1d33be6da8e509df21cc35964723180eed7532537db9ae5e7d48f195c915");
}

TEST_F(TestContractManager, Blake2CompressionInvalidInput) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractBlake2_compression,
        "00000c48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2"
        "b8c68059b6bbd41fbabd9831f79217e1319cde05b6162630000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000300000000000000000000000000000001",
        &output_hex), kContractError);
}

TEST_F(TestContractManager, AltBn128G1ScalarMul) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractAlt_bn128_G1_mul,
        "2bd3e6d0f3b142924f5ca7b49ce5b9d54c4703d7ae5648e61d02268b1a0a9fb7"
        "21611ce0a6af85915e2f1d70300909ce2e49dfad4a4619c8390cae66cefdb204"
        "00000000000000000000000000000000000000000000000011138ce750fa15c2",
        &output_hex), kContractSuccess);
    ASSERT_EQ(
        output_hex,
        "070a8d6a982153cae4be29d434e8faef8a47b274a053f5a4ee2a6c9c13c31e5c"
        "031b8ce914eba3a9ffb989f9cdd5b0f01943074bf4f0f315690ec3cec6981afc");
}

TEST_F(TestContractManager, AltBn128PairingProduct) {
    std::string output_hex;
    ASSERT_EQ(Call(
        kContractAlt_bn128_pairing_product,
        "1c76476f4def4bb94541d57ebba1193381ffa7aa76ada664dd31c16024c43f59"
        "3034dd2920f673e204fee2811c678745fc819b55d3e9d294e45c9b03a76aef41"
        "209dd15ebff5d46c4bd888e51a93cf99a7329636c63514396b4a452003a35bf7"
        "04bf11ca01483bfa8b34b43561848d28905960114c8ac04049af4b6315a41678"
        "2bb8324af6cfc93537a2ad1a445cfd0ca2a71acd7ac41fadbf933c2a51be344d"
        "120a2a4cf30c1bf9845f20c6fe39e07ea2cce61f0c9bb048165fe5e4de877550"
        "111e129f1cf1097710d41c4ac70fcdfa5ba2023c6ff1cbeac322de49d1b6df7c"
        "2032c61a830e3c17286de9462bf242fca2883585b93870a73853face6a6bf411"
        "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"
        "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"
        "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"
        "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa",
        &output_hex), kContractSuccess);
    ASSERT_EQ(output_hex, "0000000000000000000000000000000000000000000000000000000000000001");
}

TEST_F(TestContractManager, Blake2CompressionValidInput) {
    std::string data;
    data.append("\x00\x00\x00\x0c", 4);  // rounds and gas price
    data.append(64, '\0');                // state vector
    data.append(128, '\0');               // message block
    data.append(8, '\0');                 // t0
    data.append(8, '\0');                 // t1
    data.push_back('\x01');               // final block indicator
    ASSERT_EQ(data.size(), 213u);

    std::string output_hex;
    ASSERT_EQ(CallRaw(
        kContractBlake2_compression,
        data,
        &output_hex), kContractSuccess);
    ASSERT_EQ(output_hex.size(), 128u);
}

TEST_F(TestContractManager, ModexpGasPriceFermatCase) {
    const std::string input = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "03"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    int64_t gas_left = 0;
    std::string output_hex;
    ASSERT_EQ(CallRaw(kContractModexp, input, &output_hex, &gas_left), kContractSuccess);
    ASSERT_EQ(100000000 - gas_left, 13056);
}

TEST_F(TestContractManager, ModexpGasPriceZeroExponent) {
    const std::string input = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000000"  // len(B)
        "0000000000000000000000000000000000000000000000000000000000000003"  // len(E)
        "000000000000000000000000000000000000000000000000000000000000000a"  // len(M)
        "000000"  // E
        "112233445566778899aa");
    int64_t gas_left = 0;
    std::string output_hex;
    ASSERT_EQ(CallRaw(kContractModexp, input, &output_hex, &gas_left), kContractSuccess);
    ASSERT_EQ(100000000 - gas_left, 5);
}

TEST_F(TestContractManager, ModexpGasPriceMonotonicByModLen) {
    const std::string basePrefix = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000003"  // len(B)
        "0000000000000000000000000000000000000000000000000000000000000021");  // len(E)
    const std::string payload = common::Encode::HexDecode(
        "111111"
        "02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "112233445566778899aa");
    const std::string modLenSmall = common::Encode::HexDecode(
        "000000000000000000000000000000000000000000000000000000000000000a");
    const std::string modLenMid = common::Encode::HexDecode(
        "000000000000000000000000000000000000000000000000000000000000004a");
    const std::string modLenLarge = common::Encode::HexDecode(
        "0000000000000000000000000000000000000000000000000000000000000401");

    int64_t gas_left_small = 0;
    int64_t gas_left_mid = 0;
    int64_t gas_left_large = 0;
    std::string output_hex;
    ASSERT_EQ(CallRaw(kContractModexp, basePrefix + modLenSmall + payload, &output_hex, &gas_left_small), kContractSuccess);
    ASSERT_EQ(CallRaw(kContractModexp, basePrefix + modLenMid + payload, &output_hex, &gas_left_mid), kContractSuccess);
    ASSERT_EQ(CallRaw(kContractModexp, basePrefix + modLenLarge + payload, &output_hex, &gas_left_large), kContractSuccess);

    const uint64_t gasSmall = 100000000 - gas_left_small;
    const uint64_t gasMid = 100000000 - gas_left_mid;
    const uint64_t gasLarge = 100000000 - gas_left_large;
    ASSERT_GT(gasSmall, 0u);
    ASSERT_GT(gasMid, gasSmall);
    ASSERT_GT(gasLarge, gasMid);
}

}  // namespace test

}  // namespace contract

}  // namespace shardora
