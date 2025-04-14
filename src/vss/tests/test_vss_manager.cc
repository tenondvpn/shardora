#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "bzlib.h"

#define private public
#include "test_transport.h"
#include "election/elect_pool_manager.h"
#include "election/elect_manager.h"
#include "election/elect_manager.h"
#include "network/network_utils.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "vss/vss_manager.h"
#include "vss/vss_utils.h"

namespace tenon {

using namespace elect;

namespace vss {

namespace test {

static const char* kRootNodeIdEndFix = "2f72f72efffee770264ec22dc21c9d2bab63aec39941aad09acda57b4851";
static const char* kWaitingNodeIdEndFix = "1f72f72efffee770264ec22dc21c9d2bab63aec39941aad09acda57b4851";

class TestVssManager : public testing::Test {
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
        common::global_stop = true;
        std::string config_path_ = "./";
        std::string conf_path = config_path_ + "/tenon.conf";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/tenon.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);

        auto transport_ptr = std::dynamic_pointer_cast<transport::Transport>(
            std::make_shared<transport::TestTransport>());
        transport::MultiThreadHandler::Instance()->Init(transport_ptr, transport_ptr);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    std::string GetIdByPrikey(const std::string& private_key) {
        security::PrivateKey prikey(private_key);
        security::PublicKey pubkey(prikey);
        std::string pubkey_str;
        EXPECT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
        std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
        return id;
    }

    std::string GetPubkeyByPrikey(const std::string& private_key, bool compress = true) {
        security::PrivateKey prikey(private_key);
        security::PublicKey pubkey(prikey);
        std::string pubkey_str;
        EXPECT_EQ(pubkey.Serialize(pubkey_str, compress), security::kPublicKeySize);
        return pubkey_str;
    }

    void CreateElectionBlockMemeber(uint32_t network_id, std::vector<std::string>& pri_vec) {
        std::map<uint32_t, elect::MembersPtr> in_members;
        std::map<uint32_t, elect::MembersPtr> out_members;
        std::map<uint32_t, elect::NodeIndexMapPtr> in_index_members;
        std::map<uint32_t, uint32_t> begin_index_map_;
        for (uint32_t i = 0; i < pri_vec.size(); ++i) {
            auto net_id = network_id;
            auto iter = in_members.find(net_id);
            if (iter == in_members.end()) {
                in_members[net_id] = std::make_shared<elect::Members>();
                in_index_members[net_id] = std::make_shared<
                    std::unordered_map<std::string, uint32_t>>();
                begin_index_map_[net_id] = 0;
            }

            security::PrivateKey prikey(pri_vec[i]);
            security::PublicKey pubkey(prikey);
            std::string pubkey_str;
            ASSERT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
            std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
            security::CommitSecret secret;
            in_members[net_id]->push_back(std::make_shared<elect::BftMember>(
                net_id, id, pubkey_str, begin_index_map_[net_id], "", i == 0 ? 0 : -1));
            in_index_members[net_id]->insert(std::make_pair(id, begin_index_map_[net_id]));
            ++begin_index_map_[net_id];
        }

        static uint64_t elect_height = 0;
        for (auto iter = in_members.begin(); iter != in_members.end(); ++iter) {
            auto index_map_iter = in_index_members.find(iter->first);
            ASSERT_TRUE(index_map_iter != in_index_members.end());
//             ElectManager::Instance()->SetNetworkMember(
//                 elect_height++,
//                 iter->first,
//                 iter->second,
//                 index_map_iter->second,
//                 1);
        }
    }

