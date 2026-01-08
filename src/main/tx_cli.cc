#include <common/encode.h>
#include <iostream>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "nlohmann/json.hpp"

#include "common/random.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "db/db.h"
#include "dht/dht_key.h"
// #include "http/http_client.h"
#include "pools/tx_utils.h"
#include "protos/address.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"
#include "api.h"

using namespace shardora;
static bool global_stop = false;
static const std::string kBroadcastIp = "127.0.0.1";
static const uint16_t kBroadcastPort = 13001;
static int shardnum = 3;
static const int delayus = 0;
static const bool multi_pool = true;
static const std::string db_path = "./txclidb";
static const std::string from_prikey =
    "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848";
static std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> net_pool_sk_map = {
    {3, {{15, "b5039128131f96f6164a33bc7fbc48c2f5cf425e8476b1c4d0f4d186fbd0d708"},
         {9, "580bb274af80b8d39b33f25ddbc911b14a1b3a2a6ec8ca376ffe9661cf809d36"}}},
    {4, {{15, "ed8aa75374998a6fb20139171e570ae67ceb34817b87b05400023ff9f1e06532"},
         {12, "c2e8fb3673f82cadd860d7523c12e71a7279faec0814803e547286bb0363d0e8"}}}
};

// http::HttpClient cli;
std::mutex cli_mutex;
std::condition_variable cli_con;
std::string global_chain_node_ip = "127.0.0.1";
std::unordered_map<std::string, uint64_t> prikey_with_nonce;
std::unordered_map<std::string, uint64_t> src_prikey_with_nonce;
uint64_t batch_nonce_check_count = 10240;
static uint32_t kThreadCount = 16u;
std::map<std::string, std::shared_ptr<nlohmann::json>> account_info_jsons;


void UpdateAddressNonce();
void UpdateAddressNonceThread() {
    while (!global_stop) {
        UpdateAddressNonce();
        usleep(3000000);
    }
}
static void SignalCallback(int sig_int) { global_stop = true; }

static const std::string get_from_prikey(uint32_t net_id, int32_t pool_id) {
    if (pool_id == -1) {
        return from_prikey;
    }
    return net_pool_sk_map[net_id][pool_id];
}

void SignalRegister() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    signal(SIGINT, SignalCallback);
    signal(SIGTERM, SignalCallback);

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
#endif
}

static void WriteDefaultLogConf() {
    spdlog::init_thread_pool(8192, 1);

    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "async_file", "log/shardora.log", true);
    // auto logger = spdlog::basic_logger_mt("sync_file", "log/shardora.log", false);
    spdlog::set_default_logger(logger);

    // 关键：强制设置全局 pattern
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");

    // 额外保险：遍历所有 sink 重新设置（防止被覆盖）
    for (auto& sink : logger->sinks()) {
        sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");
    }

    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::err);

    spdlog::debug("init spdlog success: %d", 1);
}

static transport::MessagePtr CreateTransactionWithAttr(
        std::shared_ptr<security::Security>& security,
        uint64_t nonce,
        const std::string& from_prikey,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    // auto* brd = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(security->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }
/*
    std::cout << " tx nonce: " << nonce << std::endl
        << "tx from: " << common::Encode::HexEncode(security->GetAddress()) << std::endl
        << "tx pukey: " << common::Encode::HexEncode(new_tx->pubkey()) << std::endl
        << "tx to: " << common::Encode::HexEncode(new_tx->to()) << std::endl
        << "tx hash: " << common::Encode::HexEncode(tx_hash) << std::endl
        << "tx sign: " << common::Encode::HexEncode(sign) << std::endl
        << "tx sign v: " << (char)sign[64] << std::endl
        << "amount: " << amount << std::endl
        << "gas_limit: " << gas_limit << std::endl
        << std::endl;
*/
    new_tx->set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}

static transport::MessagePtr GmsslCreateTransactionWithAttr(
        security::GmSsl& gmssl,
        uint64_t nonce,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    // auto* brd = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(gmssl.GetPublicKey());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (gmssl.Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

    std::cout << " tx nonce: " << nonce << std::endl
        << "tx pukey: " << common::Encode::HexEncode(new_tx->pubkey()) << std::endl
        << "tx to: " << common::Encode::HexEncode(new_tx->to()) << std::endl
        << "tx hash: " << common::Encode::HexEncode(tx_hash) << std::endl
        << "tx sign: " << common::Encode::HexEncode(sign) << std::endl
        << "hash64: " << msg.hash64() << std::endl
        << "amount: " << amount << std::endl
        << "gas_limit: " << gas_limit << std::endl
        << std::endl;
    new_tx->set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}

