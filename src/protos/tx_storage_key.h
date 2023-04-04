#pragma once

#include <string>

namespace zjchain {

namespace protos {

static const std::string kRootCreateAddressKey = "_kRootCreateAddressKey";
static const std::string kContractAddress = "__caddress";
static const std::string kContractBytesCode = "__cbytescode";
static const std::string kContractSourceCode = "__csourcecode";
static const std::string kContractBytesStartCode = "60806040";
static const std::string kContractName = "__ctname";
static const std::string kContractDesc = "__ctdesc";
static const std::string kContractCreatedBytesCode = "__cbytescodecreated";
static const std::string kContractInputCode = "__cinput";
static const std::string kContractCallerbalance = "__ccontractcallerbalance";
static const std::string kContractCallerChangeAmount = "__ccontractcallerchangeamount";
static const std::string kContractCallerGasUsed = "__ccontractcallergasused";
static const std::string kStatisticAttr = "__statisticattr";
static const std::string kNormalTos = "__normal_tos";
static const std::string kLocalNormalTos = "__local_tos";
static const std::string kConsensusLocalNormalTos = "__consensus_local_tos";
static const std::string kCreateContractCallerSharding = "__new_contract_user_shard";

};  // namespace protos

};  // namespace zjchain