#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <iostream>

#include "common/time_utils.h"
#include "common/global_info.h"
#include "common/log.h"
#include "db/db.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/tcp_transport.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"

using namespace shardora;

std::shared_ptr<security::Security> InitSecurity() {
    std::string prikey("eaa16b476667652327eb9afe2751fdcf22a272e70407b630596fa756abf80ee1");
    auto security = std::make_shared<security::Ecdsa>();
    if (security->SetPrivateKey(
            common::Encode::HexDecode(prikey)) != security::kSecuritySuccess) {
        ZJC_ERROR("init security failed!");
        return nullptr;
    }

    return security;
}

int main(int argc, char* argv[]) {
    log4cpp::PropertyConfigurator::configure("../zjnodes_local/zjchain/conf/log4cpp.properties");
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
        false,
        &net_handler);
    srand(time(NULL));
    std::atomic<uint32_t> receive_count = 0;
    uint32_t type = common::kDhtMessage;
    if (argc >= 2) {
        common::StringUtil::ToUint32(argv[1], &type);
    }

    std::mutex receive_mutex;
    uint64_t b_time = common::TimeUtils::TimestampMs();
    static const uint32_t kTestThreadCount = common::kMaxMessageTypeCount - 1;
    auto cli_msg_callback = [&](const transport::MessagePtr& message) {
        auto thread_idx = message->header.type() - 1;
        message->header.set_src_sharding_id(message->header.type());
        message->header.set_type(type);
        message->header.set_hash64(message->header.hash64() + 1);
        std::string str_msg;
        message->header.SerializeToString(&str_msg);
        message->conn->Send(str_msg);
    };

    net_handler.Start();
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
            "127.0.0.1", 8990, msg);
    }

    char stop;
    std::cin >> stop;
    transport::TcpTransport::Instance()->Stop();
    return 0;
}