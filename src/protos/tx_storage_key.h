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

static const std::string kElectNodeAttrKeyAllBloomfilter = "__elect_allbloomfilter";
static const std::string kElectNodeAttrKeyWeedoutBloomfilter = "__elect_weedoutbloomfilter";
static const std::string kElectNodeAttrKeyAllPickBloomfilter = "__elect_allpickbloomfilter";
static const std::string kElectNodeAttrKeyPickInBloomfilter = "__elect_pickinbloomfilter";
static const std::string kElectNodeAttrElectBlock = "__elect_block";

static const std::string kAttrTimerBlock = "__tmblock_tmblock";
static const std::string kAttrTimerBlockHeight = "__tmblock_tmblock_height";
static const std::string kAttrTimerBlockTm = "__tmblock_tmblock_tm";
static const std::string kVssRandomAttr = "__vssrandomattr";

static const std::string kStatisticAttr = "__kStatisticAttr";

};  // namespace protos

};  // namespace zjchain