static transport::MessagePtr OqsCreateTransactionWithAttr(
        security::Oqs& oqs,
        uint64_t nonce,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    // auto* brd = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(oqs.GetPublicKey());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (oqs.Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

    std::cout << " tx nonce: " << nonce << std::endl
        << "tx pukey: " << common::Encode::HexEncode(new_tx->pubkey()) << std::endl
        << "tx to: " << common::Encode::HexEncode(new_tx->to()) << std::endl
        << "tx hash: " << common::Encode::HexEncode(tx_hash) << std::endl
        << "tx sign: " << common::Encode::HexEncode(sign) << std::endl
        << "hash64: " << msg.hash64() << std::endl
        << "amount: " << amount << std::endl
        << "gas_limit: " << gas_limit << std::endl
        << std::endl;
    new_tx->set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}

static std::unordered_map<std::string, std::string> g_pri_addrs_map;
static std::vector<std::string> g_prikeys;
static std::vector<std::string> g_addrs;
static std::unordered_map<std::string, std::string> g_pri_pub_map;
static std::vector<std::string> g_oqs_prikeys;
static std::unordered_map<std::string, std::string> g_oqs_pri_pub_map;
static void LoadAllAccounts(int32_t shardnum=3) {
    FILE* fd = fopen((std::string("../init_accounts") + std::to_string(shardnum)).c_str(), "r");
    if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    bool res = true;
    std::string filed;
    const uint32_t kMaxLen = 1024;
    char* read_buf = new char[kMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        std::string prikey = common::Encode::HexDecode(std::string(read_res, 64));
        g_prikeys.push_back(prikey);
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        g_pri_pub_map[prikey] = security->GetPublicKey();
        std::string addr = security->GetAddress();
        g_pri_addrs_map[prikey] = addr;
        g_addrs.push_back(addr);
        if (g_pri_addrs_map.size() >= common::kImmutablePoolSize) {
            break;
        }
        std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(addr) << std::endl;
    }

    assert(!g_prikeys.empty());
    while (g_prikeys.size() < common::kImmutablePoolSize) {
        g_prikeys.push_back(g_prikeys[0]);
    }

    fclose(fd);
    delete[]read_buf;
}

static void GetOqsKeys() {
    FILE* fd = fopen((std::string("../oqs_addrs")).c_str(), "r");
    if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    bool res = true;
    std::string filed;
    const uint32_t kMaxLen = 102400;
    char* read_buf = new char[kMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        auto line_splits = common::Split<>(read_res, '\n');
        for (int32_t i = 0; i < line_splits.Count(); ++i) {
            auto item_split = common::Split<>(line_splits[i], '\t');
            if (item_split.Count() != 2) {
                break;
            }

            std::string prikey = common::Encode::HexDecode(item_split[0]);
            g_oqs_prikeys.push_back(prikey);
            g_oqs_pri_pub_map[prikey] = common::Encode::HexDecode(item_split[1]);
            if (g_oqs_prikeys.size() >= common::kImmutablePoolSize) {
                break;
            }

            std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(g_oqs_pri_pub_map[prikey]) << std::endl;
        }
    }

    assert(!g_oqs_prikeys.empty());
    while (g_oqs_prikeys.size() < common::kImmutablePoolSize) {
        g_oqs_prikeys.push_back(g_oqs_prikeys[0]);
    }

    fclose(fd);
    delete[]read_buf;
}

