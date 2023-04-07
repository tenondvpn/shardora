#pragma once

#include <string>

#include "common/encode.h"

namespace zjchain {

namespace protos {

static const std::string kRootCreateAddressKey = "_kRootCreateAddressKey";
static const std::string kContractBytesStartCode = common::Encode::HexDecode("60806040");
static const std::string kNormalTos = "__normal_tos";
static const std::string kLocalNormalTos = "__local_tos";
static const std::string kConsensusLocalNormalTos = "__consensus_local_tos";
static const std::string kCreateContractCallerSharding = "__new_contract_user_shard";
static const std::string kCreateContractBytesCode = "__kCreateContractBytesCode";

};  // namespace protos

};  // namespace zjchain