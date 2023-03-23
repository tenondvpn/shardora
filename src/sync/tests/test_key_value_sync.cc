#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <unordered_set>
#include <vector>

#define private public
#define protected public
#include "sync/key_value_sync.h"
#include "init/network_init.h"
#include "init/init_utils.h"
#include "common/split.h"
#include "block/block_manager.h"
#include "network/network_utils.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "dht/dht_key.h"
#include "dht/base_dht.h"
#include "security/security.h"
#include "election/elect_dht.h"
#include "db/db.h"

namespace zjchain {

namespace sync {

namespace test {

class TestKeyValueSync : public testing::Test {
public:
    static void SetUpTestCase() {
//         db::Db::Instance()->Init("./test_db");
        db::Db::Instance()->db_.reset();
        db::Db::Instance()->inited_ = false;
        JoinNetwork(network::kRootCongressNetworkId);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
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

        auto test_dht = network::DhtManager::Instance()->GetDht(network_id);
        dht::DhtKeyManager test_dht_key(
            network_id,
            common::GlobalInfo::Instance()->country(),
            "test_id1");
        dht::NodePtr test_node = std::make_shared<dht::Node>(
            "test_id1",
            test_dht_key.StrKey(),
            dht::kNatTypeFullcone,
            false,
            "192.1.1.1",
            123,
            "192.1.1.1",
            123,
            "",
            "");
        test_dht->dht_.push_back(test_node);
    }

private:

};

TEST_F(TestKeyValueSync, TestSyncKeyValue) {

}

TEST_F(TestKeyValueSync, TestSyncHeight) {
    init::NetworkInit net_init;
    std::string params = "pl -U -1 031d29587f946b7e57533725856e3b2fc840ac8395311fea149642334629cd5757:127.0.0.1:9001,03a6f3b7a4a3b546d515bfa643fc4153b86464543a13ab5dd05ce6f095efb98d87:127.0.0.1:8001,031e886027cdf3e7c58b9e47e8aac3fe67c393a155d79a96a0572dd2163b4186f0:127.0.0.1:7001 -2 0315a968643f2ada9fd24f0ca92ae5e57d05226cfe7c58d959e510b27628c1cac0:127.0.0.1:7301,030d62d31adf3ccbc6283727e2f4493a9228ef80f113504518c7cae46931115138:127.0.0.1:7201,028aa5aec8f1cbcd995ffb0105b9c59fd76f29eaffe55521aad4f7a54e78f01e58:127.0.0.1:7101";
    common::Split<> params_split(params.c_str(), ' ', params.size());
    ASSERT_EQ(net_init.Init(params_split.cnt_, params_split.pt_), init::kInitSuccess);
    ASSERT_EQ(KeyValueSync::Instance()->AddSyncHeight(
        network::kRootCongressNetworkId, 0, 0, kSyncHigh), sync::kSyncSuccess);
    KeyValueSync::Instance()->CheckSyncItem();
    KeyValueSync::Instance()->HandleMessage(
        std::make_shared<transport::protobuf::Header>(
            KeyValueSync::Instance()->test_sync_req_msg_));
    KeyValueSync::Instance()->HandleMessage(
        std::make_shared<transport::protobuf::Header>(
            KeyValueSync::Instance()->test_sync_res_msg_));
}

}  // namespace test

}  // namespace sync

}  // namespace zjchain
