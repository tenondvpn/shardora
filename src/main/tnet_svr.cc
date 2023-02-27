#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>

#include "common/global_info.h"
#include "common/time_utils.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"

using namespace zjchain;

int main(int argc, char* argv[]) {
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security_ptr = std::make_shared<security::Ecdsa>();
    security_ptr->SetPrivateKey(common::Encode::HexDecode(
        "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
    auto db_ptr = std::make_shared<db::Db>();
    if (net_handler.Init(security_ptr, db_ptr) != 0) {
        return 1;
    }

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
        if (receive_count >= 1000000) {
            std::lock_guard<std::mutex> g(receive_mutex);
            if (receive_count >= 1000000) {
                uint64_t e_time = common::TimeUtils::TimestampMs();
                std::cout << "qps: " << (uint32_t)(float(receive_count) / (float(e_time - b_time) / 1000.0f)) << std::endl;
                receive_count = 0;
                b_time = e_time;
            }
        }

        transport::protobuf::Header msg;
        msg.set_type(message->header.src_sharding_id());
        msg.set_hash64(message->header.hash64() + 1);
        std::string str_msg;
        msg.SerializeToString(&str_msg);
        message->conn->Send(str_msg);
    };

    for (uint8_t i = 0; i <= common::kBlsMessage; ++i) {
        transport::Processor::Instance()->RegisterProcessor(i, svr_msg_callback);
    }
    transport::TcpTransport::Instance()->Start(false);
    char stop;
    std::cin >> stop;
    transport::TcpTransport::Instance()->Stop();
    return 0;
}