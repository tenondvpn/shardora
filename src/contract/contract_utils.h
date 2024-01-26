#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/encode.h"

#define CONTRACT_DEBUG(fmt, ...) ZJC_DEBUG("[CONTRACT]" fmt, ## __VA_ARGS__)
#define CONTRACT_INFO(fmt, ...) ZJC_INFO("[CONTRACT]" fmt, ## __VA_ARGS__)
#define CONTRACT_WARN(fmt, ...) ZJC_WARN("[CONTRACT]" fmt, ## __VA_ARGS__)
#define CONTRACT_ERROR(fmt, ...) ZJC_ERROR("[CONRTACT]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace contract {

enum ContractErrorCode {
    kContractSuccess = 0,
    kContractError = 1,
    kContractNotExists = 2,
};

enum CallContractStep {
    kCallStepCallerInited = 0,
    kCallStepContractCalled = 1,
    kCallStepContractFinal = 2,
};

static const std::string kContractEcrecover = common::Encode::HexDecode("0000000000000000000000000000000000000001");
static const std::string kContractSha256 = common::Encode::HexDecode("0000000000000000000000000000000000000002");
static const std::string kContractRipemd160 = common::Encode::HexDecode("0000000000000000000000000000000000000003");
static const std::string kContractIdentity = common::Encode::HexDecode("0000000000000000000000000000000000000004");
static const std::string kContractModexp = common::Encode::HexDecode("0000000000000000000000000000000000000005");
static const std::string kContractAlt_bn128_G1_add = common::Encode::HexDecode("0000000000000000000000000000000000000006");
static const std::string kContractAlt_bn128_G1_mul = common::Encode::HexDecode("0000000000000000000000000000000000000007");
static const std::string kContractAlt_bn128_pairing_product = common::Encode::HexDecode("0000000000000000000000000000000000000008");
static const std::string kContractBlake2_compression = common::Encode::HexDecode("0000000000000000000000000000000000000009");

}  // namespace contact

}  // namespace zjchain
