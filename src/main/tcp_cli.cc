#include <vector>

#include "common/string_utils.h"
#include "tcp/tcp_client.h"

using namespace zjchain;
using namespace zjchain::tcp;

int main(int argc, char* argv[]) {
    int32_t kThreadCount = 10;
    std::string peer_ip = "127.0.0.1";
    if (argc > 1) {
        peer_ip = argv[1];
    }

    if (argc > 2) {
        if (!common::StringUtil::ToInt32(argv[2], &kThreadCount)) {
            std::cout << "invalid thread count" << std::endl;
            return 1;
        }
    }

    int64_t sleep_sec = 30ll;
    if (argc > 3) {
        if (!common::StringUtil::ToInt64(argv[3], &sleep_sec)) {
            std::cout << "invalid sleep_sec" << std::endl;
            return 1;
        }
    }

    std::cout << peer_ip << ", " << kThreadCount << std::endl;
    if (kThreadCount > 300) {
        std::cout << "invalid thread count" << std::endl;
        return 1;
    }

    uint32_t thread_arr1[kThreadCount] = { 0 };
    std::vector<std::thread> thread_vec;
    TcpClient::InitClientLoop();
    TcpClient* clients[kThreadCount];
    for (int i = 0; i < kThreadCount; ++i) {
        clients[i] = new TcpClient("", 0);
    }

    TcpClientCallback callbacks[kThreadCount];
    TcpClientEventCallback ev_callbacks[kThreadCount];
    std::string test_str = "hello world.";
    int32_t send_size = test_str.size() + 4;
    for (int32_t i = 0; i < kThreadCount; ++i) {
        callbacks[i] = [&clients, test_str, i, &thread_arr1](
                TcpClient* c, const char* data, int32_t len) ->int32_t {
            clients[i]->Send(test_str);
            ++thread_arr1[i];
            return 0;
        };

        ev_callbacks[i] = [i](TcpClient* c, int32_t event)->int32_t {
            std::cout << "thread i: " << i << ", get event: " << event << std::endl;
            return 0;
        };
    }

    for (int32_t i = 0; i < kThreadCount; ++i) {
        int res = clients[i]->Connect(peer_ip.c_str(), 9090, ev_callbacks[i], callbacks[i]);
        if (res != 0) {
            std::cout << "connect server failed!" << std::endl;
            return 1;
        }
    }

    for (int32_t i = 0; i < kThreadCount; ++i) {
        clients[i]->Send(test_str);
    }

    std::this_thread::sleep_for(std::chrono::microseconds(sleep_sec * 1000000ull));
    TcpClient::StopClientLoop();
    for (int32_t i = 0; i < kThreadCount; ++i) {
        std::cout << thread_arr1[i] << ", ";
    }

    for (int i = 0; i < kThreadCount; ++i) {
        delete clients[i];
    }
    std::cout << std::endl;
    return 0;
}