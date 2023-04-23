#pragma once

#include <string>

#include "common/encode.h"
#include "common/string_utils.h"
#include "common/limit_heap.h"

namespace zjchain {

namespace  common {

enum ConsensusType {
    kConsensusInvalidType = 0,
    kConsensusCreateGenesisConsensusNetwork = 1,
    kConsensusCreateGenesisAcount = 2,
    kConsensusCreateAcount = 3,
    kConsensusCreateContract = 4,
    kConsensusTransaction = 5,
    kConsensusCallContract = 6,
    kConsensusRootElectShard = 7,  // shard consensus network election
    kConsensusRootTimeBlock = 8,  // create time block
    kConsensusFinalStatistic = 9,  // create shard time block's final statistic block
};

enum ClientStatus {
    kValid = 0,
    kBandwidthFreeToUseExceeded = 1,
    kPayForExpired = 2,
    kServerOverLoaded = 3,
    kLoginByOtherTerminal = 4,
};


enum ClientPlatform {
    kUnknown = 0,
    kIos = 1,
    kAndroid = 2,
    kMac = 3,
    kWindows = 4,
};

enum VipLevel {
    kNotVip = 0,
    kVipLevel1 = 1,
    kVipLevel2 = 2,
    kVipLevel3 = 3,
    kVipLevel4 = 4,
    kVipLevel5 = 5,
};

static const std::string kCreateGenesisNetwrokAccount = common::Encode::HexDecode(
    "b5be6f0090e4f5d40458258ed9adf843324c0327145c48b55091f33673d2d5a4");
static const std::string kStatisticFromAddressMidllefix = "00000000000000000000000000000000";
static const std::string kStatisticFromAddressMidllefixDecode = common::Encode::HexDecode("00000000000000000000000000000000");
static const std::string kRootChainSingleBlockTxAddress = common::Encode::HexDecode(
        "1000000000000000000000000000000000000001");
static const std::string kRootChainTimeBlockTxAddress = common::Encode::HexDecode(
        "1000000000000000000000000000000000000002");
static const std::string kRootChainElectionBlockTxAddress = common::Encode::HexDecode(
        "1000000000000000000000000000000000000003");
static const std::string kNormalToAddress = common::Encode::HexDecode(
    "1000000000000000000000000000000000000004");
static const std::string kNormalLocalToAddress = common::Encode::HexDecode(
    "1000000000000000000000000000000000000005");
static const std::string kShardStatisticAddress = common::Encode::HexDecode(
    "1000000000000000000000000000000000000006");

static const uint32_t kVpnShareStakingPrice = 1u;

static const uint64_t kZjcMiniTransportUnit = 100000000llu;
static const uint64_t kZjcMaxAmount = 210llu * 100000000llu * kZjcMiniTransportUnit;
static const uint32_t kTransactionNoVersion = 0u;
static const uint32_t kTransactionVersion = 1u;
static const uint64_t kGenesisFoundationMaxZjc = kZjcMaxAmount / 100llu * 14llu;
static const uint64_t kGenesisShardingNodesMaxZjc = kZjcMaxAmount / 100llu * 1llu;

static const uint64_t kVpnVipMinPayfor = 66llu * kZjcMiniTransportUnit;
static const uint64_t kVpnVipMaxPayfor = 2000u * kZjcMiniTransportUnit;

static const uint32_t kDefaultBroadcastIgnBloomfilterHop = 1u;
static const uint32_t kDefaultBroadcastStopTimes = 2u;
static const uint32_t kDefaultBroadcastHopLimit = 5u;
static const uint32_t kDefaultBroadcastHopToLayer = 2u;
static const uint32_t kDefaultBroadcastNeighborCount = 7u;
static const uint64_t kBuildinTransactionGasPrice = 999999999lu;

static inline bool IsBaseAddress(const std::string& address) {
    return (address.substr(2, kStatisticFromAddressMidllefixDecode.size()) ==
        kStatisticFromAddressMidllefixDecode);
}

inline static uint32_t GetBasePoolIndex(const std::string& acc_addr) {
    if (acc_addr == common::kRootChainSingleBlockTxAddress ||
            acc_addr == common::kRootChainTimeBlockTxAddress ||
            acc_addr == common::kRootChainElectionBlockTxAddress) {
        return kRootChainPoolIndex;
    }

    return GetAddressPoolIndex(acc_addr);
}

}  // namespace  common

}  // namespace zjchain 

