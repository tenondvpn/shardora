#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#define private public
#include "common/utils.h"

namespace seth {

namespace common {

namespace test {

class TestUtils : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// --- GetSignerCount Tests ---

TEST_F(TestUtils, GetSignerCountBasic) {
    ASSERT_EQ(GetSignerCount(3), 2u);
    ASSERT_EQ(GetSignerCount(4), 3u);
    ASSERT_EQ(GetSignerCount(6), 4u);
    ASSERT_EQ(GetSignerCount(9), 6u);
    ASSERT_EQ(GetSignerCount(10), 7u);
    ASSERT_EQ(GetSignerCount(100), 67u);
    ASSERT_EQ(GetSignerCount(1024), 683u);
}

TEST_F(TestUtils, GetSignerCountBFTProperty) {
    for (uint32_t n = 3; n <= 100; ++n) {
        uint32_t t = GetSignerCount(n);
        ASSERT_GE(t, n * 2 / 3);
        ASSERT_LE(t, n);
    }
}

TEST_F(TestUtils, GetSignerCountDivisibleBy3) {
    // When n is divisible by 3, result = 2n/3
    ASSERT_EQ(GetSignerCount(3), 2u);
    ASSERT_EQ(GetSignerCount(6), 4u);
    ASSERT_EQ(GetSignerCount(9), 6u);
    ASSERT_EQ(GetSignerCount(12), 8u);
}

TEST_F(TestUtils, GetSignerCountNotDivisibleBy3) {
    // When n % 3 != 0, result = 2n/3 + 1
    ASSERT_EQ(GetSignerCount(4), 3u);   // 8/3=2, +1=3
    ASSERT_EQ(GetSignerCount(5), 4u);   // 10/3=3, +1=4
    ASSERT_EQ(GetSignerCount(7), 5u);   // 14/3=4, +1=5
    ASSERT_EQ(GetSignerCount(8), 6u);   // 16/3=5, +1=6
}

// --- ShiftUint32 Tests ---

TEST_F(TestUtils, ShiftUint32ByteSwap) {
    ASSERT_EQ(ShiftUint32(0x01020304), 0x04030201u);
    ASSERT_EQ(ShiftUint32(0x00000000), 0x00000000u);
    ASSERT_EQ(ShiftUint32(0xFFFFFFFF), 0xFFFFFFFFu);
    ASSERT_EQ(ShiftUint32(0xFF000000), 0x000000FFu);
    ASSERT_EQ(ShiftUint32(0x0000FF00), 0x00FF0000u);
}

TEST_F(TestUtils, ShiftUint32Reversible) {
    uint32_t original = 0xDEADBEEF;
    ASSERT_EQ(ShiftUint32(ShiftUint32(original)), original);
}

// --- ShiftUint64 Tests ---

TEST_F(TestUtils, ShiftUint64ByteSwap) {
    ASSERT_EQ(ShiftUint64(0x0102030405060708ULL), 0x0807060504030201ULL);
    ASSERT_EQ(ShiftUint64(0x0000000000000000ULL), 0x0000000000000000ULL);
    ASSERT_EQ(ShiftUint64(0xFFFFFFFFFFFFFFFFULL), 0xFFFFFFFFFFFFFFFFULL);
}

TEST_F(TestUtils, ShiftUint64Reversible) {
    uint64_t original = 0xDEADBEEFCAFEBABEULL;
    ASSERT_EQ(ShiftUint64(ShiftUint64(original)), original);
}

// --- IpToUint32 and Uint32ToIp Tests ---

TEST_F(TestUtils, IpToUint32Basic) {
    uint32_t ip = IpToUint32("192.168.1.1");
    ASSERT_NE(ip, 0u);
}

TEST_F(TestUtils, IpRoundTrip) {
    std::string original = "10.0.0.1";
    uint32_t ip_int = IpToUint32(original.c_str());
    std::string result = Uint32ToIp(ip_int);
    ASSERT_EQ(result, original);
}

TEST_F(TestUtils, IpRoundTripVariousAddresses) {
    std::vector<std::string> ips = {
        "127.0.0.1",
        "192.168.0.1",
        "10.0.0.1",
        "172.16.0.1",
        "255.255.255.255",
        "0.0.0.0",
        "1.2.3.4"
    };

    for (const auto& ip : ips) {
        uint32_t ip_int = IpToUint32(ip.c_str());
        std::string result = Uint32ToIp(ip_int);
        ASSERT_EQ(result, ip) << "Failed for IP: " << ip;
    }
}

// --- IsVlanIp Tests (skipped by default; behavior varies with TEST_LOCAL_NETWORK / env) ---

TEST_F(TestUtils, DISABLED_IsVlanIpPrivateRanges) {
    ASSERT_TRUE(IsVlanIp("10.0.0.1"));
    ASSERT_TRUE(IsVlanIp("10.255.255.255"));
    ASSERT_TRUE(IsVlanIp("172.16.0.1"));
    ASSERT_TRUE(IsVlanIp("172.31.255.255"));
    ASSERT_TRUE(IsVlanIp("192.168.0.1"));
    ASSERT_TRUE(IsVlanIp("192.168.255.255"));
}

TEST_F(TestUtils, DISABLED_IsVlanIpPublicAddresses) {
    ASSERT_FALSE(IsVlanIp("8.8.8.8"));
    ASSERT_FALSE(IsVlanIp("1.1.1.1"));
    ASSERT_FALSE(IsVlanIp("203.0.113.1"));
}

TEST_F(TestUtils, DISABLED_IsVlanIpBoundaryValues) {
    // 172.16.0.0 is private
    ASSERT_TRUE(IsVlanIp("172.16.0.0"));
    // 172.32.0.0 is NOT private (just outside range)
    ASSERT_FALSE(IsVlanIp("172.32.0.0"));
    // 10.0.0.0 is private
    ASSERT_TRUE(IsVlanIp("10.0.0.0"));
}

TEST_F(TestUtils, DISABLED_IsVlanIpZeroFirstOctetPairTreatedAsPrivate) {
    ASSERT_TRUE(IsVlanIp("0.0.0.1"));
    ASSERT_TRUE(IsVlanIp("0.0.255.255"));
}

TEST_F(TestUtils, DISABLED_IsVlanIp172ClassBBelowPrivateRange) {
    ASSERT_FALSE(IsVlanIp("172.15.0.1"));
    ASSERT_FALSE(IsVlanIp("172.15.255.255"));
}

// --- GetAddressPoolIndex Tests ---

TEST_F(TestUtils, GetAddressPoolIndexRange) {
    std::string addr(20, '\0');
    for (int i = 1; i <= 100; ++i) {
        addr[0] = static_cast<char>(i);
        addr[1] = static_cast<char>(i * 7);
        uint32_t pool_idx = GetAddressPoolIndex(addr);
        ASSERT_LT(pool_idx, kImmutablePoolSize);
    }
}

TEST_F(TestUtils, GetAddressPoolIndexDeterministic) {
    std::string addr(20, 'A');
    uint32_t idx1 = GetAddressPoolIndex(addr);
    uint32_t idx2 = GetAddressPoolIndex(addr);
    ASSERT_EQ(idx1, idx2);
}

TEST_F(TestUtils, GetAddressPoolIndexRootPrefix) {
    // Address with root pool prefix should return kGlobalPoolIndex
    std::string root_addr = kRootPoolsAddressPrefix;
    root_addr.resize(20, '\0');
    uint32_t idx = GetAddressPoolIndex(root_addr);
    ASSERT_EQ(idx, kGlobalPoolIndex);
}

// --- MessageType ---

TEST_F(TestUtils, MessageTypeMaxMarkerFollowsLastConcreteType) {
    EXPECT_EQ(static_cast<uint32_t>(kMaxMessageTypeCount),
              static_cast<uint32_t>(kPacemakerTimerMessage) + 1u);
}

TEST_F(TestUtils, ConsensusTypeEnumRootShardAndTimeBlock) {
    EXPECT_EQ(kConsensusRootElectShard, 7);
    EXPECT_EQ(kConsensusRootTimeBlock, 8);
    EXPECT_LT(kConsensusInvalidType, kConsensusTransaction);
}

TEST_F(TestUtils, ClientStatusEnumOrdered) {
    EXPECT_LT(static_cast<int>(kValid), static_cast<int>(kLoginByOtherTerminal));
}

TEST_F(TestUtils, ClientPlatformEnumOrdered) {
    EXPECT_LT(static_cast<int>(kUnknown), static_cast<int>(kWindows));
}

TEST_F(TestUtils, VipLevelEnumZeroThroughFive) {
    EXPECT_EQ(static_cast<int>(kNotVip), 0);
    EXPECT_EQ(static_cast<int>(kVipLevel5), 5);
}

TEST_F(TestUtils, ValidationStatusCodesDistinct) {
    EXPECT_NE(static_cast<int>(ValidationStatus::SUCCESS),
              static_cast<int>(ValidationStatus::EMPTY_BYTECODE));
    EXPECT_NE(static_cast<int>(ValidationStatus::EMPTY_BYTECODE),
              static_cast<int>(ValidationStatus::INCOMPLETE_PUSH));
}

// --- GetNodeConnectInt Tests ---

TEST_F(TestUtils, GetNodeConnectIntUnique) {
    auto key1 = GetNodeConnectInt("192.168.1.1", 8001);
    auto key2 = GetNodeConnectInt("192.168.1.1", 8002);
    auto key3 = GetNodeConnectInt("192.168.1.2", 8001);

    ASSERT_NE(key1, key2);
    ASSERT_NE(key1, key3);
    ASSERT_NE(key2, key3);
}

TEST_F(TestUtils, GetNodeConnectIntDeterministic) {
    auto key1 = GetNodeConnectInt("10.0.0.1", 9001);
    auto key2 = GetNodeConnectInt("10.0.0.1", 9001);
    ASSERT_EQ(key1, key2);
}

TEST_F(TestUtils, CreateAndFixedGidBehavior) {
    const std::string pubkey = "pubkey_abc";
    auto gid1 = CreateGID(pubkey);
    auto gid2 = CreateGID(pubkey);
    EXPECT_FALSE(gid1.empty());
    EXPECT_FALSE(gid2.empty());
    EXPECT_NE(gid1, gid2);  // includes random suffix

    auto fixed1 = FixedCreateGID(pubkey);
    auto fixed2 = FixedCreateGID(pubkey);
    EXPECT_EQ(fixed1, fixed2);
}

TEST_F(TestUtils, RandomCountryAndMemberIndexRange) {
    for (int i = 0; i < 64; ++i) {
        auto c = RandomCountry();
        EXPECT_LE(c, static_cast<uint8_t>(239));
    }

    std::string addr(kUnicastAddressLength, 'A');
    auto idx = GetAddressMemberIndex(addr);
    EXPECT_LT(idx, kElectNodeMinMemberIndex);
}

TEST_F(TestUtils, PoolAddressHelpersDeterministicAndLength) {
    auto p1 = GetPoolAddress(0);
    auto p2 = GetPoolAddress(0);
    auto p3 = GetPoolAddress(1);
    EXPECT_EQ(p1, p2);
    EXPECT_NE(p1, p3);
    EXPECT_LE(p1.size(), static_cast<size_t>(kUnicastAddressLength));

    auto root1 = GetRootStakePoolAddress();
    auto root2 = GetRootStakePoolAddress();
    EXPECT_EQ(root1, root2);
    EXPECT_LE(root1.size(), static_cast<size_t>(kUnicastAddressLength));
}

TEST_F(TestUtils, IpConversionInvalidInputs) {
    EXPECT_EQ(IpToUint32("not_an_ip"), 0u);
    EXPECT_EQ(Uint32ToIp(0u), "0.0.0.0");
}

TEST_F(TestUtils, DISABLED_IsVlanIpMalformedStrings) {
    EXPECT_FALSE(IsVlanIp("1.2.3"));
    EXPECT_FALSE(IsVlanIp("a.b.c.d"));
}

// --- Constants Sanity Tests ---

TEST_F(TestUtils, ConstantsSanity) {
    ASSERT_EQ(kImmutablePoolSize, 32u);
    ASSERT_EQ(kGlobalPoolIndex, 32u);
    ASSERT_EQ(kInvalidPoolIndex, 33u);
    ASSERT_EQ(kSethMiniTransportUnit, 100000000llu);
    ASSERT_EQ(kUnicastAddressLength, 20u);
    ASSERT_GT(kMaxTxCount, 0u);
    ASSERT_GT(kEachShardMinNodeCount, 0u);
    ASSERT_LE(kEachShardMinNodeCount, kEachShardMaxNodeCount);
}

TEST_F(TestUtils, ShardGenerationTableConsistency) {
    for (uint32_t i = 0; i < kShardGenerationCount; ++i) {
        const auto& gen = kShardGenerations[i];
        ASSERT_EQ(gen.generation, i);
        ASSERT_LE(gen.start_shard_id, gen.end_shard_id);
        ASSERT_EQ(gen.shard_count, gen.end_shard_id - gen.start_shard_id + 1);
        ASSERT_GT(gen.weight, 0.0);
        ASSERT_LE(gen.weight, 1.0);

        if (i + 1 < kShardGenerationCount) {
            ASSERT_EQ(gen.end_shard_id + 1, kShardGenerations[i + 1].start_shard_id);
        }
    }
}

TEST_F(TestUtils, ShardGenerationWeightDecay) {
    for (uint32_t i = 1; i < kShardGenerationCount; ++i) {
        double expected = kShardGenerations[i - 1].weight * kGenerationWeightDecay;
        ASSERT_NEAR(kShardGenerations[i].weight, expected, 0.001);
    }
}

// --- DhtKey Tests ---

TEST_F(TestUtils, DhtKeyInitialization) {
    DhtKey key;
    ASSERT_EQ(key.construct.net_id, 0u);
    for (int i = 0; i < 28; ++i) {
        ASSERT_EQ(key.construct.hash[i], 0);
    }
}

TEST_F(TestUtils, DhtKeyNetId) {
    DhtKey key;
    key.construct.net_id = 12345;
    ASSERT_EQ(key.construct.net_id, 12345u);
    uint32_t* raw_net_id = reinterpret_cast<uint32_t*>(key.dht_key);
    ASSERT_EQ(*raw_net_id, 12345u);
}

// --- GetTxDbKey Tests ---

TEST_F(TestUtils, GetTxDbKeyFrom) {
    auto key = GetTxDbKey(true, "test_gid");
    ASSERT_EQ(key, "TX_from_test_gid");
}

TEST_F(TestUtils, GetTxDbKeyTo) {
    auto key = GetTxDbKey(false, "test_gid");
    ASSERT_EQ(key, "TX_to_test_gid");
}

TEST_F(TestUtils, GetTxDbKeyEmptyGid) {
    auto key_from = GetTxDbKey(true, "");
    ASSERT_EQ(key_from, "TX_from_");
    auto key_to = GetTxDbKey(false, "");
    ASSERT_EQ(key_to, "TX_to_");
}

// --- TimestampToDatetime Tests ---

TEST_F(TestUtils, TimestampToDatetimeFormat) {
    auto result = TimestampToDatetime(1577880000);
    ASSERT_NE(result.find("2020"), std::string::npos);
}

TEST_F(TestUtils, MicTimestampToDatetime) {
    int64_t ts_ms = 1577836800000LL;  // 2020-01-01 in ms
    auto result = MicTimestampToDatetime(ts_ms);
    ASSERT_FALSE(result.empty());
    ASSERT_NE(result.find("2020"), std::string::npos);
}

TEST_F(TestUtils, MicTimestampToLiteDatetime) {
    int64_t ts_ms = 1577836800000LL;
    auto result = MicTimestampToLiteDatetime(ts_ms);
    ASSERT_FALSE(result.empty());
    // Should be in MM/DD HH:MM format
    ASSERT_EQ(result.size(), 11u);  // "01/01 08:00"
}

TEST_F(TestUtils, MicTimestampToDateReturnsCompactYmd) {
    auto ymd = MicTimestampToDate(1577836800000LL);
    EXPECT_GE(ymd, 20200101u);
    EXPECT_LE(ymd, 20991231u);
}

// --- Economic Model Constants Tests ---

TEST_F(TestUtils, EconomicModelConstants) {
    ASSERT_GT(kInitialTotalReward, 0u);
    ASSERT_GT(kHalvingPeriodEpochs, 0u);
    ASSERT_GE(kTxBonusMultiplier, 0.0);
    ASSERT_LE(kTxBonusMultiplier, 1.0);
    ASSERT_GE(kBurnRatio, 0.0);
    ASSERT_LE(kBurnRatio, 1.0);
    ASSERT_GT(kMinBlockReward, 0u);
    ASSERT_LE(kMinBlockReward, kInitialTotalReward);
}

// --- GetNthElement Tests ---

TEST_F(TestUtils, GetNthElement) {
    std::vector<uint64_t> v = {5, 3, 8, 1, 9, 2, 7, 4, 6};
    // 50th percentile (median)
    uint64_t median = GetNthElement(v, 0.5f);
    ASSERT_GE(median, 1u);
    ASSERT_LE(median, 9u);
}

// --- Retry Tests ---

TEST_F(TestUtils, RetrySuccess) {
    int count = 0;
    bool result = Retry([&count]() {
        ++count;
        return count >= 3;
    }, 5, std::chrono::milliseconds(1));
    ASSERT_TRUE(result);
    ASSERT_EQ(count, 3);
}

TEST_F(TestUtils, RetryFailure) {
    int count = 0;
    bool result = Retry([&count]() {
        ++count;
        return false;
    }, 3, std::chrono::milliseconds(1));
    ASSERT_FALSE(result);
    ASSERT_EQ(count, 3);
}

// --- IsContractBytescodeValid Tests ---

TEST_F(TestUtils, IsContractBytescodeValidEmpty) {
    auto status = IsContractBytescodeValid("");
    ASSERT_EQ(status, ValidationStatus::EMPTY_BYTECODE);
}

TEST_F(TestUtils, IsContractBytescodeValidSimple) {
    // PUSH1 (0x60) followed by 1 data byte (0x60) — valid
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x60));  // PUSH1
    bytecode.push_back(static_cast<char>(0x60));  // data byte
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidIncompletePush) {
    // PUSH1 (0x60) without data byte — incomplete
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x60));  // PUSH1 needs 1 more byte
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::INCOMPLETE_PUSH);
}

