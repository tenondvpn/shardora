#include <common/encode.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <queue>
#include <set>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "nlohmann/json.hpp"
#include "common/defer.h"
#include "common/random.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "db/db.h"
#include "dht/dht_key.h"
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
static const std::string kBroadcastIp = "10.10.1.115";
static const uint16_t kBroadcastPort = 13001;
static int shardnum = 3;
static const int delayus = 0;
static const bool multi_pool = true;
static const std::string db_path = "./txclidb";

// http::HttpClient cli;
std::mutex cli_mutex;
std::condition_variable cli_con;
std::string global_chain_node_ip = "10.10.1.115";
uint16_t global_chain_node_http_port = 23001;
std::unordered_map<std::string, uint64_t> prikey_with_nonce;
std::unordered_map<std::string, uint64_t> src_prikey_with_nonce;
uint64_t batch_nonce_check_count = 10240;
static uint32_t kThreadCount = 16u;
int32_t global_pool_idx = -1;
std::map<std::string, std::shared_ptr<nlohmann::json>> account_info_jsons;

std::mutex upadte_nonce_mutex;
std::condition_variable update_nonce_con;

// Global leader routing for nonce updates
std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> g_leader_map;
std::mutex g_leader_mutex;
bool g_has_leader_routing = false;

void UpdateAddressNonce();
void UpdateAddressNonce(const std::string& addr);
void UpdateAddressNonceThread() {
    while (!global_stop) {
        UpdateAddressNonce();
        std::unique_lock<std::mutex> lock(upadte_nonce_mutex);
        update_nonce_con.wait_for(lock, std::chrono::milliseconds(15000));
    }
}
static void SignalCallback(int sig_int) { global_stop = true; }

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

    // Critical: Force set global pattern
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");

    // Extra insurance: Iterate through all sinks and reset (prevent override)
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
            new_tx->set_step(pools::protobuf::kCreateContract);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prefund(490000000lu);
        } else if (key == "prefund") {
            new_tx->set_step(pools::protobuf::kContractGasPrefund);
            new_tx->set_contract_prefund(490000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(common::Encode::HexDecode(val));
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
        //assert(false);
        return nullptr;
    }

    // std::cout << " tx nonce: " << nonce << std::endl
    //     << "tx from: " << common::Encode::HexEncode(security->GetAddress()) << std::endl
    //     << "tx pukey: " << common::Encode::HexEncode(new_tx->pubkey()) << std::endl
    //     << "tx to: " << common::Encode::HexEncode(new_tx->to()) << std::endl
    //     << "tx hash: " << common::Encode::HexEncode(tx_hash) << std::endl
    //     << "tx sign: " << common::Encode::HexEncode(sign) << std::endl
    //     << "tx sign v: " << (char)sign[64] << std::endl
    //     << "amount: " << amount << std::endl
    //     << "gas_limit: " << gas_limit << std::endl
    //     << std::endl;
    new_tx->set_sign(sign);
    //assert(new_tx->gas_price() > 0);
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
        fd = fopen((std::string("./init_accounts") + std::to_string(shardnum)).c_str(), "r");
	if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
	}
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
        // if (g_pri_addrs_map.size() >= common::kImmutablePoolSize) {
        //     break;
        // }
        std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(addr) << std::endl;
    }

    //assert(!g_prikeys.empty());
    while (g_prikeys.size() < common::kImmutablePoolSize) {
        g_prikeys.push_back(g_prikeys[0]);
    }

    fclose(fd);
    delete[]read_buf;
}

