#include <atomic>
#include <mutex>

#include "common/string_utils.h"
#include "common/time_utils.h"
#include "tcp/tcp_server.h"

using namespace zjchain;
using namespace zjchain::tcp;

int main(int argc, char* argv[]) {
    TcpServer tcp_server;
    std::atomic<uint32_t> recv_count = 0;
    std::mutex test_m;
    auto b_time = common::TimeUtils::TimestampMs();
    auto callback = [&](TcpConnection* con) ->int32_t {
        if (recv_count >= 1000000)
        {
            std::lock_guard<std::mutex> g(test_m);
            if (recv_count >= 1000000) {
                auto e_time = common::TimeUtils::TimestampMs();
                std::cout << "qps: " << (float(recv_count) / float(float(e_time - b_time) / 1000.0f)) << std::endl;
                recv_count = 0;
                b_time = e_time;
            }
        }

        ++recv_count;
        return tcp_server.Send(con, con->recv_buff, con->index);
    };

    int32_t thread_count = 4;
    if (argc > 1) {
        if (!common::StringUtil::ToInt32(argv[1], &thread_count)) {
            std::cout << "invalid thread count: " << argv[1] << std::endl;
            return 1;
        }
    }

    int st = tcp_server.Init(thread_count, 3000, "0.0.0.0", 9090, callback, nullptr);
    if (st != 0) {
        std::cout << "init server failed: " << st << std::endl;
        return 1;
    }

    tcp_server.Start();
    char out;
    std::cin >> out;
    return 0;
}