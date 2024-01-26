#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>

#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"

using namespace zjchain;

int main(int argc, char* argv[]) {
    transport::MultiThreadHandler net_handler;
    auto db_ptr = std::make_shared<db::Db>();
    if (net_handler.Init(db_ptr) != 0) {
        return 1;
    }

    transport::TcpTransport::Instance()->Init(
        "127.0.0.1:8990",
        128,
        false,
        &net_handler);
    srand(time(NULL));
    std::atomic<uint32_t> receive_count = 0;
    uint32_t type = rand() % (common::kBlsMessage + 1);
    if (argc >= 2) {
        common::StringUtil::ToUint32(argv[1], &type);
    }

    std::mutex receive_mutex;
    uint64_t b_time = common::TimeUtils::TimestampMs();
    static const uint32_t kTestThreadCount = 500;
    auto cli_msg_callback = [&](const transport::MessagePtr& message) {
        auto thread_idx = message->header.type() - 1;
        message->header.set_src_sharding_id(message->header.type());
        message->header.set_type(type);
        message->header.set_hash64(message->header.hash64() + 1);
        std::string str_msg;
        message->header.SerializeToString(&str_msg);
        message->conn->Send(str_msg);
    };

    transport::TcpTransport::Instance()->Start(false);
    std::cout << "type: " << type << std::endl;
    for (uint32_t i = 1; i < kTestThreadCount; i++) {
        transport::Processor::Instance()->RegisterProcessor(i, cli_msg_callback);
        transport::protobuf::Header msg;
        msg.set_type(type);
        msg.set_src_sharding_id(i);
        uint32_t rand_num = rand();
        uint64_t hash64 = (uint64_t(rand_num) << 32) | i;
        msg.set_hash64(hash64);
        transport::TcpTransport::Instance()->Send(
            0,
            "127.0.0.1", 8990, msg);
    }

    char stop;
    std::cin >> stop;
    transport::TcpTransport::Instance()->Stop();
    return 0;
}