    std::vector<std::string> CreateElectBlocks(int32_t member_count, uint32_t network_id) {
        std::map<uint32_t, MembersPtr> in_members;
        std::map<uint32_t, NodeIndexMapPtr> in_index_members;
        std::map<uint32_t, uint32_t> begin_index_map_;
        std::vector<std::string> pri_vec;
        for (int32_t i = 0; i < member_count; ++i) {
            char from_data[128];
            snprintf(from_data, sizeof(from_data), "%04d%s", i, kRootNodeIdEndFix);
            std::string prikey = common::Encode::HexDecode(from_data);
            pri_vec.push_back(prikey);
            auto net_id = network_id;
            auto iter = in_members.find(net_id);
            if (iter == in_members.end()) {
                in_members[net_id] = std::make_shared<Members>();
                in_index_members[net_id] = std::make_shared<
                    std::unordered_map<std::string, uint32_t>>();
                begin_index_map_[net_id] = 0;
            }

            in_members[net_id]->push_back(std::make_shared<BftMember>(
                net_id,
                GetIdByPrikey(prikey),
                GetPubkeyByPrikey(prikey),
                begin_index_map_[net_id],
                "",
                -1));
            in_index_members[net_id]->insert(std::make_pair(GetIdByPrikey(prikey), begin_index_map_[net_id]));
            ++begin_index_map_[net_id];
        }

        for (auto iter = in_members.begin(); iter != in_members.end(); ++iter) {
            auto index_map_iter = in_index_members.find(iter->first);
            assert(index_map_iter != in_index_members.end());
            elect_pool_manager_.NetworkMemberChange(iter->first, iter->second);
        }

        SetGloableInfo(pri_vec[0], network::kConsensusShardBeginNetworkId);
        CreateElectionBlockMemeber(network_id, pri_vec);
        return pri_vec;
    }

    void UpdateNodeInfoWithBlock(int32_t member_count, uint64_t height) {
        bft::protobuf::Block block_info;
        block_info.set_height(height);
        for (int32_t i = 0; i < member_count; ++i) {
            char from_data[128];
            snprintf(from_data, sizeof(from_data), "%04d%s", i, kRootNodeIdEndFix);
            std::string prikey = common::Encode::HexDecode(from_data);

            char to_data[128];
            snprintf(to_data, sizeof(to_data), "%04d%s", i + member_count + 1, kRootNodeIdEndFix);
            std::string to_prikey = common::Encode::HexDecode(to_data);

            auto tx_list = block_info.mutable_tx_list();
            auto tx_info = tx_list->Add();
            tx_info->set_from(GetIdByPrikey(prikey));
            tx_info->set_to(GetIdByPrikey(to_prikey));
            tx_info->set_from_pubkey(GetPubkeyByPrikey(prikey));
            tx_info->set_balance(common::Random::RandomUint64() % (common::kTenonMaxAmount / 1000000));
        }

        elect_pool_manager_.UpdateNodeInfoWithBlock(block_info);
    }

    void AddWaitingPoolNetworkNodes(int32_t member_count, uint32_t network_id) {
        for (int32_t i = 0; i < member_count; ++i) {
            NodeDetailPtr new_node = std::make_shared<ElectNodeDetail>();
            char from_data[128];
            snprintf(from_data, sizeof(from_data), "%04d%s", i, kWaitingNodeIdEndFix);
            std::string prikey = common::Encode::HexDecode(from_data);
            new_node->id = GetIdByPrikey(prikey);
            new_node->public_key = GetPubkeyByPrikey(prikey);
            new_node->dht_key = "";
            new_node->public_ip = 0;
            new_node->public_port = 0;
            new_node->join_tm = std::chrono::steady_clock::now() - std::chrono::microseconds(kElectAvailableJoinTime + 1000);
            new_node->choosed_balance = common::Random::RandomUint64() % (common::kTenonMaxAmount / 1000000);
            elect_pool_manager_.AddWaitingPoolNode(network_id, new_node);
        }
    }