int tx_main(int argc, char** argv) {
    // ./txcli 0 $net_id $pool_id $ip $port $delay_us $multi_pool
    int32_t pool_id = -1;
    auto ip = kBroadcastIp;
    auto port = kBroadcastPort;
    auto delayus_a = delayus;
    auto multi = multi_pool;
    if (argc >= 4) {
        shardnum = std::stoi(argv[2]);
        pool_id = std::stoi(argv[3]);
    }

    if (argc >= 6) {
        ip = argv[4];
        global_chain_node_ip = ip;
        port = std::stoi(argv[5]);
    }

    if (argc >= 7) {
        delayus_a = std::stoi(argv[6]);
    }

    if (argc >= 8) {
        multi = std::stoi(argv[7]);
    }

    std::cout << "send tcp client ip_port" << ip << ": " << port << ", pool_id: " << pool_id << std::endl;

    LoadAllAccounts(shardnum);
    SignalRegister();
    WriteDefaultLogConf();
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init(db_path + "_" + std::to_string(shardnum) + "_" + std::to_string(pool_id))) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    if (net_handler.Init(db_ptr, security) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    UpdateAddressNonce();
    std::atomic<uint32_t> all_count = 0;
    prikey_with_nonce  = src_prikey_with_nonce;
    auto update_nonce_thread = [&]() {
        UpdateAddressNonceThread();
    };

    const std::string key = "";
    const std::string value = "";
    auto tx_thread = [&](uint32_t begin_idx, uint32_t end_idx) {
        std::cout << "begin: " << begin_idx << ", end: " << end_idx << ", all: " << g_prikeys.size() << std::endl;
        std::string to = common::Encode::HexDecode("27d4c39244f26c157b5a87898569ef4ce5807413");
        uint32_t prikey_pos = begin_idx;
        auto from_prikey = g_prikeys[begin_idx];;
        std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
        thread_security->SetPrivateKey(from_prikey);
        uint32_t count = 0;
        uint32_t batch_count = 1000;
        while (!global_stop) {
            if (count % batch_count == 0) {
                if (pool_id == -1) {
                    ++prikey_pos;
                    if (prikey_pos >= end_idx) {
                        prikey_pos = begin_idx;
                    }

                    from_prikey = g_prikeys[prikey_pos];
                    thread_security->SetPrivateKey(from_prikey);
                    uint64_t nonce = src_prikey_with_nonce[from_prikey];
                    if (nonce + 10000 <= prikey_with_nonce[from_prikey]) {
                        printf("update address nonce: %s, now: %lu, chain: %lu\n",
                            common::Encode::HexEncode(thread_security->GetAddress()).c_str(),
                            prikey_with_nonce[from_prikey],
                            nonce);
                        prikey_with_nonce[from_prikey] = nonce;
                    }
                }
                usleep(100000lu);
            }

            auto tx_msg_ptr = CreateTransactionWithAttr(
                thread_security,
                ++prikey_with_nonce[from_prikey],
                from_prikey,
                to,
                key,
                value,
                1980,
                10000,
                1,
                shardnum);
            if (transport::TcpTransport::Instance()->Send(ip, port, tx_msg_ptr->header) != 0) {
                std::cout << "send tcp client failed!" << std::endl;
                return 1;
            }

            count++;
            ++all_count;
        }
    };

    std::vector<std::thread> thread_vec;
    if (pool_id == -1) {
        uint32_t each_thread_size = g_prikeys.size() / kThreadCount;
        for (uint32_t i = 0; i < kThreadCount; ++i) {
            thread_vec.push_back(std::thread(tx_thread, i * each_thread_size, (i + 1) * each_thread_size));
        }
    } else {
        kThreadCount = 1;
        for (uint32_t i = 0; i < g_prikeys.size(); ++i) {
            auto from_prikey = g_prikeys[i];
            std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
            thread_security->SetPrivateKey(from_prikey);
            if (common::GetAddressPoolIndex(thread_security->GetAddress()) == pool_id) {
                thread_vec.push_back(std::thread(tx_thread, i, i + 1));
                break;
            }
        }
    }

    auto tps_thread = [&]() {
        uint64_t now_tm_us = common::TimeUtils::TimestampUs();
        while (!global_stop) {
            auto dur = common::TimeUtils::TimestampUs() - now_tm_us;
            if (dur >= 3000000lu) {
                auto tps = all_count * 1000000lu / dur;
                std::cout << "tps: " << tps << std::endl;
                now_tm_us = common::TimeUtils::TimestampUs();
                all_count.exchange(0);
            }
        }
    };

    thread_vec.push_back(std::thread(tps_thread));
    thread_vec.push_back(std::thread(update_nonce_thread));
    for (uint32_t i = 0; i < kThreadCount; ++i) {
        thread_vec[i].join();
    }

    return 0;
}