TEST_F(TestUtils, IsContractBytescodeValidNoPush) {
    // STOP (0x00) — no PUSH instruction
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x00));  // STOP
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidPush32) {
    // PUSH32 (0x7f) followed by 32 data bytes — valid
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x7f));  // PUSH32
    for (int i = 0; i < 32; ++i) {
        bytecode.push_back(static_cast<char>(i));
    }
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidPush32Incomplete) {
    // PUSH32 (0x7f) followed by only 16 data bytes — incomplete
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x7f));  // PUSH32
    for (int i = 0; i < 16; ++i) {
        bytecode.push_back(static_cast<char>(i));
    }
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::INCOMPLETE_PUSH);
}

TEST_F(TestUtils, IsContractBytescodeValidMetadataTermination) {
    // Bytecode with Solidity metadata header (0xa2 0x64) — should stop early
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x00));  // STOP
    bytecode.push_back(static_cast<char>(0xa2));  // metadata start
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.push_back(static_cast<char>(0x69));
    bytecode.push_back(static_cast<char>(0x70));
    bytecode.push_back(static_cast<char>(0x66));
    bytecode.push_back(static_cast<char>(0x73));
    bytecode.push_back(static_cast<char>(0x60));  // PUSH1 after metadata (should be ignored)
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidSolidityMetadataAfterInvalid) {
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x00));  // STOP
    bytecode.push_back(static_cast<char>(0xfe));  // INVALID before metadata
    bytecode.push_back(static_cast<char>(0xa2));  // CBOR map with 2 entries
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.append("ipfs");
    bytecode.push_back(static_cast<char>(0x58));
    bytecode.push_back(static_cast<char>(0x22));
    bytecode.append(34, static_cast<char>(0x11));
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.append("solc");
    bytecode.push_back(static_cast<char>(0x43));
    bytecode.push_back(static_cast<char>(0x00));
    bytecode.push_back(static_cast<char>(0x08));
    bytecode.push_back(static_cast<char>(0x1e));
    bytecode.push_back(static_cast<char>(0x00));
    bytecode.push_back(static_cast<char>(0x33));
    bytecode.push_back(static_cast<char>(0x73));  // Would be incomplete if scanned.
    bytecode.push_back(static_cast<char>(0x6f));
    bytecode.push_back(static_cast<char>(0x6c));
    bytecode.push_back(static_cast<char>(0x63));

    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidSolidityMetadataInsidePushData) {
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x00));  // STOP
    bytecode.push_back(static_cast<char>(0x7f));  // Metadata starts in this data.
    bytecode.append(8, static_cast<char>(0x11));
    bytecode.push_back(static_cast<char>(0xa2));  // CBOR map with 2 entries
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.append("ipfs");
    bytecode.push_back(static_cast<char>(0x58));
    bytecode.push_back(static_cast<char>(0x22));
    bytecode.append(34, static_cast<char>(0x22));
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.append("solc");
    bytecode.push_back(static_cast<char>(0x43));
    bytecode.push_back(static_cast<char>(0x00));
    bytecode.push_back(static_cast<char>(0x08));
    bytecode.push_back(static_cast<char>(0x1e));
    bytecode.push_back(static_cast<char>(0x00));
    bytecode.push_back(static_cast<char>(0x33));

    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidPush2) {
    // PUSH2 (0x61) + 2 immediate bytes + STOP
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x61));
    bytecode.push_back(static_cast<char>(0xab));
    bytecode.push_back(static_cast<char>(0xcd));
    bytecode.push_back(static_cast<char>(0x00));
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidPush2Incomplete) {
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x61));
    bytecode.push_back(static_cast<char>(0x01));
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::INCOMPLETE_PUSH);
}

TEST_F(TestUtils, IsContractBytescodeValidPush5) {
    // PUSH5 (0x64) + 5 immediate bytes + STOP
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x64));
    for (int i = 0; i < 5; ++i) {
        bytecode.push_back(static_cast<char>(i + 1));
    }
    bytecode.push_back(static_cast<char>(0x00));
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::SUCCESS);
}

TEST_F(TestUtils, IsContractBytescodeValidPush5Incomplete) {
    std::string bytecode;
    bytecode.push_back(static_cast<char>(0x64));
    bytecode.push_back(static_cast<char>(0x01));
    bytecode.push_back(static_cast<char>(0x02));
    auto status = IsContractBytescodeValid(bytecode);
    ASSERT_EQ(status, ValidationStatus::INCOMPLETE_PUSH);
}

// --- isFileExist Tests ---

TEST_F(TestUtils, IsFileExistNonExistent) {
    ASSERT_FALSE(isFileExist("/nonexistent/path/file.txt"));
}

}  // namespace test

}  // namespace common

}  // namespace seth