    void UpdateWaitingNodesConsensusCount(int32_t member_count) {
        common::BloomFilter pick_all(kBloomfilterWaitingSize, kBloomfilterWaitingHashCount);
        std::vector<NodeDetailPtr> pick_all_vec;
        elect_pool_manager_.waiting_pool_map_[
            network::kConsensusShardBeginNetworkId +
            network::kConsensusWaitingShardOffset]->GetAllValidHeartbeatNodes(
            0, pick_all, pick_all_vec);
        for (int32_t i = 0; i < member_count; ++i) {
            char from_data[128];
            snprintf(from_data, sizeof(from_data), "%04d%s", i, kRootNodeIdEndFix);
            std::string prikey = common::Encode::HexDecode(from_data);
            elect_pool_manager_.waiting_pool_map_[
                network::kConsensusShardBeginNetworkId +
                network::kConsensusWaitingShardOffset]->UpdateWaitingNodes(
                GetIdByPrikey(prikey), pick_all);
        }
    }

    static void JoinNetwork(uint32_t network_id) {
        network::DhtManager::Instance()->UnRegisterDht(network_id);
        network::UniversalManager::Instance()->UnRegisterUniversal(network_id);
        dht::DhtKeyManager dht_key(
            network_id,
            common::GlobalInfo::Instance()->country(),
            common::GlobalInfo::Instance()->id());
        dht::NodePtr local_node = std::make_shared<dht::Node>(
            common::GlobalInfo::Instance()->id(),
            dht_key.StrKey(),
            dht::kNatTypeFullcone,
            false,
            common::GlobalInfo::Instance()->config_local_ip(),
            common::GlobalInfo::Instance()->config_local_port(),
            common::GlobalInfo::Instance()->config_local_ip(),
            common::GlobalInfo::Instance()->config_local_port(),
            security::Security::Instance()->str_pubkey(),
            common::GlobalInfo::Instance()->node_tag());
        local_node->first_node = true;
        transport::TransportPtr transport;
        auto dht = std::make_shared<elect::ElectDht>(transport, local_node);
        dht->Init(nullptr, nullptr);
        auto base_dht = std::dynamic_pointer_cast<dht::BaseDht>(dht);
        network::DhtManager::Instance()->RegisterDht(network_id, base_dht);
        network::UniversalManager::Instance()->RegisterUniversal(network_id, base_dht);
    }