int tx_main(int argc, char** argv) {
    // ./txcli 0 $net_id $pool_id $ip $port $delay_us $multi_pool [$tps] [$max_tx_count]
    auto ip = kBroadcastIp;
    auto port = kBroadcastPort;
    auto delayus_a = delayus;
    auto multi = multi_pool;
    uint32_t target_tps = 0;  // 0 = unlimited
    uint64_t max_tx_count = 0;  // 0 = run until Ctrl+C

    if (argc >= 4) {
        shardnum = std::stoi(argv[2]);
        global_pool_idx = std::stoi(argv[3]);
    }

    if (argc >= 6) {
        ip = argv[4];
        global_chain_node_ip = ip;
        port = std::stoi(argv[5]);
        global_chain_node_http_port = port + 10000;
    }

    if (argc >= 7) {
        delayus_a = std::stoi(argv[6]);
    }

    if (argc >= 8) {
        multi = std::stoi(argv[7]);
    }

    if (argc >= 9) {
        target_tps = std::stoi(argv[8]);
    }

    if (argc >= 10) {
        max_tx_count = std::stoull(argv[9]);
    }

    std::cout << "send tcp client ip_port" << ip << ": " << port << ", pool_id: " << global_pool_idx << std::endl;
    if (target_tps > 0) {
        std::cout << "Target TPS: " << target_tps << std::endl;
    } else {
        std::cout << "Target TPS: unlimited" << std::endl;
    }
    if (max_tx_count > 0) {
        std::cout << "Max tx count: " << max_tx_count << " (stop when reached)" << std::endl;
    }

    LoadAllAccounts(shardnum);
    SignalRegister();
    WriteDefaultLogConf();
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init(db_path + "_" + std::to_string(shardnum) + "_" + std::to_string(global_pool_idx))) {
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
    std::atomic<uint64_t> sent_total = 0;
    prikey_with_nonce  = src_prikey_with_nonce;
    auto update_nonce_thread = [&]() {
        UpdateAddressNonceThread();
    };

    // Fetch leader routing table
    ShardoraSDK sdk(global_chain_node_ip, global_chain_node_http_port);
    std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> leader_map;
    uint32_t leader_count = 0;
    bool has_leader_routing = sdk.fetchLeaders(leader_map, leader_count);
    std::mutex leader_mutex;  // Protect leader_map access
    
    if (has_leader_routing) {
        std::cout << "Leader routing enabled: " << leader_count << " leaders" << std::endl;
        for (auto& [mod, info] : leader_map) {
            std::cout << "  pool " << mod << " -> " << info.ip << ":" << info.port << std::endl;
        }
        
        // Initialize global leader map for nonce updates
        std::lock_guard<std::mutex> lock(g_leader_mutex);
        g_leader_map = leader_map;
        g_has_leader_routing = true;
    } else {
        std::cout << "Leader routing unavailable, using default node" << std::endl;
    }

    const std::string key = "";
    const std::string value = "";
    
    // Compute per-thread sleep interval to achieve target TPS
    // interval_us = kThreadCount * 1000000 / target_tps
    uint64_t tps_interval_us = 0;  // 0 = no sleep (unlimited)
    if (target_tps > 0) {
        tps_interval_us = (uint64_t)kThreadCount * 1000000ULL / target_tps;
        if (tps_interval_us == 0) tps_interval_us = 1;
        std::cout << "TPS interval: " << tps_interval_us << "us/thread ("
                  << kThreadCount << " threads)" << std::endl;
    }
    
    auto tx_thread = [&](std::vector<std::string> prikeys) {
        uint32_t prikey_pos = 0;
        auto from_prikey = prikeys[0];
        std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
        thread_security->SetPrivateKey(from_prikey);
        uint32_t count = 0;
        uint32_t batch_count = 256;
        auto addr = thread_security->GetAddress();
        while (!global_stop) {
            if (max_tx_count > 0 && sent_total.load(std::memory_order_relaxed) >= max_tx_count) {
                break;
            }

            if (count % batch_count == 0 && count > 0) {
                if (global_pool_idx == -1) {
                    ++prikey_pos;
                    if (prikey_pos >= prikeys.size()) {
                        prikey_pos = 0;
                    }

                    from_prikey = prikeys[prikey_pos];
                    thread_security->SetPrivateKey(from_prikey);
                    addr = thread_security->GetAddress();
                }
                // Brief pause when rotating accounts to let nonces settle
                usleep(10000lu);
            }

            if (src_prikey_with_nonce[addr] + 2 * common::kMaxTxCount <= prikey_with_nonce[addr]) {
                usleep(2000000);
                update_nonce_con.notify_one();
                usleep(1000000);
                if (src_prikey_with_nonce[addr] + 2 * common::kMaxTxCount <= prikey_with_nonce[addr]) {
                    prikey_with_nonce[addr] = src_prikey_with_nonce[addr];
                    std::cout << "reset add nonce " << common::Encode::HexEncode(addr) << ":" << prikey_with_nonce[addr] << std::endl;
                    usleep(10000000);
                    continue;
                }
            }

            // Randomly select a 'to' address from g_addrs, ensuring it's different from 'from'
            std::string to;
            do {
                uint32_t random_idx = common::Random::RandomUint32() % g_addrs.size();
                to = g_addrs[random_idx];
            } while (to == addr && g_addrs.size() > 1);  // Avoid sending to self if there are other options

            auto tx_msg_ptr = CreateTransactionWithAttr(
                thread_security,
                ++prikey_with_nonce[addr],
                from_prikey,
                to,
                key,
                value,
                10,
                1000,
                1,
                shardnum);
            
            // Route to the leader responsible for the sender's pool
            std::string dest_ip = ip;
            uint16_t dest_port = port;
            if (has_leader_routing) {
                uint32_t pool_idx = common::GetAddressPoolIndex(addr);
                std::lock_guard<std::mutex> lock(leader_mutex);
                auto it = leader_map.find(pool_idx);
                if (it != leader_map.end()) {
                    dest_ip = it->second.ip;
                    dest_port = it->second.port;
                }
            }
            
            // Retry send up to 3 times on failure. On failure, roll back the nonce
            // so we don't create permanent nonce gaps that block all future txs.
            bool sent_ok = false;
            for (int retry = 0; retry < 3 && !global_stop; ++retry) {
                if (transport::TcpTransport::Instance()->Send(dest_ip, dest_port, tx_msg_ptr->header) == 0) {
                    sent_ok = true;
                    break;
                }
                std::cout << "send tcp client failed, retry " << (retry + 1) << "/3, addr: "
                          << common::Encode::HexEncode(addr) << ", nonce: " << prikey_with_nonce[addr] << std::endl;
                usleep(100000);  // 100ms between retries
            }

            if (!sent_ok) {
                // All retries failed �?roll back nonce to avoid permanent gap
                --prikey_with_nonce[addr];
                std::cout << "send failed after 3 retries, rolled back nonce to "
                          << prikey_with_nonce[addr] << " for addr: "
                          << common::Encode::HexEncode(addr) << std::endl;
                usleep(1000000);  // 1s cooldown before next attempt
                continue;
            }

            count++;
            ++all_count;
            if (max_tx_count > 0) {
                const uint64_t n = sent_total.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n >= max_tx_count) {
                    global_stop = true;
                }
            }
            if (tps_interval_us > 0) {
                usleep(tps_interval_us);
            }
        }
    };

    std::vector<std::thread> thread_vec;
    std::vector<std::string> all_valid_keys;
    kThreadCount = 4;
    for (uint32_t i = 0; i < g_prikeys.size(); ++i) {
        auto from_prikey = g_prikeys[i];
        std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
        thread_security->SetPrivateKey(from_prikey);
        if (common::GetAddressPoolIndex(thread_security->GetAddress()) == global_pool_idx) {
            all_valid_keys.push_back(from_prikey);
        }
    }

    if (all_valid_keys.empty()) {
        return 1;
    }

    uint32_t start = 0;
    uint32_t length = all_valid_keys.size() / kThreadCount;
    for (uint32_t i = 0; i < kThreadCount; ++i) {
        if (i == kThreadCount - 1) {
            length = all_valid_keys.size() - start;
        }

        std::vector<std::string> tmp_vec(all_valid_keys.begin() + start, all_valid_keys.begin() + start + length);
        thread_vec.push_back(std::thread(tx_thread, tmp_vec));
        start += length;
    }

    auto tps_thread = [&]() {
        uint64_t now_tm_us = common::TimeUtils::TimestampUs();
        while (!global_stop) {
            usleep(100000);  // Sleep 100ms to avoid busy-wait
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

    // Leader synchronization thread - refreshes every 3 seconds
    auto leader_sync_thread = [&]() {
        while (!global_stop) {
            // Sleep 3 seconds in 100ms chunks to allow quick exit
            for (int i = 0; i < 30 && !global_stop; ++i) {
                usleep(100000);  // 100ms
            }
            if (global_stop) break;
            
            std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> new_leaders;
            uint32_t new_count = 0;
            if (sdk.fetchLeaders(new_leaders, new_count) && !new_leaders.empty()) {
                // Update local leader map
                std::lock_guard<std::mutex> lock(leader_mutex);
                leader_map = new_leaders;
                leader_count = new_count;
                has_leader_routing = true;
                
                // Update global leader map for nonce updates
                {
                    std::lock_guard<std::mutex> g_lock(g_leader_mutex);
                    g_leader_map = new_leaders;
                    g_has_leader_routing = true;
                }
                
                std::cout << "[Leader Sync] Refreshed: " << new_count << " leaders" << std::endl;
            }
        }
    };
    thread_vec.push_back(std::thread(leader_sync_thread));

    // When Ctrl+C fires, global_stop becomes true but the nonce thread may be
    // sleeping in wait_for(15s).  Wake it so join() returns promptly.
    // We spin-wait briefly for all tx threads to notice global_stop, then kick
    // the nonce condvar.
    std::thread waker([&]() {
        while (!global_stop) {
            usleep(100000);
        }
        update_nonce_con.notify_all();
    });

    for (uint32_t i = 0; i < thread_vec.size(); ++i) {
        thread_vec[i].join();
    }
    waker.join();
    for (uint32_t i = 0; i < thread_vec.size(); ++i) {
        thread_vec[i].join();
    }

    // All worker threads have exited — safe to stop the transport now.
    transport::TcpTransport::Instance()->Stop();
    usleep(200000);
    if (max_tx_count > 0) {
        std::cout << "Stress test finished: sent " << sent_total.load()
                  << " / " << max_tx_count << std::endl;
    }
    return 0;
}

void UpdateAddressNonce() {
    std::string contract_address;
    UpdateAddressNonce(contract_address);
}

void UpdateAddressNonce(const std::string& contract_address) {
    for (auto iter = g_prikeys.begin(); iter != g_prikeys.end(); ++iter) {
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(*iter);
        auto addr = security->GetAddress();
        // Only filter by pool when a specific pool is requested.
        if (global_pool_idx != -1 &&
                common::GetAddressPoolIndex(addr) != (uint32_t)global_pool_idx) {
            continue;
        }

        if (!contract_address.empty()) {
            addr = contract_address + addr;
        }

        // Route nonce query to the leader of this account's pool
        std::string query_ip = global_chain_node_ip;
        uint16_t query_port = global_chain_node_http_port;
        
        if (g_has_leader_routing) {
            uint32_t pool_idx = common::GetAddressPoolIndex(addr);
            std::lock_guard<std::mutex> lock(g_leader_mutex);
            auto it = g_leader_map.find(pool_idx);
            if (it != g_leader_map.end()) {
                query_ip = it->second.ip;
                query_port = it->second.port + 10000;  // HTTP port = TCP port + 10000
            }
        }
        
        ShardoraSDK client(query_ip, query_port);

        // Retry up to 3 times on transient failures.
        int64_t nonce = -1;
        for (int retry = 0; retry < 3 && nonce < 0; ++retry) {
            nonce = client.fetchNonce(common::Encode::HexEncode(addr));
            if (nonce < 0 && retry < 2) {
                usleep(500000);
            }
        }

        if (nonce < 0) {
            std::cout << "fetch nonce failed for addr: "
                      << common::Encode::HexEncode(addr) << std::endl;
            continue;
        }

        src_prikey_with_nonce[addr] = nonce;
        std::cout << common::Encode::HexEncode(addr) << ", nonce: " << nonce << std::endl;
    }
}

int InitPrefund(const std::string& contract_address) {
    // Route prefund to each sender's pool leader (not the contract's),
    // because the server dispatches step 7 to the sender's pool_index.
    for (auto iter = g_prikeys.begin(); iter != g_prikeys.end(); ++iter) {
        std::string dest_ip = kBroadcastIp;
        int dest_port = kBroadcastPort + 10000;  // default HTTP port
        if (g_has_leader_routing) {
            std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
            sec->SetPrivateKey(*iter);
            uint32_t pool_idx = common::GetAddressPoolIndex(sec->GetAddress());
            std::lock_guard<std::mutex> lock(g_leader_mutex);
            auto it = g_leader_map.find(pool_idx);
            if (it != g_leader_map.end()) {
                dest_ip = it->second.ip;
                dest_port = it->second.port + 10000;
            }
        }
        ShardoraSDK client(dest_ip, dest_port);
        auto prikey = common::Encode::HexEncode(*iter);
        auto res_json = client.setGasPrefund(prikey, contract_address, 490000000lu);
        if (res_json["status"] != 0) {
            std::cout << "set prefund failed: " << contract_address << ", " << prikey << ", " << res_json.dump() << std::endl;
            return -1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argv[1][0] == '0') {
        tx_main(argc, argv);
        // Stop() is already called inside tx_main after all threads join.
        return 0;
    }

    // ── Mode 4: 10,000 Account Stress Test ────────────────────────────────
    // Usage: txcli 4 <shard> <pool> <ip> <port> [threads] [tps] [max_tx_count]
    if (argv[1][0] == '4') {
        const uint32_t kAccountCount = 10000;
        uint32_t num_threads = (argc >= 7) ? std::stoi(argv[6]) : 16;
        uint32_t target_tps  = (argc >= 8) ? std::stoi(argv[7]) : 0;  // 0 = unlimited
        uint64_t max_tx_count = (argc >= 9) ? std::stoull(argv[8]) : 0;  // 0 = run until Ctrl+C
        
        if (argc >= 4) {
            shardnum = std::stoi(argv[2]);
            global_pool_idx = std::stoi(argv[3]);
        }
        if (argc >= 6) {
            global_chain_node_ip = argv[4];
            global_chain_node_http_port = std::stoi(argv[5]) + 10000;
        }

        // Compute per-thread sleep interval (us) to achieve target TPS.
        // interval_us = num_threads * 1000000 / target_tps
        // 0 means no rate limiting (use the original 5ms delay).
        uint64_t tps_interval_us = 5000;  // default 5ms
        if (target_tps > 0) {
            tps_interval_us = (uint64_t)num_threads * 1000000ULL / target_tps;
            if (tps_interval_us == 0) tps_interval_us = 1;
        }

        std::cout << "\n=== 10,000 Account Stress Test ===" << std::endl;
        std::cout << "Shard: " << shardnum << ", Pool: " << global_pool_idx << std::endl;
        std::cout << "Node: " << global_chain_node_ip << ":" << (global_chain_node_http_port - 10000) << std::endl;
        std::cout << "Threads: " << num_threads << std::endl;
        if (target_tps > 0) {
            std::cout << "Target TPS: " << target_tps << " (interval=" << tps_interval_us << "us/thread)" << std::endl;
        } else {
            std::cout << "Target TPS: unlimited (interval=5000us/thread)" << std::endl;
        }
        if (max_tx_count > 0) {
            std::cout << "Max tx count: " << max_tx_count << " (stop when reached)" << std::endl;
        }

        LoadAllAccounts(shardnum);
        SignalRegister();
        WriteDefaultLogConf();

        // Setup transport
        transport::MultiThreadHandler net_handler;
        std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
        auto db_ptr = std::make_shared<db::Db>();
        if (!db_ptr->Init(db_path + "_stress_10k")) {
            std::cerr << "init db failed" << std::endl;
            return 1;
        }
        if (net_handler.Init(db_ptr, sec) != 0) {
            std::cerr << "init net handler failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Init("127.0.0.1:13793", 128, false, &net_handler) != 0) {
            std::cerr << "init tcp failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Start(false) != 0) {
            std::cerr << "start tcp failed" << std::endl;
            return 1;
        }

        // Phase 1: Generate 10,000 accounts
        std::cout << "\n[Phase 1] Generating " << kAccountCount << " accounts..." << std::endl;
        std::vector<std::string> test_prikeys;
        std::vector<std::string> test_addrs;
        std::unordered_map<std::string, std::string> test_pri_addr_map;

        for (uint32_t i = 0; i < kAccountCount; ++i) {
            // Generate random private key
            std::string prikey;
            prikey.resize(32);
            for (uint32_t j = 0; j < 32; ++j) {
                prikey[j] = static_cast<char>(common::Random::RandomUint32() % 256);
            }

            std::shared_ptr<security::Security> test_sec = std::make_shared<security::Ecdsa>();
            test_sec->SetPrivateKey(prikey);
            std::string addr = test_sec->GetAddress();

            test_prikeys.push_back(prikey);
            test_addrs.push_back(addr);
            test_pri_addr_map[prikey] = addr;

            if ((i + 1) % 1000 == 0) {
                std::cout << "  Generated " << (i + 1) << " accounts..." << std::endl;
            }
        }
        std::cout << "�?Generated " << kAccountCount << " accounts" << std::endl;

        // Phase 2: Create accounts on blockchain (send initial transactions)
        std::cout << "\n[Phase 2] Creating accounts on blockchain..." << std::endl;
        std::cout << "  Using " << g_prikeys.size() << " funded accounts to create test accounts..." << std::endl;

        ShardoraSDK sdk(global_chain_node_ip, global_chain_node_http_port);
        std::atomic<uint32_t> created_count{0};
        std::atomic<uint32_t> failed_count{0};

        // Limit thread count to number of funded accounts to avoid nonce collisions.
        // Each funder must be used by exactly one thread.
        uint32_t create_threads_count = std::min(num_threads, (uint32_t)g_prikeys.size());
        uint32_t accounts_per_create_thread = kAccountCount / create_threads_count;

        // Use existing funded accounts to send initial coins to test accounts
        auto create_account_thread = [&](uint32_t thread_id, uint32_t start_idx, uint32_t end_idx) {
            // Fix: Each thread creates its own ShardoraSDK instance.
            // httplib::SSLClient is NOT thread-safe — sharing one across threads
            // causes SIGSEGV in ensure_socket_connection.
            ShardoraSDK thread_sdk(global_chain_node_ip, global_chain_node_http_port);

            // Each thread gets a unique funder (thread_id < g_prikeys.size() guaranteed)
            std::string funder_prikey = g_prikeys[thread_id];
            std::shared_ptr<security::Security> funder_sec = std::make_shared<security::Ecdsa>();
            funder_sec->SetPrivateKey(funder_prikey);
            std::string funder_addr = funder_sec->GetAddress();

            // Get initial nonce
            int64_t nonce = thread_sdk.fetchNonce(common::Encode::HexEncode(funder_addr));
            if (nonce < 0) {
                std::cerr << "  Thread " << thread_id << ": Failed to fetch nonce for funder "
                          <<  common::Encode::HexEncode(funder_prikey) << " : " << common::Encode::HexEncode(funder_addr) << std::endl;
                failed_count += (end_idx - start_idx);
                return;
            }

            std::cout << "  Thread " << thread_id << ": funder="
                      << common::Encode::HexEncode(funder_addr) << "..."
                      << " nonce=" << nonce
                      << " accounts=[" << start_idx << "," << end_idx << ")" << std::endl;

            for (uint32_t i = start_idx; i < end_idx && !global_stop; ++i) {
                // Send 1000 coins to test account to create it on-chain
                auto tx_msg_ptr = CreateTransactionWithAttr(
                    funder_sec,
                    ++nonce,
                    funder_prikey,
                    test_addrs[i],
                    "",
                    "",
                    1000000000,  // Initial balance
                    210000,
                    1,
                    shardnum);

                if (tx_msg_ptr && transport::TcpTransport::Instance()->Send(
                        global_chain_node_ip, 
                        global_chain_node_http_port - 10000, 
                        tx_msg_ptr->header) == 0) {
                    ++created_count;
                    std::cout << "success send from: " << global_chain_node_ip << ":" << (global_chain_node_http_port - 10000) << ", from:" << common::Encode::HexEncode(funder_addr) << ", to:" << common::Encode::HexEncode(test_addrs[i]) << ", nonce: " << nonce << std::endl;
                } else {
                    ++failed_count;
                    std::cout << "failed send from: " << common::Encode::HexEncode(funder_addr) << ", to:" << common::Encode::HexEncode(test_addrs[i]) << ", nonce: " << nonce << std::endl;
                }

                // Rate limiting
                usleep(1000);  // 1ms delay
            }
        };

        std::vector<std::thread> create_threads;
        for (uint32_t t = 0; t < create_threads_count; ++t) {
            uint32_t start_idx = t * accounts_per_create_thread;
            uint32_t end_idx = (t == create_threads_count - 1) ? kAccountCount : (start_idx + accounts_per_create_thread);
            create_threads.emplace_back(create_account_thread, t, start_idx, end_idx);
            std::cout << "start create account thread " << t << ", " << start_idx << ", " << end_idx << std::endl;
        }

        // Progress monitor
        std::thread progress_thread([&]() {
            while (created_count + failed_count < kAccountCount && !global_stop) {
                // Sleep 2 seconds in 100ms chunks to allow quick exit
                for (int i = 0; i < 20 && !global_stop; ++i) {
                    usleep(100000);  // 100ms
                }
                if (global_stop) break;
                
                std::cout << "  Progress: " << created_count.load() << " created, " 
                          << failed_count.load() << " failed" << std::endl;
            }
        });

        for (auto& th : create_threads) {
            th.join();
        }
        progress_thread.join();

        std::cout << "�?Account creation complete: " << created_count.load() 
                  << " created, " << failed_count.load() << " failed" << std::endl;

        // Phase 3: Wait for accounts to be confirmed using batch query (up to 240s)
        // Strategy:
        //   1. Wait 10s upfront for consensus to process the creation txs.
        //   2. Batch-query ALL pending addresses in one shot (500 per HTTP call).
        //   3. Adaptive polling: if progress is being made, poll faster (2s);
        //      if no progress, back off (5s). This avoids hammering the node
        //      while accounts are still in the mempool.
        std::cout << "\n[Phase 3] Waiting 10s for consensus before batch verification..." << std::endl;
        for (int w = 0; w < 100 && !global_stop; ++w) usleep(100000);  // 10s in 100ms chunks

        std::cout << "[Phase 3] Starting batch account verification (up to 240s)..." << std::endl;
        uint32_t accounts_per_thread = kAccountCount / num_threads;
        auto phase3_start = std::chrono::steady_clock::now();
        const auto kPhase3Timeout = std::chrono::seconds(240);
        uint32_t confirmed_count = 0;
        const uint32_t kBatchSize = 500;

        // Track which accounts are still pending confirmation
        std::vector<bool> is_confirmed(kAccountCount, false);

        // Pending list: only query these indices each round.
        // Accounts not found are kept in the list for the next round.
        std::vector<uint32_t> pending_list;
        pending_list.reserve(kAccountCount);
        for (uint32_t i = 0; i < kAccountCount; ++i) {
            pending_list.push_back(i);
        }

        uint32_t round = 0;
        while (confirmed_count < kAccountCount && !pending_list.empty() && !global_stop) {
            auto elapsed = std::chrono::steady_clock::now() - phase3_start;
            if (elapsed >= kPhase3Timeout) {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                std::cout << "  Timeout reached (" << secs << "s). Confirmed " << confirmed_count
                          << "/" << kAccountCount << std::endl;
                break;
            }

            ++round;
            auto round_start = std::chrono::steady_clock::now();
            uint32_t round_confirmed = 0;

            // Next round's pending list �?accounts not found this round go here
            std::vector<uint32_t> next_pending;
            next_pending.reserve(pending_list.size());

            // Batch-query only the pending addresses
            std::vector<std::string> batch_addrs;
            std::vector<uint32_t> batch_indices;
            batch_addrs.reserve(kBatchSize);
            batch_indices.reserve(kBatchSize);

            for (uint32_t p = 0; p < pending_list.size() && !global_stop; ++p) {
                uint32_t i = pending_list[p];
                batch_addrs.push_back(common::Encode::HexEncode(test_addrs[i]));
                batch_indices.push_back(i);

                // When batch is full or last pending entry, fire the query
                bool is_last = (p == pending_list.size() - 1);
                if (batch_addrs.size() >= kBatchSize || is_last) {
                    auto batch_res = sdk.batchQueryAccounts(batch_addrs);
                    if (batch_res.contains("status") && batch_res["status"] == 0 &&
                        batch_res.contains("accounts")) {
                        for (uint32_t k = 0; k < batch_indices.size(); ++k) {
                            uint32_t idx = batch_indices[k];
                            const std::string& hex_addr = batch_addrs[k];
                            if (batch_res["accounts"].contains(hex_addr)) {
                                auto& acc = batch_res["accounts"][hex_addr];
                                int64_t nonce = 0;
                                if (acc.contains("nonce")) {
                                    auto nonce_str = acc["nonce"].get<std::string>();
                                    std::from_chars(nonce_str.data(),
                                                    nonce_str.data() + nonce_str.size(), nonce);
                                }
                                src_prikey_with_nonce[test_addrs[idx]] = nonce;
                                prikey_with_nonce[test_addrs[idx]] = nonce;
                                is_confirmed[idx] = true;
                                ++confirmed_count;
                                ++round_confirmed;
                            } else {
                                // Not found �?keep in pending for next round
                                next_pending.push_back(idx);
                            }
                        }
                    } else {
                        // Entire batch request failed �?keep all in pending
                        for (uint32_t k = 0; k < batch_indices.size(); ++k) {
                            next_pending.push_back(batch_indices[k]);
                        }
                    }
                    batch_addrs.clear();
                    batch_indices.clear();
                }
            }

            // Swap pending list for next round
            pending_list = std::move(next_pending);

            auto round_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - round_start).count();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - phase3_start).count();
            std::cout << "  [Round " << round << ", " << total_elapsed << "s] +"
                      << round_confirmed << " confirmed, "
                      << confirmed_count << "/" << kAccountCount << " total, "
                      << pending_list.size() << " pending (" << round_ms << "ms)" << std::endl;

            if (confirmed_count >= kAccountCount || pending_list.empty()) break;

            // Adaptive wait: if we made progress this round, poll again quickly (2s).
            // If no progress, back off to 5s to avoid wasting HTTP calls.
            uint32_t wait_ms = (round_confirmed > 0) ? 2000 : 5000;
            // On first round with zero progress, wait longer (8s) �?consensus may still be running
            if (round == 1 && round_confirmed == 0) wait_ms = 8000;
            for (uint32_t w = 0; w < wait_ms / 100 && !global_stop; ++w) usleep(100000);
        }

        auto total_secs = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - phase3_start).count();
        std::cout << "�?Account confirmation complete: " << confirmed_count
                  << "/" << kAccountCount << " confirmed in " << total_secs << "s" << std::endl;

        if (confirmed_count < kAccountCount) {
            uint32_t failed_total = kAccountCount - confirmed_count;
            std::cerr << "\nERROR: " << failed_total << " accounts failed to confirm:" << std::endl;
            uint32_t print_limit = std::min(failed_total, 20u);
            uint32_t printed = 0;
            for (uint32_t i = 0; i < kAccountCount && printed < print_limit; ++i) {
                if (!is_confirmed[i]) {
                    std::cerr << "  [" << i << "] " << common::Encode::HexEncode(test_addrs[i]) << std::endl;
                    ++printed;
                }
            }
            if (failed_total > print_limit) {
                std::cerr << "  ... and " << (failed_total - print_limit) << " more" << std::endl;
            }
            std::cerr << "Aborting stress test." << std::endl;
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }

        // Phase 4: Stress test - random transfers
        std::cout << "\n[Phase 4] Starting stress test - random transfers..." << std::endl;
        if (max_tx_count > 0) {
            std::cout << "  Will stop after " << max_tx_count << " successful sends" << std::endl;
        } else {
            std::cout << "  Press Ctrl+C to stop" << std::endl;
        }

        // Shared leader routing: pool_idx -> {ip, port}, updated by leader sync thread
        std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> leader_map;
        std::mutex leader_mutex;

        // Try initial fetch
        {
            uint32_t lc = 0;
            std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> tmp;
            if (sdk.fetchLeaders(tmp, lc) && !tmp.empty()) {
                std::lock_guard<std::mutex> lock(leader_mutex);
                leader_map = tmp;
                std::cout << "  Leader routing enabled: " << lc << " leaders" << std::endl;
            } else {
                std::cout << "  Leader routing not yet available, using default node" << std::endl;
            }
        }

        // Pre-compute pool index for each test address
        std::vector<uint32_t> addr_pool_idx(kAccountCount);
        for (uint32_t i = 0; i < kAccountCount; ++i) {
            addr_pool_idx[i] = common::GetAddressPoolIndex(test_addrs[i]);
        }

        std::atomic<uint64_t> tx_count{0};
        std::atomic<uint64_t> tx_failed{0};

        // Per-pool tx counters for statistics
        struct PoolStats {
            std::atomic<uint64_t> tx_sent{0};
            std::atomic<uint64_t> tx_failed{0};
        };
        std::vector<PoolStats> pool_stats(common::kImmutablePoolSize);

        // Group accounts by pool index
        std::unordered_map<uint32_t, std::vector<uint32_t>> pool_accounts;
        for (uint32_t i = 0; i < kAccountCount; ++i) {
            pool_accounts[addr_pool_idx[i]].push_back(i);
        }

        std::cout << "  Account distribution by pool:" << std::endl;
        for (auto& [pool_idx, accs] : pool_accounts) {
            std::cout << "    pool " << pool_idx << ": " << accs.size() << " accounts" << std::endl;
        }

        // Each thread handles one or more pools, sends to the pool's leader directly
        auto stress_test_thread = [&](uint32_t thread_id, std::vector<uint32_t> my_account_indices) {
            if (my_account_indices.empty()) return;
            // Per-thread SDK for nonce queries (httplib is not thread-safe)
            auto thread_sdk = std::make_unique<ShardoraSDK>(global_chain_node_ip, global_chain_node_http_port);
            uint32_t consecutive_failures = 0;
            static const uint32_t kFailureThreshold = 10;  // After 10 consecutive failures, assume node is down
            uint32_t pos = 0;
            while (!global_stop) {
                if (max_tx_count > 0 && tx_count.load(std::memory_order_relaxed) >= max_tx_count) {
                    global_stop = true;
                    break;
                }

                uint32_t from_idx = my_account_indices[pos % my_account_indices.size()];
                ++pos;

                uint32_t to_idx;
                do {
                    to_idx = common::Random::RandomUint32() % kAccountCount;
                } while (to_idx == from_idx);

                std::string from_prikey = test_prikeys[from_idx];
                std::string from_addr = test_addrs[from_idx];
                std::string to_addr = test_addrs[to_idx];

                if (src_prikey_with_nonce[from_addr] + 2 * common::kMaxTxCount <= prikey_with_nonce[from_addr]) {
                    usleep(100000);
                    continue;
                }

                std::shared_ptr<security::Security> from_sec = std::make_shared<security::Ecdsa>();
                from_sec->SetPrivateKey(from_prikey);

                uint64_t amount = 1 + (common::Random::RandomUint32() % 10);
                auto tx_msg_ptr = CreateTransactionWithAttr(
                    from_sec,
                    ++prikey_with_nonce[from_addr],
                    from_prikey,
                    to_addr,
                    "", "", amount, 210000, 1, shardnum);

                if (!tx_msg_ptr) {
                    --prikey_with_nonce[from_addr];
                    ++tx_failed;
                    continue;
                }

                // Route by pool: GetAddressPoolIndex(from_addr) -> leader_map[pool] -> ip:port
                uint32_t pool = addr_pool_idx[from_idx];
                std::string dest_ip = global_chain_node_ip;
                uint16_t dest_port = global_chain_node_http_port - 10000;
                {
                    std::lock_guard<std::mutex> lock(leader_mutex);
                    auto it = leader_map.find(pool);
                    if (it != leader_map.end()) {
                        dest_ip = it->second.ip;
                        dest_port = it->second.port;
                    }
                }

                // Retry send up to 3 times, roll back nonce on total failure
                bool sent_ok = false;
                for (int retry = 0; retry < 3 && !global_stop; ++retry) {
                    if (transport::TcpTransport::Instance()->Send(
                            dest_ip, dest_port, tx_msg_ptr->header) == 0) {
                        sent_ok = true;
                        break;
                    }
                    usleep(100000);  // 100ms between retries
                }

                if (sent_ok) {
                    ++tx_count;
                    ++(pool_stats[pool].tx_sent);
                    consecutive_failures = 0;
                    if (max_tx_count > 0 && tx_count.load(std::memory_order_relaxed) >= max_tx_count) {
                        global_stop = true;
                    }
                } else {
                    // Roll back nonce to avoid permanent gap
                    --prikey_with_nonce[from_addr];
                    ++tx_failed;
                    ++(pool_stats[pool].tx_failed);
                    ++consecutive_failures;

                    // Node likely down — pause, wait for reconnection, re-fetch nonces
                    if (consecutive_failures >= kFailureThreshold) {
                        std::cerr << "  [Thread " << thread_id << "] " << consecutive_failures
                                  << " consecutive send failures to " << dest_ip << ":" << dest_port
                                  << " — node may be down, pausing..." << std::endl;

                        // Wait until we can reach the node again (poll every 2s)
                        while (!global_stop) {
                            usleep(2000000);  // 2s
                            // Try a simple nonce fetch as connectivity probe
                            thread_sdk = std::make_unique<ShardoraSDK>(global_chain_node_ip, global_chain_node_http_port);
                            int64_t probe_nonce = thread_sdk->fetchNonce(
                                common::Encode::HexEncode(from_addr));
                            if (probe_nonce >= 0) {
                                std::cerr << "  [Thread " << thread_id << "] Node " << dest_ip
                                          << ":" << dest_port << " is back, re-fetching nonces..."
                                          << std::endl;
                                break;
                            }
                        }

                        if (global_stop) break;

                        // Re-fetch nonces for all accounts this thread handles
                        uint32_t refreshed = 0;
                        for (uint32_t idx : my_account_indices) {
                            if (global_stop) break;
                            std::string addr_hex = common::Encode::HexEncode(test_addrs[idx]);
                            int64_t fresh_nonce = thread_sdk->fetchNonce(addr_hex);
                            if (fresh_nonce >= 0) {
                                prikey_with_nonce[test_addrs[idx]] = fresh_nonce;
                                src_prikey_with_nonce[test_addrs[idx]] = fresh_nonce;
                                ++refreshed;
                            }
                        }

                        std::cerr << "  [Thread " << thread_id << "] Refreshed " << refreshed
                                  << "/" << my_account_indices.size() << " nonces, resuming sends"
                                  << std::endl;
                        consecutive_failures = 0;
                    }
                }

                usleep(tps_interval_us);
            }
        };

        // Launch threads: one per pool (or merge if num_threads < pool count)
        std::vector<std::thread> stress_threads;
        std::vector<uint32_t> pool_list;
        for (auto& [pool_idx, accs] : pool_accounts) {
            pool_list.push_back(pool_idx);
        }
        std::sort(pool_list.begin(), pool_list.end());

        uint32_t actual_threads = std::min(num_threads, (uint32_t)pool_list.size());
        std::cout << "  Starting " << actual_threads << " stress threads for "
                  << pool_list.size() << " pools" << std::endl;

        for (uint32_t t = 0; t < actual_threads; ++t) {
            std::vector<uint32_t> thread_accounts;
            for (uint32_t p = t; p < pool_list.size(); p += actual_threads) {
                auto& accs = pool_accounts[pool_list[p]];
                thread_accounts.insert(thread_accounts.end(), accs.begin(), accs.end());
            }
            std::cout << "    thread " << t << ": " << thread_accounts.size() << " accounts, pools=[";
            for (uint32_t p = t; p < pool_list.size(); p += actual_threads) {
                if (p != t) std::cout << ",";
                std::cout << pool_list[p];
            }
            std::cout << "]" << std::endl;
            stress_threads.emplace_back(stress_test_thread, t, std::move(thread_accounts));
        }

        // TPS monitor: per-pool detail showing pool -> server mapping
        std::thread tps_thread([&]() {
            uint64_t prev_count = 0;
            std::vector<uint64_t> prev_pool_tx(common::kImmutablePoolSize, 0);
            while (!global_stop) {
                for (int i = 0; i < 30 && !global_stop; ++i) usleep(100000);
                if (global_stop) break;

                uint64_t cur_count = tx_count.load();
                uint64_t tps = (cur_count >= prev_count) ? (cur_count - prev_count) / 3 : 0;
                std::cout << "[Stress] TPS: " << tps
                          << ", Total: " << cur_count
                          << ", Failed: " << tx_failed.load() << std::endl;

                // Per-pool detail: pool -> server, tps, sent
                struct ServerAgg {
                    std::vector<uint32_t> pools;
                    uint32_t accounts = 0;
                    uint64_t tps = 0;
                    uint64_t sent = 0;
                    uint64_t fail = 0;
                };
                std::map<std::string, ServerAgg> server_agg;
                {
                    std::lock_guard<std::mutex> lock(leader_mutex);
                    for (uint32_t p = 0; p < common::kImmutablePoolSize; ++p) {
                        std::string key;
                        auto it = leader_map.find(p);
                        if (it != leader_map.end()) {
                            key = it->second.ip + ":" + std::to_string(it->second.port);
                        } else {
                            key = global_chain_node_ip + ":" + std::to_string(global_chain_node_http_port - 10000);
                        }

                        uint64_t cur_tx = pool_stats[p].tx_sent.load();
                        uint64_t prev_tx = prev_pool_tx[p];
                        uint64_t delta = (cur_tx >= prev_tx) ? (cur_tx - prev_tx) : cur_tx;

                        auto& agg = server_agg[key];
                        agg.pools.push_back(p);
                        if (pool_accounts.count(p)) agg.accounts += pool_accounts[p].size();
                        agg.tps += delta / 3;
                        agg.sent += cur_tx;
                        agg.fail += pool_stats[p].tx_failed.load();
                        prev_pool_tx[p] = cur_tx;
                    }
                }

                for (auto& [key, agg] : server_agg) {
                    std::cout << "  -> " << key
                              << " pools=[";
                    for (uint32_t i = 0; i < agg.pools.size(); ++i) {
                        if (i > 0) std::cout << ",";
                        std::cout << agg.pools[i];
                    }
                    std::cout << "]"
                              << " accounts=" << agg.accounts
                              << " tps=" << agg.tps
                              << " sent=" << agg.sent
                              << " fail=" << agg.fail << std::endl;
                }

                prev_count = cur_count;
            }
        });

        // Nonce update thread (batch mode only, leader sync is separate)
        std::thread nonce_update_thread([&]() {
            uint32_t full_update_counter = 0;
            const uint32_t kNonceBatchSize = 500;
            while (!global_stop) {
                // Sleep 5 seconds in 100ms chunks to allow quick exit
                for (int i = 0; i < 50 && !global_stop; ++i) {
                    usleep(100000);  // 100ms
                }
                if (global_stop) break;
                
                // Do a full update every 30 seconds (6 iterations × 5s)
                // Otherwise only update throttled accounts
                bool do_full_update = (++full_update_counter % 6 == 0);
                
                if (do_full_update) {
                    std::cout << "  [Full] Batch updating all nonces..." << std::endl;
                } else {
                    std::cout << "  [Quick] Batch updating throttled nonces..." << std::endl;
                }
                
                // Collect addresses that need nonce refresh
                std::vector<std::string> addrs_to_query;
                std::vector<uint32_t> indices_to_query;
                uint32_t throttled = 0;
                
                for (uint32_t i = 0; i < kAccountCount && !global_stop; ++i) {
                    auto& addr = test_addrs[i];
                    bool is_throttled_flag = (src_prikey_with_nonce[addr] + 2 * common::kMaxTxCount <= prikey_with_nonce[addr]);
                    
                    if (is_throttled_flag) {
                        ++throttled;
                    }
                    
                    // Skip non-throttled accounts unless doing full update
                    if (!is_throttled_flag && !do_full_update) {
                        continue;
                    }
                    
                    addrs_to_query.push_back(common::Encode::HexEncode(addr));
                    indices_to_query.push_back(i);
                }
                
                if (global_stop) break;
                
                // Batch query all collected addresses
                uint32_t updated = 0;
                for (uint32_t offset = 0; offset < addrs_to_query.size() && !global_stop; offset += kNonceBatchSize) {
                    uint32_t end = std::min(offset + kNonceBatchSize, (uint32_t)addrs_to_query.size());
                    std::vector<std::string> batch(addrs_to_query.begin() + offset, addrs_to_query.begin() + end);
                    
                    auto batch_res = sdk.batchQueryAccounts(batch);
                    if (batch_res.contains("status") && batch_res["status"] == 0 &&
                        batch_res.contains("accounts")) {
                        for (uint32_t k = offset; k < end; ++k) {
                            const std::string& hex_addr = addrs_to_query[k];
                            uint32_t idx = indices_to_query[k];
                            if (batch_res["accounts"].contains(hex_addr)) {
                                auto& acc = batch_res["accounts"][hex_addr];
                                if (acc.contains("nonce")) {
                                    int64_t nonce = 0;
                                    auto nonce_str = acc["nonce"].get<std::string>();
                                    std::from_chars(nonce_str.data(),
                                                    nonce_str.data() + nonce_str.size(), nonce);
                                    src_prikey_with_nonce[test_addrs[idx]] = nonce;
                                    ++updated;
                                }
                            }
                        }
                    }
                }
                
                if (global_stop) break;
                
                std::cout << "  Nonce batch update done: " << updated << "/" << addrs_to_query.size()
                          << " refreshed, " << throttled << " throttled" << std::endl;
            }
        });

        // Leader sync thread: polls every 3 seconds, updates leader_map
        std::thread leader_sync_thread([&]() {
            while (!global_stop) {
                for (int i = 0; i < 30 && !global_stop; ++i) usleep(100000);
                if (global_stop) break;

                std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> new_leaders;
                uint32_t new_count = 0;
                if (sdk.fetchLeaders(new_leaders, new_count) && !new_leaders.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(leader_mutex);
                        leader_map = new_leaders;
                    }
                    // Summarize by server
                    std::map<std::string, uint32_t> server_pool_count;
                    for (auto& [p, info] : new_leaders) {
                        server_pool_count[info.ip + ":" + std::to_string(info.port)]++;
                    }
                    std::cout << "  [LeaderSync] " << new_count << " leaders, "
                              << server_pool_count.size() << " servers:";
                    for (auto& [key, cnt] : server_pool_count) {
                        std::cout << " " << key << "(" << cnt << "pools)";
                    }
                    std::cout << std::endl;
                }
            }
        });

        for (auto& th : stress_threads) {
            th.join();
        }
        tps_thread.join();
        nonce_update_thread.join();
        leader_sync_thread.join();

        transport::TcpTransport::Instance()->Stop();
        std::cout << "\n=== Stress Test Complete ===" << std::endl;
        std::cout << "Total transactions: " << tx_count.load() << std::endl;
        std::cout << "Failed transactions: " << tx_failed.load() << std::endl;
        return 0;
    }

    // ── Mode 5: AMM Contract Deployment + Swap Stress Test ──────────────
    // Usage: txcli 5 <shard> <pool> <ip> <port> [user_count] [threads] [rounds] [tps]
    //
    // 1. Create user + deployer accounts on chain + verify
    // 2. Deploy 256 AMM contract sets (TokenA + TokenB + AMMPool each)
    // 3. Deployer adds liquidity to all pools
    // 4. Pair users, set prefund, transfer tokens, approve
    // 5. Execute matched AMM swaps (UserA: A→B, UserB: B→A) as stress test
    // 6. Save results
    if (argv[1][0] == '5') {
        const uint32_t kUserCount = (argc >= 7) ? std::stoi(argv[6]) : 10000;
        const uint32_t kContractSets = 1024;  // 256 AMM contract sets (TokenA+TokenB+AMMPool)
        const uint32_t kDeployThreads = (argc >= 8) ? std::stoi(argv[7]) : 16;
        const uint32_t kStressRoundsArg = (argc >= 9) ? std::stoi(argv[8]) : 1000;
        const uint32_t kTargetTps = (argc >= 10) ? std::stoi(argv[9]) : 0;  // 0 = unlimited

        if (argc >= 4) {
            shardnum = std::stoi(argv[2]);
            global_pool_idx = std::stoi(argv[3]);
        }
        if (argc >= 6) {
            global_chain_node_ip = argv[4];
            uint16_t input_port = std::stoi(argv[5]);
            // Auto-detect: if port < 20000, assume TCP port (add 10000 for HTTP).
            // Otherwise assume HTTP port directly.
            if (input_port < 20000) {
                global_chain_node_http_port = input_port + 10000;
            } else {
                global_chain_node_http_port = input_port;
            }
        }

        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "  AMM Contract Deployment + Swap Stress Test" << std::endl;
        std::cout << "  " << kUserCount << " users + " << kContractSets << " deployers" << std::endl;
        std::cout << "  " << kContractSets << " x 3 contracts = " << kContractSets * 3 << " deployments" << std::endl;
        std::cout << "  Stress rounds: " << kStressRoundsArg
                  << ", target TPS: " << (kTargetTps > 0 ? std::to_string(kTargetTps) : "unlimited") << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout << "Shard: " << shardnum << std::endl;
        std::cout << "Node: " << global_chain_node_ip << std::endl;
        std::cout << "  HTTP port: " << global_chain_node_http_port << std::endl;
        std::cout << "  TCP port:  " << (global_chain_node_http_port - 10000) << std::endl;
        std::cout << "Threads: " << kDeployThreads << std::endl;

        LoadAllAccounts(shardnum);
        SignalRegister();
        WriteDefaultLogConf();

        // ── TCP transport setup (same as Mode 4) ─────────────────────────
        transport::MultiThreadHandler net_handler;
        std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
        auto db_ptr = std::make_shared<db::Db>();
        if (!db_ptr->Init(db_path + "_amm_deploy")) {
            std::cerr << "init db failed" << std::endl;
            return 1;
        }
        if (net_handler.Init(db_ptr, sec) != 0) {
            std::cerr << "init net handler failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Init("127.0.0.1:13794", 128, false, &net_handler) != 0) {
            std::cerr << "init tcp failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Start(false) != 0) {
            std::cerr << "start tcp failed" << std::endl;
            return 1;
        }

        ShardoraSDK sdk(global_chain_node_ip, global_chain_node_http_port);

        // Quick connectivity test — verify HTTP port is reachable
        {
            std::shared_ptr<security::Security> test_sec = std::make_shared<security::Ecdsa>();
            test_sec->SetPrivateKey(g_prikeys[0]);
            std::string test_addr = common::Encode::HexEncode(test_sec->GetAddress());
            std::cout << "  Testing HTTP connectivity to " << global_chain_node_ip
                      << ":" << global_chain_node_http_port << "..." << std::endl;
            int64_t test_nonce = sdk.fetchNonce(test_addr);
            if (test_nonce < 0) {
                std::cerr << "  ERROR: Cannot reach node at " << global_chain_node_ip
                          << ":" << global_chain_node_http_port << std::endl;
                std::cerr << "  Check: is this the HTTPS port (e.g. 23001)?" << std::endl;
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  HTTP OK (test nonce=" << test_nonce << " for " << test_addr << ")" << std::endl;
        }

        // ── Solidity sources (same as clipy/amm.py) ──────────────────────
        const std::string SIMPLE_TOKEN_SOL = R"(
pragma solidity ^0.8.0;

contract SimpleToken {
    bytes32 public name;
    uint256 public totalSupply;
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;

    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);

    constructor(bytes32 _name, uint256 _initialSupply) {
        name = _name;
        totalSupply = _initialSupply;
        balanceOf[msg.sender] = _initialSupply;
    }

    function transfer(address to, uint256 amount) external returns (bool) {
        require(balanceOf[msg.sender] >= amount, "insufficient");
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        emit Transfer(msg.sender, to, amount);
        return true;
    }

    function approve(address spender, uint256 amount) external returns (bool) {
        allowance[msg.sender][spender] = amount;
        emit Approval(msg.sender, spender, amount);
        return true;
    }

    function transferFrom(address from, address to, uint256 amount) external returns (bool) {
        require(allowance[from][msg.sender] >= amount, "not approved");
        require(balanceOf[from] >= amount, "insufficient");
        allowance[from][msg.sender] -= amount;
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        emit Transfer(from, to, amount);
        return true;
    }
}
)";

        const std::string AMM_POOL_SOL = R"(
