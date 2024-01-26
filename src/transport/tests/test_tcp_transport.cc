#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>

#include "common/time_utils.h"
#include "common/global_info.h"
#define private public
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"

namespace zjchain {

namespace tcp {

namespace test {

class TestTcpTransport : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestTcpTransport, TestServer) {
    transport::MultiThreadHandler net_handler;
    auto db_ptr = std::make_shared<db::Db>();
    ASSERT_EQ(net_handler.Init(db_ptr), 0);
    transport::TcpTransport::Instance()->Init(
        "127.0.0.1:8990",
        128,
        true,
        &net_handler);
    std::atomic<uint32_t> receive_count = 0;
    std::mutex receive_mutex;
    uint64_t b_time = common::TimeUtils::TimestampMs();
    auto svr_msg_callback = [&](const transport::MessagePtr& message) {
        ++receive_count;
        if (receive_count >= 100000) {
            std::lock_guard<std::mutex> g(receive_mutex);
            if (receive_count >= 100000) {
                uint64_t e_time = common::TimeUtils::TimestampMs();
                std::cout << "qps: " << (float(receive_count) / (float(e_time - b_time) / 1000.0f)) << std::endl;
                receive_count = 0;
                b_time = e_time;
            }
        }

        transport::protobuf::Header msg;
        msg.set_type(message->header.src_sharding_id());
        msg.set_hash64(message->header.hash64() + 1);
        std::string str_msg;
        msg.SerializeToString(&str_msg);
        ASSERT_EQ(message->conn->Send(str_msg), 0);
    };

    static const uint32_t kTestThreadCount = 500;
    std::condition_variable con[kTestThreadCount];
    std::mutex mu[kTestThreadCount];
    auto cli_msg_callback = [&](const transport::MessagePtr& message) {
        auto thread_idx = message->header.type() - 1;
        message->header.set_src_sharding_id(message->header.type());
        message->header.set_type(0);
        message->header.set_hash64(message->header.hash64() + 1);
        std::string str_msg;
        message->header.SerializeToString(&str_msg);
        ASSERT_EQ(message->conn->Send(str_msg), 0);
    };

    transport::Processor::Instance()->RegisterProcessor(0, svr_msg_callback);
    transport::TcpTransport::Instance()->Start(false);
    for (uint32_t i = 11; i <= kTestThreadCount; i++) {
        if (i == common::kPoolsMessage) {
            continue;
        }

        transport::Processor::Instance()->RegisterProcessor(i, cli_msg_callback);
        transport::protobuf::Header msg;
        msg.set_type(0);
        msg.set_src_sharding_id(i);
        uint64_t hash64 = ((uint64_t)i) << 32;
        msg.set_hash64(hash64);
        ASSERT_EQ(transport::TcpTransport::Instance()->Send(
            0,
            "127.0.0.1", 8990, msg), 0);
    }

    usleep(200000);
    transport::TcpTransport::Instance()->Stop();
}

}  // namespace test

}  // namespace tcp

}  // namespace zjchain