    void SetGloableInfo(const std::string& private_key, uint32_t network_id) {
        security::PrivateKey prikey(common::Encode::HexDecode(private_key));
        security::PublicKey pubkey(prikey);
        std::string pubkey_str;
        ASSERT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
        std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
        security::Security::Instance()->set_prikey(std::make_shared<security::PrivateKey>(prikey));
        security::EcdhCreateKey::Instance()->Init();
        common::GlobalInfo::Instance()->set_id(id);
        common::GlobalInfo::Instance()->set_consensus_shard_count(1);
        common::GlobalInfo::Instance()->set_network_id(network_id);
        JoinNetwork(network::kRootCongressNetworkId);
        JoinNetwork(network::kUniversalNetworkId);
        JoinNetwork(network::kConsensusShardBeginNetworkId);
    }

private:
    ElectPoolManager elect_pool_manager_;
};

TEST_F(TestVssManager, RandomNumXorTest) {
    std::vector<uint64_t> all_num;
    for (uint32_t i = 0; i < 1024; ++i) {
        all_num.push_back(common::Random::RandomUint64());
    }

    uint64_t xor1 = 0;
    for (uint32_t i = 0; i < 1024; ++i) {
        xor1 ^= all_num[i];
    }
    
    for (uint32_t i = 0; i < 1000; ++i) {
        std::random_shuffle(all_num.begin(), all_num.end());
        uint64_t xor2 = 0;
        for (uint32_t i = 0; i < 1024; ++i) {
            xor2 ^= all_num[i];
        }

        ASSERT_EQ(xor1, xor2);
    }
}

TEST_F(TestVssManager, AllNodeRandomValid) {
    const uint32_t kMemberCount = 31;
    auto first_prikey_shard_1 = CreateElectBlocks(
        kMemberCount,
        network::kConsensusShardBeginNetworkId);
    auto first_prikey_root = CreateElectBlocks(
        kMemberCount,
        network::kRootCongressNetworkId);
    SetGloableInfo(
        common::Encode::HexEncode(first_prikey_root[0]),
        network::kRootCongressNetworkId);
    security::EcdhCreateKey::Instance()->Init();
    VssManager vss_mgr;
    auto latest_time_block_tm = common::TimeUtils::TimestampSeconds();
    elect_pool_manager_.OnTimeBlock(latest_time_block_tm);
    vss_mgr.OnTimeBlock(latest_time_block_tm, 0, 1, 123456789llu);
    ASSERT_EQ(vss_mgr.EpochRandom(), 123456789llu);
    ASSERT_TRUE(vss_mgr.local_random_.valid_);
    ASSERT_FALSE(vss_mgr.local_random_.invalid_);
    ASSERT_EQ(vss_mgr.local_random_.owner_id_, common::GlobalInfo::Instance()->id());
    auto tmp_id = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[0]);
    ASSERT_EQ(vss_mgr.local_random_.owner_id_, tmp_id);
    // first period random hash
    uint32_t root_member_count = elect::ElectManager::Instance()->GetMemberCount(
        network::kRootCongressNetworkId);
    VssManager* vss_mgrs = new VssManager[root_member_count];
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].OnTimeBlock(latest_time_block_tm, 0, 1, 123456789llu);
        ASSERT_EQ(vss_mgrs[i].EpochRandom(), 123456789llu);
        ASSERT_TRUE(vss_mgrs[i].local_random_.valid_);
        ASSERT_FALSE(vss_mgrs[i].local_random_.invalid_);
        ASSERT_EQ(vss_mgrs[i].local_random_.owner_id_, common::GlobalInfo::Instance()->id());
    }

    // first period split random
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastFirstPeriodHash();
        auto first_msg = vss_mgrs[i].first_msg_;
        protobuf::VssMessage vss_msg;
        vss_msg.ParseFromString(first_msg.data());
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleFirstPeriodHash(vss_msg);
            ASSERT_EQ(
                vss_mgrs[j].other_randoms_[i].random_num_hash_,
                vss_mgrs[i].local_random_.random_num_hash_);
        }
    }

    // second period random
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastSecondPeriodRandom();
        auto second_msg = vss_mgrs[i].second_msg_;
        protobuf::VssMessage vss_msg;
        vss_msg.ParseFromString(second_msg.data());
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleSecondPeriodRandom(vss_msg);
            ASSERT_TRUE(vss_mgrs[j].other_randoms_[i].valid_);
        }
    }

    // third period broadcast the random number with the highest aggregate final ratio
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastThirdPeriodRandom();
        protobuf::VssMessage vss_msg;
        ASSERT_TRUE(vss_msg.ParseFromString(vss_mgrs[i].third_msg_.data()));
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            if (i == j) {
                continue;
            }

            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleThirdPeriodRandom(vss_msg);
        }
    }

    // all success
    auto final_random = vss_mgrs[0].GetConsensusFinalRandom();
    ASSERT_NE(final_random, 0llu);
    for (uint32_t i = 1; i < root_member_count; ++i) {
        auto tmp_final_random = vss_mgrs[0].GetConsensusFinalRandom();
        ASSERT_EQ(final_random, tmp_final_random);
    }
}