pragma solidity ^0.8.0;

interface IERC20 {
    function transferFrom(address from, address to, uint256 amount) external returns (bool);
    function transfer(address to, uint256 amount) external returns (bool);
    function balanceOf(address account) external view returns (uint256);
}

contract AMMPool {
    IERC20 public tokenA;
    IERC20 public tokenB;
    uint256 public reserveA;
    uint256 public reserveB;
    uint256 public totalLiquidity;
    mapping(address => uint256) public liquidity;

    event LiquidityAdded(address indexed provider, uint256 amountA, uint256 amountB, uint256 lp);
    event LiquidityRemoved(address indexed provider, uint256 amountA, uint256 amountB);
    event Swap(address indexed user, address tokenIn, uint256 amountIn, uint256 amountOut);

    constructor(address _tokenA, address _tokenB) {
        tokenA = IERC20(_tokenA);
        tokenB = IERC20(_tokenB);
    }

    function addLiquidity(uint256 amountA, uint256 amountB) external returns (uint256 lp) {
        tokenA.transferFrom(msg.sender, address(this), amountA);
        tokenB.transferFrom(msg.sender, address(this), amountB);
        if (totalLiquidity == 0) {
            lp = amountA;
        } else {
            lp = (amountA * totalLiquidity) / reserveA;
        }
        reserveA += amountA;
        reserveB += amountB;
        totalLiquidity += lp;
        liquidity[msg.sender] += lp;
        emit LiquidityAdded(msg.sender, amountA, amountB, lp);
    }

    function removeLiquidity(uint256 lpAmount) external {
        require(liquidity[msg.sender] >= lpAmount, "insufficient lp");
        uint256 amountA = (lpAmount * reserveA) / totalLiquidity;
        uint256 amountB = (lpAmount * reserveB) / totalLiquidity;
        liquidity[msg.sender] -= lpAmount;
        totalLiquidity -= lpAmount;
        reserveA -= amountA;
        reserveB -= amountB;
        tokenA.transfer(msg.sender, amountA);
        tokenB.transfer(msg.sender, amountB);
        emit LiquidityRemoved(msg.sender, amountA, amountB);
    }

    function swapAForB(uint256 amountIn, uint256 minOut) external returns (uint256 amountOut) {
        require(amountIn > 0 && reserveA > 0 && reserveB > 0, "invalid");
        amountOut = (amountIn * reserveB) / (reserveA + amountIn);
        require(amountOut >= minOut, "slippage");
        tokenA.transferFrom(msg.sender, address(this), amountIn);
        tokenB.transfer(msg.sender, amountOut);
        reserveA += amountIn;
        reserveB -= amountOut;
        emit Swap(msg.sender, address(tokenA), amountIn, amountOut);
    }

    function swapBForA(uint256 amountIn, uint256 minOut) external returns (uint256 amountOut) {
        require(amountIn > 0 && reserveA > 0 && reserveB > 0, "invalid");
        amountOut = (amountIn * reserveA) / (reserveB + amountIn);
        require(amountOut >= minOut, "slippage");
        tokenB.transferFrom(msg.sender, address(this), amountIn);
        tokenA.transfer(msg.sender, amountOut);
        reserveB += amountIn;
        reserveA -= amountOut;
        emit Swap(msg.sender, address(tokenB), amountIn, amountOut);
    }

    function getReserves() external view returns (uint256, uint256) {
        return (reserveA, reserveB);
    }
}
)";

        // ── Phase 1: Compile contracts ────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 1: Compile Solidity Contracts" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        auto token_compiled = sdk.compileSolidity(SIMPLE_TOKEN_SOL);
        if (token_compiled["status"] != 0) {
            std::cerr << "SimpleToken compile failed: " << token_compiled["msg"] << std::endl;
            return 1;
        }
        std::string token_bytecode = token_compiled["bytecode"];
        std::cout << "  SimpleToken bytecode: " << token_bytecode.size() << " chars" << std::endl;

        auto pool_compiled = sdk.compileSolidity(AMM_POOL_SOL);
        if (pool_compiled["status"] != 0) {
            std::cerr << "AMMPool compile failed: " << pool_compiled["msg"] << std::endl;
            return 1;
        }
        std::string pool_bytecode = pool_compiled["bytecode"];
        std::cout << "  AMMPool bytecode: " << pool_bytecode.size() << " chars" << std::endl;
        std::cout << "  Compilation complete" << std::endl;

        // ── Phase 2: Generate accounts ────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 2: Generate " << kUserCount << " User + "
                  << kContractSets << " Deployer Accounts" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        struct AccountInfo {
            std::string prikey_hex;
            std::string addr_hex;
            bool confirmed = false;
        };

        struct DeployerInfo {
            std::string prikey_hex;
            std::string addr_hex;
            std::string token_a_addr;
            std::string token_b_addr;
            std::string pool_addr;
            bool confirmed = false;
            bool token_a_deployed = false;
            bool token_b_deployed = false;
            bool pool_deployed = false;
        };

        std::vector<AccountInfo> users(kUserCount);
        for (uint32_t i = 0; i < kUserCount; ++i) {
            std::string prikey; prikey.resize(32);
            for (uint32_t j = 0; j < 32; ++j) prikey[j] = static_cast<char>(common::Random::RandomUint32() % 256);
            users[i].prikey_hex = common::Encode::HexEncode(prikey);
            auto s = std::make_shared<security::Ecdsa>(); s->SetPrivateKey(prikey);
            users[i].addr_hex = common::Encode::HexEncode(s->GetAddress());
            if ((i + 1) % 2000 == 0) std::cout << "  Generated " << (i+1) << "/" << kUserCount << " users" << std::endl;
        }
        std::cout << "  Generated " << kUserCount << " user accounts" << std::endl;

        std::vector<DeployerInfo> deployers(kContractSets);
        for (uint32_t i = 0; i < kContractSets; ++i) {
            std::string prikey; prikey.resize(32);
            for (uint32_t j = 0; j < 32; ++j) prikey[j] = static_cast<char>(common::Random::RandomUint32() % 256);
            deployers[i].prikey_hex = common::Encode::HexEncode(prikey);
            auto s = std::make_shared<security::Ecdsa>(); s->SetPrivateKey(prikey);
            deployers[i].addr_hex = common::Encode::HexEncode(s->GetAddress());
        }
        std::cout << "  Generated " << kContractSets << " deployer accounts" << std::endl;

        // Deduplicate funded accounts
        std::vector<std::string> unique_funders;
        { std::set<std::string> seen; for (auto& pk : g_prikeys) if (seen.insert(pk).second) unique_funders.push_back(pk); }
        std::cout << "  Unique funded accounts: " << unique_funders.size() << std::endl;

        // ── Phase 3: Create all accounts on chain + verify ────────────────
        const uint32_t kTotalAccounts = kUserCount + kContractSets;
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 3: Create " << kTotalAccounts << " Accounts on Chain (TCP)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        std::vector<std::string> all_addr_hex(kTotalAccounts);
        std::vector<bool> all_confirmed(kTotalAccounts, false);
        for (uint32_t i = 0; i < kUserCount; ++i) all_addr_hex[i] = users[i].addr_hex;
        for (uint32_t i = 0; i < kContractSets; ++i) all_addr_hex[kUserCount + i] = deployers[i].addr_hex;

        const uint64_t kFundAmount = 3000000000lu;
        std::atomic<uint32_t> fund_success{0}, fund_fail{0};
        uint32_t fund_threads = std::min({kDeployThreads, kTotalAccounts, (uint32_t)unique_funders.size()});
        if (fund_threads == 0) fund_threads = 1;
        uint32_t accs_per_thread = kTotalAccounts / fund_threads;

        auto create_account_fn = [&](uint32_t tid, uint32_t start_idx, uint32_t end_idx) {
            ShardoraSDK tsdk(global_chain_node_ip, global_chain_node_http_port);
            std::string fpk = unique_funders[tid % unique_funders.size()];
            std::shared_ptr<security::Security> fsec = std::make_shared<security::Ecdsa>();
            fsec->SetPrivateKey(fpk);
            std::string faddr = fsec->GetAddress();
            int64_t nonce = tsdk.fetchNonce(common::Encode::HexEncode(faddr));
            if (nonce < 0) { fund_fail += (end_idx - start_idx); return; }
            for (uint32_t i = start_idx; i < end_idx && !global_stop; ++i) {
                auto tx = CreateTransactionWithAttr(fsec, ++nonce, common::Encode::HexEncode(fpk),
                    common::Encode::HexDecode(all_addr_hex[i]), "", "", kFundAmount, 210000, 1, shardnum);
                if (tx && transport::TcpTransport::Instance()->Send(global_chain_node_ip,
                        global_chain_node_http_port - 10000, tx->header) == 0) ++fund_success;
                else ++fund_fail;
                usleep(1000);
            }
        };

        std::vector<std::thread> fund_vec;
        std::cout << "  Threads: " << fund_threads << ", per thread: " << accs_per_thread << std::endl;
        for (uint32_t t = 0; t < fund_threads; ++t) {
            uint32_t s = t * accs_per_thread;
            uint32_t e = (t == fund_threads - 1) ? kTotalAccounts : (s + accs_per_thread);
            fund_vec.emplace_back(create_account_fn, t, s, e);
        }
        std::thread fund_prog([&]() {
            while (fund_success.load() + fund_fail.load() < kTotalAccounts && !global_stop) {
                for (int i = 0; i < 20 && !global_stop; ++i) usleep(100000);
                if (global_stop) break;
                std::cout << "  Send: " << fund_success.load() << " ok, " << fund_fail.load()
                          << " fail / " << kTotalAccounts << std::endl;
            }
        });
        for (auto& th : fund_vec) th.join();
        fund_prog.join();
        std::cout << "  Send complete: " << fund_success.load() << " ok, " << fund_fail.load() << " fail" << std::endl;

        // ── Batch verify all accounts on chain ─────────────────────────────
        std::cout << "\n  Waiting 10s for consensus..." << std::endl;
        for (int w = 0; w < 100 && !global_stop; ++w) usleep(100000);

        std::cout << "  Batch verifying " << kTotalAccounts << " accounts (up to 600s)..." << std::endl;
        auto vstart = std::chrono::steady_clock::now();
        uint32_t confirmed = 0;
        const uint32_t kBatchSize = 500;
        std::vector<uint32_t> pend; pend.reserve(kTotalAccounts);
        for (uint32_t i = 0; i < kTotalAccounts; ++i) pend.push_back(i);
        uint32_t vround = 0;
        while (!pend.empty() && !global_stop) {
            if (std::chrono::steady_clock::now() - vstart >= std::chrono::seconds(600)) {
                std::cout << "  Timeout. Confirmed " << confirmed << "/" << kTotalAccounts << std::endl; break;
            }
            ++vround; uint32_t rok = 0;
            std::vector<uint32_t> npend; npend.reserve(pend.size());
            std::vector<std::string> ba; std::vector<uint32_t> bi;
            for (uint32_t p = 0; p < pend.size() && !global_stop; ++p) {
                ba.push_back(all_addr_hex[pend[p]]); bi.push_back(pend[p]);
                if (ba.size() >= kBatchSize || p == pend.size() - 1) {
                    auto r = sdk.batchQueryAccounts(ba);
                    if (r.contains("status") && r["status"] == 0 && r.contains("accounts")) {
                        for (uint32_t k = 0; k < bi.size(); ++k) {
                            if (r["accounts"].contains(ba[k])) { all_confirmed[bi[k]] = true; ++confirmed; ++rok; }
                            else npend.push_back(bi[k]);
                        }
                    } else { for (auto idx : bi) npend.push_back(idx); }
                    ba.clear(); bi.clear();
                }
            }
            pend = std::move(npend);
            auto es = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - vstart).count();
            std::cout << "  [Round " << vround << ", " << es << "s] +" << rok << ", "
                      << confirmed << "/" << kTotalAccounts << " confirmed, " << pend.size() << " pending" << std::endl;
            if (pend.empty()) break;
            uint32_t wt = (rok > 0) ? 2000 : 5000;
            if (vround == 1 && rok == 0) wt = 8000;
            for (uint32_t w = 0; w < wt / 100 && !global_stop; ++w) usleep(100000);
        }
        uint32_t users_ok = 0, deployers_ok = 0;
        for (uint32_t i = 0; i < kUserCount; ++i) { users[i].confirmed = all_confirmed[i]; if (all_confirmed[i]) ++users_ok; }
        for (uint32_t i = 0; i < kContractSets; ++i) { deployers[i].confirmed = all_confirmed[kUserCount+i]; if (all_confirmed[kUserCount+i]) ++deployers_ok; }
        std::cout << "  Users confirmed: " << users_ok << "/" << kUserCount << std::endl;
        std::cout << "  Deployers confirmed: " << deployers_ok << "/" << kContractSets << std::endl;
        if (deployers_ok == 0) {
            std::cerr << "  ERROR: No deployer accounts confirmed. Aborting." << std::endl;
            transport::TcpTransport::Instance()->Stop(); return 1;
        }

        // ── Phase 4: Deploy 256 AMM contract sets ─────────────────────────
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 4: Deploy " << deployers_ok << " AMM Contract Sets (x3)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        std::atomic<uint32_t> ta_ok{0}, tb_ok{0}, pool_ok{0}, dfail{0};
        auto dstart = std::chrono::steady_clock::now();

        // Deploy in 3 phases: TokenA → verify → TokenB → verify → AMMPool
        auto deploy_one_type = [&](const std::string& label, std::function<void(uint32_t, ShardoraSDK&)> fn) {
            std::vector<std::thread> dt;
            uint32_t nt = std::min(kDeployThreads, kContractSets); if (!nt) nt = 1;
            uint32_t pp = kContractSets / nt;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s2 = t*pp, e2 = (t==nt-1)?kContractSets:(s2+pp);
                dt.emplace_back([&,s2,e2](){
                    ShardoraSDK tsdk(global_chain_node_ip, global_chain_node_http_port);
                    for (uint32_t i=s2;i<e2&&!global_stop;++i) { if(!deployers[i].confirmed) continue; fn(i,tsdk); usleep(100000); }
                });
            }
            for (auto& th:dt) th.join();
            auto el=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-dstart).count();
            std::cout<<"  ["<<el<<"s] "<<label<<": A="<<ta_ok.load()<<" B="<<tb_ok.load()<<" Pool="<<pool_ok.load()<<" fail="<<dfail.load()<<std::endl;
        };
        auto wait_addrs = [&](const std::vector<std::string>& addrs, const std::string& label) {
            if (addrs.empty()) return;
            std::cout<<"  Waiting for "<<addrs.size()<<" "<<label<<"..."<<std::endl;
            for(int w=0;w<100&&!global_stop;++w) usleep(100000);
            std::vector<uint32_t> pd; for(uint32_t i=0;i<addrs.size();++i) pd.push_back(i);
            uint32_t ok=0;
            for(uint32_t rd=0;rd<20&&!pd.empty()&&!global_stop;++rd){
                uint32_t rok=0; std::vector<uint32_t> np;
                std::vector<std::string> ba; std::vector<uint32_t> bi;
                for(uint32_t p=0;p<pd.size();++p){
                    ba.push_back(addrs[pd[p]]); bi.push_back(pd[p]);
                    if(ba.size()>=200||p==pd.size()-1){
                        auto r=sdk.batchQueryAccounts(ba);
                        if(r.contains("status")&&r["status"]==0&&r.contains("accounts")){
                            for(uint32_t k=0;k<bi.size();++k){ if(r["accounts"].contains(ba[k])){++ok;++rok;} else np.push_back(bi[k]); }
                        } else { for(auto idx:bi) np.push_back(idx); }
                        ba.clear(); bi.clear();
                    }
                }
                pd=std::move(np);
                std::cout<<"    "<<label<<" round "<<(rd+1)<<": "<<ok<<"/"<<addrs.size()<<std::endl;
                if(pd.empty()) break;
                for(uint32_t w=0;w<((rok>0)?30:80)&&!global_stop;++w) usleep(100000);
            }
        };
        auto mkname=[](uint32_t i,const char* pfx){
            std::string h=utils::bytesToHex(std::vector<uint8_t>(pfx,pfx+strlen(pfx)));
            auto is=std::to_string(i); h+=utils::bytesToHex(std::vector<uint8_t>(is.begin(),is.end())); return h;
        };
        const uint64_t kPf=400000000lu;

        std::cout<<"  Step 1/3: Deploy TokenA..."<<std::endl;
        deploy_one_type("TokenA",[&](uint32_t i,ShardoraSDK& t){
            auto r=t.deploySolidity(deployers[i].prikey_hex,token_bytecode,0,kPf,0,{"bytes32","uint256"},{mkname(i,"TkA_"),"10000000"});
            if(r["status"]==0){deployers[i].token_a_addr=r["id"];deployers[i].token_a_deployed=true;++ta_ok;} else ++dfail;
        });
        {std::vector<std::string> v; for(auto& d:deployers) if(d.token_a_deployed) v.push_back(d.token_a_addr); wait_addrs(v,"TokenA");}

        std::cout<<"  Step 2/3: Deploy TokenB..."<<std::endl;
        deploy_one_type("TokenB",[&](uint32_t i,ShardoraSDK& t){
            if(!deployers[i].token_a_deployed){++dfail;return;}
            auto r=t.deploySolidity(deployers[i].prikey_hex,token_bytecode,0,kPf,0,{"bytes32","uint256"},{mkname(i,"TkB_"),"10000000"});
            if(r["status"]==0){deployers[i].token_b_addr=r["id"];deployers[i].token_b_deployed=true;++tb_ok;} else ++dfail;
        });
        {std::vector<std::string> v; for(auto& d:deployers) if(d.token_b_deployed) v.push_back(d.token_b_addr); wait_addrs(v,"TokenB");}

        std::cout<<"  Step 3/3: Deploy AMMPool..."<<std::endl;
        deploy_one_type("AMMPool",[&](uint32_t i,ShardoraSDK& t){
            if(!deployers[i].token_a_deployed||!deployers[i].token_b_deployed){++dfail;return;}
            auto r=t.deploySolidity(deployers[i].prikey_hex,pool_bytecode,0,kPf,0,{"address","address"},{deployers[i].token_a_addr,deployers[i].token_b_addr});
            if(r["status"]==0){deployers[i].pool_addr=r["id"];deployers[i].pool_deployed=true;++pool_ok;} else ++dfail;
        });
        {std::vector<std::string> v; for(auto& d:deployers) if(d.pool_deployed) v.push_back(d.pool_addr); wait_addrs(v,"AMMPool");}
        auto delapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-dstart).count();

        std::vector<std::string> all_contracts;
        uint32_t full_sets = 0;
        for (auto& d : deployers) {
            if (d.token_a_deployed && d.token_b_deployed && d.pool_deployed) {
                ++full_sets;
                all_contracts.push_back(d.token_a_addr);
                all_contracts.push_back(d.token_b_addr);
                all_contracts.push_back(d.pool_addr);
            }
        }
        std::cout << "\n  Deploy done in " << delapsed << "s: A=" << ta_ok.load()
                  << " B=" << tb_ok.load() << " Pool=" << pool_ok.load()
                  << " fail=" << dfail.load() << std::endl;
        std::cout << "  Full AMM sets: " << full_sets << "/" << kContractSets
                  << ", contracts for prefund: " << all_contracts.size() << std::endl;
        if (all_contracts.empty()) {
            std::cerr << "  ERROR: No contracts deployed. Aborting." << std::endl;
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }

        // ── Verify contract addresses exist on chain ─────────────────────
        std::cout << "\n  Waiting 5s for contract deployment consensus..." << std::endl;
        for (int w = 0; w < 50 && !global_stop; ++w) usleep(100000);

        std::cout << "  Verifying " << all_contracts.size() << " contract addresses (up to 300s)..." << std::endl;
        auto cv_start = std::chrono::steady_clock::now();
        uint32_t contracts_verified = 0;
        std::vector<uint32_t> cv_pending;
        cv_pending.reserve(all_contracts.size());
        for (uint32_t i = 0; i < all_contracts.size(); ++i) cv_pending.push_back(i);
        std::vector<bool> cv_confirmed(all_contracts.size(), false);

        uint32_t cv_round = 0;
        while (!cv_pending.empty() && !global_stop) {
            if (std::chrono::steady_clock::now() - cv_start >= std::chrono::seconds(300)) {
                std::cout << "  Contract verify timeout. Verified " << contracts_verified
                          << "/" << all_contracts.size() << std::endl;
                break;
            }
            ++cv_round;
            uint32_t cv_rok = 0;
            std::vector<uint32_t> cv_next;
            cv_next.reserve(cv_pending.size());
            // Contract addresses are 40-char hex (20 bytes) — normal batch size is fine
            std::vector<std::string> ba;
            std::vector<uint32_t> bi;
            for (uint32_t p = 0; p < cv_pending.size() && !global_stop; ++p) {
                ba.push_back(all_contracts[cv_pending[p]]);
                bi.push_back(cv_pending[p]);
                if (ba.size() >= 200 || p == cv_pending.size() - 1) {
                    auto r = sdk.batchQueryAccounts(ba);
                    if (r.contains("status") && r["status"] == 0 && r.contains("accounts")) {
                        for (uint32_t k = 0; k < bi.size(); ++k) {
                            if (r["accounts"].contains(ba[k])) {
                                cv_confirmed[bi[k]] = true;
                                ++contracts_verified;
                                ++cv_rok;
                            } else {
                                cv_next.push_back(bi[k]);
                            }
                        }
                    } else {
                        for (auto idx : bi) cv_next.push_back(idx);
                    }
                    ba.clear();
                    bi.clear();
                }
            }
            cv_pending = std::move(cv_next);
            auto es = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - cv_start).count();
            std::cout << "  [Contract Round " << cv_round << ", " << es << "s] +" << cv_rok
                      << ", " << contracts_verified << "/" << all_contracts.size()
                      << " verified, " << cv_pending.size() << " pending" << std::endl;
            // On round 2+ with no progress, print which contract types are pending
            if (cv_round == 2 && cv_rok == 0 && !cv_pending.empty()) {
                // all_contracts is ordered: [tokenA_0, tokenB_0, pool_0, tokenA_1, tokenB_1, pool_1, ...]
                // Index % 3: 0=TokenA, 1=TokenB, 2=AMMPool
                uint32_t pa=0, pb=0, pp=0;
                for (auto idx : cv_pending) {
                    if (idx % 3 == 0) ++pa; else if (idx % 3 == 1) ++pb; else ++pp;
                }
                std::cout << "    Pending by type: TokenA=" << pa << " TokenB=" << pb
                          << " AMMPool=" << pp << std::endl;
                // Print first 3 pending addresses
                for (uint32_t j = 0; j < std::min((uint32_t)3, (uint32_t)cv_pending.size()); ++j) {
                    std::cout << "    [" << cv_pending[j] << "] " << all_contracts[cv_pending[j]] << std::endl;
                }
            }
            if (cv_pending.empty()) break;
            uint32_t wt = (cv_rok > 0) ? 3000 : 8000;
            if (cv_round <= 2 && cv_rok == 0) wt = 15000;
            for (uint32_t w = 0; w < wt / 100 && !global_stop; ++w) usleep(100000);
        }

        // Remove unverified contracts from all_contracts
        if (contracts_verified < all_contracts.size()) {
            std::cout << "  WARNING: " << (all_contracts.size() - contracts_verified)
                      << " contracts not verified on chain." << std::endl;
            std::vector<std::string> verified_contracts;
            for (uint32_t i = 0; i < all_contracts.size(); ++i) {
                if (cv_confirmed[i]) verified_contracts.push_back(all_contracts[i]);
            }
            all_contracts = std::move(verified_contracts);
            std::cout << "  Using " << all_contracts.size() << " verified contracts." << std::endl;
        } else {
            std::cout << "  All " << contracts_verified << " contracts verified on chain." << std::endl;
        }

        if (all_contracts.empty()) {
            std::cerr << "  ERROR: No contracts verified. Aborting." << std::endl;
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }

        // Dedicated TCP sender thread — TcpTransport::Send uses per-thread
        // ReaderWriterQueues (single-producer). Worker threads push to a
        // thread-safe queue; one sender thread drains it via Send().
        struct TcpSendItem {
            transport::MessagePtr msg;
            std::string dest_ip;
            uint16_t dest_port;
        };
        std::queue<TcpSendItem> tcp_send_queue;
        std::mutex tcp_send_mtx;
        std::condition_variable tcp_send_cv;
        std::atomic<bool> tcp_sender_stop{false};
        std::atomic<uint64_t> tcp_sent_count{0};

        // Leader routing for contract calls
        std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> amm_leader_map;
        std::mutex amm_leader_mutex;
        bool amm_has_leaders = false;

        std::thread tcp_sender_thread([&]() {
            std::vector<TcpSendItem> batch;
            batch.reserve(4096);
            auto rate_start = std::chrono::steady_clock::now();
            uint64_t rate_sent = 0;
            while (!tcp_sender_stop.load()) {
                {
                    std::unique_lock<std::mutex> lk(tcp_send_mtx);
                    tcp_send_cv.wait_for(lk, std::chrono::milliseconds(1),
                        [&]{ return !tcp_send_queue.empty() || tcp_sender_stop.load(); });
                    while (!tcp_send_queue.empty()) {
                        batch.push_back(std::move(tcp_send_queue.front()));
                        tcp_send_queue.pop();
                    }
                }
                for (auto& item : batch) {
                    if (kTargetTps > 0) {
                        ++rate_sent;
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - rate_start).count();
                        int64_t expected_us = (int64_t)rate_sent * 1000000 / kTargetTps;
                        if (expected_us > elapsed_us + 100) {
                            usleep((uint32_t)(expected_us - elapsed_us));
                        }
                        if (elapsed_us >= 1000000) {
                            rate_start = std::chrono::steady_clock::now();
                            rate_sent = 0;
                        }
                    }
                    transport::TcpTransport::Instance()->Send(item.dest_ip, item.dest_port, item.msg->header);
                    ++tcp_sent_count;
                }
                batch.clear();
            }
            std::lock_guard<std::mutex> lk(tcp_send_mtx);
            while (!tcp_send_queue.empty()) {
                auto item = std::move(tcp_send_queue.front());
                tcp_send_queue.pop();
                transport::TcpTransport::Instance()->Send(item.dest_ip, item.dest_port, item.msg->header);
                ++tcp_sent_count;
            }
        });

        // Default destination
        std::string default_dest_ip = global_chain_node_ip;
        uint16_t default_dest_port = global_chain_node_http_port - 10000;

        // Map contract_addr_hex → pool_index (populated after pool deployment)
        std::unordered_map<std::string, uint32_t> contract_pool_index_map;

        // Helper: get destination for a contract address (leader routing)
        auto get_dest = [&](const std::string& contract_addr_hex) -> std::pair<std::string, uint16_t> {
            if (amm_has_leaders) {
                auto it = contract_pool_index_map.find(contract_addr_hex);
                uint32_t pool_idx;
                if (it != contract_pool_index_map.end()) {
                    pool_idx = it->second;
                } else {
                    pool_idx = common::GetAddressPoolIndex(common::Encode::HexDecode(contract_addr_hex));
                }
                std::lock_guard<std::mutex> lk(amm_leader_mutex);
                auto lit = amm_leader_map.find(pool_idx);
                if (lit != amm_leader_map.end()) {
                    return {lit->second.ip, lit->second.port};
                }
            }
            return {default_dest_ip, default_dest_port};
        };

        // Helper: get destination for a raw (binary) address — used for sender-based routing
        auto get_dest_raw = [&](const std::string& addr_raw) -> std::pair<std::string, uint16_t> {
            if (amm_has_leaders) {
                uint32_t pool_idx = common::GetAddressPoolIndex(addr_raw);
                std::lock_guard<std::mutex> lk(amm_leader_mutex);
                auto lit = amm_leader_map.find(pool_idx);
                if (lit != amm_leader_map.end()) {
                    return {lit->second.ip, lit->second.port};
                }
            }
            return {default_dest_ip, default_dest_port};
        };

        // Helper: get leader HTTP port for a contract (for nonce/balance queries)
        auto get_leader_http = [&](const std::string& contract_addr_hex) -> std::pair<std::string, uint16_t> {
            auto [ip, tcp_port] = get_dest(contract_addr_hex);
            return {ip, (uint16_t)(tcp_port + 10000)};
        };

        auto fetch_nonce_retry = [&](const std::string& contract_addr_hex,
                                     const std::string& prepay_addr_hex,
                                     int retries = 3) -> int64_t {
            auto [lip, lhttp] = get_leader_http(contract_addr_hex);
            for (int i = 0; i < retries; ++i) {
                ShardoraSDK leader_sdk(lip, lhttp);
                int64_t n = leader_sdk.fetchNonce(prepay_addr_hex);
                if (n >= 0) {
                    return n;
                }
                if (i + 1 < retries) {
                    usleep(100000);
                }
            }
            ShardoraSDK fallback_sdk(global_chain_node_ip, global_chain_node_http_port);
            return fallback_sdk.fetchNonce(prepay_addr_hex);
        };

        // Helper: enqueue a message for the sender thread (with routing)
        auto tcp_enqueue = [&](transport::MessagePtr msg, const std::string& dest_ip, uint16_t dest_port) -> bool {
            if (!msg) return false;
            {
                std::lock_guard<std::mutex> lk(tcp_send_mtx);
                tcp_send_queue.push({std::move(msg), dest_ip, dest_port});
            }
            tcp_send_cv.notify_one();
            return true;
        };

        // Shorthand: enqueue with default destination
        auto tcp_enqueue_default = [&](transport::MessagePtr msg) -> bool {
            return tcp_enqueue(std::move(msg), default_dest_ip, default_dest_port);
        };

        // ── Phase 5: Deployer adds liquidity to pools ──────────────────────
        // Each deployer: prefund on TokenA, TokenB, Pool → approve → addLiquidity
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 5: Deployer Add Liquidity (" << full_sets << " pools)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        const uint64_t kDeployerPrefund = 800000000lu;
        const uint64_t kInitialLiquidity = 5000000lu;  // 5M tokens each side
        std::atomic<uint32_t> liq_ok{0}, liq_fail{0};
        auto liq_start = std::chrono::steady_clock::now();

        // Helper: create ShardoraSDK routed to the pool leader of a contract address
        auto make_routed_sdk = [&](const std::string& contract_addr_hex) -> ShardoraSDK {
            if (amm_has_leaders) {
                std::string ca_raw = common::Encode::HexDecode(contract_addr_hex);
                uint32_t pidx = common::GetAddressPoolIndex(ca_raw);
                std::lock_guard<std::mutex> lk(amm_leader_mutex);
                auto it = amm_leader_map.find(pidx);
                if (it != amm_leader_map.end()) {
                    return ShardoraSDK(it->second.ip, it->second.port + 10000);
                }
            }
            return ShardoraSDK(global_chain_node_ip, global_chain_node_http_port);
        };

        // Step 5a: Set deployer prefund on contracts — route to contract's pool leader
        std::cout << "  Step 5a: Set deployer prefund on contracts..." << std::endl;
        {
            std::vector<std::thread> pt;
            uint32_t nt = std::min(kDeployThreads, kContractSets); if (!nt) nt = 1;
            uint32_t pp = kContractSets / nt;
            std::atomic<uint32_t> dpf_ok{0}, dpf_fail{0};

            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s = t*pp, e = (t==nt-1)?kContractSets:(s+pp);
                pt.emplace_back([&,s,e](){
                    for (uint32_t i=s;i<e&&!global_stop;++i) {
                        if (!deployers[i].token_a_deployed||!deployers[i].token_b_deployed||!deployers[i].pool_deployed) continue;
                        auto sdk_a = make_routed_sdk(deployers[i].token_a_addr);
                        auto sdk_b = make_routed_sdk(deployers[i].token_b_addr);
                        auto sdk_p = make_routed_sdk(deployers[i].pool_addr);
                        auto r1=sdk_a.setGasPrefund(deployers[i].prikey_hex, deployers[i].token_a_addr, kDeployerPrefund);
                        auto r2=sdk_b.setGasPrefund(deployers[i].prikey_hex, deployers[i].token_b_addr, kDeployerPrefund);
                        auto r3=sdk_p.setGasPrefund(deployers[i].prikey_hex, deployers[i].pool_addr, kDeployerPrefund);
                        if(r1["status"]==0&&r2["status"]==0&&r3["status"]==0) dpf_ok+=3; else dpf_fail++;
                        usleep(50000);
                    }
                });
            }
            for(auto& th:pt) th.join();
            std::cout << "    Deployer prefund: " << dpf_ok.load() << " ok, " << dpf_fail.load() << " fail" << std::endl;
        }

        // Wait for deployer prefund consensus
        std::cout << "  Waiting 5s for deployer prefund consensus..." << std::endl;
        for(int w=0;w<50&&!global_stop;++w) usleep(100000);

        // Step 5b: Deployer approve (all deployers first, then wait for consensus)
        std::cout << "  Step 5b-1: Deployer approve TokenA + TokenB for Pool..." << std::endl;
        std::atomic<uint32_t> appr5_ok{0}, appr5_fail{0};
        {
            std::vector<std::thread> pt;
            uint32_t nt = std::min(kDeployThreads, kContractSets); if (!nt) nt = 1;
            uint32_t pp = kContractSets / nt;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s = t*pp, e = (t==nt-1)?kContractSets:(s+pp);
                pt.emplace_back([&,s,e](){
                    for (uint32_t i=s;i<e&&!global_stop;++i) {
                        if (!deployers[i].token_a_deployed||!deployers[i].token_b_deployed||!deployers[i].pool_deployed) continue;
                        auto approve_str = std::to_string(kInitialLiquidity * 2);
                        // Route approve to each token's pool leader
                        auto sdk_a = make_routed_sdk(deployers[i].token_a_addr);
                        auto sdk_b = make_routed_sdk(deployers[i].token_b_addr);
                        auto ra = sdk_a.callFunctionSolidity(deployers[i].prikey_hex, deployers[i].token_a_addr, 0,
                            "approve", {"address","uint256"}, {deployers[i].pool_addr, approve_str});
                        auto rb = sdk_b.callFunctionSolidity(deployers[i].prikey_hex, deployers[i].token_b_addr, 0,
                            "approve", {"address","uint256"}, {deployers[i].pool_addr, approve_str});
                        if (ra["status"]==0 && rb["status"]==0) appr5_ok+=2; else ++appr5_fail;
                        usleep(50000);
                    }
                });
            }
            for(auto& th:pt) th.join();
        }
        std::cout << "    Approve: " << appr5_ok.load() << " ok, " << appr5_fail.load() << " fail" << std::endl;

        // Wait for approve consensus before calling addLiquidity
        std::cout << "  Waiting 5s for approve consensus..." << std::endl;
        for(int w=0;w<50&&!global_stop;++w) usleep(100000);

        // Step 5b-2: addLiquidity (now approvals are on-chain)
        std::cout << "  Step 5b-2: Deployer addLiquidity (" << kInitialLiquidity << " each)..." << std::endl;
        {
            std::vector<std::thread> pt;
            uint32_t nt = std::min(kDeployThreads, kContractSets); if (!nt) nt = 1;
            uint32_t pp = kContractSets / nt;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s = t*pp, e = (t==nt-1)?kContractSets:(s+pp);
                pt.emplace_back([&,s,e](){
                    for (uint32_t i=s;i<e&&!global_stop;++i) {
                        if (!deployers[i].token_a_deployed||!deployers[i].token_b_deployed||!deployers[i].pool_deployed) continue;
                        auto liq_str = std::to_string(kInitialLiquidity);
                        // Route addLiquidity to the pool contract's leader
                        auto sdk_pool = make_routed_sdk(deployers[i].pool_addr);
                        auto r = sdk_pool.callFunctionSolidity(deployers[i].prikey_hex, deployers[i].pool_addr, 0,
                            "addLiquidity", {"uint256","uint256"}, {liq_str, liq_str});
                        if(r["status"]==0) ++liq_ok; else ++liq_fail;
                        usleep(50000);
                    }
                });
            }
            for(auto& th:pt) th.join();
        }
        auto liq_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-liq_start).count();
        std::cout << "  Liquidity done in " << liq_elapsed << "s: " << liq_ok.load()
                  << " ok, " << liq_fail.load() << " fail" << std::endl;

        // Wait for liquidity consensus
        std::cout << "  Waiting 5s for liquidity consensus..." << std::endl;
        for(int w=0;w<50&&!global_stop;++w) usleep(100000);

        // Step 5c: Verify reserves on all pools via getReserves() query
        // getReserves() selector = 0x0902f1ac, returns (uint256, uint256)
        std::cout << "  Step 5c: Verifying pool reserves..." << std::endl;
        uint32_t pools_with_liquidity = 0, pools_no_liquidity = 0;
        for (uint32_t i = 0; i < kContractSets && !global_stop; ++i) {
            if (!deployers[i].token_a_deployed||!deployers[i].token_b_deployed||!deployers[i].pool_deployed) continue;
            auto qr = sdk.queryFunctionSolidity(deployers[i].prikey_hex, deployers[i].pool_addr,
                "getReserves", {}, {}, {"uint256","uint256"});
            bool has_liq = false;
            if (qr["status"] == 0 && qr.contains("decoded") && qr["decoded"].is_array() && qr["decoded"].size() >= 2) {
                // decoded[0] = reserveA, decoded[1] = reserveB
                try {
                    uint64_t rA = 0, rB = 0;
                    if (qr["decoded"][0].is_number()) rA = qr["decoded"][0].get<uint64_t>();
                    else if (qr["decoded"][0].is_string()) {
                        auto s = qr["decoded"][0].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rA);
                    }
                    if (qr["decoded"][1].is_number()) rB = qr["decoded"][1].get<uint64_t>();
                    else if (qr["decoded"][1].is_string()) {
                        auto s = qr["decoded"][1].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rB);
                    }
                    if (rA > 0 && rB > 0) has_liq = true;
                    if (i < 3) std::cout << "    Pool[" << i << "] reserves: A=" << rA << " B=" << rB
                                         << (has_liq ? " ✓" : " ✗") << std::endl;
                } catch (...) {}
            }
            if (has_liq) ++pools_with_liquidity; else ++pools_no_liquidity;
        }
        std::cout << "  Pools with liquidity: " << pools_with_liquidity
                  << ", without: " << pools_no_liquidity << std::endl;
        if (pools_with_liquidity == 0) {
            std::cerr << "  ERROR: No pools have liquidity. Aborting." << std::endl;
            std::cerr << "  Check that approve + addLiquidity succeeded (keccak256 selector must match solc)." << std::endl;
            tcp_sender_stop.store(true);
            tcp_send_cv.notify_one();
            tcp_sender_thread.join();
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }

        // ── Phase 6: Pair users and assign pools ──────────────────────────
        // Each pair: (UserA swaps A→B, UserB swaps B→A) on up to 3 pools
        // This guarantees matched trades — both sides of the AMM get exercised
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 6: Pair Users + Assign Pools" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        // Build confirmed user list
        std::vector<uint32_t> confirmed_users;
        for (uint32_t i = 0; i < kUserCount; ++i)
            if (users[i].confirmed) confirmed_users.push_back(i);

        // Build complete pool list (only fully deployed sets)
        struct PoolInfo {
            uint32_t deployer_idx;
            std::string token_a;
            std::string token_b;
            std::string pool;
            uint32_t pool_index;  // actual pool index from chain (for leader routing)
        };
        std::vector<PoolInfo> pools;
        for (uint32_t i = 0; i < kContractSets; ++i) {
            if (deployers[i].token_a_deployed && deployers[i].token_b_deployed && deployers[i].pool_deployed) {
                pools.push_back({i, deployers[i].token_a_addr, deployers[i].token_b_addr, deployers[i].pool_addr, 0});
            }
        }

        // Query actual pool_index for each contract from chain
        std::cout << "  Querying pool indices for " << pools.size() << " contracts..." << std::endl;
        for (auto& p : pools) {
            // Query pool contract's on-chain pool_index via batchQueryAccounts
            auto r = sdk.batchQueryAccounts({p.pool});
            bool got_pool_index = false;
            if (r.contains("status") && r["status"] == 0 && r.contains("accounts") && r["accounts"].contains(p.pool)) {
                auto& acc = r["accounts"][p.pool];
                if (acc.contains("pool_index")) {
                    p.pool_index = acc["pool_index"].get<uint32_t>();
                    got_pool_index = true;
                } else if (acc.contains("poolIndex")) {
                    p.pool_index = acc["poolIndex"].get<uint32_t>();
                    got_pool_index = true;
                }
            }
            if (!got_pool_index) {
                // Fallback: use deployer address to compute pool index.
                // Contract is deployed by deployer, so it lives in the deployer's pool.
                std::string deployer_addr = deployers[p.deployer_idx].addr_hex;
                p.pool_index = common::GetAddressPoolIndex(
                    common::Encode::HexDecode(deployer_addr));
                SHARDORA_WARN("pool %s: using deployer %s pool_index=%u (fallback)",
                    p.pool.substr(0,12).c_str(), deployer_addr.substr(0,12).c_str(), p.pool_index);
            }
            contract_pool_index_map[p.pool] = p.pool_index;
            contract_pool_index_map[p.token_a] = p.pool_index;
            contract_pool_index_map[p.token_b] = p.pool_index;
        }
        // Print first few for debug
        for (uint32_t i = 0; i < std::min((uint32_t)3, (uint32_t)pools.size()); ++i) {
            std::cout << "    Pool[" << i << "] addr=" << pools[i].pool.substr(0,12)
                      << "... pool_index=" << pools[i].pool_index << std::endl;
        }

        const uint32_t kPoolsPerPair = 1;  // each user pair trades on 1 pool
        const uint64_t kSwapAmount = 100lu;  // tokens per swap
        const uint64_t kTokenTransfer = kSwapAmount * kStressRoundsArg + 1000lu;  // enough tokens for all swap rounds + headroom
        const uint64_t kUserPrefund = 500000000lu;

        // Pair users: (confirmed_users[0], confirmed_users[1]), (confirmed_users[2], confirmed_users[3]), ...
        struct TradePair {
            uint32_t user_a_idx;  // swaps A→B
            uint32_t user_b_idx;  // swaps B→A
            std::vector<uint32_t> pool_indices;  // indices into pools[]
        };
        std::vector<TradePair> trade_pairs;
        uint32_t pair_count = confirmed_users.size() / 2;
        trade_pairs.reserve(pair_count);
        for (uint32_t p = 0; p < pair_count; ++p) {
            TradePair tp;
            tp.user_a_idx = confirmed_users[p * 2];
            tp.user_b_idx = confirmed_users[p * 2 + 1];
            // Assign up to kPoolsPerPair pools round-robin
            for (uint32_t k = 0; k < kPoolsPerPair && !pools.empty(); ++k) {
                tp.pool_indices.push_back((p * kPoolsPerPair + k) % pools.size());
            }
            trade_pairs.push_back(std::move(tp));
        }
        std::cout << "  Confirmed users: " << confirmed_users.size()
                  << ", trade pairs: " << trade_pairs.size()
                  << ", pools: " << pools.size()
                  << ", pools per pair: " << kPoolsPerPair << std::endl;

        // ── Phase 7: Set prefund for all users on their assigned contracts ─
        // Uses raw TCP for maximum throughput (same as Phase 3 account creation)
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 7: Set User Prefund (TCP fast path)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        // Initialize nonces for all confirmed users before prefund (batch query)
        std::cout << "  Initializing nonces for " << confirmed_users.size() << " users (batch)..." << std::endl;
        {
            // Collect all user addresses
            std::vector<std::string> all_addr_hex;
            all_addr_hex.reserve(confirmed_users.size());
            for (uint32_t idx : confirmed_users) {
                all_addr_hex.push_back(users[idx].addr_hex);
            }

            // Batch query via default node (which confirmed all accounts)
            const size_t kBatchSize = 300;
            std::atomic<uint32_t> nonce_init_ok{0};
            uint32_t batch_threads = std::min(8u, (uint32_t)((all_addr_hex.size() + kBatchSize - 1) / kBatchSize));
            if (batch_threads == 0) batch_threads = 1;
            uint32_t addrs_per_thread = all_addr_hex.size() / batch_threads;
            std::vector<std::thread> nonce_threads;
            std::mutex nonce_map_mtx;

            for (uint32_t t = 0; t < batch_threads; ++t) {
                uint32_t s = t * addrs_per_thread;
                uint32_t e = (t == batch_threads - 1) ? (uint32_t)all_addr_hex.size() : (s + addrs_per_thread);
                nonce_threads.emplace_back([&, s, e]() {
                    ShardoraSDK batch_sdk(global_chain_node_ip, global_chain_node_http_port);
                    for (uint32_t offset = s; offset < e && !global_stop; offset += kBatchSize) {
                        uint32_t batch_end = std::min(offset + (uint32_t)kBatchSize, e);
                        std::vector<std::string> batch(all_addr_hex.begin() + offset,
                                                       all_addr_hex.begin() + batch_end);
                        auto result = batch_sdk.batchQueryAccounts(batch);
                        if (result.contains("status") && result["status"] == 0 && result.contains("accounts")) {
                            for (uint32_t i = 0; i < batch.size(); ++i) {
                                uint32_t user_ci = offset + i;  // index into confirmed_users
                                std::string addr_raw = common::Encode::HexDecode(batch[i]);
                                int64_t nonce = 0;
                                if (result["accounts"].contains(batch[i])) {
                                    auto& acc = result["accounts"][batch[i]];
                                    if (acc.contains("nonce")) {
                                        try {
                                            auto ns = acc["nonce"].get<std::string>();
                                            std::from_chars(ns.data(), ns.data() + ns.size(), nonce);
                                        } catch (...) {}
                                    }
                                }
                                {
                                    std::lock_guard<std::mutex> lk(nonce_map_mtx);
                                    prikey_with_nonce[addr_raw] = nonce;
                                }
                                ++nonce_init_ok;
                            }
                        } else {
                            // Batch failed — default all to nonce 0
                            for (uint32_t i = 0; i < batch.size(); ++i) {
                                std::string addr_raw = common::Encode::HexDecode(batch[i]);
                                {
                                    std::lock_guard<std::mutex> lk(nonce_map_mtx);
                                    if (prikey_with_nonce.find(addr_raw) == prikey_with_nonce.end()) {
                                        prikey_with_nonce[addr_raw] = 0;
                                    }
                                }
                                ++nonce_init_ok;
                            }
                        }
                    }
                });
            }
            for (auto& th : nonce_threads) th.join();
            uint32_t nonce_ok = nonce_init_ok.load();
            std::cout << "  Nonce initialization: " << nonce_ok << "/" << confirmed_users.size() << " users" << std::endl;
            if (nonce_ok < confirmed_users.size()) {
                uint32_t nonce_fail = confirmed_users.size() - nonce_ok;
                std::cerr << "  WARNING: " << nonce_fail << " users failed nonce fetch — "
                          << "their prefund txs will use nonce=0 (may fail if account already has txs)" << std::endl;
            }
        }

        // Group prefund ops by sender (user prikey) for nonce management
        struct UserPrefundGroup {
            std::string prikey_hex;
            std::vector<std::string> contract_addrs;
        };
        std::unordered_map<std::string, uint32_t> prikey_to_group;
        std::vector<UserPrefundGroup> pf_groups;

        for (const auto& tp : trade_pairs) {
            for (uint32_t pi : tp.pool_indices) {
                const auto& pool = pools[pi];
                for (auto* uk : {&users[tp.user_a_idx].prikey_hex, &users[tp.user_b_idx].prikey_hex}) {
                    auto it = prikey_to_group.find(*uk);
                    if (it == prikey_to_group.end()) {
                        prikey_to_group[*uk] = pf_groups.size();
                        pf_groups.push_back({*uk, {}});
                        it = prikey_to_group.find(*uk);
                    }
                    pf_groups[it->second].contract_addrs.push_back(pool.token_a);
                    pf_groups[it->second].contract_addrs.push_back(pool.token_b);
                    pf_groups[it->second].contract_addrs.push_back(pool.pool);
                }
            }
        }
        uint64_t total_pf_ops = 0;
        for (auto& g : pf_groups) total_pf_ops += g.contract_addrs.size();
        std::cout << "  Total prefund ops: " << total_pf_ops
                  << ", unique senders: " << pf_groups.size() << std::endl;

        std::atomic<uint64_t> pf_ok{0}, pf_fail{0};
        auto pfstart = std::chrono::steady_clock::now();
        {
            uint32_t pf_threads = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)pf_groups.size());
            if (pf_threads == 0) pf_threads = 1;
            uint32_t groups_per_thread = pf_groups.size() / pf_threads;
            std::vector<std::thread> pt;
            for (uint32_t t = 0; t < pf_threads; ++t) {
                uint32_t s = t * groups_per_thread;
                uint32_t e = (t == pf_threads-1) ? (uint32_t)pf_groups.size() : (s + groups_per_thread);
                pt.emplace_back([&,s,e](){
                    for (uint32_t gi = s; gi < e && !global_stop; ++gi) {
                        auto& grp = pf_groups[gi];
                        std::string prikey_raw = common::Encode::HexDecode(grp.prikey_hex);
                        std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                        sec->SetPrivateKey(prikey_raw);
                        std::string addr = sec->GetAddress();
                        
                        // Read initial nonce from the shared map (safe: nonce init is done, no concurrent writes)
                        auto nonce_it = prikey_with_nonce.find(addr);
                        uint64_t local_nonce = (nonce_it != prikey_with_nonce.end()) ? nonce_it->second : 0;

                        // Use local nonce to ensure continuity across multiple prefund ops
                        for (const auto& ca : grp.contract_addrs) {
                            if (global_stop) break;
                            uint64_t next_nonce = ++local_nonce;
                            auto tx = CreateTransactionWithAttr(sec, next_nonce,
                                common::Encode::HexEncode(prikey_raw),
                                common::Encode::HexDecode(ca),
                                "prefund", "", 0, 210000, 1, shardnum);
                            // Send prefund to the default node (which confirmed all user accounts).
                            // Leader routing is unreliable here because leader nodes may not have
                            // synced the user accounts yet. The default node will internally
                            // dispatch to the correct pool.
                            if (tcp_enqueue(tx, default_dest_ip, default_dest_port)) ++pf_ok;
                            else ++pf_fail;
                            usleep(5000);  // 5ms per tx — ~200 TPS per thread
                        }
                    }
                });
            }
            std::thread pfprog([&]() {
                while (pf_ok.load()+pf_fail.load() < total_pf_ops && !global_stop) {
                    for (int i = 0; i < 20 && !global_stop; ++i) usleep(100000);
                    if (global_stop) break;
                    auto el = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now()-pfstart).count();
                    std::cout << "  [" << el << "s] prefund: " << pf_ok.load() << " ok, "
                              << pf_fail.load() << " fail / " << total_pf_ops << std::endl;
                }
            });
            for (auto& th : pt) th.join();
            pfprog.join();
        }
        // Wait for TCP sender thread to drain the queue
        {
            uint64_t prev_sent = 0;
            for (int wait = 0; wait < 100 && !global_stop; ++wait) {
                uint64_t cur_sent = tcp_sent_count.load();
                bool queue_empty = false;
                {
                    std::lock_guard<std::mutex> lk(tcp_send_mtx);
                    queue_empty = tcp_send_queue.empty();
                }
                if (queue_empty && cur_sent == prev_sent && cur_sent > 0) break;
                prev_sent = cur_sent;
                usleep(100000);  // 100ms
            }
            std::cout << "  TCP sender: " << tcp_sent_count.load() << " messages actually sent to transport" << std::endl;
        }
        auto pfelapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-pfstart).count();
        std::cout << "  Prefund done in " << pfelapsed << "s: "
                  << pf_ok.load() << " ok, " << pf_fail.load() << " fail" << std::endl;

        // Wait for prefund consensus — need enough time for all prefund txs to be confirmed
        std::cout << "  Waiting 5s for prefund consensus..." << std::endl;
        for(int w=0;w<50&&!global_stop;++w) usleep(100000);

        // Prefund prepayment accounts live on the contract's pool — fetch leaders before verify.
        {
            uint32_t lc = 0;
            if (sdk.fetchLeaders(amm_leader_map, lc) && !amm_leader_map.empty()) {
                amm_has_leaders = true;
                std::cout << "  Leader routing for prefund verify: " << lc << " pools" << std::endl;
            } else {
                std::cout << "  WARNING: Leader routing unavailable, prefund verify uses default node"
                          << std::endl;
            }
        }

        auto get_contract_pool_idx = [&](const std::string& contract_addr_hex) -> uint32_t {
            auto it = contract_pool_index_map.find(contract_addr_hex);
            if (it != contract_pool_index_map.end()) {
                return it->second;
            }
            return common::GetAddressPoolIndex(common::Encode::HexDecode(contract_addr_hex));
        };

        // Verify prefund accounts exist via batch query on each contract pool's leader
        std::cout << "  Verifying prefund accounts (batch, routed to contract pool leader)..."
                  << std::endl;
        // Build list of all prepayment addresses to verify
        struct PfVerifyItem {
            uint32_t group_idx;
            uint32_t contract_idx;
            std::string prepay_addr;       // contract_addr + user_addr (80 hex chars)
            std::string contract_addr_hex;
            uint32_t contract_pool_idx;    // pool where prepayment account is committed
        };
        std::vector<PfVerifyItem> pf_verify;
        for (uint32_t gi = 0; gi < pf_groups.size(); ++gi) {
            auto& grp = pf_groups[gi];
            std::string prikey_raw = common::Encode::HexDecode(grp.prikey_hex);
            auto sec_tmp = std::make_shared<security::Ecdsa>();
            sec_tmp->SetPrivateKey(prikey_raw);
            std::string user_addr_raw = sec_tmp->GetAddress();
            std::string user_addr = common::Encode::HexEncode(user_addr_raw);
            for (uint32_t ci = 0; ci < grp.contract_addrs.size(); ++ci) {
                const auto& ca = grp.contract_addrs[ci];
                pf_verify.push_back({
                    gi, ci, ca + user_addr, ca, get_contract_pool_idx(ca)});
            }
        }

        std::vector<bool> pf_confirmed(pf_verify.size(), false);
        uint32_t pf_verified = 0;
        std::vector<uint32_t> pf_pending;
        for (uint32_t i = 0; i < pf_verify.size(); ++i) pf_pending.push_back(i);

        const uint32_t kPfBatchSize = 50;
        for (uint32_t round = 0; round < 40 && !pf_pending.empty() && !global_stop; ++round) {
            uint32_t round_ok = 0;
            std::vector<uint32_t> next_pending;

            // Group pending items by contract_pool_idx — prepay state is on the contract's pool
            std::unordered_map<uint32_t, std::vector<uint32_t>> pool_groups;
            for (auto idx : pf_pending) {
                pool_groups[pf_verify[idx].contract_pool_idx].push_back(idx);
            }

            for (auto& [pidx, indices] : pool_groups) {
                if (global_stop) break;
                // Query the leader of the contract's pool (where prepayment accounts commit)
                std::string ldr_ip = global_chain_node_ip;
                uint16_t ldr_port = global_chain_node_http_port;
                if (amm_has_leaders) {
                    std::lock_guard<std::mutex> lk(amm_leader_mutex);
                    auto it = amm_leader_map.find(pidx);
                    if (it != amm_leader_map.end()) {
                        ldr_ip = it->second.ip;
                        ldr_port = it->second.port + 10000;
                    }
                }
                ShardoraSDK leader_sdk(ldr_ip, ldr_port);

                // Batch query this pool's pending prepay addresses
                std::vector<std::string> ba;
                std::vector<uint32_t> bi;
                for (uint32_t j = 0; j < indices.size() && !global_stop; ++j) {
                    ba.push_back(pf_verify[indices[j]].prepay_addr);
                    bi.push_back(indices[j]);
                    if (ba.size() >= kPfBatchSize || j == indices.size() - 1) {
                        auto r = leader_sdk.batchQueryAccounts(ba);
                        if (r.contains("status") && r["status"] == 0 && r.contains("accounts")) {
                            for (uint32_t k = 0; k < bi.size(); ++k) {
                                if (r["accounts"].contains(ba[k])) {
                                    pf_confirmed[bi[k]] = true;
                                    ++pf_verified; ++round_ok;
                                } else {
                                    next_pending.push_back(bi[k]);
                                }
                            }
                        } else {
                            for (auto idx2 : bi) next_pending.push_back(idx2);
                        }
                        ba.clear(); bi.clear();
                    }
                }
            }

            pf_pending = std::move(next_pending);
            std::cout << "  [PF verify round " << (round+1) << "] +" << round_ok
                      << ", " << pf_verified << "/" << pf_verify.size()
                      << ", pending: " << pf_pending.size() << std::endl;
            if (pf_pending.empty()) break;

            // After round 20, re-send pending prefunds via HTTP SDK (routed to contract pool leader)
            if (round == 19 && !pf_pending.empty()) {
                std::cout << "  [PF round 20] Re-sending " << pf_pending.size()
                          << " unconfirmed prefunds via HTTP..." << std::endl;
                std::atomic<uint32_t> resend_ok{0}, resend_fail{0};
                uint32_t resend_threads = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)pf_pending.size());
                if (resend_threads == 0) resend_threads = 1;
                uint32_t resend_per = pf_pending.size() / resend_threads;
                std::vector<std::thread> rtvec;
                for (uint32_t t = 0; t < resend_threads; ++t) {
                    uint32_t rs = t * resend_per;
                    uint32_t re = (t == resend_threads - 1) ? (uint32_t)pf_pending.size() : (rs + resend_per);
                    rtvec.emplace_back([&, rs, re]() {
                        for (uint32_t i = rs; i < re && !global_stop; ++i) {
                            auto& item = pf_verify[pf_pending[i]];
                            auto& grp = pf_groups[item.group_idx];
                            const auto& ca = grp.contract_addrs[item.contract_idx];
                            auto [retry_ip, retry_port] = get_leader_http(ca);
                            ShardoraSDK tsdk(retry_ip, retry_port);
                            auto r = tsdk.setGasPrefund(grp.prikey_hex, ca, kUserPrefund);
                            if (r["status"] == 0) ++resend_ok; else ++resend_fail;
                        }
                    });
                }
                for (auto& th : rtvec) th.join();
                std::cout << "  [PF resend] " << resend_ok.load() << " ok, "
                          << resend_fail.load() << " fail" << std::endl;
                std::cout << "  Waiting 5s for resend consensus..." << std::endl;
                for (int w = 0; w < 50 && !global_stop; ++w) usleep(100000);
            }

            // Wait before retry
            for (int w = 0; w < ((round_ok > 0) ? 30 : 80) && !global_stop; ++w) usleep(100000);
        }
        std::cout << "  Prefund verified: " << pf_verified << "/" << pf_verify.size() << std::endl;

        // Retry missing prefunds via HTTP SDK — route to contract pool leader
        if (!pf_pending.empty()) {
            std::cout << "  Retrying " << pf_pending.size() << " missing prefunds via HTTP..." << std::endl;
            std::atomic<uint32_t> retry_ok{0}, retry_fail{0};
            {
                uint32_t rt = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)pf_pending.size());
                if (rt == 0) rt = 1;
                uint32_t rpp = pf_pending.size() / rt;
                std::vector<std::thread> rthreads;
                for (uint32_t t = 0; t < rt; ++t) {
                    uint32_t s = t*rpp, e = (t==rt-1)?(uint32_t)pf_pending.size():(s+rpp);
                    rthreads.emplace_back([&,s,e](){
                        for (uint32_t i=s;i<e&&!global_stop;++i) {
                            auto& item = pf_verify[pf_pending[i]];
                            const auto& ca = pf_groups[item.group_idx].contract_addrs[item.contract_idx];
                            auto [retry_ip, retry_port] = get_leader_http(ca);
                            ShardoraSDK tsdk(retry_ip, retry_port);
                            auto r = tsdk.setGasPrefund(pf_groups[item.group_idx].prikey_hex, ca, kUserPrefund);
                            if (r["status"]==0) ++retry_ok; else ++retry_fail;
                        }
                    });
                }
                for (auto& th:rthreads) th.join();
            }
            std::cout << "  Retry: " << retry_ok.load() << " ok, " << retry_fail.load() << " fail" << std::endl;
            std::cout << "  Waiting 5s for retry consensus..." << std::endl;
            for(int w=0;w<50&&!global_stop;++w) usleep(100000);
        }

        // ── Phase 8: Deployer transfers tokens to users (TCP fast path) ────
        // Nonce for contract calls = fetchNonce(contract_addr + caller_addr)
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 8: Transfer Tokens to Users (TCP)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        // Helper: encode a contract call input (selector + ABI-encoded args)
        // For transfer(address,uint256) and approve(address,uint256)
        // NOTE: Use standard Keccak-256 selectors (solc-compatible), NOT SHA3-256.
        //   transfer(address,uint256) → 0xa9059cbb
        //   approve(address,uint256)  → 0x095ea7b3
        auto encode_call_addr_uint = [](const std::string& selector_hex,
                                    const std::string& addr_hex,
                                    uint64_t amount) -> std::string {
            std::string addr = addr_hex;
            if (addr.size() >= 2 && addr.substr(0,2) == "0x") addr = addr.substr(2);
            std::string addr_padded = std::string(64 - addr.size(), '0') + addr;
            std::stringstream ss; ss << std::hex << amount;
            std::string amt_hex = ss.str();
            std::string amt_padded = std::string(64 - amt_hex.size(), '0') + amt_hex;
            return selector_hex + addr_padded + amt_padded;
        };

        // Each transfer op: deployer calls token_addr.transfer(user, amount)
        // Nonce key = token_addr + deployer_addr
        // Group by (contract_addr, deployer_prikey) for nonce management
        struct ContractCallOp {
            std::string prikey_hex;
            std::string contract_addr;  // the contract being called (to)
            std::string caller_addr;    // caller address (for prepayment nonce)
            std::string input_data;     // ABI-encoded call
        };
        std::vector<ContractCallOp> xfer_ops;
        for (const auto& tp : trade_pairs) {
            for (uint32_t pi : tp.pool_indices) {
                const auto& pool = pools[pi];
                const auto& dpk = deployers[pool.deployer_idx].prikey_hex;
                const auto& daddr = deployers[pool.deployer_idx].addr_hex;
                // UserA gets TokenA (will swap A→B)
                xfer_ops.push_back({dpk, pool.token_a, daddr,
                    encode_call_addr_uint("a9059cbb", users[tp.user_a_idx].addr_hex, kTokenTransfer)});
                // UserB gets TokenB (will swap B→A)
                xfer_ops.push_back({dpk, pool.token_b, daddr,
                    encode_call_addr_uint("a9059cbb", users[tp.user_b_idx].addr_hex, kTokenTransfer)});
            }
        }
        uint64_t total_xfer_ops = xfer_ops.size();
        std::cout << "  Total transfer ops: " << total_xfer_ops << std::endl;

        // Group by prepayment key (contract_addr + caller_addr) for nonce
        struct NoncedCallGroup {
            std::string prikey_hex;
            std::string caller_addr;
            std::string contract_addr;
            std::vector<std::string> inputs;  // each call's ABI input
        };
        auto group_by_prepay = [](const std::vector<ContractCallOp>& ops) {
            std::unordered_map<std::string, uint32_t> key_to_idx;
            std::vector<NoncedCallGroup> groups;
            for (const auto& op : ops) {
                std::string key = op.contract_addr + op.caller_addr;
                auto it = key_to_idx.find(key);
                if (it == key_to_idx.end()) {
                    key_to_idx[key] = groups.size();
                    groups.push_back({op.prikey_hex, op.caller_addr, op.contract_addr, {}});
                    it = key_to_idx.find(key);
                }
                groups[it->second].inputs.push_back(op.input_data);
            }
            return groups;
        };

        auto parse_account_nonce = [](const nlohmann::json& acc) -> int64_t {
            if (!acc.contains("nonce")) {
                return -1;
            }
            try {
                auto ns = acc["nonce"].get<std::string>();
                int64_t n = 0;
                std::from_chars(ns.data(), ns.data() + ns.size(), n);
                return n;
            } catch (...) {
                return -1;
            }
        };

        auto try_confirm_from_batch = [&](const nlohmann::json& r,
                const std::vector<std::string>& batch,
                const std::vector<uint32_t>& batch_gi,
                std::vector<bool>& grp_confirmed,
                const std::vector<int64_t>& expected_nonces) {
            if (!r.contains("accounts")) {
                return;
            }
            for (uint32_t k = 0; k < batch_gi.size(); ++k) {
                uint32_t gi2 = batch_gi[k];
                if (grp_confirmed[gi2]) {
                    continue;
                }
                if (!r["accounts"].contains(batch[k])) {
                    continue;
                }
                int64_t n = parse_account_nonce(r["accounts"][batch[k]]);
                if (n >= expected_nonces[gi2]) {
                    grp_confirmed[gi2] = true;
                }
            }
        };

        auto fetch_nonce_confirm_groups = [&](const std::vector<uint32_t>& indices,
                const std::vector<NoncedCallGroup>& groups,
                std::vector<bool>& grp_confirmed,
                const std::vector<int64_t>& expected_nonces) {
            for (auto gi : indices) {
                if (grp_confirmed[gi]) {
                    continue;
                }
                const auto& grp = groups[gi];
                const std::string prepay = grp.contract_addr + grp.caller_addr;
                int64_t n = fetch_nonce_retry(grp.contract_addr, prepay, 2);
                if (n >= expected_nonces[gi]) {
                    grp_confirmed[gi] = true;
                }
            }
        };

        auto reconcile_unconfirmed_groups = [&](
                std::vector<uint32_t> unconfirmed,
                const std::vector<NoncedCallGroup>& groups,
                std::vector<bool>& grp_confirmed,
                const std::vector<int64_t>& expected_nonces,
                int retries = 8,
                int retry_sleep_ms = 500) -> std::vector<uint32_t> {
            for (int attempt = 0; attempt < retries && !unconfirmed.empty() && !global_stop; ++attempt) {
                std::vector<uint32_t> still;
                for (auto gi : unconfirmed) {
                    if (grp_confirmed[gi]) {
                        continue;
                    }
                    const auto& grp = groups[gi];
                    const std::string prepay = grp.contract_addr + grp.caller_addr;
                    int64_t n = fetch_nonce_retry(grp.contract_addr, prepay, 3);
                    if (n >= expected_nonces[gi]) {
                        grp_confirmed[gi] = true;
                    } else {
                        still.push_back(gi);
                    }
                }
                unconfirmed = std::move(still);
                if (unconfirmed.empty()) {
                    break;
                }
                if (attempt + 1 < retries) {
                    for (int w = 0; w < retry_sleep_ms / 100 && !global_stop; ++w) {
                        usleep(100000);
                    }
                }
            }
            return unconfirmed;
        };

        auto xfer_groups = group_by_prepay(xfer_ops);
        std::cout << "  Unique (contract,caller) groups: " << xfer_groups.size() << std::endl;

        std::atomic<uint64_t> xfer_ok{0}, xfer_fail{0};
        auto xfer_start = std::chrono::steady_clock::now();

        // Generic TCP sender for grouped contract calls
        auto send_grouped_calls = [&](std::vector<NoncedCallGroup>& groups,
                                      std::atomic<uint64_t>& ok_cnt,
                                      std::atomic<uint64_t>& fail_cnt,
                                      uint64_t total_ops,
                                      const std::string& label,
                                      std::chrono::steady_clock::time_point start_time) {
            uint32_t nt = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)groups.size());
            if (nt == 0) nt = 1;
            uint32_t gpp = groups.size() / nt;
            // Per-thread TPS share (0 = unlimited)
            uint32_t per_thread_tps = (kTargetTps > 0) ? (kTargetTps / nt + 1) : 0;
            std::vector<std::thread> threads;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s = t * gpp, e = (t == nt-1) ? (uint32_t)groups.size() : (s + gpp);
                threads.emplace_back([&,s,e,per_thread_tps](){
                    auto rate_start = std::chrono::steady_clock::now();
                    uint64_t rate_sent = 0;
                    // Per-thread caches to avoid repeated SSL connections and key setup
                    std::unordered_map<std::string, std::shared_ptr<ShardoraSDK>> sdk_cache;
                    std::unordered_map<std::string, std::shared_ptr<security::Security>> sec_cache;
                    auto get_cached_sdk = [&](const std::string& ip, uint16_t port) -> ShardoraSDK& {
                        std::string key = ip + ":" + std::to_string(port);
                        auto it = sdk_cache.find(key);
                        if (it == sdk_cache.end())
                            it = sdk_cache.emplace(key, std::make_shared<ShardoraSDK>(ip, port)).first;
                        return *it->second;
                    };
                    auto get_cached_sec = [&](const std::string& prikey_hex) -> std::shared_ptr<security::Security> {
                        auto it = sec_cache.find(prikey_hex);
                        if (it == sec_cache.end()) {
                            auto sec = std::make_shared<security::Ecdsa>();
                            sec->SetPrivateKey(common::Encode::HexDecode(prikey_hex));
                            it = sec_cache.emplace(prikey_hex, sec).first;
                        }
                        return it->second;
                    };
                    for (uint32_t gi = s; gi < e && !global_stop; ++gi) {
                        auto& grp = groups[gi];
                        std::string prikey_raw = common::Encode::HexDecode(grp.prikey_hex);
                        auto sec = get_cached_sec(grp.prikey_hex);
                        // Nonce from prepayment account via leader node
                        std::string prepay_addr = grp.contract_addr + grp.caller_addr;
                        auto [ldr_ip, ldr_http] = get_leader_http(grp.contract_addr);
                        ShardoraSDK& leader_sdk = get_cached_sdk(ldr_ip, ldr_http);
                        // Retry nonce query up to 3 times on transient failures
                        int64_t nonce = -1;
                        for (int retry = 0; retry < 3 && nonce < 0; ++retry) {
                            nonce = leader_sdk.fetchNonce(prepay_addr);
                            if (nonce < 0) {
                                std::cerr << "  [" << label << " NONCE FAIL] grp=" << gi
                                          << " retry=" << retry << "/3"
                                          << " leader=" << ldr_ip << ":" << ldr_http
                                          << " prepay=" << prepay_addr
                                          << " contract=" << grp.contract_addr
                                          << " caller=" << grp.caller_addr
                                          << " ops=" << grp.inputs.size() << std::endl;
                                if (retry < 2) usleep(200000);
                            }
                        }
                        // Fallback: if leader failed, try default node
                        if (nonce < 0) {
                            ShardoraSDK fallback_sdk(global_chain_node_ip, global_chain_node_http_port);
                            nonce = fallback_sdk.fetchNonce(prepay_addr);
                            if (nonce >= 0) {
                                std::cerr << "  [" << label << " FALLBACK OK] grp=" << gi
                                          << " default=" << global_chain_node_ip << ":" << global_chain_node_http_port
                                          << " nonce=" << nonce << std::endl;
                            }
                        }

                        if (nonce < 0) {
                            std::cerr << "  [" << label << " SKIP] grp=" << gi
                                      << " leader=" << ldr_ip << ":" << ldr_http
                                      << " prepay=" << prepay_addr
                                      << " using nonce=0 (account may not exist yet)" << std::endl;
                            nonce = 0;
                        }
                        // Cache destination for this group (avoid repeated lock + lookup per tx)
                        auto [grp_dest_ip, grp_dest_port] = get_dest(grp.contract_addr);
                        for (const auto& input : grp.inputs) {
                            if (global_stop) break;
                            // Rate limit per thread
                            if (per_thread_tps > 0) {
                                ++rate_sent;
                                auto now = std::chrono::steady_clock::now();
                                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    now - rate_start).count();
                                int64_t expected_us = (int64_t)rate_sent * 1000000 / per_thread_tps;
                                if (expected_us > elapsed_us + 100) {
                                    usleep((uint32_t)(expected_us - elapsed_us));
                                }
                                if (elapsed_us >= 1000000) {
                                    rate_start = std::chrono::steady_clock::now();
                                    rate_sent = 0;
                                }
                            }
                            auto tx = CreateTransactionWithAttr(sec, ++nonce,
                                common::Encode::HexEncode(prikey_raw),
                                common::Encode::HexDecode(grp.contract_addr),
                                "call", input, 0, 5000000, 1, shardnum);
                            if (tcp_enqueue(tx, grp_dest_ip, grp_dest_port)) ++ok_cnt;
                            else ++fail_cnt;
                        }
                    }
                });
            }
            std::thread prog([&]() {
                while (ok_cnt.load()+fail_cnt.load() < total_ops && !global_stop) {
                    for (int i = 0; i < 20 && !global_stop; ++i) usleep(100000);
                    if (global_stop) break;
                    auto el = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now()-start_time).count();
                    std::cout << "  [" << el << "s] " << label << ": " << ok_cnt.load() << " ok, "
                              << fail_cnt.load() << " fail / " << total_ops << std::endl;
                }
            });
            for (auto& th : threads) th.join();
            prog.join();
        };

        send_grouped_calls(xfer_groups, xfer_ok, xfer_fail, total_xfer_ops, "transfer", xfer_start);
        auto xfer_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-xfer_start).count();
        std::cout << "  Transfer done in " << xfer_elapsed << "s: "
                  << xfer_ok.load() << " ok, " << xfer_fail.load() << " fail" << std::endl;

        // Wait for transfer consensus
        std::cout << "  Waiting 5s for transfer consensus..." << std::endl;
        for(int w=0;w<50&&!global_stop;++w) usleep(100000);

        // ── Fetch leader routing table for contract calls ───────────────────
        {
            uint32_t lc = 0;
            if (sdk.fetchLeaders(amm_leader_map, lc) && !amm_leader_map.empty()) {
                amm_has_leaders = true;
                std::cout << "  Leader routing enabled: " << lc << " leaders" << std::endl;
                uint32_t printed = 0;
                for (auto& [pidx, info] : amm_leader_map) {
                    if (printed++ < 3) std::cout << "    pool " << pidx << " → " << info.ip << ":" << info.port << std::endl;
                }
                if (lc > 3) std::cout << "    ... (" << lc << " total)" << std::endl;
            } else {
                std::cout << "  Leader routing unavailable, using default node" << std::endl;
            }
        }

        // ── Phase 9: Users approve Pool to spend tokens (TCP fast path) ───
        // Nonce = fetchNonce(token_addr + user_addr)
        // Repeat kStressRounds times for performance stress testing
        const uint32_t kStressRounds = kStressRoundsArg;
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 9: User Approve Pool x" << kStressRounds << " (TCP stress)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        // Build approve ops — each user approves kStressRounds times (stress test).
        // Each approve sets allowance to cover ALL swap rounds so that even the
        // last approve (which overwrites previous ones) leaves enough allowance.
        const uint64_t kApproveAmount = kSwapAmount * kStressRounds + kTokenTransfer;
        std::vector<ContractCallOp> appr_ops;
        for (const auto& tp : trade_pairs) {
            for (uint32_t pi : tp.pool_indices) {
                const auto& pool = pools[pi];
                appr_ops.push_back({users[tp.user_a_idx].prikey_hex, pool.token_a,
                    users[tp.user_a_idx].addr_hex,
                    encode_call_addr_uint("095ea7b3", pool.pool, kApproveAmount)});
                appr_ops.push_back({users[tp.user_b_idx].prikey_hex, pool.token_b,
                    users[tp.user_b_idx].addr_hex,
                    encode_call_addr_uint("095ea7b3", pool.pool, kApproveAmount)});
            }
        }
        // Repeat each op kStressRounds times within its group (contract call stress test)
        auto appr_groups = group_by_prepay(appr_ops);
        uint64_t total_appr_ops = 0;
        for (auto& grp : appr_groups) {
            std::vector<std::string> expanded;
            for (uint32_t r = 0; r < kStressRounds; ++r)
                for (const auto& inp : grp.inputs) expanded.push_back(inp);
            grp.inputs = std::move(expanded);
            total_appr_ops += grp.inputs.size();
        }
        std::cout << "  Total approve ops: " << total_appr_ops
                  << " (" << appr_groups.size() << " groups x " << kStressRounds
                  << " rounds, allowance=" << kApproveAmount << ")" << std::endl;

        std::atomic<uint64_t> appr_ok{0}, appr_fail{0};
        auto appr_start = std::chrono::steady_clock::now();
        send_grouped_calls(appr_groups, appr_ok, appr_fail, total_appr_ops, "approve", appr_start);
        auto appr_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-appr_start).count();
        std::cout << "  Approve done in " << appr_elapsed << "s: "
                  << appr_ok.load() << " ok, " << appr_fail.load() << " fail" << std::endl;

        auto decode_approve_spender = [](const std::string& input) -> std::string {
            if (input.size() < 8 + 64) return "";
            const std::string& field = input.substr(8, 64);
            return field.size() >= 40 ? field.substr(field.size() - 40) : field;
        };

        auto print_call_group_diagnostics = [&](const char* label, uint32_t gi,
                const NoncedCallGroup& grp) {
            const int64_t expected_nonce = (int64_t)grp.inputs.size();
            const std::string prepay = grp.contract_addr + grp.caller_addr;
            const uint32_t pool_idx = get_contract_pool_idx(grp.contract_addr);
            auto [ldr_ip, ldr_http] = get_leader_http(grp.contract_addr);
            ShardoraSDK leader_sdk(ldr_ip, ldr_http);
            ShardoraSDK default_sdk(global_chain_node_ip, global_chain_node_http_port);

            const int64_t nonce_leader = leader_sdk.fetchNonce(prepay);
            const int64_t nonce_default = default_sdk.fetchNonce(prepay);
            const int64_t prefund_bal = leader_sdk.fetchBalance(prepay);

            std::cout << "\n    ══ " << label << " grp=" << gi << " ══" << std::endl;
            std::cout << "    contract(token) = " << grp.contract_addr << std::endl;
            std::cout << "    caller(user)    = " << grp.caller_addr << std::endl;
            std::cout << "    prepay_addr     = " << prepay << std::endl;
            std::cout << "    contract_pool   = " << pool_idx << std::endl;
            std::cout << "    leader_node     = " << ldr_ip << ":" << ldr_http << std::endl;
            std::cout << "    expected_nonce  = " << expected_nonce << std::endl;
            std::cout << "    nonce(leader)   = " << nonce_leader
                      << (nonce_leader < 0 ? " (query failed)" : "") << std::endl;
            std::cout << "    nonce(default)  = " << nonce_default
                      << (nonce_default < 0 ? " (query failed)" : "") << std::endl;
            if (nonce_leader >= 0 && nonce_leader < expected_nonce) {
                std::cout << "    nonce_gap       = missing "
                          << (expected_nonce - nonce_leader) << " approve tx(s)" << std::endl;
            }
            std::cout << "    prefund_balance = " << prefund_bal << std::endl;
            std::cout << "    ops_sent        = " << grp.inputs.size() << std::endl;
            if (!grp.inputs.empty()) {
                std::cout << "    first_input     = " << grp.inputs[0].substr(0, 72) << "..."
                          << std::endl;
                std::cout << "    approve_spender = "
                          << decode_approve_spender(grp.inputs[0]) << std::endl;
                const std::string spender = decode_approve_spender(grp.inputs[0]);
                if (!spender.empty()) {
                    auto allowance = leader_sdk.queryFunctionSolidity(
                        grp.prikey_hex, grp.contract_addr,
                        "allowance", {"address", "address"},
                        {grp.caller_addr, spender}, {"uint256"});
                    std::cout << "    allowance(leader)= "
                              << (allowance["status"] == 0
                                  ? allowance.value("return_value", "?")
                                  : allowance.dump())
                              << " (need>=" << kApproveAmount << ")" << std::endl;
                    auto bal = leader_sdk.queryFunctionSolidity(
                        grp.prikey_hex, grp.contract_addr,
                        "balanceOf", {"address"}, {grp.caller_addr}, {"uint256"});
                    std::cout << "    balanceOf(user) = "
                              << (bal["status"] == 0 ? bal.value("return_value", "?") : bal.dump())
                              << std::endl;
                }
            }
        };

        std::vector<uint32_t> appr_unconfirmed_groups;

        // Wait for approve consensus — batch query nonces on each contract pool's leader
        {
            const int kMaxWaitSec = 120;
            const int kPollIntervalMs = 2000;
            const uint32_t kBatchSize = 100;
            std::cout << "  Waiting for approve consensus (timeout " << kMaxWaitSec << "s for "
                      << total_appr_ops << " txs)..." << std::endl;
            uint32_t total_groups = appr_groups.size();
            std::vector<bool> grp_confirmed(total_groups, false);
            std::vector<std::string> prepay_addrs(total_groups);
            std::vector<int64_t> expected_nonces(total_groups);
            for (uint32_t gi = 0; gi < total_groups; ++gi) {
                prepay_addrs[gi] = appr_groups[gi].contract_addr + appr_groups[gi].caller_addr;
                expected_nonces[gi] = (int64_t)appr_groups[gi].inputs.size();
            }
            auto wait_start = std::chrono::steady_clock::now();
            uint32_t confirmed = 0;

            for (int elapsed = 0; elapsed < kMaxWaitSec && !global_stop; ) {
                std::vector<uint32_t> pending;
                for (uint32_t gi = 0; gi < total_groups; ++gi) {
                    if (!grp_confirmed[gi]) pending.push_back(gi);
                }
                if (pending.empty()) break;

                // Group pending by contract pool — prepay nonce commits on token's pool
                std::unordered_map<uint32_t, std::vector<uint32_t>> pool_pending;
                for (auto gi : pending) {
                    pool_pending[get_contract_pool_idx(appr_groups[gi].contract_addr)].push_back(gi);
                }

                for (auto& [pidx, indices] : pool_pending) {
                    if (global_stop) break;
                    std::string ldr_ip = global_chain_node_ip;
                    uint16_t ldr_http = global_chain_node_http_port;
                    if (amm_has_leaders) {
                        std::lock_guard<std::mutex> lk(amm_leader_mutex);
                        auto it = amm_leader_map.find(pidx);
                        if (it != amm_leader_map.end()) {
                            ldr_ip = it->second.ip;
                            ldr_http = (uint16_t)(it->second.port + 10000);
                        }
                    }
                    ShardoraSDK leader_sdk(ldr_ip, ldr_http);

                    std::vector<std::string> batch;
                    std::vector<uint32_t> batch_gi;
                    for (uint32_t j = 0; j < indices.size() && !global_stop; ++j) {
                        uint32_t gi = indices[j];
                        batch.push_back(prepay_addrs[gi]);
                        batch_gi.push_back(gi);
                        if (batch.size() >= kBatchSize || j == indices.size() - 1) {
                            auto r = leader_sdk.batchQueryAccounts(batch);
                            if (r.contains("status") && r["status"] == 0) {
                                try_confirm_from_batch(
                                    r, batch, batch_gi, grp_confirmed, expected_nonces);
                            }
                            std::vector<uint32_t> batch_miss;
                            batch_miss.reserve(batch_gi.size());
                            for (uint32_t k = 0; k < batch_gi.size(); ++k) {
                                if (!grp_confirmed[batch_gi[k]]) {
                                    batch_miss.push_back(batch_gi[k]);
                                }
                            }
                            if (!batch_miss.empty()) {
                                fetch_nonce_confirm_groups(
                                    batch_miss, appr_groups, grp_confirmed, expected_nonces);
                            }
                            batch.clear();
                            batch_gi.clear();
                        }
                    }
                }

                confirmed = 0;
                for (auto c : grp_confirmed) if (c) ++confirmed;

                elapsed = (int)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - wait_start).count();
                std::cout << "  [" << elapsed << "s] approve nonce confirmed: "
                          << confirmed << "/" << total_groups << std::endl;

                if (confirmed >= total_groups) {
                    std::cout << "  ✓ All " << total_groups << " approve groups confirmed" << std::endl;
                    break;
                }

                for (int w = 0; w < kPollIntervalMs / 100 && !global_stop; ++w) usleep(100000);
            }

            for (uint32_t gi = 0; gi < total_groups; ++gi) {
                if (!grp_confirmed[gi]) appr_unconfirmed_groups.push_back(gi);
            }

            appr_unconfirmed_groups = reconcile_unconfirmed_groups(
                std::move(appr_unconfirmed_groups),
                appr_groups,
                grp_confirmed,
                expected_nonces);

            if (!appr_unconfirmed_groups.empty()) {
                const int appr_elapsed_sec = (int)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - wait_start).count();
                std::cout << "  ⚠ WARNING: " << appr_unconfirmed_groups.size() << "/" << total_groups
                          << " approve groups NOT confirmed after " << appr_elapsed_sec << "s" << std::endl;
                std::cout << "  Unconfirmed group indices: ";
                for (uint32_t i = 0; i < appr_unconfirmed_groups.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << appr_unconfirmed_groups[i];
                }
                std::cout << std::endl;
                std::cout << "  ── Unconfirmed approve diagnostics (" << appr_unconfirmed_groups.size()
                          << " groups) ──" << std::endl;
                for (auto gi : appr_unconfirmed_groups) {
                    print_call_group_diagnostics("APPROVE UNCONFIRMED", gi, appr_groups[gi]);
                }
            }
        }

        // ── Phase 9.5: Verify approve on-chain via allowance query ────────
        {
            std::cout << "\n  Verifying approve on-chain (allowance + diagnostics)..." << std::endl;
            uint32_t verify_count = 0, verify_ok = 0, verify_fail = 0;
            uint32_t detail_printed = 0;
            const uint32_t kMaxDetailPrint = 10;
            uint32_t sample_limit = std::min((uint32_t)trade_pairs.size(), 20u);
            
            for (uint32_t tpi = 0; tpi < sample_limit && !global_stop; ++tpi) {
                const auto& tp = trade_pairs[tpi];
                for (uint32_t pi : tp.pool_indices) {
                    const auto& pool = pools[pi];
                    std::string owner_a = users[tp.user_a_idx].addr_hex;
                    std::string spender = pool.pool;

                    auto [ldr_ip, ldr_http] = get_leader_http(pool.token_a);
                    ShardoraSDK query_sdk(ldr_ip, ldr_http);

                    // Query allowance(owner, spender)
                    auto result = query_sdk.queryFunctionSolidity(
                        users[tp.user_a_idx].prikey_hex, pool.token_a,
                        "allowance", {"address", "address"},
                        {owner_a, spender}, {"uint256"});

                    ++verify_count;
                    uint64_t allowance_val = 0;
                    bool query_ok = false;
                    if (result["status"] == 0 && result.contains("decoded") &&
                            !result["decoded"].empty()) {
                        query_ok = true;
                        try {
                            auto decoded = result["decoded"];
                            auto& first = decoded.is_array() && !decoded.empty() ? decoded[0] : decoded;
                            if (first.is_number_unsigned())
                                allowance_val = first.get<uint64_t>();
                            else if (first.is_number())
                                allowance_val = (uint64_t)first.get<int64_t>();
                            else if (first.is_string()) {
                                std::string s = first.get<std::string>();
                                // Handle "0x..." hex or plain decimal
                                if (s.size() > 2 && s[0] == '0' && s[1] == 'x')
                                    allowance_val = std::stoull(s, nullptr, 16);
                                else
                                    allowance_val = std::stoull(s, nullptr, 10);
                            }
                        } catch (...) {}
                    }

                    if (query_ok && allowance_val >= kApproveAmount) {
                        ++verify_ok;
                        continue;
                    }

                    ++verify_fail;
                    if (detail_printed >= kMaxDetailPrint) continue;
                    ++detail_printed;

                    // ── Print full diagnostics for this failure ──
                    std::cout << "\n    ══ FAIL #" << detail_printed << " ══"
                              << " pair=" << tpi << " pool_idx=" << pi << std::endl;
                    std::cout << "    token_a     = " << pool.token_a << std::endl;
                    std::cout << "    token_b     = " << pool.token_b << std::endl;
                    std::cout << "    pool(spender)= " << pool.pool << std::endl;
                    std::cout << "    owner(userA) = " << owner_a << std::endl;
                    std::cout << "    query_node   = " << ldr_ip << ":" << ldr_http << std::endl;
                    std::cout << "    allowance_raw= " << result.value("return_value", "(none)") << std::endl;
                    std::cout << "    allowance_val= " << allowance_val << std::endl;
                    if (!query_ok)
                        std::cout << "    query_error  = " << result.dump() << std::endl;

                    // balanceOf(owner) on tokenA
                    auto bal = query_sdk.queryFunctionSolidity(
                        users[tp.user_a_idx].prikey_hex, pool.token_a,
                        "balanceOf", {"address"}, {owner_a}, {"uint256"});
                    std::cout << "    balanceOf(A) = " << (bal["status"]==0 ? bal.value("return_value","") : bal.dump()) << std::endl;

                    // nonce of prepay account (token_a + owner)
                    std::string prepay_a = pool.token_a + owner_a;
                    int64_t nonce_a = query_sdk.fetchNonce(prepay_a);
                    std::cout << "    nonce(tok+usr)= " << nonce_a << std::endl;

                    // The approve input that was sent
                    std::string approve_input = encode_call_addr_uint("095ea7b3", pool.pool, kApproveAmount);
                    std::cout << "    approve_input= " << approve_input.substr(0, 72) << "..." << std::endl;

                    // Try querying from a different node (default node) to rule out routing
                    ShardoraSDK default_sdk(global_chain_node_ip, global_chain_node_http_port);
                    auto result2 = default_sdk.queryFunctionSolidity(
                        users[tp.user_a_idx].prikey_hex, pool.token_a,
                        "allowance", {"address", "address"},
                        {owner_a, spender}, {"uint256"});
                    std::cout << "    allowance(default_node)= "
                              << (result2["status"]==0 ? result2.value("return_value","") : result2.dump())
                              << std::endl;

                    // Check if the contract exists on chain (has bytecode)
                    int64_t contract_bal = query_sdk.fetchBalance(pool.token_a);
                    std::cout << "    contract_balance= " << contract_bal << std::endl;

                    // Check user prefund on this token
                    std::string prefund_addr = pool.token_a + owner_a;
                    int64_t prefund_bal = query_sdk.fetchBalance(prefund_addr);
                    std::cout << "    user_prefund = " << prefund_bal << std::endl;
                }
            }
            
            std::cout << "\n  Allowance verification: " << verify_ok << "/" << verify_count
                      << " ok, " << verify_fail << " fail" << std::endl;
            if (verify_fail > 0 && verify_fail > verify_ok) {
                std::cout << "  ⚠ Most approves failed. Waiting 5s and retrying..." << std::endl;
                for(int w=0;w<50&&!global_stop;++w) usleep(100000);
                uint32_t retry_ok = 0;
                for (uint32_t tpi = 0; tpi < sample_limit && !global_stop; ++tpi) {
                    const auto& tp = trade_pairs[tpi];
                    for (uint32_t pi : tp.pool_indices) {
                        const auto& pool = pools[pi];
                        std::string owner_a = users[tp.user_a_idx].addr_hex;
                        auto [ldr_ip, ldr_http] = get_leader_http(pool.token_a);
                        ShardoraSDK query_sdk(ldr_ip, ldr_http);
                        auto result = query_sdk.queryFunctionSolidity(
                            users[tp.user_a_idx].prikey_hex, pool.token_a,
                            "allowance", {"address", "address"},
                            {owner_a, pool.pool}, {"uint256"});
                        if (result["status"] == 0 && result.contains("decoded")) {
                            try {
                                uint64_t v = 0;
                                auto d = result["decoded"];
                                auto& first = d.is_array() && !d.empty() ? d[0] : d;
                                if (first.is_number()) v = first.get<uint64_t>();
                                else if (first.is_string()) {
                                    std::string s = first.get<std::string>();
                                    v = std::stoull(s, nullptr, s.find("0x")==0 ? 16 : 10);
                                }
                                if (v >= kApproveAmount) ++retry_ok;
                            } catch (...) {}
                        }
                    }
                }
                std::cout << "  Retry verification: " << retry_ok << "/" << verify_count
                          << " ok" << std::endl;
            }
        }

        // ── Phase 10: Execute AMM Swaps (stress test, TCP fast path) ──────
        // Each pair repeats kStressRounds swap rounds on each pool.
        // Each round: UserA swaps A→B, UserB swaps B→A.
        // Uses the same grouped-call pattern as approve for reliable nonce management.
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 10: AMM Swap Stress Test x" << kStressRounds << " (TCP)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        // Refresh leader routing before swap to ensure we have current leaders
        {
            std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> fresh_leaders;
            uint32_t lc = 0;
            if (sdk.fetchLeaders(fresh_leaders, lc) && !fresh_leaders.empty()) {
                std::lock_guard<std::mutex> lk(amm_leader_mutex);
                amm_leader_map = fresh_leaders;
                amm_has_leaders = true;
                std::cout << "  Leader routing refreshed: " << lc << " leaders" << std::endl;
            } else {
                std::cout << "  WARNING: Leader refresh failed, using stale routing" << std::endl;
            }
        }

        // Print routing diagnostics for first few pools
        {
            uint32_t printed = 0;
            for (const auto& pool : pools) {
                if (printed >= 5) break;
                auto [ip, port] = get_dest(pool.pool);
                std::cout << "  [route] pool=" << pool.pool.substr(0,12) << "..."
                          << " pool_idx=" << pool.pool_index
                          << " → " << ip << ":" << port << std::endl;
                ++printed;
            }
        }

        // Helper: encode swap call input
        auto encode_swap_input = [](const std::string& selector_hex,
                                    uint64_t amountIn, uint64_t minOut) -> std::string {
            std::stringstream ss1; ss1 << std::hex << amountIn;
            std::string a1 = ss1.str(); a1 = std::string(64 - a1.size(), '0') + a1;
            std::stringstream ss2; ss2 << std::hex << minOut;
            std::string a2 = ss2.str(); a2 = std::string(64 - a2.size(), '0') + a2;
            return selector_hex + a1 + a2;
        };

        // Pre-encode swap inputs (same for every round)
        std::string input_swap_a = encode_swap_input("553a1db7", kSwapAmount, 0);
        std::string input_swap_b = encode_swap_input("5938c86b", kSwapAmount, 0);

        // Build swap ops using the same ContractCallOp pattern as approve
        std::vector<ContractCallOp> swap_ops;
        for (const auto& tp : trade_pairs) {
            for (uint32_t pi : tp.pool_indices) {
                const auto& pool = pools[pi];
                for (uint32_t round = 0; round < kStressRounds; ++round) {
                    // UserA swaps A→B on pool
                    swap_ops.push_back({users[tp.user_a_idx].prikey_hex, pool.pool,
                        users[tp.user_a_idx].addr_hex, input_swap_a});
                    // UserB swaps B→A on pool
                    swap_ops.push_back({users[tp.user_b_idx].prikey_hex, pool.pool,
                        users[tp.user_b_idx].addr_hex, input_swap_b});
                }
            }
        }
        uint64_t total_swaps = swap_ops.size();
        auto swap_groups = group_by_prepay(swap_ops);
        std::cout << "  Trade pairs: " << trade_pairs.size()
                  << ", rounds: " << kStressRounds
                  << ", total swap ops: " << total_swaps
                  << " (" << swap_groups.size() << " groups)" << std::endl;

        std::unordered_map<std::string, const PoolInfo*> pool_by_addr;
        for (const auto& p : pools) pool_by_addr[p.pool] = &p;

        auto decode_swap_selector = [](const std::string& input) -> std::string {
            return input.size() >= 8 ? input.substr(0, 8) : "";
        };

        auto print_swap_group_diagnostics = [&](const char* label, uint32_t gi,
                const NoncedCallGroup& grp) {
            const int64_t expected_nonce = (int64_t)grp.inputs.size();
            const std::string prepay = grp.contract_addr + grp.caller_addr;
            const uint32_t pool_idx = get_contract_pool_idx(grp.contract_addr);
            auto [ldr_ip, ldr_http] = get_leader_http(grp.contract_addr);
            ShardoraSDK leader_sdk(ldr_ip, ldr_http);
            ShardoraSDK default_sdk(global_chain_node_ip, global_chain_node_http_port);

            const int64_t nonce_leader = leader_sdk.fetchNonce(prepay);
            const int64_t nonce_default = default_sdk.fetchNonce(prepay);
            const int64_t prefund_bal = leader_sdk.fetchBalance(prepay);
            const uint32_t pool_idx_addr = common::GetAddressPoolIndex(
                common::Encode::HexDecode(grp.contract_addr));

            std::cout << "\n    ══ " << label << " grp=" << gi << " ══" << std::endl;
            std::cout << "    contract(pool)  = " << grp.contract_addr << std::endl;
            std::cout << "    caller(user)    = " << grp.caller_addr << std::endl;
            std::cout << "    prepay_addr     = " << prepay << std::endl;
            std::cout << "    contract_pool   = " << pool_idx << std::endl;
            if (pool_idx != pool_idx_addr) {
                std::cout << "    pool_idx(addr)  = " << pool_idx_addr
                          << " (MISMATCH — batch confirm on wrong leader if used)" << std::endl;
            }
            std::cout << "    leader_node     = " << ldr_ip << ":" << ldr_http << std::endl;
            std::cout << "    expected_nonce  = " << expected_nonce << std::endl;
            std::cout << "    nonce(leader)   = " << nonce_leader
                      << (nonce_leader < 0 ? " (query failed)" : "") << std::endl;
            std::cout << "    nonce(default)  = " << nonce_default
                      << (nonce_default < 0 ? " (query failed)" : "") << std::endl;
            if (nonce_leader >= expected_nonce) {
                std::cout << "    confirm_status  = nonce OK on leader — likely false unconfirmed"
                          << " (wrong leader in batch poll or batch miss)" << std::endl;
            } else if (nonce_leader >= 0 && nonce_leader < expected_nonce) {
                std::cout << "    nonce_gap       = missing "
                          << (expected_nonce - nonce_leader) << " swap tx(s)" << std::endl;
            }
            std::cout << "    prefund_balance = " << prefund_bal << std::endl;
            std::cout << "    ops_sent        = " << grp.inputs.size() << std::endl;
            if (!grp.inputs.empty()) {
                const std::string& first = grp.inputs[0];
                std::cout << "    first_input     = " << first.substr(0, std::min<size_t>(72, first.size()));
                if (first.size() > 72) std::cout << "...";
                std::cout << std::endl;
                const std::string selector = decode_swap_selector(first);
                const char* direction = "?";
                if (selector == "553a1db7") direction = "A→B (token0→token1)";
                else if (selector == "5938c86b") direction = "B→A (token1→token0)";
                std::cout << "    swap_selector   = 0x" << selector << " (" << direction << ")" << std::endl;

                const PoolInfo* pinfo = nullptr;
                auto pit = pool_by_addr.find(grp.contract_addr);
                if (pit != pool_by_addr.end()) pinfo = pit->second;

                if (pinfo) {
                    const std::string& token_addr = (selector == "5938c86b")
                        ? pinfo->token_b : pinfo->token_a;
                    auto allowance = leader_sdk.queryFunctionSolidity(
                        grp.prikey_hex, token_addr,
                        "allowance", {"address", "address"},
                        {grp.caller_addr, grp.contract_addr}, {"uint256"});
                    std::cout << "    allowance(leader)= "
                              << (allowance["status"] == 0
                                  ? allowance.value("return_value", "?")
                                  : allowance.dump())
                              << " (need>=" << kSwapAmount << ")" << std::endl;
                    auto bal = leader_sdk.queryFunctionSolidity(
                        grp.prikey_hex, token_addr,
                        "balanceOf", {"address"}, {grp.caller_addr}, {"uint256"});
                    std::cout << "    balanceOf(user) = "
                              << (bal["status"] == 0 ? bal.value("return_value", "?") : bal.dump())
                              << std::endl;
                } else {
                    std::cout << "    (pool token lookup failed)" << std::endl;
                }
            }
        };

        std::atomic<uint64_t> swap_ok{0}, swap_fail{0};
        auto swap_start = std::chrono::steady_clock::now();
        send_grouped_calls(swap_groups, swap_ok, swap_fail, total_swaps, "swap", swap_start);
        auto swap_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-swap_start).count();
        double swap_tps = (swap_elapsed > 0) ? (double)(swap_ok.load()+swap_fail.load()) / swap_elapsed : 0;
        std::cout << "  Swap done in " << swap_elapsed << "s: "
                  << swap_ok.load() << " ok, " << swap_fail.load() << " fail"
                  << " (" << std::fixed << std::setprecision(1) << swap_tps << " tx/s)" << std::endl;

        // ── Phase 10b: Verify swap results ─────────────────────────────────
        // Wait for consensus — batch query nonces via per-pool leaders
        {
            // ~1.5k–3k contract executes/s cluster-wide; scale timeout with load.
            const int kMaxWaitSec = std::min(900, std::max(120,
                (int)(total_swaps / 1500) + 120));
            const int kPollIntervalMs = 2000;
            const uint32_t kBatchSize = 100;
            std::cout << "\n  Waiting for swap consensus (timeout " << kMaxWaitSec << "s for "
                      << total_swaps << " txs)..." << std::endl;
            uint32_t total_groups = swap_groups.size();
            std::vector<bool> grp_confirmed(total_groups, false);
            std::vector<std::string> prepay_addrs(total_groups);
            std::vector<int64_t> expected_nonces(total_groups);
            for (uint32_t gi = 0; gi < total_groups; ++gi) {
                prepay_addrs[gi] = swap_groups[gi].contract_addr + swap_groups[gi].caller_addr;
                expected_nonces[gi] = (int64_t)swap_groups[gi].inputs.size();
            }
            auto wait_start = std::chrono::steady_clock::now();
            uint32_t confirmed = 0;

            for (int elapsed = 0; elapsed < kMaxWaitSec && !global_stop; ) {
                std::vector<uint32_t> pending;
                confirmed = 0;
                for (uint32_t gi = 0; gi < total_groups; ++gi) {
                    if (grp_confirmed[gi]) ++confirmed;
                    else pending.push_back(gi);
                }
                if (pending.empty()) break;

                // Group pending by contract pool leader — use on-chain pool_index (same as
                // approve wait and send_grouped_calls), NOT GetAddressPoolIndex(contract).
                std::unordered_map<uint32_t, std::vector<uint32_t>> pool_pending;
                for (auto gi : pending) {
                    pool_pending[get_contract_pool_idx(swap_groups[gi].contract_addr)].push_back(gi);
                }

                for (auto& [pidx, indices] : pool_pending) {
                    if (global_stop) break;
                    std::string ldr_ip = global_chain_node_ip;
                    uint16_t ldr_http = global_chain_node_http_port;
                    if (amm_has_leaders) {
                        std::lock_guard<std::mutex> lk(amm_leader_mutex);
                        auto it = amm_leader_map.find(pidx);
                        if (it != amm_leader_map.end()) {
                            ldr_ip = it->second.ip;
                            ldr_http = (uint16_t)(it->second.port + 10000);
                        }
                    }
                    ShardoraSDK leader_sdk(ldr_ip, ldr_http);

                    std::vector<std::string> batch;
                    std::vector<uint32_t> batch_gi;
                    for (uint32_t j = 0; j < indices.size() && !global_stop; ++j) {
                        uint32_t gi = indices[j];
                        batch.push_back(prepay_addrs[gi]);
                        batch_gi.push_back(gi);
                        if (batch.size() >= kBatchSize || j == indices.size() - 1) {
                            auto r = leader_sdk.batchQueryAccounts(batch);
                            if (r.contains("status") && r["status"] == 0) {
                                try_confirm_from_batch(
                                    r, batch, batch_gi, grp_confirmed, expected_nonces);
                            }
                            std::vector<uint32_t> batch_miss;
                            batch_miss.reserve(batch_gi.size());
                            for (uint32_t k = 0; k < batch_gi.size(); ++k) {
                                if (!grp_confirmed[batch_gi[k]]) {
                                    batch_miss.push_back(batch_gi[k]);
                                }
                            }
                            if (!batch_miss.empty()) {
                                fetch_nonce_confirm_groups(
                                    batch_miss, swap_groups, grp_confirmed, expected_nonces);
                            }
                            batch.clear();
                            batch_gi.clear();
                        }
                    }
                }

                confirmed = 0;
                for (auto c : grp_confirmed) if (c) ++confirmed;

                elapsed = (int)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - wait_start).count();
                std::cout << "  [" << elapsed << "s] swap nonce confirmed: "
                          << confirmed << "/" << total_groups << std::endl;

                if (confirmed >= total_groups) {
                    std::cout << "  ✓ All " << total_groups << " swap groups confirmed" << std::endl;
                    break;
                }

                for (int w = 0; w < kPollIntervalMs / 100 && !global_stop; ++w) usleep(100000);
            }

            std::vector<uint32_t> swap_unconfirmed_groups;
            for (uint32_t gi = 0; gi < total_groups; ++gi) {
                if (!grp_confirmed[gi]) swap_unconfirmed_groups.push_back(gi);
            }

            swap_unconfirmed_groups = reconcile_unconfirmed_groups(
                std::move(swap_unconfirmed_groups),
                swap_groups,
                grp_confirmed,
                expected_nonces);

            if (!swap_unconfirmed_groups.empty()) {
                const int swap_wait_elapsed_sec = (int)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - wait_start).count();
                std::cout << "  ⚠ WARNING: " << swap_unconfirmed_groups.size() << "/" << total_groups
                          << " swap groups NOT confirmed after " << swap_wait_elapsed_sec << "s" << std::endl;
                std::cout << "  Unconfirmed group indices: ";
                for (uint32_t i = 0; i < swap_unconfirmed_groups.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << swap_unconfirmed_groups[i];
                }
                std::cout << std::endl;
                std::cout << "  ── Unconfirmed swap diagnostics (" << swap_unconfirmed_groups.size()
                          << " groups) ──" << std::endl;
                for (auto gi : swap_unconfirmed_groups) {
                    print_swap_group_diagnostics("SWAP UNCONFIRMED", gi, swap_groups[gi]);
                }
            }
        }

        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "  Phase 10b: Verify Contract Execution Results" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        uint32_t verify_ok = 0, verify_fail = 0, verify_skip = 0;
        // Sample verification: check a subset of pools and users
        uint32_t verify_count = std::min((uint32_t)pools.size(), (uint32_t)20);

        // 1. Verify pool reserves: reserveA > 0 && reserveB > 0 (liquidity still exists)
        //    After matched swaps (A→B then B→A), reserves should be close to initial
        std::cout << "  [1] Verifying pool reserves (getReserves)..." << std::endl;
        uint32_t reserves_ok = 0, reserves_fail = 0;
        for (uint32_t i = 0; i < verify_count && !global_stop; ++i) {
            auto qr = sdk.queryFunctionSolidity(deployers[pools[i].deployer_idx].prikey_hex,
                pools[i].pool, "getReserves", {}, {}, {"uint256","uint256"});
            if (qr["status"] == 0 && qr.contains("decoded") && qr["decoded"].is_array() && qr["decoded"].size() >= 2) {
                uint64_t rA = 0, rB = 0;
                try {
                    if (qr["decoded"][0].is_number()) rA = qr["decoded"][0].get<uint64_t>();
                    else if (qr["decoded"][0].is_string()) {
                        auto s = qr["decoded"][0].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rA);
                    }
                    if (qr["decoded"][1].is_number()) rB = qr["decoded"][1].get<uint64_t>();
                    else if (qr["decoded"][1].is_string()) {
                        auto s = qr["decoded"][1].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rB);
                    }
                } catch (...) {}
                if (rA > 0 && rB > 0) {
                    ++reserves_ok;
                    if (i < 3) std::cout << "    Pool[" << i << "] reserves: A=" << rA << " B=" << rB << " ✓" << std::endl;
                } else {
                    ++reserves_fail;
                    if (i < 5) std::cout << "    Pool[" << i << "] reserves: A=" << rA << " B=" << rB << " ✗ (empty!)" << std::endl;
                }
            } else {
                ++reserves_fail;
                if (i < 5) std::cout << "    Pool[" << i << "] query failed" << std::endl;
            }
        }
        std::cout << "    Reserves: " << reserves_ok << "/" << verify_count << " ok" << std::endl;
        verify_ok += reserves_ok; verify_fail += reserves_fail;

        // 2. Verify prepayment nonces: poll until all reach expected value or timeout
        //    This confirms all swap transactions were actually processed on-chain
        std::cout << "  [2] Verifying swap nonces (prepayment accounts, polling up to "
                  << std::min(600, std::max(180, (int)kStressRounds * 15)) << "s)..." << std::endl;
        uint32_t nonce_ok = 0, nonce_fail = 0, nonce_skip = 0;
        uint32_t nonce_check_count = std::min((uint32_t)trade_pairs.size(), (uint32_t)50);

        // Build list of (prepay_addr, expected_min_nonce) to check
        struct NonceCheck {
            std::string prepay_addr;
            std::string pool_addr;  // for leader routing
            int64_t expected_min;
            std::string label;
        };
        std::vector<NonceCheck> nonce_checks;
        for (uint32_t pi = 0; pi < nonce_check_count; ++pi) {
            const auto& tp = trade_pairs[pi];
            for (uint32_t pool_idx : tp.pool_indices) {
                const auto& pool = pools[pool_idx];
                nonce_checks.push_back({pool.pool + users[tp.user_a_idx].addr_hex,
                    pool.pool, (int64_t)kStressRounds, "Pair[" + std::to_string(pi) + "] UserA"});
                nonce_checks.push_back({pool.pool + users[tp.user_b_idx].addr_hex,
                    pool.pool, (int64_t)kStressRounds, "Pair[" + std::to_string(pi) + "] UserB"});
            }
        }

        // Poll in rounds until all confirmed or timeout (scale with stress rounds)
        const int kNoncePollMaxSec = std::min(600, std::max(180, (int)kStressRounds * 15));
        std::vector<bool> nonce_confirmed(nonce_checks.size(), false);
        auto nonce_start = std::chrono::steady_clock::now();
        for (uint32_t round = 0; round < 60 && !global_stop; ++round) {
            uint32_t round_ok = 0, still_pending = 0;
            for (uint32_t i = 0; i < nonce_checks.size(); ++i) {
                if (nonce_confirmed[i]) continue;
                int64_t n = fetch_nonce_retry(
                    nonce_checks[i].pool_addr, nonce_checks[i].prepay_addr);
                if (n >= nonce_checks[i].expected_min) {
                    nonce_confirmed[i] = true;
                    ++round_ok;
                } else {
                    ++still_pending;
                }
            }
            nonce_ok = 0;
            for (auto c : nonce_confirmed) if (c) ++nonce_ok;
            auto es = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - nonce_start).count();
            std::cout << "    [Round " << (round+1) << ", " << es << "s] " << nonce_ok
                      << "/" << nonce_checks.size() << " confirmed" << std::endl;
            if (still_pending == 0) break;
            if (es > kNoncePollMaxSec) break;
            for (int w = 0; w < ((round_ok > 0) ? 30 : 60) && !global_stop; ++w) usleep(100000);
        }
        // Final pass: reconcile transient query failures
        for (uint32_t i = 0; i < nonce_checks.size(); ++i) {
            if (nonce_confirmed[i]) continue;
            int64_t n = fetch_nonce_retry(
                nonce_checks[i].pool_addr, nonce_checks[i].prepay_addr, 5);
            if (n >= nonce_checks[i].expected_min) {
                nonce_confirmed[i] = true;
            }
        }
        nonce_ok = 0; nonce_fail = 0;
        for (uint32_t i = 0; i < nonce_checks.size(); ++i) {
            if (nonce_confirmed[i]) ++nonce_ok; else ++nonce_fail;
        }
        if (nonce_fail > 0) {
            uint32_t printed = 0;
            for (uint32_t i = 0; i < nonce_checks.size() && printed < 10; ++i) {
                if (!nonce_confirmed[i]) {
                    int64_t n = fetch_nonce_retry(
                        nonce_checks[i].pool_addr, nonce_checks[i].prepay_addr, 5);
                    if (n >= nonce_checks[i].expected_min) {
                        nonce_confirmed[i] = true;
                        ++nonce_ok;
                        --nonce_fail;
                        continue;
                    }
                    auto [lip, lport] = get_leader_http(nonce_checks[i].pool_addr);
                    std::string pool_hex = nonce_checks[i].pool_addr;
                    std::string user_hex = nonce_checks[i].prepay_addr.substr(pool_hex.size());
                    uint32_t pool_idx = common::GetAddressPoolIndex(common::Encode::HexDecode(pool_hex));
                    std::cout << "    [" << (printed+1) << "] " << nonce_checks[i].label
                              << " pool=" << pool_hex.substr(0,12) << "..."
                              << " user=" << user_hex.substr(0,12) << "..."
                              << " pool_idx=" << pool_idx
                              << " leader=" << lip << ":" << lport
                              << " nonce=" << n
                              << " expected>=" << nonce_checks[i].expected_min
                              << " ✗" << std::endl;
                    ++printed;
                }
            }
            if (nonce_fail > 10) std::cout << "    ... (" << nonce_fail << " total failures)" << std::endl;
        }
        std::cout << "    Nonces: " << nonce_ok << " ok, " << nonce_fail << " fail" << std::endl;
        verify_ok += nonce_ok; verify_fail += nonce_fail; verify_skip += nonce_skip;

        // 3. Verify token balances: deployer should still have tokens (not drained)
        //    Users should have non-zero balances after swaps
        std::cout << "  [3] Verifying token balances (balanceOf)..." << std::endl;
        uint32_t bal_ok = 0, bal_fail = 0;
        uint32_t bal_check_count = std::min((uint32_t)pools.size(), (uint32_t)10);
        for (uint32_t i = 0; i < bal_check_count && !global_stop; ++i) {
            const auto& pool = pools[i];
            const auto& dpk = deployers[pool.deployer_idx].prikey_hex;
            const auto& daddr = deployers[pool.deployer_idx].addr_hex;
            // Query deployer's TokenA balance
            auto qr = sdk.queryFunctionSolidity(dpk, pool.token_a,
                "balanceOf", {"address"}, {daddr}, {"uint256"});
            if (qr["status"] == 0 && qr.contains("decoded")) {
                uint64_t bal = 0;
                try {
                    if (qr["decoded"].is_array() && qr["decoded"].size() > 0) {
                        if (qr["decoded"][0].is_number()) bal = qr["decoded"][0].get<uint64_t>();
                        else if (qr["decoded"][0].is_string()) {
                            auto s = qr["decoded"][0].get<std::string>();
                            std::from_chars(s.data(), s.data()+s.size(), bal);
                        }
                    }
                } catch (...) {}
                if (bal > 0) {
                    ++bal_ok;
                    if (i < 3) std::cout << "    Pool[" << i << "] deployer TokenA bal=" << bal << " ✓" << std::endl;
                } else {
                    ++bal_fail;
                    if (i < 5) std::cout << "    Pool[" << i << "] deployer TokenA bal=0 ✗" << std::endl;
                }
            } else {
                ++bal_fail;
            }
        }
        std::cout << "    Balances: " << bal_ok << "/" << bal_check_count << " ok" << std::endl;
        verify_ok += bal_ok; verify_fail += bal_fail;

        // 4. Verify AMM invariant: reserveA * reserveB should be >= initial k
        //    k = initialLiquidity^2 = 5000000 * 5000000 = 25 * 10^12
        //    After swaps, k should be maintained or slightly increased (due to rounding)
        std::cout << "  [4] Verifying AMM invariant (k = reserveA * reserveB)..." << std::endl;
        uint64_t initial_k = kInitialLiquidity * kInitialLiquidity;
        uint32_t k_ok = 0, k_fail = 0;
        for (uint32_t i = 0; i < verify_count && !global_stop; ++i) {
            auto qr = sdk.queryFunctionSolidity(deployers[pools[i].deployer_idx].prikey_hex,
                pools[i].pool, "getReserves", {}, {}, {"uint256","uint256"});
            if (qr["status"] == 0 && qr.contains("decoded") && qr["decoded"].is_array() && qr["decoded"].size() >= 2) {
                uint64_t rA = 0, rB = 0;
                try {
                    if (qr["decoded"][0].is_number()) rA = qr["decoded"][0].get<uint64_t>();
                    else if (qr["decoded"][0].is_string()) {
                        auto s = qr["decoded"][0].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rA);
                    }
                    if (qr["decoded"][1].is_number()) rB = qr["decoded"][1].get<uint64_t>();
                    else if (qr["decoded"][1].is_string()) {
                        auto s = qr["decoded"][1].get<std::string>();
                        std::from_chars(s.data(), s.data()+s.size(), rB);
                    }
                } catch (...) {}
                // Use __int128 to avoid overflow for large reserves
                __int128 k = (__int128)rA * (__int128)rB;
                __int128 k0 = (__int128)initial_k;
                if (k >= k0) {
                    ++k_ok;
                    if (i < 3) std::cout << "    Pool[" << i << "] k=" << (uint64_t)(k/1000000) << "M >= k0="
                                         << (uint64_t)(k0/1000000) << "M ✓" << std::endl;
                } else {
                    ++k_fail;
                    if (i < 5) std::cout << "    Pool[" << i << "] k=" << (uint64_t)(k/1000000) << "M < k0="
                                         << (uint64_t)(k0/1000000) << "M ✗ (invariant violated!)" << std::endl;
                }
            }
        }
        std::cout << "    AMM invariant: " << k_ok << "/" << verify_count << " ok" << std::endl;
        verify_ok += k_ok; verify_fail += k_fail;

        std::cout << "\n  Verification summary: " << verify_ok << " ok, " << verify_fail << " fail, "
                  << verify_skip << " skip" << std::endl;
        if (verify_fail == 0) {
            std::cout << "  ✅ All verifications PASSED" << std::endl;
        } else {
            std::cout << "  ⚠️  Some verifications FAILED (" << verify_fail << ")" << std::endl;
        }

        // ── Phase 11: Summary + save ──────────────────────────────────────
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "  AMM STRESS TEST COMPLETE" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout << "  Users:      " << users_ok << "/" << kUserCount << std::endl;
        std::cout << "  Deployers:  " << deployers_ok << "/" << kContractSets << std::endl;
        std::cout << "  Contracts:  A=" << ta_ok.load() << " B=" << tb_ok.load()
                  << " Pool=" << pool_ok.load() << " (full: " << full_sets << ")" << std::endl;
        std::cout << "  Liquidity:  " << liq_ok.load() << " ok, " << liq_fail.load() << " fail" << std::endl;
        std::cout << "  Prefund:    " << pf_ok.load() << " ok, " << pf_fail.load() << " fail" << std::endl;
        std::cout << "  Transfers:  " << xfer_ok.load() << " ok, " << xfer_fail.load() << " fail" << std::endl;
        std::cout << "  Approves:   " << appr_ok.load() << " ok, " << appr_fail.load() << " fail" << std::endl;
        std::cout << "  Swaps:      " << swap_ok.load() << " ok, " << swap_fail.load() << " fail"
                  << " (" << std::fixed << std::setprecision(1) << swap_tps << " tx/s)" << std::endl;
        std::cout << "  Time:       deploy=" << delapsed << "s liq=" << liq_elapsed
                  << "s prefund=" << pfelapsed << "s xfer=" << xfer_elapsed
                  << "s approve=" << appr_elapsed << "s swap=" << swap_elapsed << "s" << std::endl;

        // Save results to JSON
        {
            json res;
            res["user_count"] = kUserCount;
            res["users_confirmed"] = users_ok;
            res["contract_sets"] = kContractSets;
            res["full_amm_sets"] = full_sets;
            res["trade_pairs"] = trade_pairs.size();
            res["pools_per_pair"] = kPoolsPerPair;
            res["swap_amount"] = kSwapAmount;
            res["swap_ok"] = swap_ok.load();
            res["swap_fail"] = swap_fail.load();
            res["swap_tps"] = swap_tps;
            res["swap_elapsed_s"] = swap_elapsed;
            res["prefund_ok"] = pf_ok.load();
            res["prefund_fail"] = pf_fail.load();
            res["transfer_ok"] = xfer_ok.load();
            res["transfer_fail"] = xfer_fail.load();
            res["approve_ok"] = appr_ok.load();
            res["approve_fail"] = appr_fail.load();
            res["liquidity_ok"] = liq_ok.load();
            res["liquidity_fail"] = liq_fail.load();
            res["stress_rounds"] = kStressRounds;
            res["target_tps"] = kTargetTps;

            // Verification results
            json vr;
            vr["reserves_ok"] = reserves_ok;
            vr["reserves_fail"] = reserves_fail;
            vr["nonce_ok"] = nonce_ok;
            vr["nonce_fail"] = nonce_fail;
            vr["nonce_skip"] = nonce_skip;
            vr["balance_ok"] = bal_ok;
            vr["balance_fail"] = bal_fail;
            vr["amm_invariant_ok"] = k_ok;
            vr["amm_invariant_fail"] = k_fail;
            vr["total_ok"] = verify_ok;
            vr["total_fail"] = verify_fail;
            vr["all_passed"] = (verify_fail == 0);
            res["verification"] = vr;

            // Confirmed users
            json ul = json::array();
            for (uint32_t i = 0; i < kUserCount; ++i) {
                if (users[i].confirmed) {
                    json u;
                    u["prikey"] = users[i].prikey_hex;
                    u["addr"] = users[i].addr_hex;
                    ul.push_back(u);
                }
            }
            res["users"] = ul;

            // Contract deployment info
            json cl = json::array();
            for (auto& d : deployers) {
                if (d.token_a_deployed || d.token_b_deployed || d.pool_deployed) {
                    json c;
                    c["deployer"] = d.addr_hex;
                    c["token_a"] = d.token_a_addr;
                    c["token_b"] = d.token_b_addr;
                    c["pool"] = d.pool_addr;
                    c["complete"] = (d.token_a_deployed && d.token_b_deployed && d.pool_deployed);
                    cl.push_back(c);
                }
            }
            res["contracts"] = cl;

            // Trade pair results
            json tpl = json::array();
            for (const auto& tp : trade_pairs) {
                json t;
                t["user_a"] = users[tp.user_a_idx].addr_hex;
                t["user_b"] = users[tp.user_b_idx].addr_hex;
                json pi = json::array();
                for (auto idx : tp.pool_indices) pi.push_back(pools[idx].pool);
                t["pools"] = pi;
                tpl.push_back(t);
            }
            res["trade_pairs_detail"] = tpl;

            std::ofstream out("amm_test_setup.json");
            out << res.dump(2) << std::endl;
        }
        std::cout << "  Results saved to amm_test_setup.json" << std::endl;

        // Stop the dedicated TCP sender thread
        tcp_sender_stop.store(true);
        tcp_send_cv.notify_one();
        tcp_sender_thread.join();

        transport::TcpTransport::Instance()->Stop();
        return 0;
    }

    return 0;
}