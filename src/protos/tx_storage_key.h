#pragma once

#include <string>

namespace zjchain {

namespace protos {

static const std::string kRootCreateAddressKey = "_kRootCreateAddressKey";
static const std::string kContractBytesStartCode = "60806040";
static const std::string kNormalTos = "__normal_tos";
static const std::string kLocalNormalTos = "__local_tos";
static const std::string kConsensusLocalNormalTos = "__consensus_local_tos";
static const std::string kCreateContractCallerSharding = "__new_contract_user_shard";
static const std::string kCreateContractUserCalled = "__kCreateContractUserCalled";

};  // namespace protos

};  // namespace zjchain