TEST_F(TestVssManager, SomeNodeRandomValid) {
    const uint32_t kMemberCount = 31;
    auto first_prikey_shard_1 = CreateElectBlocks(
        kMemberCount,
        network::kConsensusShardBeginNetworkId);
    auto first_prikey_root = CreateElectBlocks(
        kMemberCount,
        network::kRootCongressNetworkId);
    SetGloableInfo(
        common::Encode::HexEncode(first_prikey_root[0]),
        network::kRootCongressNetworkId);
    security::EcdhCreateKey::Instance()->Init();
    VssManager vss_mgr;
    auto latest_time_block_tm = common::TimeUtils::TimestampSeconds();
    elect_pool_manager_.OnTimeBlock(latest_time_block_tm);
    vss_mgr.OnTimeBlock(latest_time_block_tm, 0, 1, 123456789llu);
    ASSERT_EQ(vss_mgr.EpochRandom(), 123456789llu);
    ASSERT_TRUE(vss_mgr.local_random_.valid_);
    ASSERT_FALSE(vss_mgr.local_random_.invalid_);
    ASSERT_EQ(vss_mgr.local_random_.owner_id_, common::GlobalInfo::Instance()->id());
    auto tmp_id = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[0]);
    ASSERT_EQ(vss_mgr.local_random_.owner_id_, tmp_id);
    // first period random hash
    uint32_t root_member_count = elect::ElectManager::Instance()->GetMemberCount(
        network::kRootCongressNetworkId);
    VssManager* vss_mgrs = new VssManager[root_member_count];
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].OnTimeBlock(latest_time_block_tm, 0, 1, 123456789llu);
        ASSERT_EQ(vss_mgrs[i].EpochRandom(), 123456789llu);
        ASSERT_TRUE(vss_mgrs[i].local_random_.valid_);
        ASSERT_FALSE(vss_mgrs[i].local_random_.invalid_);
        ASSERT_EQ(vss_mgrs[i].local_random_.owner_id_, common::GlobalInfo::Instance()->id());
    }

    // first period split random
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastFirstPeriodHash();
        auto first_msg = vss_mgrs[i].first_msg_;
        protobuf::VssMessage vss_msg;
        vss_msg.ParseFromString(first_msg.data());
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleFirstPeriodHash(vss_msg);
            ASSERT_EQ(
                vss_mgrs[j].other_randoms_[i].random_num_hash_,
                vss_mgrs[i].local_random_.random_num_hash_);
        }
    }

    uint32_t invalid_count = root_member_count * 2 / 3 - 1;
    std::unordered_set<uint32_t> invalid_index_set;
    while (invalid_index_set.size() != invalid_count) {
        invalid_index_set.insert(std::rand() % root_member_count);
    }

    // second period random
    for (uint32_t i = 0; i < root_member_count; ++i) {
        auto iter = invalid_index_set.find(i);
        if (iter != invalid_index_set.end()) {
            continue;
        }

        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastSecondPeriodRandom();
        auto second_msg = vss_mgrs[i].second_msg_;
        protobuf::VssMessage vss_msg;
        vss_msg.ParseFromString(second_msg.data());
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleSecondPeriodRandom(vss_msg);
            ASSERT_TRUE(vss_mgrs[j].other_randoms_[i].valid_);
        }
    }

    // third period broadcast the random number with the highest aggregate final ratio
    for (uint32_t i = 0; i < root_member_count; ++i) {
        SetGloableInfo(
            common::Encode::HexEncode(first_prikey_root[i]),
            network::kRootCongressNetworkId);
        vss_mgrs[i].BroadcastThirdPeriodRandom();
        protobuf::VssMessage vss_msg;
        ASSERT_TRUE(vss_msg.ParseFromString(vss_mgrs[i].third_msg_.data()));
        auto tmp_id1 = security::Secp256k1::Instance()->ToAddressWithPublicKey(vss_msg.pubkey());
        auto tmp_id2 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(first_prikey_root[i]);
        ASSERT_EQ(tmp_id1, tmp_id2);
        for (uint32_t j = 0; j < root_member_count; ++j) {
            SetGloableInfo(
                common::Encode::HexEncode(first_prikey_root[j]),
                network::kRootCongressNetworkId);
            vss_mgrs[j].HandleThirdPeriodRandom(vss_msg);
        }
    }

    // all success
    auto final_random = vss_mgrs[0].GetConsensusFinalRandom();
    ASSERT_NE(final_random, 0llu);
    for (uint32_t i = 1; i < root_member_count; ++i) {
        auto tmp_final_random = vss_mgrs[0].GetConsensusFinalRandom();
        ASSERT_EQ(final_random, tmp_final_random);
    }
}

}  // namespace test

}  // namespace vss

}  // namespace tenon
