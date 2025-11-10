#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>

#include "common/global_info.h"
#include "common/time_utils.h"
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"

using namespace shardora;

std::shared_ptr<security::Security> InitSecurity() {
    std::string prikey;
    if (!conf_.Get("zjchain", "prikey", prikey)) {
        INIT_ERROR("get private key from config failed!");
        return nullptr;
    }

    auto security = std::make_shared<security::Ecdsa>();
    if (security_->SetPrivateKey(
            common::Encode::HexDecode(prikey)) != security::kSecuritySuccess) {
        INIT_ERROR("init security failed!");
        return nullptr;
    }

    return security;
}

int main(int argc, char* argv[]) {
    transport::MultiThreadHandler net_handler;
    auto db_ptr = std::make_shared<db::Db>();
    auto security_ptr = InitSecurity();
    if (security_ptr == nullptr) {  
        return 1;
    }

    if (net_handler.Init(db_ptr, security_ptr) != 0) {
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
                std::cout << "qps: " 
                    << (uint32_t)(float(receive_count) / (float(e_time - b_time) / 1000.0f)) 
                    << std::endl;
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