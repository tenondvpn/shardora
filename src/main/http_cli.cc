#include "http/http_client.h"

#include "common/string_utils.h"

using namespace zjchain;
http::HttpClient cli;
const char* peer_ip = "";

static evhtp_res print_data(evhtp_request_t* req, evbuf_t* buf, void* arg)
{
//     auto avail = evbuffer_get_length(buf);
//     if (evhtp_unlikely(avail == 0)) {
//         return EVHTP_RES_ERROR;
//     }
// 
//     char* resbuf = (char*)evbuffer_pullup(buf, avail);
//     if (evhtp_unlikely(resbuf == nullptr)) {
//         return EVHTP_RES_ERROR;
//     }
// 
//     resbuf[avail] = '\0';
    cli.Request(peer_ip, 8080, "hello_http_server", print_data);
    return EVHTP_RES_OK;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "invalid params." << std::endl;
        return 1;
    }

    peer_ip = argv[1];
    int32_t test_count = 1000;
    if (argc > 2) {
        if (!common::StringUtil::ToInt32(argv[2], &test_count)) {
            std::cout << "invalid test_count" << std::endl;
            return 1;
        }
    }

    {
        cli.Request(peer_ip, 8080, "hello_http_server", print_data);
        std::cout << "over " << argv[1] << ":" << argv[2] << ", " << test_count << std::endl;
        char stop;
        std::cin >> stop;
    }
    
    return 0;
}