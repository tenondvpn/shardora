#include <stdlib.h>
#include <math.h>

#include <condition_variable>
#include <mutex>
#include <iostream>

#include <gtest/gtest.h>

#include "bzlib.h"
#include "common/random.h"
#include "dht/dht_key.h"

#define private public
#include "pools/tx_pool_manager.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

namespace test {

static std::shared_ptr<db::Db> db_ptr = nullptr;

class TestTxPoolManager : public testing::Test {
public:
    static void SetUpTestCase() {    
        common::GlobalInfo::Instance()->set_network_id(2);
        db_ptr = std::make_shared<db::Db>();
        system((std::string("rm -rf ./tx_pool_mgr").c_str()));
        std::string db_path = std::string("./tx_pool_mgr");
        db_ptr->Init(db_path);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

static uint8_t GetTxThreadIndex(
        transport::MessagePtr& msg_ptr,
        uint32_t pool_index,
        uint32_t thread_count) {
    auto address_info = std::make_shared<address::protobuf::AddressInfo>();
    address_info->set_sharding_id(2);
    address_info->set_pool_index(pool_index);
    if (address_info == nullptr ||
        address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        return 255;
    }

    msg_ptr->address_info = address_info;
    return address_info->pool_index() % thread_count;
}

std::shared_ptr<address::protobuf::AddressInfo> CreateAddressInfo() {
    auto single_to_address_info_ = std::make_shared<address::protobuf::AddressInfo>();
    single_to_address_info_->set_pubkey("");
    single_to_address_info_->set_balance(0);
    single_to_address_info_->set_sharding_id(-1);
    single_to_address_info_->set_pool_index(0);
    single_to_address_info_->set_addr(common::kNormalToAddress);
    single_to_address_info_->set_type(address::protobuf::kNormal);
    single_to_address_info_->set_latest_height(0);
    return single_to_address_info_;
}

TEST_F(TestTxPoolManager, All) {
    std::shared_ptr<security::Security> security_ptr = std::make_shared<security::Ecdsa>();
    security_ptr->SetPrivateKey(common::Encode::HexDecode(
        "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
    TxPoolManager tx_pool_mgr(security_ptr, db_ptr);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(2);
    dht::DhtKeyManager dht_key(2);
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header, 0);
    auto& tx_msg = *header.mutable_tx_proto();
    tx_msg.set_version(0);
    tx_msg.set_gid(common::Random::RandomString(32));
    tx_msg.set_pubkey(security_ptr->GetPublicKey());
    tx_msg.set_gas_limit(100000000);
    tx_msg.set_gas_price(100);
    tx_msg.set_key("key");
    tx_msg.set_value("value");
    tx_msg.set_to(common::Encode::HexDecode("a533262d5163e6feb6e1b70ade6d512fadadf0b5"));
    tx_msg.set_amount(100000000lu);
    auto sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    std::string sign;
    if (security_ptr->Sign(
            transport::TcpTransport::Instance()->GetHeaderHashForSign(header),
            &sign) != security::kSecuritySuccess) {
        ASSERT_TRUE(false);
    }

    header.set_sign(sign);
    uint8_t thread_idx = GetTxThreadIndex(msg_ptr, 4, 4);
    ASSERT_TRUE(thread_idx != 255);
    msg_ptr->thread_idx = thread_idx;
    msg_ptr->address_info = CreateAddressInfo();
    tx_pool_mgr.HandleMessage(msg_ptr);
    ASSERT_EQ(tx_pool_mgr.msg_queues_[msg_ptr->address_info->pool_index()].size(), 1);
    std::map<std::string, pools::TxItemPtr> res_vec;
    tx_pool_mgr.GetTx(10, msg_ptr->address_info->pool_index(), res_vec);
    ASSERT_EQ(res_vec.size(), 1);
}

static void TestMultiThread(int32_t thread_count, int32_t leader_count, uint32_t tx_count) {
    std::shared_ptr<security::Security> security_ptr = std::make_shared<security::Ecdsa>();
    security_ptr->SetPrivateKey(common::Encode::HexDecode(
        "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
    TxPoolManager tx_pool_mgr(security_ptr, db_ptr);
    srand(time(NULL));
    std::condition_variable con[thread_count];
    std::mutex mu[thread_count];
    uint64_t times[64] = { 0 };
    auto write_msg = [&]() {
        for (uint32_t i = 0; i < tx_count; ++i) {
            auto time0 = common::TimeUtils::TimestampUs();
            std::shared_ptr<security::Security> security_ptr = std::make_shared<security::Ecdsa>();
            security_ptr->SetPrivateKey(common::Random::RandomString(32));
            auto time1 = common::TimeUtils::TimestampUs();
            times[0] += time1 - time0;
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            auto& header = msg_ptr->header;
            header.set_src_sharding_id(2);
            dht::DhtKeyManager dht_key(2);
            header.set_des_dht_key(dht_key.StrKey());
            transport::TcpTransport::Instance()->SetMessageHash(header, 0);
            auto& tx_msg = *header.mutable_tx_proto();
            tx_msg.set_version(0);
            tx_msg.set_gid(common::Random::RandomString(32));
            tx_msg.set_pubkey(security_ptr->GetPublicKey());
            tx_msg.set_gas_limit(100000000);
            tx_msg.set_gas_price(100);
            tx_msg.set_key("key");
            tx_msg.set_value("value");
            tx_msg.set_to(common::Encode::HexDecode("a533262d5163e6feb6e1b70ade6d512fadadf0b5"));
            tx_msg.set_amount(100000000lu);
            auto time2 = common::TimeUtils::TimestampUs();
            times[1] += time2 - time1;
            auto sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
            std::string sign;
            if (security_ptr->Sign(
                transport::TcpTransport::Instance()->GetHeaderHashForSign(header),
                &sign) != security::kSecuritySuccess) {
                ASSERT_TRUE(false);
            }

            header.set_sign(sign);
            auto time3 = common::TimeUtils::TimestampUs();
            times[2] += time3 - time2;
            uint8_t thread_idx = GetTxThreadIndex(
                msg_ptr,
                rand() % common::kInvalidPoolIndex,
                thread_count);
            ASSERT_TRUE(thread_idx != 255);
            msg_ptr->thread_idx = thread_idx;
            tx_pool_mgr.HandleMessage(msg_ptr);
            auto time4 = common::TimeUtils::TimestampUs();
            times[3] += time4 - time3;
//             con[msg_ptr->thread_idx].notify_one();
        }
    };

    std::atomic<uint32_t> handled_count = 0;
    std::mutex t;
    auto get_msg = [&](int32_t thread_idx) {
        std::vector<int32_t> pool_idxs;
        std::string tmp;
        for (int32_t i = 0; i < (int32_t)common::kInvalidPoolIndex; i++) {
            if (i % thread_count != thread_idx) {
                continue;
            }

            pool_idxs.push_back(i);
        }

        while (true) {
            uint32_t tmp_count = 0;
            for (uint32_t i = 0; i < pool_idxs.size(); ++i) {
                std::map<std::string, pools::TxItemPtr> res_vec;
                tx_pool_mgr.GetTx(10, pool_idxs[i], res_vec);
                if (!res_vec.empty()) {
                    tx_pool_mgr.TxOver(pool_idxs[i], res_vec);
                    tmp_count += res_vec.size();
                }
            }
            
            if (tmp_count == 0) {
                std::unique_lock<std::mutex> lock(mu[thread_idx]);
                con[thread_idx].wait_for(lock, std::chrono::milliseconds(10));
            } else {
                handled_count += tmp_count;
            }

            if (handled_count >= tx_count) {
                break;
            }
        }
    };
    
    std::thread wthread(write_msg);
    wthread.join();
    for (int32_t i = 0; i < 4; ++i) {
        std::cout << i << " : " << (times[i] / tx_count) << std::endl;
    }
    auto b_time = common::TimeUtils::TimestampMs();
    std::vector<std::thread*> tvec;
    for (int32_t i = 0; i < thread_count; ++i) {
        tvec.push_back(new std::thread(get_msg, i));
    }

    for (int32_t i = 0; i < thread_count; ++i) {
        tvec[i]->join();
        delete tvec[i];
    }
    auto e_time = common::TimeUtils::TimestampMs();
    std::cout << "tps: " << ((float)tx_count / (float(e_time - b_time) / 1000.0)) << std::endl;
}

TEST_F(TestTxPoolManager, ProcessBentchTest) {
    TestMultiThread(4, 256, 1000000);
}

TEST_F(TestTxPoolManager, TestMapWithPriority) {
    std::map<std::string, std::string> pri_map;
    std::string test_key = common::Random::RandomString(40);
    for (uint64_t i = 0; i < 10000; ++i) {
        uint64_t* key = (uint64_t*)test_key.data();
        key[0] = common::ShiftUint32(common::Random::RandomUint64());
        pri_map[test_key] = test_key;
    }

    for (auto iter = pri_map.begin(); iter != pri_map.end(); ++iter) {
        std::cout << common::Encode::HexEncode(iter->first) << std::endl;
    }
}

}  // namespace test

}  // namespace pools

}  // namespace zjchain
