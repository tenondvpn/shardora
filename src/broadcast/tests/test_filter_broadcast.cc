#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "dht/dht_key.h"
#define private public
#include "common/random.h"
#include "broadcast/filter_broadcast.h"
#include "transport/multi_thread.h"

namespace zjchain {

namespace broadcast {

namespace test {

static transport::MultiThreadHandler msg_handler_;

class TestFilterBroadcast : public testing::Test {
public:
    static void SetUpTestCase() {    
        zjchain::transport::TcpTransport::Instance()->Init(
            "127.0.0.1:8990",
            128,
            true,
            &msg_handler_);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestFilterBroadcast, BinarySearch) {
    dht::Dht dht;
    for (uint32_t i = 0; i < 372; ++i) {
        std::string id = std::string("id_") + std::to_string(i);
        dht::DhtKeyManager dht_key(1, id);
        dht::NodePtr node = std::make_shared<dht::Node>(
            1,
            "public_ip",
            1,
            "in_pubkey_str",
            id);
        dht.push_back(node);
    }
    std::sort(
            dht.begin(),
            dht.end(),
            [](const dht::NodePtr& lhs, const dht::NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });
    FilterBroadcast filter_broad;
    for (uint32_t i = 0; i < 1000; ++i) {
        auto rand_64 = common::Random::RandomUint64();
        auto pos = filter_broad.BinarySearch(dht, rand_64);
        if (pos > 0) {
            assert(dht[pos - 1]->id_hash <= dht[pos]->id_hash);
        }
        if (pos < dht.size() - 1) {
            assert(dht[pos + 1]->id_hash >= dht[pos]->id_hash);
        }
        if (pos > 0) {
            assert(rand_64 >= dht[pos]->id_hash);
        }
    }
}

TEST_F(TestFilterBroadcast, LayerGetNodes) {
    std::string id = std::string("local_node");
    dht::DhtKeyManager dht_key(1, id);
    dht::NodePtr local_node = std::make_shared<dht::Node>(
            1,
            "127.0.0.1",
            9701,
            "127.0.0.1",
            "tag");
    dht::BaseDhtPtr base_dht = std::make_shared<dht::BaseDht>(local_node);
    for (uint32_t i = 0; i < 372; ++i) {
        std::string id = std::string("id_") + std::to_string(i);
        dht::DhtKeyManager dht_key(1, id);
        dht::NodePtr node = std::make_shared<dht::Node>(
                1,
                "127.0.0.1",
                9702 + i,
                "in_pubkey_str",
                "tag");
        base_dht->Join(node);
    }
    FilterBroadcast filter_broad;
    transport::protobuf::Header message;
    message.set_broadcast(true);
}

TEST_F(TestFilterBroadcast, BroadcastingNoOverlap) {
    std::string id = std::string("local_node");
    dht::DhtKeyManager dht_key(1, id);
    dht::NodePtr local_node = std::make_shared<dht::Node>(
            1,
            "127.0.0.1",
            9701,
            "127.0.0.1",
            "tag");
    dht::BaseDhtPtr base_dht = std::make_shared<dht::BaseDht>(local_node);
    for (uint32_t i = 0; i < 372; ++i) {
        std::string id = std::string("id_") + std::to_string(i);
        dht::DhtKeyManager dht_key(1, id);
        dht::NodePtr node = std::make_shared<dht::Node>(
                1,
                "127.0.0.1",
                9702 + i,
                "127.0.0.1",
                "tag");
        base_dht->Join(node);
    }
    FilterBroadcast filter_broad;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& message = msg_ptr->header;
}

TEST_F(TestFilterBroadcast, BroadcastingOverlap) {
    std::string id = std::string("local_node");
    dht::DhtKeyManager dht_key(1, id);
    dht::NodePtr local_node = std::make_shared<dht::Node>(
            1,
            "127.0.0.1",
            9701,
            "127.0.0.1",
            "tag");
    dht::BaseDhtPtr base_dht = std::make_shared<dht::BaseDht>(local_node);
    for (uint32_t i = 0; i < 372; ++i) {
        std::string id = std::string("id_") + std::to_string(i);
        dht::DhtKeyManager dht_key(1, id);
        dht::NodePtr node = std::make_shared<dht::Node>(
                1,
                "127.0.0.1",
                9702 + i,
                "127.0.0.1",
                "tag");
        base_dht->Join(node);
    }
    FilterBroadcast filter_broad;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& message = msg_ptr->header;
    msg_ptr->thread_idx = 0;
}

}  // namespace test

}  // namespace db

}  // namespace zjchain
