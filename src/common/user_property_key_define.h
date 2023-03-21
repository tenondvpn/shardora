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

static const std::string kClientFreeBandwidthOver = "bwo";
static const std::string kServerClientOverload = "sol";
static const std::string kCountryInvalid = "cni";
static const std::string kClientIsNotVip = "nvp";

static const std::string kVpnLoginAttrKey = "vpn_login";
static const std::string kUserPayForVpn = "user_pay_for_vpn";
static const std::string kCheckVpnVersion = "zjc_vpn_url";
static const std::string kSetValidVpnClientAccount = "set_valid_vpn_client_account";
static const std::string kIncreaseVpnBandwidth = "kIncreaseVpnBandwidth";
static const std::string kVpnMiningBandwidth = "kVpnMiningBandwidth";
static const std::string kVpnClientLoginAttr = "kVpnClientLoginAttr";
static const std::string kActiveUser = "kActiveUser";
static const std::string kDefaultEnocdeMethod = "aes-128-cfb";
static const std::string kAdRewardVersionStr = "5.1.0";
static const std::string kVpnVipNodeTag = "tvip";

static const std::string kVpnAdminAccount = "e8a1ceb6b807a98a20e3aa10aa2199e47cbbed08c2540bd48aa3e1e72ba6bd99";
static const std::string kVpnLoginManageAccount = "008817d7557fc65cec2c212a6a8bde3e3b8672331c6e206a60dceb60391d71b8";
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

static const uint32_t kVpnShareStakingPrice = 1u;

static const uint64_t kZjcMiniTransportUnit = 100000000llu;
static const uint64_t kZjcMaxAmount = 210llu * 100000000llu * kZjcMiniTransportUnit;
static const uint32_t kTransactionNoVersion = 0u;
static const uint32_t kTransactionVersion = 1u;
static const uint64_t kGenesisFoundationMaxZjc = kZjcMaxAmount / 100llu * 35llu;

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

    uint32_t pool_index = common::Hash::Hash32(acc_addr);
    pool_index %= kImmutablePoolSize;
    return pool_index;
}

}  // namespace  common

}  // namespace zjchain 