int gmssl_tx(const std::string& private_key, const std::string& to, uint64_t amount) {
    LoadAllAccounts(shardnum);
    SignalRegister();
    WriteDefaultLogConf();
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("gmssl.db")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("txcli_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr, security) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    security::GmSsl gmssl;
    gmssl.SetPrivateKey(private_key);
    std::cout << "gmssl address: " << common::Encode::HexEncode(gmssl.GetAddress()) <<
        ", pk: " << common::Encode::HexEncode(gmssl.GetPublicKey()) << std::endl;
    auto test_hash = common::Random::RandomString(32);
    std::string test_sign;
    auto sign_res = gmssl.Sign(test_hash, &test_sign);
    assert(sign_res == 0);
    int verify_res = gmssl.Verify(test_hash, gmssl.GetPublicKey(), test_sign);
    std::cout << "test sign: " << common::Encode::HexEncode(test_sign)
        << ", verify res: " << verify_res << std::endl;

    auto tx_msg_ptr = GmsslCreateTransactionWithAttr(
        gmssl,
        0,
        to,
        "",
        "",
        amount,
        10000,
        1,
        3);


    if (transport::TcpTransport::Instance()->Send("127.0.0.1", 13001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "send success." << std::endl;
    return 0;
}

int oqs_tx(const std::string& to, uint64_t amount) {
    GetOqsKeys();
    SignalRegister();
    WriteDefaultLogConf();
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("oqs.db")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("txcli_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr, security) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    security::Oqs oqs;
    auto private_key = g_oqs_prikeys[0];
    auto public_key = g_oqs_pri_pub_map[private_key];
    oqs.SetPrivateKey(private_key, public_key);
    std::cout << "oqs address: " << common::Encode::HexEncode(oqs.GetAddress()) <<
        ", pk: " << common::Encode::HexEncode(oqs.GetPublicKey()) << std::endl;
    auto test_hash = common::Random::RandomString(32);
    std::string test_sign;
    auto sign_res = oqs.Sign(test_hash, &test_sign);
    assert(sign_res == 0);
    int verify_res = oqs.Verify(test_hash, oqs.GetPublicKey(), test_sign);
    std::cout << "test sign: " << common::Encode::HexEncode(test_sign)
        << ", verify res: " << verify_res << std::endl;

    auto tx_msg_ptr = OqsCreateTransactionWithAttr(
        oqs,
        0,
        to,
        "",
        "",
        amount,
        10000,
        1,
        3);


    if (transport::TcpTransport::Instance()->Send("127.0.0.1", 13001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "send success." << std::endl;
}

void UpdateAddressNonce(const std::string& contract_address = "") {
    ShardoraSDK client(kBroadcastIp);
    for (auto iter = g_prikeys.begin(); iter != g_prikeys.end(); ++iter) {
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(*iter);
        auto addr = security->GetAddress();
        if (contract_address.empty()) {
            addr = contract_address + addr;
        }

        int64_t nonce = client.fetchNonce(common::Encode::HexEncode(addr));
        if (nonce <= -1) {
            continue;
        }

        src_prikey_with_nonce[*iter] = nonce;
    }
}

int InitPrepayment(const std::string& contract_address) {
    ShardoraSDK client(kBroadcastIp);
    for (auto iter = g_prikeys.begin(); iter != g_prikeys.end(); ++iter) {
        auto prikey = common::Encode::HexEncode(*iter);
        auto res_json = client.setGasPrepayment(prikey, contract_address, 9000000000lu);
        if (res_json["status"] != 0) {
            std::cout << "set prepayment failed: " << contract_address << ", " << prikey << ", " << res_json.dump() << std::endl;
            return -1;
        }
    }

    return 0;
}

int call_bentchmark(int argc, char** argv) {
    // ./txcli 0 $net_id $pool_id $ip $port $delay_us $multi_pool
    int32_t pool_id = -1;
    auto ip = kBroadcastIp;
    auto port = kBroadcastPort;
    std::string to = "";
    std::string input = "";
    if (argc >= 3) {
        to = argv[2];
    }

    if (argc >= 4) {
        input = argv[3];
    }

    if (to.empty()) {
        std::cout << "to is empty" << std::endl;
        return -1;
    }

    if (argc >= 6) {
        shardnum = std::stoi(argv[4]);
        pool_id = std::stoi(argv[5]);
    }

    if (argc >= 8) {
        ip = argv[6];
        global_chain_node_ip = ip;
        port = std::stoi(argv[7]);
    }

    std::cout << "send tcp client ip_port" << ip << ": " << port << ", pool_id: " << pool_id << std::endl;
    LoadAllAccounts(shardnum);
    SignalRegister();
    WriteDefaultLogConf();
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init(db_path + "_" + std::to_string(shardnum) + "_" + std::to_string(pool_id))) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    if (net_handler.Init(db_ptr, security) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    UpdateAddressNonce();
    if (InitPrepayment(to) != 0) {
        return -1;
    }

    std::atomic<uint32_t> all_count = 0;
    prikey_with_nonce  = src_prikey_with_nonce;
    UpdateAddressNonce();
    UpdateAddressNonce(common::Encode::HexDecode(to));
    auto update_nonce_thread = [&]() {
        // UpdateAddressNonceThread();
    };

    auto tx_thread = [&](uint32_t begin_idx, uint32_t end_idx) {
        std::cout << "begin: " << begin_idx << ", end: " << end_idx << ", all: " << g_prikeys.size() << std::endl;
        uint32_t prikey_pos = begin_idx;
        auto from_prikey = g_prikeys[begin_idx];;
        std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
        thread_security->SetPrivateKey(from_prikey);
        uint32_t count = 0;
        uint32_t batch_count = 1;
        while (!global_stop) {
            if (count % batch_count == 0) {
                if (pool_id == -1) {
                    ++prikey_pos;
                    if (prikey_pos >= end_idx) {
                        prikey_pos = begin_idx;
                    }

                    from_prikey = g_prikeys[prikey_pos];
                    thread_security->SetPrivateKey(from_prikey);
                    uint64_t nonce = src_prikey_with_nonce[from_prikey];
                    if (nonce + 10000 <= prikey_with_nonce[from_prikey]) {
                        printf("update address nonce: %s, now: %lu, chain: %lu\n",
                            common::Encode::HexEncode(thread_security->GetAddress()).c_str(),
                            prikey_with_nonce[from_prikey],
                            nonce);
                        prikey_with_nonce[from_prikey] = nonce;
                    }
                }
                usleep(100000lu);
            }

            auto addr = common::Encode::HexDecode(to) + thread_security->GetAddress();
            auto tx_msg_ptr = CreateTransactionWithAttr(
                thread_security,
                ++prikey_with_nonce[addr],
                from_prikey,
                common::Encode::HexDecode(to),
                "call",
                input,
                0,
                100000000lu,
                1,
                shardnum);
            if (transport::TcpTransport::Instance()->Send(ip, port, tx_msg_ptr->header) != 0) {
                std::cout << "send tcp client failed!" << std::endl;
                return 1;
            }

            count++;
            ++all_count;
        }
    };

    std::vector<std::thread> thread_vec;
    if (pool_id == -1) {
        uint32_t each_thread_size = g_prikeys.size() / kThreadCount;
        for (uint32_t i = 0; i < kThreadCount; ++i) {
            thread_vec.push_back(std::thread(tx_thread, i * each_thread_size, (i + 1) * each_thread_size));
        }
    } else {
        kThreadCount = 1;
        for (uint32_t i = 0; i < g_prikeys.size(); ++i) {
            auto from_prikey = g_prikeys[i];
            std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
            thread_security->SetPrivateKey(from_prikey);
            if (common::GetAddressPoolIndex(thread_security->GetAddress()) == pool_id) {
                thread_vec.push_back(std::thread(tx_thread, i, i + 1));
                break;
            }
        }
    }

    auto tps_thread = [&]() {
        uint64_t now_tm_us = common::TimeUtils::TimestampUs();
        while (!global_stop) {
            auto dur = common::TimeUtils::TimestampUs() - now_tm_us;
            if (dur >= 3000000lu) {
                auto tps = all_count * 1000000lu / dur;
                std::cout << "tps: " << tps << std::endl;
                now_tm_us = common::TimeUtils::TimestampUs();
                all_count.exchange(0);
            }
        }
    };

    thread_vec.push_back(std::thread(tps_thread));
    thread_vec.push_back(std::thread(update_nonce_thread));
    for (uint32_t i = 0; i < kThreadCount; ++i) {
        thread_vec[i].join();
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argv[1][0] == '0') {
        tx_main(argc, argv);
        transport::TcpTransport::Instance()->Stop();
        usleep(1000000);
        return 0;
    }

    if (argv[1][0] == '1') {
        call_bentchmark(argc, argv);
        transport::TcpTransport::Instance()->Stop();
        usleep(1000000);
        return 0;
    }

    return 0;
}

