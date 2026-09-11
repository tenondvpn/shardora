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
#include <unordered_map>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "common/defer.h"
#include "common/random.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"
#include "protos/address.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"
#include "shardoravm/reversible_feistel_address.h"
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

    spdlog::set_level(spdlog::level::warn);
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
            new_tx->set_contract_prefund(60000000lu);
        } else if (key == "prefund") {
            new_tx->set_step(pools::protobuf::kContractGasPrefund);
            new_tx->set_contract_prefund(60000000lu);
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
                auto tps = all_count * 60000000lu / dur;
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
        auto res_json = client.setGasPrefund(prikey, contract_address, 60000000lu);
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
                        usleep(100);
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
                        usleep(100);
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
                        usleep(100);
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
                            std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
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

    // ── Mode 6: Exchange Contract Multi-Shard Stress Test ─────────────────
        setvbuf(stdout, NULL, _IONBF, 0);  // disable stdout buffering for live log
    // Usage: txcli 6 <shard> <pool> <ip> <port> [users_per_shard] [contracts_per_shard] [call_rounds] [tps]
    //
    // Shards 3/4/5/6:
    //   1. Generate 100000 user accounts per shard (address hash → correct shard)
    //   2. Cross-shard fund all users from shard-3 funder via 3-shard transfer
    //   3. Generate 10000 deployer accounts per shard, deploy Exchange contracts
    //   4. Set prefund for all users on all contracts (mode-5 pattern)
    //   5. Stress test: CreateNewItem × 1000000, PurchaseItem × 1000000 per shard (concurrent)
    if (argv[1][0] == '6') {
        const uint32_t kUsersPerShard     = (argc >= 7)  ? std::stoul(argv[6])  : 10000u;
        const uint32_t kContractsPerShard = (argc >= 8)  ? std::stoul(argv[7])  : 256u;
        const uint64_t kCallRounds        = (argc >= 9)  ? std::stoull(argv[8]) : 10000ULL;
        const uint32_t kTargetTps         = (argc >= 10) ? std::stoul(argv[9])  : 0u;
        // Per-shard node IPs: argv[10..13] = ip3 ip4 ip5 ip6 (optional, default = argv[4])
        std::string shard_ips[4];   // index 0=shard3, 1=shard4, 2=shard5, 3=shard6

        if (argc >= 4) {
            shardnum = std::stoi(argv[2]);
            global_pool_idx = std::stoi(argv[3]);
        }
        if (argc >= 6) {
            global_chain_node_ip = argv[4];
            uint16_t input_port = (uint16_t)std::stoi(argv[5]);
            global_chain_node_http_port = (input_port < 20000) ? input_port + 10000 : input_port;
        }
        // Fill per-shard IPs; default to shard3 IP if not specified
        for (int _i = 0; _i < 4; ++_i)
            shard_ips[_i] = (argc >= 11 + _i) ? argv[10 + _i] : global_chain_node_ip;

        const std::vector<int> kShards = {3, 4, 5, 6};

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "  Exchange Contract Multi-Shard Stress Test (Mode 6)\n";
        std::cout << "  Shards: 3/4/5/6\n";
        std::cout << "  Users/shard:     " << kUsersPerShard     << "\n";
        std::cout << "  Contracts/shard: " << kContractsPerShard << "\n";
        std::cout << "  Call rounds:     " << kCallRounds        << " (CreateNewItem + PurchaseItem)\n";
        std::cout << "  Target TPS:      " << (kTargetTps > 0 ? std::to_string(kTargetTps) : "unlimited") << "\n";
        std::cout << "  Node: " << global_chain_node_ip << ":" << (global_chain_node_http_port - 10000) << "\n";
        std::cout << "  Shard IPs: s3=" << shard_ips[0] << " s4=" << shard_ips[1]
                  << " s5=" << shard_ips[2] << " s6=" << shard_ips[3] << "\n";
        std::cout << std::string(70, '=') << "\n";

        LoadAllAccounts(shardnum);
        SignalRegister();
        WriteDefaultLogConf();

        // ── TCP transport setup ──────────────────────────────────────────
        transport::MultiThreadHandler net_handler6;
        std::shared_ptr<security::Security> sec6 = std::make_shared<security::Ecdsa>();
        auto db_ptr6 = std::make_shared<db::Db>();
        // Remove stale db from previous runs to prevent lock conflicts
        std::string db6_path = db_path + "_exchange_stress";
        { std::string cmd = "rm -rf '" + db6_path + "' 2>/dev/null"; system(cmd.c_str()); }
        if (!db_ptr6->Init(db6_path)) {
            std::cerr << "init db failed\n"; return 1;
        }
        if (net_handler6.Init(db_ptr6, sec6) != 0) {
            std::cerr << "init net handler failed\n"; return 1;
        }
        if (transport::TcpTransport::Instance()->Init("127.0.0.1:13795", 128, false, &net_handler6) != 0) {
            std::cerr << "init tcp failed\n"; return 1;
        }
        if (transport::TcpTransport::Instance()->Start(false) != 0) {
            std::cerr << "start tcp failed\n"; return 1;
        }

        ShardoraSDK sdk6(global_chain_node_ip, global_chain_node_http_port);

        // Connectivity check
        {
            std::shared_ptr<security::Security> ts = std::make_shared<security::Ecdsa>();
            ts->SetPrivateKey(g_prikeys[0]);
            int64_t tn = sdk6.fetchNonce(common::Encode::HexEncode(ts->GetAddress()));
            if (tn < 0) {
                std::cerr << "  ERROR: Cannot reach " << global_chain_node_ip
                          << ":" << global_chain_node_http_port << "\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  HTTP OK (nonce=" << tn << ")\n";
        }

        // ── Compile Exchange contract ────────────────────────────────────
        std::cout << "\n[Phase 1] Compile Exchange contract...\n";

        // Exchange contract Solidity source
        const std::string EXCHANGE_SOL = R"(
// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.17 <0.9.0;
contract Exchange {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;

    // Pack owner+exists into one slot (address=20b, bool=1b → same 32b slot)
    struct ItemInfo {
        address payable owner;
        bool exists;
        uint256 price;
        uint256 start_time_ms;
        uint256 end_time_ms;
    }

    struct BuyerInfo {
        address payable buyer;
        uint256 price;
        uint256 time;
    }

    mapping(bytes32 => ItemInfo) public item_map;
    // purchase dedup: keccak256(hash, buyer) → bool (no dynamic bytes allocation)
    mapping(bytes32 => bool) public purchase_map;
    // buyers stored separately to keep ItemInfo struct small
    mapping(bytes32 => BuyerInfo[]) public item_buyers;

    function CreateNewItem(bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end) public payable {
        require(!item_map[hash].exists);
        ItemInfo storage item = item_map[hash];
        item.owner = payable(msg.sender);
        item.price = price;
        item.start_time_ms = start;
        item.end_time_ms = end;
        item.exists = true;
    }

    function PurchaseItem(bytes32 hash, uint256 time) public payable {
        ItemInfo storage item = item_map[hash];
        require(item.exists);
        require(item.owner != msg.sender);
        bytes32 key = keccak256(abi.encodePacked(hash, msg.sender));
        require(!purchase_map[key]);
        require(item.price <= msg.value);
        item_buyers[hash].push(BuyerInfo(payable(msg.sender), msg.value, time));
        purchase_map[key] = true;
    }

    function TotalBuyers(bytes32 hash) public view returns (uint256) {
        return item_buyers[hash].length;
    }
}
)";

        auto compiled = sdk6.compileSolidity(EXCHANGE_SOL);
        if (compiled["status"] != 0) {
            std::cerr << "  Exchange compile failed: " << compiled["msg"] << "\n";
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }
        std::string exchange_bytecode = compiled["bytecode"];
        std::cout << "  Exchange bytecode: " << exchange_bytecode.size() << " chars\n";

        // Pre-compute ABI selectors (keccak256 of signature, first 4 bytes)
        // CreateNewItem(bytes32,bytes,uint256,uint256,uint256)
        // PurchaseItem(bytes32,uint256)
        const std::string sel_create = utils::keccak256Str(
            "CreateNewItem(bytes32,bytes,uint256,uint256,uint256)").substr(0, 8);
        const std::string sel_purchase = utils::keccak256Str(
            "PurchaseItem(bytes32,uint256)").substr(0, 8);
        std::cout << "  CreateNewItem selector: 0x" << sel_create << "\n";
        std::cout << "  PurchaseItem selector:  0x" << sel_purchase << "\n";


        // ── Phase 2: Generate accounts per shard ────────────────────────
        // For each shard S, we need accounts whose address maps to shard S.
        // Shardora shards are identified by GetAddressPoolIndex() % num_shards,
        // but the simpler rule used throughout the codebase is:
        //   addr[0] >> ? — actually the chain uses net_id / shard_id embedded in
        //   the DHT key, not the address. For cross-shard, we just send with
        //   des_net_id = target_shard. Accounts generated here are used to CALL
        //   contracts deployed on each specific shard, so we need the accounts
        //   to reside on the correct shard.
        //
        // Address-to-shard mapping in Shardora:
        //   The first byte of the address (big-endian) determines the shard.
        //   shard_id = addr[0] * num_active_shards / 256
        //   With 4 shards (3,4,5,6), indices 0-3:
        //     addr[0] in [0,63]   → shard index 0 → shard 3
        //     addr[0] in [64,127] → shard index 1 → shard 4
        //     addr[0] in [128,191]→ shard index 2 → shard 5
        //     addr[0] in [192,255]→ shard index 3 → shard 6
        // We generate random keys and keep those whose address falls in the
        // right range — retry until we have enough.
        std::cout << "\n[Phase 2] Generate " << kUsersPerShard
                  << " users + " << kContractsPerShard << " deployers per shard...\n";

        struct ShardAccounts {
            int shard_id;
            // per-shard node endpoints
            std::string node_ip;
            uint16_t    node_http{0};
            uint16_t    node_tcp{0};
            // user accounts
            std::vector<std::string> user_prikeys;  // raw bytes
            std::vector<std::string> user_addrs;    // raw bytes
            // deployer accounts (one per contract)
            std::vector<std::string> deployer_prikeys;
            std::vector<std::string> deployer_addrs;
            // contract addresses (populated after deployment)
            std::vector<std::string> contract_addrs;
            // confirmed flags
            std::vector<bool> users_confirmed;
            std::vector<bool> deployers_confirmed;
            std::vector<bool> contracts_confirmed;
        };

        // Determine shard membership via Shardora's actual pool routing:
        //   pool = XXH32(addr_bytes[0:20], seed=623453345) % 32
        //   shard = 6 - pool/8  =>  pool 24-31→shard3, 16-23→shard4, 8-15→shard5, 0-7→shard6
        const int kNumShards = 4;
        auto addr_pool_index = [](const std::string& addr_raw) -> uint32_t {
            return common::GetAddressPoolIndex(addr_raw);
        };
        auto addr_shard_id = [&](const std::string& addr_raw) -> int {
            return 6 - (int)(addr_pool_index(addr_raw) / 8);
        };

        std::vector<ShardAccounts> shard_data(kNumShards);
        for (int i = 0; i < kNumShards; ++i) {
            shard_data[i].shard_id  = kShards[i];
            shard_data[i].node_ip   = shard_ips[i];
            // HTTP port = 2{shard}001, TCP = HTTP - 10000
            shard_data[i].node_http = (uint16_t)(20000 + kShards[i] * 1000 + 1);
            shard_data[i].node_tcp  = shard_data[i].node_http - 10000;
        }

        // Phase 2: Generate exactly kUsersPerShard users + kContractsPerShard deployers per shard.
        // Uses Shardora shard routing (addr_shard_id) to filter — same approach as mode 7.
        auto shard_to_idx = [](int shard_id) -> int { return shard_id - 3; };
        for (int i = 0; i < kNumShards; ++i) {
            shard_data[i].contract_addrs.clear();
            shard_data[i].users_confirmed.clear();
            shard_data[i].deployers_confirmed.clear();
            shard_data[i].contracts_confirmed.clear();
        }
        {
            std::mutex gen_mu;
            std::vector<std::thread> gen_threads;
            for (int si = 0; si < kNumShards && !global_stop; ++si) {
                gen_threads.emplace_back([&, si]() {
                    int target_shard = kShards[si];
                    std::vector<std::string> lpk, laddr, dpk, daddr;
                    lpk.reserve(kUsersPerShard);
                    dpk.reserve(kContractsPerShard);
                    while ((uint32_t)lpk.size() < kUsersPerShard && !global_stop) {
                        std::string pk(32, '\0');
                        for (int j = 0; j < 32; ++j)
                            pk[j] = static_cast<char>(common::Random::RandomUint32() % 256);
                        auto s = std::make_shared<security::Ecdsa>();
                        s->SetPrivateKey(pk);
                        std::string addr = s->GetAddress();
                        if (addr_shard_id(addr) == target_shard) {
                            lpk.push_back(pk);
                            laddr.push_back(addr);
                        }
                    }
                    while ((uint32_t)dpk.size() < kContractsPerShard && !global_stop) {
                        std::string pk(32, '\0');
                        for (int j = 0; j < 32; ++j)
                            pk[j] = static_cast<char>(common::Random::RandomUint32() % 256);
                        auto s = std::make_shared<security::Ecdsa>();
                        s->SetPrivateKey(pk);
                        std::string addr = s->GetAddress();
                        if (addr_shard_id(addr) == target_shard) {
                            dpk.push_back(pk);
                            daddr.push_back(addr);
                        }
                    }
                    std::lock_guard<std::mutex> lk(gen_mu);
                    int idx = shard_to_idx(target_shard);
                    shard_data[idx].user_prikeys     = std::move(lpk);
                    shard_data[idx].user_addrs       = std::move(laddr);
                    shard_data[idx].deployer_prikeys = std::move(dpk);
                    shard_data[idx].deployer_addrs   = std::move(daddr);
                });
            }
            for (auto& t : gen_threads) t.join();
        }
        for (int i = 0; i < kNumShards; ++i) {
            uint32_t nu = shard_data[i].user_addrs.size();
            uint32_t nd = shard_data[i].deployer_addrs.size();
            shard_data[i].users_confirmed.assign(nu, false);
            shard_data[i].deployers_confirmed.assign(nd, false);
            shard_data[i].contract_addrs.resize(nd);
            shard_data[i].contracts_confirmed.assign(nd, false);
            std::cout << "  Shard " << kShards[i] << ": " << nu << " users, " << nd << " deployers\n";
        }
        std::cout << "  Account generation complete\n";


        // ── TCP send queue (shared across all phases) ────────────────────
        struct TcpSendItem6 {
            transport::MessagePtr msg;
            std::string dest_ip;
            uint16_t dest_port;
        };
        std::queue<TcpSendItem6> tcp_q6;
        std::mutex tcp_mtx6;
        std::condition_variable tcp_cv6;
        std::atomic<bool> tcp_stop6{false};
        std::atomic<uint64_t> tcp_sent6{0};

        std::thread tcp_sender6([&]() {
            std::vector<TcpSendItem6> batch;
            batch.reserve(4096);
            while (!tcp_stop6.load()) {
                {
                    std::unique_lock<std::mutex> lk(tcp_mtx6);
                    tcp_cv6.wait_for(lk, std::chrono::milliseconds(1),
                        [&]{ return !tcp_q6.empty() || tcp_stop6.load(); });
                    while (!tcp_q6.empty()) { batch.push_back(std::move(tcp_q6.front())); tcp_q6.pop(); }
                }
                for (auto& item : batch) {
                    transport::TcpTransport::Instance()->Send(item.dest_ip, item.dest_port, item.msg->header);
                    ++tcp_sent6;
                }
                batch.clear();
            }
            std::lock_guard<std::mutex> lk(tcp_mtx6);
            while (!tcp_q6.empty()) {
                auto item = std::move(tcp_q6.front()); tcp_q6.pop();
                transport::TcpTransport::Instance()->Send(item.dest_ip, item.dest_port, item.msg->header);
                ++tcp_sent6;
            }
        });

        auto tcp_enq6 = [&](transport::MessagePtr msg, const std::string& ip, uint16_t port) -> bool {
            if (!msg) return false;
            { std::lock_guard<std::mutex> lk(tcp_mtx6); tcp_q6.push({std::move(msg), ip, port}); }
            tcp_cv6.notify_one();
            return true;
        };

        const std::string node_ip  = global_chain_node_ip;
        const uint16_t   node_tcp  = (uint16_t)(global_chain_node_http_port - 10000);
        const uint16_t   node_http = global_chain_node_http_port;


        // ── Phase 3: Fund all accounts (users + deployers) via cross-shard txs ──
        // Funders may live on different shards; route each funder's TX via its home shard node.
        std::cout << "\n[Phase 3] Fund accounts on shards 3/4/5/6 (cross-shard from shard "
                  << shardnum << " funder)...\n";

        // All genesis funders live on shard3 — always use shard3 node IP for nonce/send
        auto funder_shard_ip = [&](const std::string&) -> std::pair<std::string, uint16_t> {
            return {shard_data[0].node_ip, shard_data[0].node_http};  // shard_data[0] = shard3
        };

        // Deduplicate funded accounts
        std::vector<std::string> unique_funders6;
        { std::set<std::string> seen;
          for (auto& pk : g_prikeys)
              if (seen.insert(pk).second) unique_funders6.push_back(pk); }
        std::cout << "  Unique funders: " << unique_funders6.size() << "\n";

        const uint64_t kFundAmt6 = 8000000000ULL;
        std::atomic<uint32_t> fund_ok6{0}, fund_fail6{0};

        // Build flat list: (addr_hex, target_shard)
        std::vector<std::pair<std::string, int>> all_to_fund;
        for (int si = 0; si < kNumShards; ++si) {
            auto& sd = shard_data[si];
            for (auto& a : sd.user_addrs)
                all_to_fund.push_back({common::Encode::HexEncode(a), sd.shard_id});
            for (auto& a : sd.deployer_addrs)
                all_to_fund.push_back({common::Encode::HexEncode(a), sd.shard_id});
        }
        uint32_t total_to_fund = all_to_fund.size();
        std::cout << "  Total accounts to fund: " << total_to_fund << "\n";

        // ── Fund send: 4 funder threads, each 100us interval = ~10000 tx/s total ──
        // Each thread owns its funder exclusively (no nonce conflict).
        // Rate: 4 threads * (1 tx / 100us) = 40000 tx/s ceiling; chain limits actual rate.
        struct FunderState {
            std::string prikey;
            std::shared_ptr<security::Security> sec;
            std::string addr_hex;
            int64_t nonce_start{0};
            int64_t nonce_sent{0};
            std::atomic<uint32_t> sent{0};
            FunderState() = default;
            FunderState(FunderState&& o) noexcept
                : prikey(std::move(o.prikey)), sec(std::move(o.sec)),
                  addr_hex(std::move(o.addr_hex)),
                  nonce_start(o.nonce_start), nonce_sent(o.nonce_sent),
                  sent(o.sent.load()) {}
            FunderState& operator=(FunderState&&) = delete;
        };
        std::vector<FunderState> funders;
        {
            auto [f_ip, f_http] = funder_shard_ip("");
            ShardoraSDK fqsdk(f_ip, f_http);
            for (auto& pk : unique_funders6) {
                FunderState fs;
                fs.prikey = pk;
                fs.sec = std::make_shared<security::Ecdsa>();
                fs.sec->SetPrivateKey(pk);
                fs.addr_hex = common::Encode::HexEncode(fs.sec->GetAddress());
                int64_t n = fqsdk.fetchNonce(fs.addr_hex);
                fs.nonce_start = (n >= 0) ? n : 0;
                fs.nonce_sent  = fs.nonce_start;
                std::cout << "  Funder " << fs.addr_hex.substr(0,16)
                          << "... chain_nonce=" << fs.nonce_start << "\n";
                funders.push_back(std::move(fs));
            }
        }
        auto [f_ip, f_http] = funder_shard_ip("");
        uint16_t f_tcp = f_http - 10000;
        uint32_t nf2 = (uint32_t)funders.size(); if (!nf2) nf2 = 1;

        // ── Inline raw-TCP send: each funder thread owns its own socket ──────────
        // Wire format: [PacketHeader(4B): length(24b)|type(8b)] [protobuf payload]
        // type=0 (kProtobuff). Bypasses TcpTransport single-Output-thread bottleneck.
        auto raw_tcp_send = [](const std::string& ip, uint16_t port,
                               const std::string& payload) -> bool {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) return false;
            // disable Nagle for low-latency bulk sends
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(port);
            ::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);
            if (::connect(fd, (sockaddr*)&sa, sizeof(sa)) != 0) { ::close(fd); return false; }
            // reuse fd across many sends — return it as a persistent connection
            // (we wrap into a lambda-captured int below)
            ::close(fd);
            return true; // placeholder — real logic below uses persistent fd
        };
        (void)raw_tcp_send;

        {
            std::vector<std::thread> fund_threads;
            for (uint32_t fi = 0; fi < nf2; ++fi) {
                fund_threads.emplace_back([&, fi, f_ip, f_tcp]() {
                    // Open a persistent TCP connection for this thread
                    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (fd < 0) {
                        // fallback: count all as fail
                        for (uint32_t i = fi; i < total_to_fund; i += nf2) ++fund_fail6;
                        return;
                    }
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(f_tcp);
                    ::inet_pton(AF_INET, f_ip.c_str(), &sa.sin_addr);
                    if (::connect(fd, (sockaddr*)&sa, sizeof(sa)) != 0) {
                        ::close(fd);
                        for (uint32_t i = fi; i < total_to_fund; i += nf2) ++fund_fail6;
                        return;
                    }
                    auto& fs = funders[fi];
                    int64_t nonce = fs.nonce_sent;
                    for (uint32_t i = fi; i < total_to_fund && !global_stop; i += nf2) {
                        auto& [addr_hex, tgt] = all_to_fund[i];
                        auto tx = CreateTransactionWithAttr(fs.sec, ++nonce,
                            fs.prikey,
                            common::Encode::HexDecode(addr_hex),
                            "", "", kFundAmt6, 210000, 1, kShards[0]);
                        if (!tx) { ++fund_fail6; --nonce; continue; }
                        // Set required fields (same as TcpTransport::Send does)
                        tx->header.set_from_public_port(
                            common::GlobalInfo::Instance()->config_public_port());
                        if (!tx->header.has_hash64() || tx->header.hash64() == 0) {
                            std::string hs = tx->header.SerializeAsString();
                            tx->header.set_hash64(common::Hash::Hash64(hs));
                        }
                        std::string payload = tx->header.SerializeAsString();
                        // PacketHeader: 4 bytes (length[23:0] | type[31:24]), LE
                        uint32_t plen = (uint32_t)payload.size();
                        uint8_t hdr[4];
                        hdr[0] = plen & 0xFF;
                        hdr[1] = (plen >> 8) & 0xFF;
                        hdr[2] = (plen >> 16) & 0xFF;
                        hdr[3] = 0; // type = kProtobuff = 0
                        bool ok = true;
                        if (::send(fd, hdr, 4, MSG_NOSIGNAL) != 4) ok = false;
                        if (ok) {
                            uint32_t off = 0;
                            while (off < plen) {
                                ssize_t n = ::send(fd, payload.data()+off, plen-off, MSG_NOSIGNAL);
                                if (n <= 0) { ok = false; break; }
                                off += n;
                            }
                        }
                        if (ok) {
                            ++fund_ok6; ++fs.sent;
                        } else {
                            ++fund_fail6; --nonce;
                            // reconnect
                            ::close(fd);
                            fd = ::socket(AF_INET, SOCK_STREAM, 0);
                            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                            if (::connect(fd, (sockaddr*)&sa, sizeof(sa)) != 0) {
                                ::close(fd); fd = -1; break;
                            }
                        }
                    }
                    if (fd >= 0) ::close(fd);
                    fs.nonce_sent = nonce;
                });
            }
            std::thread fund_prog([&]() {
                uint32_t last = 0;
                while (fund_ok6.load() + fund_fail6.load() < total_to_fund && !global_stop) {
                    usleep(1000000);
                    uint32_t ok = fund_ok6.load(), fail = fund_fail6.load();
                    uint32_t total_sent = ok + fail;
                    uint32_t tps = total_sent - last;
                    last = total_sent;
                    std::cout << "  Fund: " << ok << " ok, " << fail
                              << " fail / " << total_to_fund
                              << "  tps=" << tps << "\n";
                }
            });
            for (auto& t : fund_threads) t.join();
            fund_prog.join();
        }
        std::cout << "  Fund sends: " << fund_ok6.load() << " ok, "
                  << fund_fail6.load() << " fail\n";

        // ── Verify funder nonces on-chain ──────────────────────────────────────
        // Each funder's on-chain nonce must equal nonce_start + sent_count.
        std::cout << "\n[Phase 3a] Verify funder nonces on-chain...\n";
        {
            ShardoraSDK verify_sdk(shard_data[0].node_ip, shard_data[0].node_http);
            std::vector<std::string> funder_addrs;
            for (auto& fs : funders) funder_addrs.push_back(fs.addr_hex);
            auto t0 = std::chrono::steady_clock::now();
            bool nonce_ok = false;
            for (int rd = 0; rd < 120 && !global_stop; ++rd) {
                auto r = verify_sdk.batchQueryAccounts(funder_addrs);
                bool all_match = true;
                if (r.contains("accounts")) {
                    for (auto& fs : funders) {
                        int64_t expected = fs.nonce_sent;
                        int64_t actual = -1;
                        if (r["accounts"].contains(fs.addr_hex)) {
                            auto& acc = r["accounts"][fs.addr_hex];
                            if (acc.contains("nonce")) {
                                try {
                                    auto ns = acc["nonce"].get<std::string>();
                                    std::from_chars(ns.data(), ns.data()+ns.size(), actual);
                                } catch (...) {}
                            }
                        }
                        std::cout << "  Funder " << fs.addr_hex.substr(0,16)
                                  << "... expected_nonce=" << expected
                                  << " chain_nonce=" << actual
                                  << (actual >= expected ? " OK" : " PENDING") << "\n";
                        if (actual < expected) all_match = false;
                    }
                } else { all_match = false; }
                if (all_match) { nonce_ok = true; break; }
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count();
                std::cout << "  Nonce verify round " << (rd+1) << " (" << elapsed << "s), retrying...\n";
                usleep(5000000);
            }
            if (!nonce_ok) {
                std::cerr << "  FATAL: Funder nonce verify failed after 600s. Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  All funder nonces confirmed on-chain.\n";
        }

        // ── Verify funded accounts per shard (each shard queries its OWN node) ──
        std::cout << "\n[Phase 3b] Verify funded accounts per shard (own-node query)...\n";

        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            auto& sd = shard_data[si];
            uint32_t nu = (uint32_t)sd.user_addrs.size();
            uint32_t nd = (uint32_t)sd.deployer_addrs.size();
            std::vector<std::string> all_hex;
            all_hex.reserve(nu + nd);
            for (auto& a : sd.user_addrs)     all_hex.push_back(common::Encode::HexEncode(a));
            for (auto& a : sd.deployer_addrs) all_hex.push_back(common::Encode::HexEncode(a));

            std::vector<bool> confirmed(all_hex.size(), false);
            std::vector<uint32_t> pending;
            for (uint32_t i = 0; i < (uint32_t)all_hex.size(); ++i) pending.push_back(i);

            uint32_t total_confirmed = 0;
            auto t0 = std::chrono::steady_clock::now();
            const uint32_t kVBatch = 300;

            // Query ONLY this shard's own node (sd.node_ip:sd.node_http)
            ShardoraSDK own_sdk(sd.node_ip, sd.node_http);

            for (uint32_t rd = 0; !pending.empty() && !global_stop; ++rd) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count();
                if (elapsed >= 600) {
                    std::cerr << "  FATAL: Shard " << sd.shard_id << ": fund verify timeout after 600s, "
                              << total_confirmed << "/" << all_hex.size() << " funded. Aborting.\n";
                    transport::TcpTransport::Instance()->Stop();
                    return 1;
                }
                uint32_t rok = 0;
                std::vector<uint32_t> next_pending;
                std::vector<std::string> ba; std::vector<uint32_t> bi;
                for (uint32_t p = 0; p < (uint32_t)pending.size() && !global_stop; ++p) {
                    ba.push_back(all_hex[pending[p]]); bi.push_back(pending[p]);
                    if (ba.size() >= kVBatch || p == (uint32_t)pending.size() - 1) {
                        auto r = own_sdk.batchQueryAccounts(ba);
                        if (r.contains("status") && r["status"] == 0 && r.contains("accounts")) {
                            for (uint32_t k = 0; k < (uint32_t)bi.size(); ++k) {
                                if (!confirmed[bi[k]] && r["accounts"].contains(ba[k])) {
                                    confirmed[bi[k]] = true; ++total_confirmed; ++rok;
                                } else if (!confirmed[bi[k]]) {
                                    next_pending.push_back(bi[k]);
                                }
                            }
                        } else {
                            for (auto idx : bi) next_pending.push_back(idx);
                        }
                        ba.clear(); bi.clear();
                    }
                }
                pending = std::move(next_pending);
                elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count();
                std::cout << "  [shard" << sd.shard_id << "@" << sd.node_ip
                          << " round " << (rd+1) << ", " << elapsed << "s] +"
                          << rok << " = " << total_confirmed << "/" << all_hex.size()
                          << " (" << pending.size() << " pending)\n";
                if (pending.empty()) break;
                uint32_t wt = (rok > 0) ? 2000 : 5000;
                for (uint32_t w = 0; w < wt/100 && !global_stop; ++w) usleep(100000);
            }

            // Strict: require 100%
            uint32_t uc = 0, dc = 0;
            for (uint32_t i = 0; i < nu; ++i) {
                sd.users_confirmed[i] = confirmed[i];
                if (confirmed[i]) ++uc;
            }
            for (uint32_t i = 0; i < nd; ++i) {
                sd.deployers_confirmed[i] = confirmed[nu + i];
                if (confirmed[nu + i]) ++dc;
            }
            std::cout << "  Shard " << sd.shard_id << " (" << sd.node_ip << "): "
                      << uc << "/" << nu << " users funded, "
                      << dc << "/" << nd << " deployers funded\n";
            if (uc < nu || dc < nd) {
                std::cerr << "  FATAL: Shard " << sd.shard_id
                          << " fund incomplete (" << uc << "/" << nu << " users, "
                          << dc << "/" << nd << " deployers). Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }


        // ── Phase 4: Deploy Exchange contracts (one per deployer) ────────
        std::cout << "\n[Phase 4] Deploy Exchange contracts...\n";
        const uint64_t kDeployPrefund6 = 800000000lu;
        std::atomic<uint32_t> deploy_ok6{0}, deploy_fail6{0};

        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            auto& sd = shard_data[si];
            uint32_t nd4 = (uint32_t)sd.deployer_addrs.size();
            if (!nd4) { std::cout << "  Shard " << sd.shard_id << ": 0 deployers, skip\n"; continue; }
            uint32_t nt = std::min(32u, nd4); if (!nt) nt = 1;
            uint32_t pp = nd4 / nt;
            std::vector<std::thread> dth;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s2 = t * pp;
                uint32_t e2 = (t == nt-1) ? nd4 : (s2 + pp);
                dth.emplace_back([&, si, s2, e2]() {
                    ShardoraSDK tsdk(shard_data[si].node_ip, shard_data[si].node_http);
                    for (uint32_t i = s2; i < e2 && !global_stop; ++i) {
                        if (!sd.deployers_confirmed[i]) { ++deploy_fail6; continue; }
                        std::string pk_hex = common::Encode::HexEncode(sd.deployer_prikeys[i]);
                        int target_shard = shard_data[si].shard_id;
                        // Try salts until contract address lands on target shard
                        static std::atomic<uint64_t> deploy_counter6{0};
                        std::string to_address;
                        for (int attempt = 0; attempt < 10000 && !global_stop; ++attempt) {
                            uint64_t cnt = deploy_counter6.fetch_add(1);
                            std::string salt = sd.deployer_prikeys[i] + std::to_string(cnt) +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                            std::string candidate = utils::keccak256Str(exchange_bytecode + salt).substr(24);
                            std::string candidate_raw = common::Encode::HexDecode(candidate);
                            if (addr_shard_id(candidate_raw) == target_shard) {
                                to_address = candidate;
                                break;
                            }
                        }
                        if (to_address.empty()) { ++deploy_fail6; continue; }
                        auto r = tsdk.deployToAddress(pk_hex, exchange_bytecode, to_address, kDeployPrefund6);
                        if (r["status"] == 0) {
                            sd.contract_addrs[i] = to_address;
                            ++deploy_ok6;
                        } else {
                            ++deploy_fail6;
                        }
                        usleep(100);
                    }
                });
            }
            for (auto& t : dth) t.join();
            std::cout << "  Shard " << sd.shard_id << ": " << deploy_ok6.load()
                      << " deployed so far\n";
        }
        std::cout << "  Deploy sends: " << deploy_ok6.load() << " ok, "
                  << deploy_fail6.load() << " fail\n";

        std::cout << "  Waiting 15s for deploy consensus...\n";
        for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);

        // Verify contract addresses on-chain per shard (up to 300s per shard)
        std::cout << "  Verifying contract addresses on-chain (up to 300s per shard)...\n";
        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            auto& sd = shard_data[si];
            uint32_t nd4v = (uint32_t)sd.deployer_addrs.size();
            ShardoraSDK shard_sdk(sd.node_ip, sd.node_http);
            std::vector<uint32_t> pending;
            for (uint32_t i = 0; i < nd4v; ++i)
                if (!sd.contract_addrs[i].empty()) pending.push_back(i);

            uint32_t total_confirmed = 0;
            auto t0 = std::chrono::steady_clock::now();
            const uint32_t kVBatch = 200;

            for (uint32_t rd = 0; !pending.empty() && !global_stop; ++rd) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count();
                if (elapsed >= 600) {
                    std::cerr << "  FATAL: Shard " << sd.shard_id << ": contract verify timeout after 600s, "
                              << total_confirmed << "/" << nd4v << " confirmed. Aborting.\n";
                    transport::TcpTransport::Instance()->Stop();
                    return 1;
                }
                uint32_t rok = 0;
                std::vector<uint32_t> next_pending;
                std::vector<std::string> ba; std::vector<uint32_t> bi;
                for (uint32_t p = 0; p < pending.size() && !global_stop; ++p) {
                    ba.push_back(sd.contract_addrs[pending[p]]); bi.push_back(pending[p]);
                    if (ba.size() >= kVBatch || p == pending.size() - 1) {
                        // Query ALL shard nodes - contract addr may land on any shard
                        std::vector<bool> ba_found(bi.size(), false);
                        for (int qsi = 0; qsi < kNumShards && !global_stop; ++qsi) {
                            ShardoraSDK qsdk(shard_data[qsi].node_ip, shard_data[qsi].node_http);
                            auto r = qsdk.batchQueryAccounts(ba);
                            if (!r.contains("status") || r["status"] != 0 || !r.contains("accounts")) continue;
                            for (uint32_t k = 0; k < bi.size(); ++k)
                                if (!ba_found[k] && r["accounts"].contains(ba[k]))
                                    ba_found[k] = true;
                        }
                        for (uint32_t k = 0; k < bi.size(); ++k) {
                            if (ba_found[k]) {
                                sd.contracts_confirmed[bi[k]] = true; ++total_confirmed; ++rok;
                            } else { next_pending.push_back(bi[k]); }
                        }
                        ba.clear(); bi.clear();
                    }
                }
                pending = std::move(next_pending);
                elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count();
                std::cout << "  [shard" << sd.shard_id << " contract round " << (rd+1)
                          << ", " << elapsed << "s] +" << rok
                          << " = " << total_confirmed << "/" << nd4v
                          << " (" << pending.size() << " pending)\n";
                if (pending.empty()) break;
                uint32_t wt = (rok > 0) ? 3000 : 8000;
                if (rd == 0 && rok == 0) wt = 15000;
                for (uint32_t w = 0; w < wt/100 && !global_stop; ++w) usleep(100000);
            }
            std::cout << "  Shard " << sd.shard_id << ": " << total_confirmed
                      << "/" << nd4v << " contracts confirmed\n";
            if (total_confirmed < nd4v) {
                std::cerr << "  FATAL: Shard " << sd.shard_id
                          << " contract deploy incomplete (" << total_confirmed << "/" << nd4v
                          << "). Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }

        // ── Phase 5: Prefund users on contracts (mode-5 pattern) ────────
        // Each user on shard S prefunds on every contract on shard S.
        // To keep ops manageable: pair each user with 1 contract (round-robin).
        // For 100000 users and 10000 contracts → 10 users per contract.
        std::cout << "\n[Phase 5] Set prefund for users on contracts...\n";
        const uint64_t kUserPrefund6 = 500000000lu;
        std::atomic<uint64_t> pf_ok6{0}, pf_fail6{0};

        // Build prefund op list per shard
        struct PfOp6 {
            std::string user_prikey_hex;
            std::string contract_addr;
        };

        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            auto& sd = shard_data[si];
            uint32_t nd5 = (uint32_t)sd.deployer_addrs.size();
            uint32_t nu5 = (uint32_t)sd.user_addrs.size();
            ShardoraSDK shard_sdk5(sd.node_ip, sd.node_http);
            // Build list of valid (confirmed) contracts
            std::vector<uint32_t> valid_contracts;
            for (uint32_t i = 0; i < nd5; ++i)
                if (sd.contracts_confirmed[i] && !sd.contract_addrs[i].empty())
                    valid_contracts.push_back(i);
            if (valid_contracts.empty()) {
                std::cout << "  Shard " << sd.shard_id << ": no valid contracts, skip prefund\n";
                continue;
            }
            std::vector<PfOp6> pf_ops;
            for (uint32_t ui = 0; ui < nu5; ++ui) {
                if (!sd.users_confirmed[ui]) continue;
                std::string pk_hex = common::Encode::HexEncode(sd.user_prikeys[ui]);
                // Each user gets prefunded on one contract (round-robin)
                uint32_t ci = valid_contracts[ui % valid_contracts.size()];
                pf_ops.push_back({pk_hex, sd.contract_addrs[ci]});
            }
            uint32_t total_pf = pf_ops.size();
            std::cout << "  Shard " << sd.shard_id << ": " << total_pf << " prefund ops\n";

            // Nonce init for users
            std::unordered_map<std::string, uint64_t> user_nonces6;
            {
                const uint32_t kBatch = 300;
                std::vector<std::string> addrs;
                for (auto& op : pf_ops) {
                    security::Ecdsa e;
                    e.SetPrivateKey(common::Encode::HexDecode(op.user_prikey_hex));
                    addrs.push_back(common::Encode::HexEncode(e.GetAddress()));
                }
                for (uint32_t off = 0; off < addrs.size(); off += kBatch) {
                    uint32_t end = std::min(off + kBatch, (uint32_t)addrs.size());
                    std::vector<std::string> batch(addrs.begin() + off, addrs.begin() + end);
                    auto r = shard_sdk5.batchQueryAccounts(batch);
                    for (uint32_t k = 0; k < batch.size(); ++k) {
                        int64_t n = 0;
                        if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                            auto& acc = r["accounts"][batch[k]];
                            if (acc.contains("nonce")) {
                                try {
                                    auto ns = acc["nonce"].get<std::string>();
                                    std::from_chars(ns.data(), ns.data()+ns.size(), n);
                                } catch (...) {}
                            }
                        }
                        user_nonces6[common::Encode::HexDecode(addrs[off + k])] = (uint64_t)n;
                    }
                }
            }

            // Send prefund txs via TCP
            uint32_t pf_threads = std::min(32u, total_pf); if (!pf_threads) pf_threads = 1;
            uint32_t pf_per = total_pf / pf_threads;
            std::vector<std::thread> pfv;
            for (uint32_t t = 0; t < pf_threads; ++t) {
                uint32_t ps = t * pf_per;
                uint32_t pe = (t == pf_threads - 1) ? total_pf : (ps + pf_per);
                pfv.emplace_back([&, ps, pe, si]() {
                    auto& sd2 = shard_data[si];
                    for (uint32_t i = ps; i < pe && !global_stop; ++i) {
                        auto& op = pf_ops[i];
                        std::string pk_raw = common::Encode::HexDecode(op.user_prikey_hex);
                        std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                        sec->SetPrivateKey(pk_raw);
                        std::string addr = sec->GetAddress();
                        uint64_t& nonce = user_nonces6[addr];
                        // Send via shard3 node (same as fund sends — cross-shard routing)
                        auto tx = CreateTransactionWithAttr(sec, ++nonce,
                            op.user_prikey_hex,
                            common::Encode::HexDecode(op.contract_addr),
                            "prefund", "", 0, 210000, 1, kShards[0]);
                        if (tcp_enq6(tx, shard_data[0].node_ip, shard_data[0].node_tcp)) ++pf_ok6; else ++pf_fail6;
                        usleep(2000);
                    }
                });
            }
            for (auto& t : pfv) t.join();
        }
        // Drain TCP queue
        for (int w = 0; w < 100 && !global_stop; ++w) {
            bool empty = false;
            { std::lock_guard<std::mutex> lk(tcp_mtx6); empty = tcp_q6.empty(); }
            if (empty) break;
            usleep(100000);
        }
        std::cout << "  Prefund: " << pf_ok6.load() << " ok, " << pf_fail6.load() << " fail\n";

        std::cout << "  Waiting 15s for prefund consensus...\n";
        for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);

        // Verify prefund on-chain: query prepay accounts (key = contract_addr + user_addr, 80 hex)
        // Mirrors Mode 5 Phase 7 logic. Re-sends failed ops up to 3 times.
        std::cout << "  Verifying prefund on-chain (prepay key = contract+user, up to 300s per shard)...\n";
        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            auto& sd = shard_data[si];
            uint32_t nd5v = (uint32_t)sd.deployer_addrs.size();
            uint32_t nu5v = (uint32_t)sd.user_addrs.size();
            ShardoraSDK shard_sdk_pf(sd.node_ip, sd.node_http);
            // Rebuild pf_ops list for this shard (same as sent above)
            std::vector<uint32_t> valid_contracts2;
            for (uint32_t i = 0; i < nd5v; ++i)
                if (sd.contracts_confirmed[i] && !sd.contract_addrs[i].empty())
                    valid_contracts2.push_back(i);
            if (valid_contracts2.empty()) continue;

            // Build (prepay_key_hex → {user_prikey_hex, contract_addr, nonce_ref}) map
            struct PfVerOp {
                std::string prepay_key;   // 80-char hex: contract(40) + user(40)
                std::string user_pk_hex;
                std::string contract_addr;
            };
            std::vector<PfVerOp> pf_ver;
            pf_ver.reserve(nu5v);
            {
                std::unordered_map<std::string, uint64_t> resend_nonces;
                for (uint32_t ui = 0; ui < nu5v; ++ui) {
                    if (!sd.users_confirmed[ui]) continue;
                    security::Ecdsa e;
                    e.SetPrivateKey(sd.user_prikeys[ui]);
                    std::string user_addr_hex = common::Encode::HexEncode(e.GetAddress());
                    uint32_t ci = valid_contracts2[ui % valid_contracts2.size()];
                    std::string prepay = sd.contract_addrs[ci] + user_addr_hex;
                    pf_ver.push_back({prepay, common::Encode::HexEncode(sd.user_prikeys[ui]),
                                      sd.contract_addrs[ci]});
                }
            }

            std::vector<bool> pf_confirmed(pf_ver.size(), false);
            std::vector<uint32_t> pf_pending;
            pf_pending.reserve(pf_ver.size());
            for (uint32_t i = 0; i < pf_ver.size(); ++i) pf_pending.push_back(i);
            uint32_t pf_total_ok = 0;
            uint32_t pf_resend_round = 0;
            auto pf_t0 = std::chrono::steady_clock::now();
            const uint32_t kPfBatch = 50;

            for (uint32_t rd = 0; !pf_pending.empty() && !global_stop; ++rd) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - pf_t0).count();
                if (elapsed >= 600) {
                    std::cerr << "  FATAL: Shard " << sd.shard_id << ": prefund verify timeout after 600s, "
                              << pf_total_ok << "/" << pf_ver.size() << " confirmed. Aborting.\n";
                    transport::TcpTransport::Instance()->Stop();
                    return 1;
                }
                uint32_t rok = 0;
                std::vector<uint32_t> next_pending;
                std::vector<std::string> ba; std::vector<uint32_t> bi;
                for (uint32_t p = 0; p < pf_pending.size() && !global_stop; ++p) {
                    ba.push_back(pf_ver[pf_pending[p]].prepay_key); bi.push_back(pf_pending[p]);
                    if (ba.size() >= kPfBatch || p == pf_pending.size() - 1) {
                        auto r = shard_sdk_pf.batchQueryAccounts(ba);
                        if (r.contains("status") && r["status"] == 0 && r.contains("accounts")) {
                            for (uint32_t k = 0; k < bi.size(); ++k) {
                                if (r["accounts"].contains(ba[k])) {
                                    pf_confirmed[bi[k]] = true; ++pf_total_ok; ++rok;
                                } else { next_pending.push_back(bi[k]); }
                            }
                        } else { for (auto idx : bi) next_pending.push_back(idx); }
                        ba.clear(); bi.clear();
                    }
                }
                pf_pending = std::move(next_pending);
                elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - pf_t0).count();
                std::cout << "  [shard" << sd.shard_id << " prefund round " << (rd+1)
                          << ", " << elapsed << "s] +" << rok
                          << " = " << pf_total_ok << "/" << pf_ver.size()
                          << " (" << pf_pending.size() << " pending)\n";
                if (pf_pending.empty()) break;

                // Re-send unconfirmed prefund TXs (up to 3 resend rounds)
                if (rok == 0 && !pf_pending.empty() && pf_resend_round < 3) {
                    ++pf_resend_round;
                    std::cout << "  Resending " << pf_pending.size()
                              << " unconfirmed prefunds (resend round " << pf_resend_round << ")...\n";
                    std::unordered_map<std::string, uint64_t> rs_nonces;
                    for (auto idx : pf_pending) {
                        auto& op = pf_ver[idx];
                        std::string pk_raw = common::Encode::HexDecode(op.user_pk_hex);
                        std::shared_ptr<security::Security> sec2 = std::make_shared<security::Ecdsa>();
                        sec2->SetPrivateKey(pk_raw);
                        std::string addr_raw = sec2->GetAddress();
                        if (rs_nonces.find(addr_raw) == rs_nonces.end()) {
                            ShardoraSDK tsdk2(sd.node_ip, sd.node_http);
                            int64_t n2 = tsdk2.fetchNonce(common::Encode::HexEncode(addr_raw));
                            rs_nonces[addr_raw] = (n2 >= 0) ? (uint64_t)n2 : 0;
                        }
                        uint64_t& nn = rs_nonces[addr_raw];
                        auto tx2 = CreateTransactionWithAttr(sec2, ++nn,
                            op.user_pk_hex,
                            common::Encode::HexDecode(op.contract_addr),
                            "prefund", "", 0, 210000, 1, kShards[0]);
                        tcp_enq6(tx2, shard_data[0].node_ip, shard_data[0].node_tcp);
                        usleep(1000);
                    }
                    for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);
                    continue;
                }
                uint32_t wt = (rok > 0) ? 2000 : 5000;
                for (uint32_t w = 0; w < wt/100 && !global_stop; ++w) usleep(100000);
            }
            std::cout << "  Shard " << sd.shard_id << ": " << pf_total_ok
                      << "/" << pf_ver.size() << " prefunds confirmed on-chain\n";
            if (pf_total_ok < pf_ver.size()) {
                std::cerr << "  FATAL: Shard " << sd.shard_id
                          << " prefund incomplete (" << pf_total_ok << "/" << pf_ver.size()
                          << "). Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }


        // ── Phase 6: Stress test – CreateNewItem × kCallRounds per shard ──
        // Each user calls CreateNewItem on their assigned contract.
        // Since each user has a unique prefund account (contract+user), they can run independently.
        // We use the same grouped-call / TCP fast-path as mode 5.
        //
        // ABI encoding helpers for Exchange calls:
        //
        // CreateNewItem(bytes32 hash, bytes info, uint256 price, uint256 start, uint256 end)
        //   - hash: 32 bytes, padded (we use a unique per-call hash = keccak256(user_addr + nonce))
        //   - info: dynamic bytes (offset + length + data)
        //   - price: uint256
        //   - start, end: uint256
        //
        // PurchaseItem(bytes32 hash, uint256 time)
        //   - hash: same as used in CreateNewItem
        //   - time: current timestamp ms
        //   - value: must be >= price (send via amount field)
        //
        // For stress test simplicity:
        //   - CreateNewItem: hash = sha256(shard||user_idx||round), info = "test", price = 1, start=0, end=2^64-1
        //   - PurchaseItem:  hash = same hash, called by a DIFFERENT user on the same contract

        // ABI encoder helpers
        auto pad32_hex = [](const std::string& hex) -> std::string {
            if (hex.size() >= 64) return hex.substr(hex.size() - 64);
            return std::string(64 - hex.size(), '0') + hex;
        };
        auto uint256_hex = [&](uint64_t v) -> std::string {
            std::stringstream ss; ss << std::hex << v;
            return pad32_hex(ss.str());
        };
        auto bytes32_hex = [](const std::string& raw32) -> std::string {
            return common::Encode::HexEncode(raw32);  // 64 hex chars
        };

        // Encode CreateNewItem call
        // Signature: CreateNewItem(bytes32,bytes,uint256,uint256,uint256)
        // ABI layout: [selector 4B][hash 32B][offset_info 32B][price 32B][start 32B][end 32B][len_info 32B][info_data padded]
        auto encode_create_new_item = [&](const std::string& hash_raw32,
                                           uint64_t price, uint64_t start_ms, uint64_t end_ms) -> std::string {
            // Dynamic bytes "info" = fixed ascii bytes "info" = 0x696e666f
            // offset of "bytes info" = 5 * 32 = 160 = 0xa0 (after: hash, offset, price, start, end)
            std::string info_data = "696e666f";  // "info" as hex
            uint64_t info_len = 4;
            uint64_t info_padded_len = ((info_len + 31) / 32) * 32;
            std::string info_padding((info_padded_len - info_len) * 2, '0');

            std::string enc;
            enc += sel_create;                        // 4B selector
            enc += bytes32_hex(hash_raw32);           // bytes32 hash
            enc += uint256_hex(5 * 32);               // offset of bytes arg (= 5 params * 32)
            enc += uint256_hex(price);                // uint256 price
            enc += uint256_hex(start_ms);             // uint256 start
            enc += uint256_hex(end_ms);               // uint256 end
            enc += uint256_hex(info_len);             // length of bytes
            enc += info_data + info_padding;          // bytes data padded to 32
            return enc;
        };

        // Encode PurchaseItem call
        // Signature: PurchaseItem(bytes32,uint256)
        // ABI layout: [selector 4B][hash 32B][time 32B]
        auto encode_purchase_item = [&](const std::string& hash_raw32, uint64_t time_ms) -> std::string {
            return sel_purchase + bytes32_hex(hash_raw32) + uint256_hex(time_ms);
        };

        // Generate a deterministic hash for round r, user ui, shard si
        auto make_item_hash = [&](int si, uint32_t ui, uint64_t round) -> std::string {
            // 32-byte hash: sha256(shard_id || user_idx || round)
            std::string seed(12, '\0');
            seed[0]  = (char)((si * 64 + ui) >> 24 & 0xFF);
            seed[1]  = (char)((si * 64 + ui) >> 16 & 0xFF);
            seed[2]  = (char)((si * 64 + ui) >>  8 & 0xFF);
            seed[3]  = (char)((si * 64 + ui)       & 0xFF);
            seed[4]  = (char)(round >> 24 & 0xFF);
            seed[5]  = (char)(round >> 16 & 0xFF);
            seed[6]  = (char)(round >>  8 & 0xFF);
            seed[7]  = (char)(round       & 0xFF);
            seed[8]  = (char)(kCallRounds >> 24 & 0xFF);
            seed[9]  = (char)(kCallRounds >> 16 & 0xFF);
            seed[10] = (char)(kCallRounds >>  8 & 0xFF);
            seed[11] = (char)(kCallRounds       & 0xFF);
            // Use keccak256 of seed for uniqueness
            std::string h = common::Encode::HexDecode(utils::keccak256Str(common::Encode::HexEncode(seed)));
            h.resize(32);
            return h;
        };

        std::cout << "\n[Phase 6] Stress test: CreateNewItem x" << kCallRounds
                  << " then PurchaseItem x" << kCallRounds << " per shard (concurrent)\n";

        // Per-shard: collect (user, contract, prefund_nonce) tuples
        // For CreateNewItem: caller = user (who creates the item, owns it)
        // For PurchaseItem:  caller = DIFFERENT user (buyer ≠ owner)
        //   Since each user creates items on their contract, we pair users:
        //   user[ui] creates → user[ui+1 % N] purchases (different user on same contract)

        struct StressUser6 {
            std::string prikey_raw;
            std::string addr_raw;
            std::string addr_hex;
            std::string contract_addr;   // assigned contract
            uint32_t    contract_idx;
        };

        std::atomic<uint64_t> create_ok6{0}, create_fail6{0};
        std::atomic<uint64_t> purchase_ok6{0}, purchase_fail6{0};

        // Run shards concurrently
        std::vector<std::thread> shard_stress_threads;

        for (int si = 0; si < kNumShards && !global_stop; ++si) {
            shard_stress_threads.emplace_back([&, si]() {
                auto& sd = shard_data[si];

                // Build stress user list
                uint32_t nd6 = (uint32_t)sd.deployer_addrs.size();
                uint32_t nu6 = (uint32_t)sd.user_addrs.size();
                std::vector<uint32_t> valid_contracts;
                for (uint32_t i = 0; i < nd6; ++i)
                    if (sd.contracts_confirmed[i] && !sd.contract_addrs[i].empty())
                        valid_contracts.push_back(i);
                if (valid_contracts.empty()) {
                    std::cout << "  Shard " << sd.shard_id << ": no contracts, skip stress\n";
                    return;
                }

                std::vector<StressUser6> stress_users;
                for (uint32_t ui = 0; ui < nu6; ++ui) {
                    if (!sd.users_confirmed[ui]) continue;
                    uint32_t ci = valid_contracts[ui % valid_contracts.size()];
                    StressUser6 su;
                    su.prikey_raw = sd.user_prikeys[ui];
                    su.addr_raw   = sd.user_addrs[ui];
                    su.addr_hex   = common::Encode::HexEncode(su.addr_raw);
                    su.contract_addr = sd.contract_addrs[ci];
                    su.contract_idx  = ci;
                    stress_users.push_back(std::move(su));
                }
                if (stress_users.empty()) {
                    std::cout << "  Shard " << sd.shard_id << ": no confirmed users, skip\n";
                    return;
                }

                uint32_t N = stress_users.size();
                std::cout << "  Shard " << sd.shard_id << ": " << N << " stress users, "
                          << valid_contracts.size() << " contracts\n";

                // Fetch initial prepay nonces for CreateNewItem
                // Prepay key = contract_addr + user_addr (for step 8 calls)
                std::unordered_map<std::string, int64_t> prepay_nonce;
                {
                    const uint32_t kBN = 50;
                    std::vector<std::string> pkeys;
                    for (auto& su : stress_users)
                        pkeys.push_back(su.contract_addr + su.addr_hex);
                    ShardoraSDK stress_sdk(sd.node_ip, sd.node_http);
                    for (uint32_t off = 0; off < pkeys.size(); off += kBN) {
                        uint32_t end = std::min(off + kBN, (uint32_t)pkeys.size());
                        std::vector<std::string> batch(pkeys.begin() + off, pkeys.begin() + end);
                        auto r = stress_sdk.batchQueryAccounts(batch);
                        for (uint32_t k = 0; k < batch.size(); ++k) {
                            int64_t n = 0;
                            if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                                auto& acc = r["accounts"][batch[k]];
                                if (acc.contains("nonce")) {
                                    try {
                                        auto ns = acc["nonce"].get<std::string>();
                                        std::from_chars(ns.data(), ns.data()+ns.size(), n);
                                    } catch (...) {}
                                }
                            }
                            prepay_nonce[pkeys[off + k]] = n;
                        }
                    }
                    std::cout << "  Shard " << sd.shard_id << ": nonces fetched for "
                              << pkeys.size() << " prepay accounts\n";
                }

                // ── CreateNewItem stress ─────────────────────────────────
                // kCallRounds = total sends per shard; distribute evenly across N users
                std::cout << "  Shard " << sd.shard_id << ": launching CreateNewItem x"
                          << kCallRounds << "...\n";
                {
                    uint32_t nthreads = std::min(32u, N); if (!nthreads) nthreads = 1;
                    uint32_t per_t = N / nthreads;
                    // rounds per user = kCallRounds / N (last user gets remainder)
                    uint64_t rounds_per_user = (N > 0) ? kCallRounds / N : kCallRounds;
                    uint64_t rounds_last     = (N > 0) ? kCallRounds - rounds_per_user * (N - 1) : kCallRounds;
                    uint64_t interval_us = (kTargetTps > 0)
                        ? (uint64_t)nthreads * 1000000ULL / kTargetTps : 0;
                    std::vector<std::thread> tv;
                    for (uint32_t t = 0; t < nthreads; ++t) {
                        uint32_t us = t * per_t;
                        uint32_t ue = (t == nthreads-1) ? N : (us + per_t);
                        tv.emplace_back([&, si, us, ue, interval_us, rounds_per_user, rounds_last, N]() {
                            auto& sd2 = shard_data[si];
                            for (uint32_t ui = us; ui < ue && !global_stop; ++ui) {
                                auto& su = stress_users[ui];
                                std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                                sec->SetPrivateKey(su.prikey_raw);
                                std::string prepay_key = su.contract_addr + su.addr_hex;
                                int64_t& n = prepay_nonce[prepay_key];
                                uint64_t my_rounds = (ui == N - 1) ? rounds_last : rounds_per_user;

                                for (uint64_t r = 0; r < my_rounds && !global_stop; ++r) {
                                    std::string hash_raw = make_item_hash(si, ui, r);
                                    std::string input = encode_create_new_item(hash_raw, 1, 0, UINT64_MAX);
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(su.prikey_raw),
                                        common::Encode::HexDecode(su.contract_addr),
                                        "call", input, 0, 5000000, 1, kShards[0]);
                                    if (tcp_enq6(tx, shard_data[0].node_ip, shard_data[0].node_tcp)) ++create_ok6;
                                    else { ++create_fail6; --n; }
                                    if (interval_us > 0) usleep(interval_us);
                                }
                            }
                        });
                    }
                    uint64_t shard_creates = kCallRounds;
                    std::thread prog([&, si, shard_creates]() {
                        uint64_t prev = 0;
                        while (create_ok6.load() + create_fail6.load() < shard_creates && !global_stop) {
                            for (int i = 0; i < 30 && !global_stop; ++i) usleep(100000);
                            uint64_t cur = create_ok6.load();
                            std::cout << "  [Shard " << kShards[si] << " CreateNewItem] "
                                      << cur << "/" << shard_creates
                                      << " sent (" << (cur - prev)/3 << " tx/s)\n";
                            prev = cur;
                        }
                    });
                    for (auto& t : tv) t.join();
                    prog.join();
                }
                std::cout << "  Shard " << sd.shard_id << ": CreateNewItem done: "
                          << create_ok6.load() << " ok, " << create_fail6.load() << " fail\n";

                // Wait for CreateNewItem consensus before PurchaseItem
                std::cout << "  Shard " << sd.shard_id << ": waiting 10s for CreateNewItem consensus...\n";
                for (int w = 0; w < 100 && !global_stop; ++w) usleep(100000);

                // ── PurchaseItem stress ──────────────────────────────────
                // Buyer = user[ui+1 % N] purchases item created by user[ui]
                // Buyer's prepay key = contract_addr + buyer_addr
                std::cout << "  Shard " << sd.shard_id << ": fetching PurchaseItem prepay nonces...\n";
                std::unordered_map<std::string, int64_t> buy_prepay_nonce;
                {
                    const uint32_t kBN = 50;
                    std::vector<std::string> pkeys;
                    for (uint32_t ui = 0; ui < N; ++ui) {
                        uint32_t buyer_ui = (ui + 1) % N;
                        auto& buyer = stress_users[buyer_ui];
                        // Use seller's contract (buyer pays on seller's contract)
                        auto& seller = stress_users[ui];
                        std::string pk = seller.contract_addr + buyer.addr_hex;
                        pkeys.push_back(pk);
                    }
                    // dedup
                    std::sort(pkeys.begin(), pkeys.end());
                    pkeys.erase(std::unique(pkeys.begin(), pkeys.end()), pkeys.end());
                    ShardoraSDK stress_sdk2(sd.node_ip, sd.node_http);
                    for (uint32_t off = 0; off < pkeys.size(); off += kBN) {
                        uint32_t end = std::min(off + kBN, (uint32_t)pkeys.size());
                        std::vector<std::string> batch(pkeys.begin() + off, pkeys.begin() + end);
                        auto r = stress_sdk2.batchQueryAccounts(batch);
                        for (uint32_t k = 0; k < batch.size(); ++k) {
                            int64_t n = 0;
                            if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                                auto& acc = r["accounts"][batch[k]];
                                if (acc.contains("nonce")) {
                                    try {
                                        auto ns = acc["nonce"].get<std::string>();
                                        std::from_chars(ns.data(), ns.data()+ns.size(), n);
                                    } catch (...) {}
                                }
                            }
                            buy_prepay_nonce[pkeys[off + k]] = n;
                        }
                    }
                }

                std::cout << "  Shard " << sd.shard_id << ": launching PurchaseItem x"
                          << kCallRounds << "...\n";
                {
                    uint32_t nthreads = std::min(32u, N); if (!nthreads) nthreads = 1;
                    uint32_t per_t = N / nthreads;
                    uint64_t rounds_per_user = (N > 0) ? kCallRounds / N : kCallRounds;
                    uint64_t rounds_last     = (N > 0) ? kCallRounds - rounds_per_user * (N - 1) : kCallRounds;
                    uint64_t interval_us = (kTargetTps > 0)
                        ? (uint64_t)nthreads * 1000000ULL / kTargetTps : 0;
                    std::vector<std::thread> tv;
                    uint64_t now_ms = (uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                    for (uint32_t t = 0; t < nthreads; ++t) {
                        uint32_t us = t * per_t;
                        uint32_t ue = (t == nthreads-1) ? N : (us + per_t);
                        tv.emplace_back([&, si, us, ue, interval_us, now_ms, rounds_per_user, rounds_last, N]() {
                            auto& sd2 = shard_data[si];
                            for (uint32_t ui = us; ui < ue && !global_stop; ++ui) {
                                uint32_t buyer_ui = (ui + 1) % N;
                                auto& seller = stress_users[ui];
                                auto& buyer  = stress_users[buyer_ui];
                                std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                                sec->SetPrivateKey(buyer.prikey_raw);
                                std::string prepay_key = seller.contract_addr + buyer.addr_hex;
                                int64_t& n = buy_prepay_nonce[prepay_key];
                                uint64_t my_rounds = (ui == N - 1) ? rounds_last : rounds_per_user;

                                for (uint64_t r = 0; r < my_rounds && !global_stop; ++r) {
                                    std::string hash_raw = make_item_hash(si, ui, r);
                                    std::string input = encode_purchase_item(hash_raw, now_ms + r);
                                    // amount = price (1 wei) to satisfy require(price <= msg.value)
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(buyer.prikey_raw),
                                        common::Encode::HexDecode(seller.contract_addr),
                                        "call", input, 1, 5000000, 1, kShards[0]);
                                    if (tcp_enq6(tx, shard_data[0].node_ip, shard_data[0].node_tcp)) ++purchase_ok6;
                                    else { ++purchase_fail6; --n; }
                                    if (interval_us > 0) usleep(interval_us);
                                }
                            }
                        });
                    }
                    uint64_t shard_purchases = kCallRounds;
                    std::thread prog([&, si, shard_purchases]() {
                        uint64_t prev = 0;
                        while (purchase_ok6.load() + purchase_fail6.load() < shard_purchases && !global_stop) {
                            for (int i = 0; i < 30 && !global_stop; ++i) usleep(100000);
                            uint64_t cur = purchase_ok6.load();
                            std::cout << "  [Shard " << kShards[si] << " PurchaseItem] "
                                      << cur << "/" << shard_purchases
                                      << " sent (" << (cur - prev)/3 << " tx/s)\n";
                            prev = cur;
                        }
                    });
                    for (auto& t : tv) t.join();
                    prog.join();
                }
                std::cout << "  Shard " << sd.shard_id << ": PurchaseItem done: "
                          << purchase_ok6.load() << " ok, " << purchase_fail6.load() << " fail\n";
            }); // end shard_stress_threads[si]
        }

        for (auto& t : shard_stress_threads) t.join();

        // Drain TCP queue
        std::cout << "  Draining TCP send queue...\n";
        for (int w = 0; w < 300 && !global_stop; ++w) {
            bool empty = false;
            { std::lock_guard<std::mutex> lk(tcp_mtx6); empty = tcp_q6.empty(); }
            if (empty) break;
            usleep(100000);
        }

        // ── Summary ──────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '=') << "\n";
        // ── Final verification: query on-chain state for all shards ──
        {
            std::cout << "\n[Final Verify] Querying on-chain state...\n";
            // Write addresses to file for external verification
            std::ofstream addr_out("/tmp/mode6_addrs.txt");
            for (int i = 0; i < kNumShards; ++i) {
                auto& sd = shard_data[i];
                addr_out << "# shard " << sd.shard_id << " users " << sd.user_addrs.size() << "\n";
                for (auto& a : sd.user_addrs)
                    addr_out << sd.shard_id << " user " << common::Encode::HexEncode(a) << "\n";
                addr_out << "# shard " << sd.shard_id << " contracts " << sd.contract_addrs.size() << "\n";
                for (auto& a : sd.contract_addrs)
                    addr_out << sd.shard_id << " contract " << a << "\n";
            }
            addr_out.close();
            std::cout << "  Addresses written to /tmp/mode6_addrs.txt\n";

            // Batch-query each shard via HTTP
            for (int i = 0; i < kNumShards; ++i) {
                auto& sd = shard_data[i];
                ShardoraSDK vsdk(sd.node_ip, sd.node_http);
                // Query sample of user addrs (first 300)
                std::vector<std::string> sample_users;
                for (size_t j = 0; j < sd.user_addrs.size() && j < 300; ++j)
                    sample_users.push_back(common::Encode::HexEncode(sd.user_addrs[j]));
                int user_found = 0;
                if (!sample_users.empty()) {
                    auto res = vsdk.batchQueryAccounts(sample_users);
                    if (res["status"] == 0)
                        user_found = (int)res["accounts"].size();
                }
                // Query all contract addrs
                int contract_found = 0;
                if (!sd.contract_addrs.empty()) {
                    std::vector<std::string> caddrs(sd.contract_addrs.begin(), sd.contract_addrs.end());
                    for (size_t j = 0; j < caddrs.size(); j += 300) {
                        auto batch = std::vector<std::string>(
                            caddrs.begin() + j,
                            caddrs.begin() + std::min(j + 300, caddrs.size()));
                        auto res = vsdk.batchQueryAccounts(batch);
                        if (res["status"] == 0)
                            contract_found += (int)res["accounts"].size();
                    }
                }
                std::cout << "  Shard " << sd.shard_id
                          << ": users_onchain=" << user_found << "/" << (int)std::min(sd.user_addrs.size(), (size_t)300)
                          << "  contracts_onchain=" << contract_found << "/" << sd.contract_addrs.size()
                          << "\n";
            }
        }

        std::cout << "  EXCHANGE STRESS TEST COMPLETE\n";
        std::cout << std::string(70, '=') << "\n";
        std::cout << "  Fund:        " << fund_ok6.load() << " ok, " << fund_fail6.load() << " fail\n";
        std::cout << "  Deploy:      " << deploy_ok6.load() << " ok, " << deploy_fail6.load() << " fail\n";
        std::cout << "  Prefund:     " << pf_ok6.load() << " ok, " << pf_fail6.load() << " fail\n";
        std::cout << "  CreateNew:   " << create_ok6.load() << " ok, " << create_fail6.load() << " fail\n";
        std::cout << "  Purchase:    " << purchase_ok6.load() << " ok, " << purchase_fail6.load() << " fail\n";
        std::cout << "  TCP sent:    " << tcp_sent6.load() << " total\n";

        // Stop TCP sender
        tcp_stop6.store(true);
        tcp_cv6.notify_one();
        tcp_sender6.join();

        transport::TcpTransport::Instance()->Stop();
        return 0;
    }


    // ── Mode 7: Multi-Shard Account Creation with Per-Shard Verification ──
    // Creates N accounts on EACH consensus shard (3,4,5,6) via XXH64 routing.
    // Sends all txs from funder shard. Saves addresses to file. Verifies on each shard's node.
    // Usage: txcli 7 <funder_shard> <ip> <port> [accounts_per_shard] [threads] [call_rounds] [purchase_rounds]
    //   Per-shard verification nodes: argv[9..12] = ip3:port3 ip4:port4 ip5:port5 ip6:port6
    if (argv[1][0] == '7') {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        uint32_t funder_shard = (argc >= 3) ? std::stoi(argv[2]) : 3;
        std::string node_ip = (argc >= 4) ? argv[3] : "192.168.26.168";
        uint16_t node_port = (argc >= 5) ? std::stoi(argv[4]) : 13001;
        uint32_t accounts_per_shard = (argc >= 6) ? std::stoi(argv[5]) : 10000;
        uint32_t num_threads = (argc >= 7) ? std::stoi(argv[6]) : 8;
        uint32_t kCallRounds7  = (argc >= 8) ? std::stoul(argv[7]) : 20;
        uint32_t kPurchaseRounds7 = (argc >= 9) ? std::stoul(argv[8]) : 20;

        // Per-shard verification endpoints (ip:http_port)
        struct ShardEndpoint { std::string ip; uint16_t http_port; };
        std::map<uint32_t, ShardEndpoint> shard_endpoints = {
    {3, {"192.168.27.111", 23001}},  // shard 3 查询端口
    {4, {"192.168.27.132", 24001}},  // shard 4 查询端口
    {5, {"192.168.27.122", 25001}},  // shard 5 查询端口
    {6, {"192.168.27.119", 26001}},  // shard 6 查询端口
	};
        // Override from argv[9..12] if provided: format "ip:port"
        for (int i = 9; i < std::min(argc, 13); ++i) {
            uint32_t shard_id = 3 + (i - 9);
            std::string arg(argv[i]);
            auto colon = arg.find(':');
            if (colon != std::string::npos) {
                shard_endpoints[shard_id] = {arg.substr(0, colon), (uint16_t)std::stoi(arg.substr(colon+1))};
            }
        }

        global_chain_node_ip = node_ip;
        global_chain_node_http_port = node_port + 10000;
        shardnum = funder_shard;

        const uint32_t kConsensusBegin = network::kConsensusShardBeginNetworkId;  // 3
        const uint32_t kMaxShardId = 6;
        const uint32_t kNumConsensusShards = kMaxShardId - kConsensusBegin + 1;  // 4

        std::cout << "\n=== Mode 7: Multi-Shard Account Creation ===" << std::endl;
        std::cout << "Funder shard: " << funder_shard << std::endl;
        std::cout << "Send node: " << node_ip << ":" << node_port << " (TCP)" << std::endl;
        std::cout << "Accounts per shard: " << accounts_per_shard << std::endl;
        std::cout << "Target shards: " << kConsensusBegin << "-" << kMaxShardId << std::endl;
        std::cout << "CreateNewItem rounds: " << kCallRounds7 << std::endl;
        std::cout << "PurchaseItem rounds:  " << kPurchaseRounds7 << std::endl;
        std::cout << "Verification endpoints:" << std::endl;
        for (auto& [sid, ep] : shard_endpoints) {
            std::cout << "  Shard " << sid << ": " << ep.ip << ":" << ep.http_port << std::endl;
        }

        LoadAllAccounts(funder_shard);
        if (g_prikeys.empty()) {
            std::cerr << "No funded accounts loaded!" << std::endl;
            return 1;
        }
        std::cout << "Loaded " << g_prikeys.size() << " funder accounts" << std::endl;

        SignalRegister();
        WriteDefaultLogConf();

        // Setup transport
        transport::MultiThreadHandler net_handler;
        std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
        auto db_ptr = std::make_shared<db::Db>();
        if (!db_ptr->Init(db_path + "_mode7")) {
            std::cerr << "init db failed" << std::endl;
            return 1;
        }
        if (net_handler.Init(db_ptr, sec) != 0) {
            std::cerr << "init net handler failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Init("127.0.0.1:13899", 128, false, &net_handler) != 0) {
            std::cerr << "init tcp failed" << std::endl;
            return 1;
        }
        if (transport::TcpTransport::Instance()->Start(false) != 0) {
            std::cerr << "start tcp failed" << std::endl;
            return 1;
        }

        // Dedicated TCP sender thread (same pattern as mode 5/6).
        // Worker threads must NOT call TcpTransport::Send directly — get_thread_index()
        // only allows the fixed system-thread pool; overflow causes SHARDORA_FATAL/SIGSEGV.
        struct TcpSendItem7 {
            transport::MessagePtr msg;
            std::string dest_ip;
            uint16_t dest_port;
        };
        std::queue<TcpSendItem7> tcp_q7;
        std::mutex tcp_mtx7;
        std::condition_variable tcp_cv7;
        std::atomic<bool> tcp_stop7{false};
        std::atomic<uint64_t> tcp_sent7{0};

        std::thread tcp_sender7([&]() {
            // Register thread index immediately while Init/Start window still allows it.
            // Otherwise the first Send() at Phase 5 (minutes later) hits SHARDORA_FATAL.
            (void)common::GlobalInfo::Instance()->get_thread_index();
            std::vector<TcpSendItem7> batch;
            batch.reserve(4096);
            while (!tcp_stop7.load()) {
                {
                    std::unique_lock<std::mutex> lk(tcp_mtx7);
                    tcp_cv7.wait_for(lk, std::chrono::milliseconds(1),
                        [&]{ return !tcp_q7.empty() || tcp_stop7.load(); });
                    while (!tcp_q7.empty()) {
                        batch.push_back(std::move(tcp_q7.front()));
                        tcp_q7.pop();
                    }
                }
                for (auto& item : batch) {
                    transport::TcpTransport::Instance()->Send(
                        item.dest_ip, item.dest_port, item.msg->header);
                    ++tcp_sent7;
                }
                batch.clear();
            }
            std::lock_guard<std::mutex> lk(tcp_mtx7);
            while (!tcp_q7.empty()) {
                auto item = std::move(tcp_q7.front());
                tcp_q7.pop();
                transport::TcpTransport::Instance()->Send(
                    item.dest_ip, item.dest_port, item.msg->header);
                ++tcp_sent7;
            }
        });

        auto tcp_enq7 = [&](transport::MessagePtr msg, const std::string& ip, uint16_t port) -> bool {
            if (!msg) return false;
            // Back-pressure: block if queue exceeds 8192 items to prevent OOM on 10M+ ops.
            while (!global_stop) {
                {
                    std::lock_guard<std::mutex> lk(tcp_mtx7);
                    if (tcp_q7.size() < 8192) {
                        tcp_q7.push({std::move(msg), ip, port});
                        tcp_cv7.notify_one();
                        return true;
                    }
                }
                usleep(200);
            }
            return false;
        };

        auto tcp_drain7 = [&]() {
            for (int w = 0; w < 200 && !global_stop; ++w) {
                bool empty = false;
                { std::lock_guard<std::mutex> lk(tcp_mtx7); empty = tcp_q7.empty(); }
                if (empty) break;
                usleep(100000);
            }
        };

        // Phase 1: Generate addresses routed to each shard
        std::cout << "\n[Phase 1] Generating " << accounts_per_shard << " addresses per shard..." << std::endl;
        std::map<uint32_t, std::vector<std::string>> shard_addrs;
        std::map<uint32_t, std::vector<std::string>> shard_prikeys;  // raw prikey per address
        for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
            shard_addrs[s].reserve(accounts_per_shard);
            shard_prikeys[s].reserve(accounts_per_shard);
        }

        auto gen_start = std::chrono::steady_clock::now();
        for (uint32_t target_shard = kConsensusBegin; target_shard <= kMaxShardId; ++target_shard) {
            uint32_t count = 0;
            while (count < accounts_per_shard && !global_stop) {
                std::string prikey;
                prikey.resize(32);
                for (uint32_t j = 0; j < 32; ++j) {
                    prikey[j] = static_cast<char>(common::Random::RandomUint32() % 256);
                }
                std::shared_ptr<security::Security> tmp_sec = std::make_shared<security::Ecdsa>();
                tmp_sec->SetPrivateKey(prikey);
                std::string addr = tmp_sec->GetAddress();

                // Use same routing as chain: shard = 6 - GetAddressPoolIndex(addr)/8
                uint32_t pool = common::GetAddressPoolIndex(addr);
                uint64_t hash_value = common::Hash::Hash64(addr);
                uint32_t routed_shard = (hash_value % (kMaxShardId - network::kConsensusShardBeginNetworkId + 1)) +
                    network::kConsensusShardBeginNetworkId;
                // uint32_t routed_shard = 6 - pool / 8;
                if (routed_shard == target_shard) {
                    shard_addrs[target_shard].push_back(addr);
                    shard_prikeys[target_shard].push_back(prikey);
                    ++count;
                }
                if (count % 5000 == 0 && count > 0) {
                    std::cout << "  Shard " << target_shard << ": " << count << "/" << accounts_per_shard << std::endl;
                }
            }
            std::cout << "  Shard " << target_shard << ": " << count << " done" << std::endl;
        }
        auto gen_secs = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - gen_start).count();
        std::cout << "Generation: " << gen_secs << "s" << std::endl;

        // Save addresses to file (hex encoded, one per line, grouped by shard)
        std::string addr_file = "/tmp/mode7_addrs.txt";
        {
            std::ofstream ofs(addr_file);
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                ofs << "# shard " << s << " (" << shard_addrs[s].size() << " addresses)" << std::endl;
                for (auto& addr : shard_addrs[s]) {
                    ofs << s << " " << common::Encode::HexEncode(addr) << std::endl;
                }
            }
            ofs.close();
            std::cout << "Addresses saved to " << addr_file << std::endl;
        }

        // Phase 2: Send transactions
        std::cout << "\n[Phase 2] Sending transactions..." << std::endl;
        ShardoraSDK sdk(global_chain_node_ip, global_chain_node_http_port);

        std::string funder_prikey = g_prikeys[0];
        std::shared_ptr<security::Security> funder_sec = std::make_shared<security::Ecdsa>();
        funder_sec->SetPrivateKey(funder_prikey);
        std::string funder_addr = funder_sec->GetAddress();

        int64_t nonce = sdk.fetchNonce(common::Encode::HexEncode(funder_addr));
        if (nonce < 0) nonce = 0;
        std::cout << "Funder: " << common::Encode::HexEncode(funder_addr) << std::endl;
        std::cout << "Starting nonce: " << nonce << std::endl;

        std::atomic<uint64_t> sent_ok{0};
        std::atomic<uint64_t> sent_fail{0};
        auto send_start = std::chrono::steady_clock::now();

        uint64_t total_targets = 0;
        for (auto& [s, addrs] : shard_addrs) total_targets += addrs.size();
        std::cout << "Total targets: " << total_targets << std::endl;

        for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
            for (size_t i = 0; i < shard_addrs[s].size() && !global_stop; ++i) {
                auto tx_msg_ptr = CreateTransactionWithAttr(
                    funder_sec, ++nonce, funder_prikey,
                    shard_addrs[s][i], "", "",
                    2000000000ULL, 210000, 1, funder_shard);

                if (tx_msg_ptr && transport::TcpTransport::Instance()->Send(
                        global_chain_node_ip, node_port, tx_msg_ptr->header) == 0) {
                    ++sent_ok;
                } else {
                    ++sent_fail;
                }
                usleep(100);
            }
            std::cout << "  Shard " << s << ": " << shard_addrs[s].size() << " sent (total ok="
                      << sent_ok.load() << " fail=" << sent_fail.load() << ")" << std::endl;
        }

        auto send_secs = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - send_start).count();
        std::cout << "Sending: " << send_secs << "s, OK=" << sent_ok.load()
                  << " Failed=" << sent_fail.load() << std::endl;

        // Phase 3: Wait and verify on each shard's own node
        std::cout << "\n[Phase 3] Waiting 30s for cross-shard confirmation..." << std::endl;
        std::cout << "Verifying ALL addresses on each shard's node..." << std::endl;
        uint64_t total_missing = 0;
        uint64_t total_verified = 0;

        std::unordered_map<uint32_t, std::unordered_set<std::string>> checked_addrs;
        for (uint32_t try_times = 0; try_times < 60; ++try_times) {
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                auto& ep = shard_endpoints[s];
                ShardoraSDK shard_sdk(ep.ip, ep.http_port);
                // Use batchQueryAccounts for efficiency
                std::vector<std::string> hex_addrs;
                hex_addrs.reserve(shard_addrs[s].size());
                auto& checked_set = checked_addrs[s];
                for (auto& addr : shard_addrs[s]) {
                    if (checked_set.find(addr) != checked_set.end()) {
                        continue;
                    }

                    hex_addrs.push_back(common::Encode::HexEncode(addr));
                }

                auto result = shard_sdk.batchQueryAccounts(hex_addrs);
                uint32_t shard_verified = 0;
                uint32_t shard_missing = 0;
                if (result.contains("accounts")) {
                    for (auto& [hex_addr, _] : result["accounts"].items()) {
                        checked_set.insert(common::Encode::HexDecode(hex_addr));
                    }
                    shard_verified = result["accounts"].size();
                }
                if (result.contains("not_found")) {
                    shard_missing = result["not_found"].size();
                }
                // Fallback: count by checking presence
                if (shard_verified == 0 && result.contains("status") && result["status"] != 0) {
                    // batch query might have failed, try individual
                    std::cout << "  Shard " << s << ": batch query failed, trying individual..." << std::endl;
                    for (auto& hex_addr : hex_addrs) {
                        int64_t bal = shard_sdk.fetchBalance(hex_addr);
                        if (bal >= 0) {
                            ++shard_verified;
                            checked_set.insert(common::Encode::HexDecode(hex_addr));
                        } else ++shard_missing;
                        if (global_stop) break;
                    }
                } else {
                    shard_missing = shard_addrs[s].size() - shard_verified;
                }

                std::cout << "  Shard " << s << " (" << ep.ip << ":" << ep.http_port << "): "
                        << checked_set.size() << "/" << shard_addrs[s].size() << " verified"
                        << (shard_missing > 0 ? " (" + std::to_string(shard_missing) + " missing)" : "")
                        << std::endl;
            }

            total_verified = 0;
            total_missing = 0;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                auto& checked_set = checked_addrs[s];
                total_verified += checked_set.size();
                total_missing += (accounts_per_shard - checked_set.size());
            }

            std::cout << "total_verified: " << total_verified << ", " 
                << ", need: " << (accounts_per_shard * (kMaxShardId - kConsensusBegin + 1)) << std::endl;
            if (total_verified == accounts_per_shard * (kMaxShardId - kConsensusBegin + 1)) {
                break;
            }

            usleep(10000000);
        }

        std::cout << "\n[Result] Total verified: " << total_verified << "/" << total_targets << std::endl;
        if (total_missing > 0) {
            std::cout << "  Missing: " << total_missing << " (may need more time for cross-shard propagation)" << std::endl;
            // Retry once after 30s more
            std::cout << "  Retrying after 30s..." << std::endl;
            sleep(30);
            total_verified = 0;
            total_missing = 0;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                auto& ep = shard_endpoints[s];
                ShardoraSDK shard_sdk(ep.ip, ep.http_port);
                std::vector<std::string> hex_addrs;
                for (auto& addr : shard_addrs[s]) {
                    hex_addrs.push_back(common::Encode::HexEncode(addr));
                }
                auto result = shard_sdk.batchQueryAccounts(hex_addrs);
                uint32_t shard_verified = 0;
                if (result.contains("accounts")) {
                    shard_verified = result["accounts"].size();
                }
                total_verified += shard_verified;
                total_missing += (shard_addrs[s].size() - shard_verified);
                std::cout << "  Shard " << s << ": " << shard_verified << "/" << shard_addrs[s].size() << std::endl;
            }
            std::cout << "\n[Final] Total verified: " << total_verified << "/" << total_targets << std::endl;
        }

        // ── Phase 4: Deploy 256 Exchange contracts per shard using verified shard addresses ──
        std::cout << "\n[Phase 4] Deploying Exchange contracts per shard..." << std::endl;

        // Compile Exchange contract
        const std::string EXCHANGE_SOL_7 = R"(
// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.17 <0.9.0;
contract Exchange {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;

    // Pack owner+exists in one slot (address 20b + bool 1b = 32b slot)
    struct ItemInfo {
        address payable owner;
        bool exists;
        uint256 price;
        uint256 start_time_ms;
        uint256 end_time_ms;
    }

    struct BuyerInfo { address payable buyer; uint256 price; uint256 time; }

    mapping(bytes32 => ItemInfo) public item_map;
    // purchase dedup: keccak256(hash, buyer) — no dynamic bytes alloc
    mapping(bytes32 => bool) public purchase_map;
    // buyers stored separately to keep ItemInfo small
    mapping(bytes32 => BuyerInfo[]) public item_buyers;

    function CreateNewItem(bytes32 hash, bytes memory info, uint256 price,
                           uint256 start, uint256 end) public payable {
        require(!item_map[hash].exists, "item exists");
        ItemInfo storage it = item_map[hash];
        it.owner = payable(msg.sender);
        it.price = price;
        it.start_time_ms = start;
        it.end_time_ms = end;
        it.exists = true;
    }

    function PurchaseItem(bytes32 hash, uint256 time) public payable {
        ItemInfo storage it = item_map[hash];
        require(it.exists, "not exists");
        require(msg.value >= it.price, "insufficient");
        bytes32 key = keccak256(abi.encodePacked(hash, msg.sender));
        require(!purchase_map[key], "already bought");
        purchase_map[key] = true;
        item_buyers[hash].push(BuyerInfo(payable(msg.sender), msg.value, time));
        if (item_buyers[hash].length == 1) {
            it.owner.transfer(msg.value);
        }
    }

    function TotalBuyers(bytes32 hash) public view returns (uint256) {
        return item_buyers[hash].length;
    }
}
)";

        ShardoraSDK compile_sdk(global_chain_node_ip, global_chain_node_http_port);
        auto compiled7 = compile_sdk.compileSolidity(EXCHANGE_SOL_7);
        if (!compiled7.contains("bytecode") || compiled7["bytecode"].get<std::string>().empty()) {
            std::cerr << "[Phase 4] Contract compilation failed, skipping deploy phase." << std::endl;
        } else {
            std::string exchange_bytecode7 = compiled7["bytecode"].get<std::string>();
            std::cout << "  Exchange bytecode: " << exchange_bytecode7.size() << " chars" << std::endl;

            const uint32_t kContractsPerShard7 = 256;
            const uint64_t kDeployPrefund7 = 800000000ULL;

            // per-shard: contract_addrs[shard] = list of deployed hex contract addresses
            std::map<uint32_t, std::vector<std::string>> contract_addrs7;
            std::map<uint32_t, std::vector<bool>> contracts_confirmed7;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                contract_addrs7[s].resize(kContractsPerShard7);
                contracts_confirmed7[s].resize(kContractsPerShard7, false);
            }

            std::atomic<uint32_t> deploy_ok7{0}, deploy_fail7{0};
            static std::atomic<uint64_t> deploy_ctr7{0};

            // shard routing consistent with mode 7 address generation
            auto shard_of_addr7 = [&](const std::string& addr_raw) -> uint32_t {
                uint64_t hv = common::Hash::Hash64(addr_raw);
                return (uint32_t)(hv % (kMaxShardId - kConsensusBegin + 1)) + kConsensusBegin;
            };

            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                if (checked_addrs[s].size() == 0) {
                    std::cout << "  Shard " << s << ": no verified addresses, skip deploy" << std::endl;
                    continue;
                }
                auto& ep = shard_endpoints[s];

                // pick up to kContractsPerShard7 deployer addresses from the verified set,
                // and retrieve their corresponding private keys from shard_prikeys
                std::vector<std::string> deployers;
                std::vector<std::string> deployer_prikeys;
                deployers.reserve(kContractsPerShard7);
                deployer_prikeys.reserve(kContractsPerShard7);
                for (uint32_t idx = 0; idx < shard_addrs[s].size() && deployers.size() < kContractsPerShard7; ++idx) {
                    if (checked_addrs[s].count(shard_addrs[s][idx])) {
                        deployers.push_back(shard_addrs[s][idx]);
                        deployer_prikeys.push_back(shard_prikeys[s][idx]);
                    }
                }

                uint32_t nd = (uint32_t)deployers.size();
                uint32_t nt = std::min(32u, nd);
                uint32_t pp = nd / nt;

                // Pre-fetch nonces for all deployers on this shard before spawning threads.
                // deployToAddress() calls fetchNonce() internally but can see stale nonce=0
                // when the deployer account already has committed txs (e.g. from Phase 2 fund).
                // Fetching here and passing explicitly avoids the double-increment bug.
                ShardoraSDK nonce_sdk7(ep.ip, ep.http_port);
                std::vector<int64_t> deployer_nonces(nd, 0);
                {
                    const uint32_t kNBatch = 300;
                    std::vector<std::string> addr_hexes;
                    addr_hexes.reserve(nd);
                    for (uint32_t i = 0; i < nd; ++i)
                        addr_hexes.push_back(common::Encode::HexEncode(deployers[i]));
                    for (uint32_t off = 0; off < nd; off += kNBatch) {
                        uint32_t bend = std::min(off + kNBatch, nd);
                        std::vector<std::string> batch(addr_hexes.begin() + off, addr_hexes.begin() + bend);
                        auto r = nonce_sdk7.batchQueryAccounts(batch);
                        for (uint32_t k = 0; k < batch.size(); ++k) {
                            int64_t n = 0;
                            if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                                auto& acc = r["accounts"][batch[k]];
                                if (acc.contains("nonce")) {
                                    try {
                                        auto ns = acc["nonce"].get<std::string>();
                                        std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                    } catch (...) {}
                                }
                            }
                            deployer_nonces[off + k] = n;
                        }
                    }
                    std::cout << "  Shard " << s << ": fetched nonces for " << nd << " deployers" << std::endl;
                }

                std::vector<std::thread> dth7;

                for (uint32_t t = 0; t < nt; ++t) {
                    uint32_t s2 = t * pp;
                    uint32_t e2 = (t == nt - 1) ? nd : s2 + pp;
                    dth7.emplace_back([&, s, s2, e2, ep]() {
                        ShardoraSDK tsdk7(ep.ip, ep.http_port);
                        for (uint32_t i = s2; i < e2 && !global_stop; ++i) {
                            // find a contract address that routes to this shard
                            std::string to_address;
                            for (int attempt = 0; attempt < 20000 && !global_stop; ++attempt) {
                                uint64_t cnt = deploy_ctr7.fetch_add(1);
                                std::string salt = deployers[i] + std::to_string(cnt) +
                                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                                std::string candidate = utils::keccak256Str(exchange_bytecode7 + salt).substr(24);
                                if (shard_of_addr7(common::Encode::HexDecode(candidate)) == s) {
                                    to_address = candidate;
                                    break;
                                }
                            }
                            if (to_address.empty()) { ++deploy_fail7; continue; }

                            std::string pk_hex = common::Encode::HexEncode(deployer_prikeys[i]);
                            // Use pre-fetched nonce so we don't hit stale nonce=0 from fetchNonce.
                            auto r = tsdk7.deployToAddressWithNonce(
                                pk_hex, exchange_bytecode7, to_address, kDeployPrefund7, deployer_nonces[i]);
                            if (r.contains("status") && r["status"] == 0) {
                                contract_addrs7[s][i] = to_address;
                                ++deploy_ok7;
                            } else {
                                ++deploy_fail7;
                            }
                            usleep(200);
                        }
                    });
                }
                for (auto& t : dth7) t.join();
                std::cout << "  Shard " << s << ": " << deploy_ok7.load() << " deployed so far" << std::endl;
            }
            std::cout << "  Deploy sends: " << deploy_ok7.load() << " ok, "
                      << deploy_fail7.load() << " fail" << std::endl;

            std::cout << "  Waiting 15s for deploy consensus..." << std::endl;
            for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);

            // Verify contracts on-chain per shard (up to 300s)
            std::cout << "  Verifying deployed contracts on-chain..." << std::endl;
            uint32_t total_contracts_confirmed = 0;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                auto& ep = shard_endpoints[s];
                std::vector<std::string> pending_addrs;
                std::vector<uint32_t>   pending_idx;
                for (uint32_t i = 0; i < kContractsPerShard7; ++i) {
                    if (!contract_addrs7[s][i].empty())  {
                        pending_addrs.push_back(contract_addrs7[s][i]);
                        pending_idx.push_back(i);
                    }
                }
                if (pending_addrs.empty()) continue;

                uint32_t shard_confirmed = 0;
                auto t0 = std::chrono::steady_clock::now();
                for (uint32_t rd = 0; !pending_addrs.empty() && !global_stop; ++rd) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - t0).count();
                    if (elapsed >= 300) {
                        std::cerr << "  Shard " << s << ": contract verify timeout, "
                                  << shard_confirmed << "/" << pending_addrs.size() + shard_confirmed
                                  << " confirmed" << std::endl;
                        break;
                    }
                    // query all shards for each batch (contract addr may land on any shard)
                    std::vector<bool> found(pending_addrs.size(), false);
                    for (uint32_t qs = kConsensusBegin; qs <= kMaxShardId && !global_stop; ++qs) {
                        ShardoraSDK qsdk(shard_endpoints[qs].ip, shard_endpoints[qs].http_port);
                        auto r = qsdk.batchQueryAccounts(pending_addrs);
                        if (!r.contains("accounts")) continue;
                        for (uint32_t k = 0; k < pending_addrs.size(); ++k)
                            if (!found[k] && r["accounts"].contains(pending_addrs[k]))
                                found[k] = true;
                    }
                    std::vector<std::string> next_pending_addrs;
                    std::vector<uint32_t>   next_pending_idx;
                    for (uint32_t k = 0; k < pending_addrs.size(); ++k) {
                        if (found[k]) {
                            contracts_confirmed7[s][pending_idx[k]] = true;
                            ++shard_confirmed;
                        } else {
                            next_pending_addrs.push_back(pending_addrs[k]);
                            next_pending_idx.push_back(pending_idx[k]);
                        }
                    }
                    pending_addrs = std::move(next_pending_addrs);
                    pending_idx   = std::move(next_pending_idx);
                    std::cout << "  [shard" << s << " contract round " << (rd+1)
                              << ", " << elapsed << "s] +" << (shard_confirmed - (shard_confirmed - (uint32_t)found.size() + (uint32_t)pending_addrs.size()))
                              << " = " << shard_confirmed
                              << " (" << pending_addrs.size() << " pending)" << std::endl;
                    if (!pending_addrs.empty()) {
                        for (int w = 0; w < 80 && !global_stop; ++w) usleep(100000);
                    }
                }
                total_contracts_confirmed += shard_confirmed;
                std::cout << "  Shard " << s << ": " << shard_confirmed
                          << "/" << kContractsPerShard7 << " contracts confirmed" << std::endl;
            }
            std::cout << "\n[Phase 4 Result] Total contracts confirmed: "
                      << total_contracts_confirmed << "/"
                      << (kContractsPerShard7 * kNumConsensusShards) << std::endl;

            // ── Phase 5: Prefund all users on all contracts — shards 3/4/5/6 in parallel ──
            // Each shard is independent (own users/contracts/nodes); run concurrently.
            std::cout << "\n[Phase 5] Setting prefund for all users on all contracts "
                      << "(shards " << kConsensusBegin << "-" << kMaxShardId
                      << " concurrent)..." << std::endl;
            std::atomic<uint64_t> pf_ok7{0}, pf_fail7{0};
            std::mutex pf_log_mtx7;

            // Per-shard progress counters (index by shard id)
            std::atomic<uint64_t> shard_pf_done7[8] = {};
            std::atomic<uint64_t> shard_pf_total7[8] = {};
            std::atomic<uint64_t> shard_pf_ok7[8] = {};
            std::atomic<uint64_t> shard_pf_fail7[8] = {};
            std::atomic<uint64_t> shard_pf_active7{0};

            auto pf_phase_t0 = std::chrono::steady_clock::now();
            uint64_t tcp_sent_base7 = tcp_sent7.load();
            std::atomic<bool> pf_global_prog_stop{false};
            std::thread pf_global_prog([&]() {
                uint64_t last_built = 0, last_sent = tcp_sent_base7;
                auto last_t = pf_phase_t0;
                while (!pf_global_prog_stop.load() && !global_stop) {
                    for (int i = 0; i < 20 && !pf_global_prog_stop.load() && !global_stop; ++i)
                        usleep(100000);
                    auto now = std::chrono::steady_clock::now();
                    double el = std::chrono::duration<double>(now - pf_phase_t0).count();
                    double dt = std::chrono::duration<double>(now - last_t).count();
                    if (dt < 0.001) dt = 0.001;

                    uint64_t built = 0, need = 0;
                    for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                        built += shard_pf_done7[s].load();
                        need += shard_pf_total7[s].load();
                    }
                    uint64_t sent = tcp_sent7.load();
                    uint64_t sent_delta = (sent >= tcp_sent_base7) ? (sent - tcp_sent_base7) : 0;
                    double build_tps_avg = (el > 0.001) ? (double)built / el : 0;
                    double send_tps_avg = (el > 0.001) ? (double)sent_delta / el : 0;
                    double build_tps_inst = (double)(built - last_built) / dt;
                    double send_tps_inst = (double)(sent - last_sent) / dt;
                    size_t qsz = 0;
                    { std::lock_guard<std::mutex> lk(tcp_mtx7); qsz = tcp_q7.size(); }

                    {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  [Phase5][" << (uint64_t)el << "s]"
                                  << " built=" << built << "/" << need
                                  << " build_tps=" << (uint64_t)build_tps_inst
                                  << " (avg " << (uint64_t)build_tps_avg << ")"
                                  << " | tcp_sent=" << sent_delta
                                  << " send_tps=" << (uint64_t)send_tps_inst
                                  << " (avg " << (uint64_t)send_tps_avg << ")"
                                  << " tcp_q=" << qsz
                                  << " |";
                        for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                            uint64_t d = shard_pf_done7[s].load();
                            uint64_t t = shard_pf_total7[s].load();
                            if (t == 0) continue;
                            uint64_t pct = d * 100 / t;
                            std::cout << " s" << s << ":" << d << "/" << t
                                      << "(" << pct << "%)"
                                      << " ok=" << shard_pf_ok7[s].load()
                                      << " fail=" << shard_pf_fail7[s].load();
                        }
                        std::cout << std::endl;
                    }
                    last_built = built;
                    last_sent = sent;
                    last_t = now;
                }
            });

            std::vector<std::thread> shard_pf_threads7;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                shard_pf_threads7.emplace_back([&, s]() {
                    if (global_stop) return;
                    auto& ep = shard_endpoints[s];

                    std::vector<std::string> valid_contracts7;
                    for (uint32_t i = 0; i < kContractsPerShard7; ++i)
                        if (contracts_confirmed7[s][i] && !contract_addrs7[s][i].empty())
                            valid_contracts7.push_back(contract_addrs7[s][i]);
                    if (valid_contracts7.empty()) {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": no confirmed contracts, skip prefund" << std::endl;
                        return;
                    }

                    struct UserEntry { std::string addr_raw; std::string prikey_raw; };
                    std::vector<UserEntry> users7;
                    users7.reserve(checked_addrs[s].size());
                    for (uint32_t idx = 0; idx < shard_addrs[s].size(); ++idx)
                        if (checked_addrs[s].count(shard_addrs[s][idx]))
                            users7.push_back({shard_addrs[s][idx], shard_prikeys[s][idx]});

                    if (users7.empty()) {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": no verified users, skip prefund" << std::endl;
                        return;
                    }

                    uint32_t nu7 = (uint32_t)users7.size();
                    uint32_t nc7 = (uint32_t)valid_contracts7.size();
                    const uint32_t kContractsPerUser = std::min(10u, nc7);
                    uint64_t total_pf7 = (uint64_t)nu7 * kContractsPerUser;
                    shard_pf_total7[s].store(total_pf7);
                    {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": " << nu7 << " users × " << kContractsPerUser
                                  << " contracts (of " << nc7 << ") = " << total_pf7 << " prefund ops" << std::endl;
                    }

                    // Build per-user contract assignment (round-robin for load balance)
                    std::vector<std::vector<uint32_t>> user_contract_indices(nu7);
                    for (uint32_t ui = 0; ui < nu7; ++ui) {
                        user_contract_indices[ui].reserve(kContractsPerUser);
                        for (uint32_t ci = 0; ci < kContractsPerUser; ++ci) {
                            user_contract_indices[ui].push_back((ui + ci) % nc7);
                        }
                    }

                    // Batch-fetch nonces for all users on this shard
                    ShardoraSDK shard_sdk7(ep.ip, ep.http_port);
                    std::unordered_map<std::string, uint64_t> user_nonces7;
                    {
                        const uint32_t kNonceBatch = 300;
                        std::vector<std::string> addr_hex_list;
                        addr_hex_list.reserve(nu7);
                        for (auto& u : users7)
                            addr_hex_list.push_back(common::Encode::HexEncode(u.addr_raw));
                        auto nonce_t0 = std::chrono::steady_clock::now();
                        for (uint32_t off = 0; off < addr_hex_list.size() && !global_stop; off += kNonceBatch) {
                            uint32_t end = std::min(off + kNonceBatch, (uint32_t)addr_hex_list.size());
                            std::vector<std::string> batch(addr_hex_list.begin() + off, addr_hex_list.begin() + end);
                            auto r = shard_sdk7.batchQueryAccounts(batch);
                            for (uint32_t k = 0; k < batch.size(); ++k) {
                                int64_t n = 0;
                                if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                                    auto& acc = r["accounts"][batch[k]];
                                    if (acc.contains("nonce")) {
                                        try {
                                            auto ns = acc["nonce"].get<std::string>();
                                            std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                        } catch (...) {}
                                    }
                                }
                                user_nonces7[users7[off + k].addr_raw] = (uint64_t)n;
                            }
                            if ((off / kNonceBatch) % 10 == 0 || end == addr_hex_list.size()) {
                                auto el = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - nonce_t0).count();
                                std::lock_guard<std::mutex> lk(pf_log_mtx7);
                                std::cout << "  Shard " << s << ": nonce fetch "
                                          << end << "/" << addr_hex_list.size()
                                          << " (" << el << "s)" << std::endl;
                            }
                        }
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": fetched nonces for "
                                  << user_nonces7.size() << " users" << std::endl;
                    }

                    std::string dest_ip7 = ep.ip;
                    uint16_t dest_tcp7 = (uint16_t)(ep.http_port - 10000);
                    {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": sending prefunds via TCP to "
                                  << dest_ip7 << ":" << dest_tcp7 << " ..." << std::endl;
                    }

                    shard_pf_active7.fetch_add(1);
                    uint32_t pf_threads7 = std::min(2u, nu7);
                    if (pf_threads7 == 0) pf_threads7 = 1;
                    uint32_t pf_per7 = nu7 / pf_threads7;
                    auto pf_t0 = std::chrono::steady_clock::now();
                    const uint32_t kMaxTpsPerShard = 10000;

                    std::vector<std::thread> pf_threads_vec7;
                    for (uint32_t t = 0; t < pf_threads7; ++t) {
                        uint32_t us = t * pf_per7;
                        uint32_t ue = (t == pf_threads7 - 1) ? nu7 : us + pf_per7;
                        pf_threads_vec7.emplace_back([&, s, us, ue, dest_ip7, dest_tcp7, kMaxTpsPerShard]() {
                            for (uint32_t ui = us; ui < ue && !global_stop; ++ui) {
                                auto& u = users7[ui];
                                std::shared_ptr<security::Security> sec =
                                    std::make_shared<security::Ecdsa>();
                                sec->SetPrivateKey(u.prikey_raw);
                                std::string pk_hex = common::Encode::HexEncode(u.prikey_raw);
                                auto nit = user_nonces7.find(u.addr_raw);
                                uint64_t nonce7 = (nit != user_nonces7.end()) ? nit->second : 0;
                                for (auto ci : user_contract_indices[ui]) {
                                    if (global_stop) break;
                                    uint64_t next_nonce = ++nonce7;
                                    auto tx = CreateTransactionWithAttr(
                                        sec, next_nonce, pk_hex,
                                        common::Encode::HexDecode(valid_contracts7[ci]),
                                        "prefund", "", 0, 210000, 1, (int32_t)s);
                                    if (tcp_enq7(tx, dest_ip7, dest_tcp7)) {
                                        ++pf_ok7;
                                        shard_pf_ok7[s].fetch_add(1);
                                    } else {
                                        ++pf_fail7;
                                        shard_pf_fail7[s].fetch_add(1);
                                    }
                                    shard_pf_done7[s].fetch_add(1);
                                    uint64_t done_now = shard_pf_done7[s].load();
                                    if (done_now % 200 == 0) {
                                        double elapsed = std::chrono::duration<double>(
                                            std::chrono::steady_clock::now() - pf_t0).count();
                                        double expected = (double)done_now / kMaxTpsPerShard;
                                        if (elapsed < expected) {
                                            usleep((uint32_t)((expected - elapsed) * 1000000));
                                        }
                                    }
                                }
                            }
                        });
                    }
                    for (auto& t : pf_threads_vec7) t.join();
                    shard_pf_active7.fetch_sub(1);

                    double el = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - pf_t0).count();
                    uint64_t done = shard_pf_done7[s].load();
                    uint64_t ok = shard_pf_ok7[s].load();
                    uint64_t fail = shard_pf_fail7[s].load();
                    double tps = (el > 0.001) ? (double)done / el : 0;
                    {
                        std::lock_guard<std::mutex> lk(pf_log_mtx7);
                        std::cout << "  Shard " << s << ": prefund done"
                                  << " ok=" << ok << " fail=" << fail
                                  << " elapsed=" << (uint64_t)el << "s"
                                  << " build_tps=" << (uint64_t)tps << std::endl;
                    }
                });
            }
            for (auto& t : shard_pf_threads7) t.join();
            pf_global_prog_stop.store(true);
            pf_global_prog.join();
            tcp_drain7();

            {
                double el = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - pf_phase_t0).count();
                uint64_t sent_delta = tcp_sent7.load() - tcp_sent_base7;
                uint64_t built = pf_ok7.load() + pf_fail7.load();
                double build_tps = (el > 0.001) ? (double)built / el : 0;
                double send_tps = (el > 0.001) ? (double)sent_delta / el : 0;
                std::cout << "  Phase 5 send done: ok=" << pf_ok7.load()
                          << " fail=" << pf_fail7.load()
                          << " tcp_sent=" << sent_delta
                          << " elapsed=" << (uint64_t)el << "s"
                          << " build_tps=" << (uint64_t)build_tps
                          << " send_tps=" << (uint64_t)send_tps << std::endl;
            }

            std::cout << "  Waiting 15s for prefund consensus..." << std::endl;
            for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);

            // Verify prefund on-chain — also concurrent across shards
            std::cout << "  Verifying prefund on-chain (prepay key = contract+user, concurrent)..."
                      << std::endl;
            std::atomic<uint64_t> total_pf_confirmed{0}, total_pf_need{0};

            std::vector<std::thread> shard_ver_threads7;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                shard_ver_threads7.emplace_back([&, s]() {
                    if (global_stop) return;
                    auto& ep = shard_endpoints[s];

                    std::vector<std::string> valid_contracts7v;
                    for (uint32_t i = 0; i < kContractsPerShard7; ++i)
                        if (contracts_confirmed7[s][i] && !contract_addrs7[s][i].empty())
                            valid_contracts7v.push_back(contract_addrs7[s][i]);
                    if (valid_contracts7v.empty()) return;

                    uint32_t nc7v = (uint32_t)valid_contracts7v.size();
                    const uint32_t kVerContractsPerUser = std::min(10u, nc7v);

                    struct PfVerEntry7 {
                        std::string prepay_key;
                        std::string user_prikey_raw;
                        std::string contract_addr;
                    };
                    std::vector<PfVerEntry7> pf_ver7;
                    uint32_t ver_user_idx = 0;
                    for (uint32_t idx = 0; idx < shard_addrs[s].size(); ++idx) {
                        if (!checked_addrs[s].count(shard_addrs[s][idx])) continue;
                        std::string user_hex = common::Encode::HexEncode(shard_addrs[s][idx]);
                        for (uint32_t ci = 0; ci < kVerContractsPerUser; ++ci) {
                            uint32_t contract_idx = (ver_user_idx + ci) % nc7v;
                            pf_ver7.push_back({valid_contracts7v[contract_idx] + user_hex,
                                               shard_prikeys[s][idx], valid_contracts7v[contract_idx]});
                        }
                        ++ver_user_idx;
                    }
                    total_pf_need.fetch_add(pf_ver7.size());

                    std::vector<bool> pf_conf7(pf_ver7.size(), false);
                    std::vector<uint32_t> pf_pending7;
                    pf_pending7.reserve(pf_ver7.size());
                    for (uint32_t i = 0; i < pf_ver7.size(); ++i) pf_pending7.push_back(i);

                    uint32_t pf_ok_shard = 0, pf_resend7 = 0;
                    auto pf_t07 = std::chrono::steady_clock::now();
                    const uint32_t kPfBatch7 = 300;
                    ShardoraSDK pf_sdk7(ep.ip, ep.http_port);
                    std::string dest_ip_rs = ep.ip;
                    uint16_t dest_tcp_rs = (uint16_t)(ep.http_port - 10000);

                    for (uint32_t rd = 0; !pf_pending7.empty() && !global_stop; ++rd) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - pf_t07).count();
                        if (elapsed >= 6000) {
                            std::lock_guard<std::mutex> lk(pf_log_mtx7);
                            std::cerr << "  Shard " << s << ": prefund verify timeout, "
                                      << pf_ok_shard << "/" << pf_ver7.size() << " confirmed" << std::endl;
                            break;
                        }

                        uint32_t rok7 = 0;
                        std::vector<uint32_t> next_pending7;
                        std::vector<std::string> ba7; std::vector<uint32_t> bi7;

                        for (uint32_t p = 0; p < pf_pending7.size() && !global_stop; ++p) {
                            ba7.push_back(pf_ver7[pf_pending7[p]].prepay_key);
                            bi7.push_back(pf_pending7[p]);
                            if (ba7.size() >= kPfBatch7 || p == pf_pending7.size() - 1) {
                                auto r7 = pf_sdk7.batchQueryAccounts(ba7);
                                for (uint32_t k = 0; k < bi7.size(); ++k) {
                                    if (r7.contains("accounts") && r7["accounts"].contains(ba7[k])) {
                                        pf_conf7[bi7[k]] = true; ++pf_ok_shard; ++rok7;
                                    } else {
                                        next_pending7.push_back(bi7[k]);
                                    }
                                }
                                ba7.clear(); bi7.clear();
                                usleep(100000);
                            }
                        }
                        pf_pending7 = std::move(next_pending7);
                        elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - pf_t07).count();
                        {
                            std::lock_guard<std::mutex> lk(pf_log_mtx7);
                            std::cout << "  [shard" << s << " prefund round " << (rd+1)
                                      << ", " << elapsed << "s] +" << rok7
                                      << " = " << pf_ok_shard << "/" << pf_ver7.size()
                                      << " (" << pf_pending7.size() << " pending)" << std::endl;
                        }
                        if (pf_pending7.empty()) break;

                        if (rok7 == 0) {
                            ++pf_resend7;
                            {
                                std::lock_guard<std::mutex> lk(pf_log_mtx7);
                                std::cout << "  Shard " << s << ": resending " << pf_pending7.size()
                                          << " unconfirmed prefunds (round " << pf_resend7 << ")..." << std::endl;
                            }
                            // Collect unique user addresses for batch nonce query
                            std::unordered_map<std::string, std::string> raw_to_hex7;
                            std::unordered_map<std::string, std::string> hex_to_raw7;
                            std::vector<std::string> unique_hex7;
                            for (auto idx7 : pf_pending7) {
                                auto& op7 = pf_ver7[idx7];
                                std::shared_ptr<security::Security> sec7r = std::make_shared<security::Ecdsa>();
                                sec7r->SetPrivateKey(op7.user_prikey_raw);
                                std::string addr_raw7 = sec7r->GetAddress();
                                if (!raw_to_hex7.count(addr_raw7)) {
                                    std::string hex7 = common::Encode::HexEncode(addr_raw7);
                                    raw_to_hex7[addr_raw7] = hex7;
                                    hex_to_raw7[hex7] = addr_raw7;
                                    unique_hex7.push_back(hex7);
                                }
                            }
                            // Batch-fetch nonces instead of individual fetchNonce calls
                            std::unordered_map<std::string, uint64_t> rs_nonces7;
                            const uint32_t kResendNonceBatch = 10000;
                            for (uint32_t off = 0; off < unique_hex7.size() && !global_stop; off += kResendNonceBatch) {
                                uint32_t end = std::min(off + kResendNonceBatch, (uint32_t)unique_hex7.size());
                                std::vector<std::string> batch(unique_hex7.begin() + off, unique_hex7.begin() + end);
                                auto r = pf_sdk7.batchQueryAccounts(batch);
                                for (uint32_t k = 0; k < batch.size(); ++k) {
                                    int64_t n = 0;
                                    if (r.contains("accounts") && r["accounts"].contains(batch[k])) {
                                        auto& acc = r["accounts"][batch[k]];
                                        if (acc.contains("nonce")) {
                                            try {
                                                auto ns = acc["nonce"].get<std::string>();
                                                std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                            } catch (...) {}
                                        }
                                    }
                                    auto it = hex_to_raw7.find(batch[k]);
                                    if (it != hex_to_raw7.end()) {
                                        rs_nonces7[it->second] = (uint64_t)n;
                                    }
                                }
                            }
                            // Resend pending prefund txs using batch-fetched nonces
                            for (auto idx7 : pf_pending7) {
                                if (global_stop) break;
                                auto& op7 = pf_ver7[idx7];
                                std::shared_ptr<security::Security> sec7r = std::make_shared<security::Ecdsa>();
                                sec7r->SetPrivateKey(op7.user_prikey_raw);
                                std::string addr_raw7 = sec7r->GetAddress();
                                uint64_t& nn7 = rs_nonces7[addr_raw7];
                                uint64_t next_n = ++nn7;
                                auto tx = CreateTransactionWithAttr(
                                    sec7r, next_n,
                                    common::Encode::HexEncode(op7.user_prikey_raw),
                                    common::Encode::HexDecode(op7.contract_addr),
                                    "prefund", "", 0, 210000, 1, (int32_t)s);
                                tcp_enq7(tx, dest_ip_rs, dest_tcp_rs);
                            }
                            // for (int w = 0; w < 150 && !global_stop; ++w) usleep(100000);
                            // continue;
                        }
                        // uint32_t wt7 = 10000;
                        // for (uint32_t w = 0; w < wt7 / 100 && !global_stop; ++w) usleep(100000);
                        usleep(3000000);
                    }
                    total_pf_confirmed.fetch_add(pf_ok_shard);
                    std::lock_guard<std::mutex> lk(pf_log_mtx7);
                    std::cout << "  Shard " << s << ": " << pf_ok_shard << "/" << pf_ver7.size()
                              << " prefunds confirmed" << std::endl;
                });
            }
            for (auto& t : shard_ver_threads7) t.join();
            tcp_drain7();
            std::cout << "\n[Phase 5 Result] Total prefunds confirmed: "
                      << total_pf_confirmed.load() << "/" << total_pf_need.load() << std::endl;
            // ── Phase 6: CreateNewItem + PurchaseItem stress test ─────────────
            std::cout << "\n[Phase 6] Contract calls: CreateNewItem x" << kCallRounds7
                      << " then PurchaseItem x" << kPurchaseRounds7
                      << " (shards " << kConsensusBegin << "-" << kMaxShardId << " concurrent)" << std::endl;

            // ABI selector helpers (same contract as mode 6)
            auto pad32_hex7 = [](const std::string& hex) -> std::string {
                if (hex.size() >= 64) return hex.substr(hex.size() - 64);
                return std::string(64 - hex.size(), '0') + hex;
            };
            auto uint256_hex7 = [&](uint64_t v) -> std::string {
                std::stringstream ss; ss << std::hex << v;
                return pad32_hex7(ss.str());
            };
            auto bytes32_hex7 = [](const std::string& raw32) -> std::string {
                return common::Encode::HexEncode(raw32);
            };
            const std::string sel_create7 = utils::keccak256Str(
                "CreateNewItem(bytes32,bytes,uint256,uint256,uint256)").substr(0, 8);
            std::cout << "  CreateNewItem selector: 0x" << sel_create7 << std::endl;
            const std::string sel_purchase7 = utils::keccak256Str(
                "PurchaseItem(bytes32,uint256)").substr(0, 8);
            std::cout << "  PurchaseItem selector: 0x" << sel_purchase7 << std::endl;

            auto encode_create7 = [&](const std::string& hash_raw32, uint64_t price,
                                      uint64_t start_ms, uint64_t end_ms) -> std::string {
                std::string info_data = "696e666f";
                uint64_t info_len = 4;
                uint64_t info_padded_len = ((info_len + 31) / 32) * 32;
                std::string info_padding((info_padded_len - info_len) * 2, '0');
                std::string enc;
                enc += sel_create7;
                enc += bytes32_hex7(hash_raw32);
                enc += uint256_hex7(5 * 32);
                enc += uint256_hex7(price);
                enc += uint256_hex7(start_ms);
                enc += uint256_hex7(end_ms);
                enc += uint256_hex7(info_len);
                enc += info_data + info_padding;
                return enc;
            };
            auto encode_purchase7 = [&](const std::string& hash_raw32, uint64_t timestamp_ms) -> std::string {
                std::string enc;
                enc += sel_purchase7;
                enc += bytes32_hex7(hash_raw32);
                enc += uint256_hex7(timestamp_ms);
                return enc;
            };
            auto make_hash7 = [&](uint32_t s, uint32_t ui, uint64_t round) -> std::string {
                std::string seed(12, '\0');
                uint32_t key = s * 10000 + ui;
                seed[0] = (char)((key >> 24) & 0xFF);
                seed[1] = (char)((key >> 16) & 0xFF);
                seed[2] = (char)((key >>  8) & 0xFF);
                seed[3] = (char)( key        & 0xFF);
                seed[4] = (char)((round >> 24) & 0xFF);
                seed[5] = (char)((round >> 16) & 0xFF);
                seed[6] = (char)((round >>  8) & 0xFF);
                seed[7] = (char)( round        & 0xFF);
                seed[8] = (char)((kCallRounds7 >> 24) & 0xFF);
                seed[9] = (char)((kCallRounds7 >> 16) & 0xFF);
                seed[10]= (char)((kCallRounds7 >>  8) & 0xFF);
                seed[11]= (char)( kCallRounds7        & 0xFF);
                std::string h = common::Encode::HexDecode(
                    utils::keccak256Str(common::Encode::HexEncode(seed)));
                h.resize(32);
                return h;
            };

            // Per-shard user lists built from already-verified checked_addrs + user_contract_indices
            // Re-derive same assignment: user i → contracts (i + ci) % nc7 for ci in [0, kContractsPerUser)
            std::atomic<uint64_t> create_ok7{0}, create_fail7{0};
            std::mutex call_log_mtx7;

            struct CallUser7 {
                std::string prikey_raw;
                std::string addr_raw;
                std::string addr_hex;
                std::vector<std::string> contract_addrs;  // assigned contracts (up to 10)
            };

            // Build per-shard call user lists
            std::map<uint32_t, std::vector<CallUser7>> call_users7;
            for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                std::vector<std::string> valid_c7;
                for (uint32_t i = 0; i < kContractsPerShard7; ++i)
                    if (contracts_confirmed7[s][i] && !contract_addrs7[s][i].empty())
                        valid_c7.push_back(contract_addrs7[s][i]);
                uint32_t nc = (uint32_t)valid_c7.size();
                const uint32_t kCPU = std::min(10u, nc);
                uint32_t uidx = 0;
                for (uint32_t idx = 0; idx < shard_addrs[s].size(); ++idx) {
                    if (!checked_addrs[s].count(shard_addrs[s][idx])) continue;
                    CallUser7 cu;
                    cu.prikey_raw = shard_prikeys[s][idx];
                    cu.addr_raw   = shard_addrs[s][idx];
                    cu.addr_hex   = common::Encode::HexEncode(cu.addr_raw);
                    cu.contract_addrs.reserve(kCPU);
                    for (uint32_t ci = 0; ci < kCPU; ++ci)
                        cu.contract_addrs.push_back(valid_c7[(uidx + ci) % nc]);
                    call_users7[s].push_back(std::move(cu));
                    ++uidx;
                }
                std::cout << "  Shard " << s << ": " << call_users7[s].size()
                          << " call users, " << nc << " contracts" << std::endl;
            }

            // ── Phase 6a: CreateNewItem ──────────────────────────────────────
            std::cout << "\n[Phase 6a] CreateNewItem x" << kCallRounds7
                      << " per user per contract..." << std::endl;
            {
                std::atomic<uint64_t> create_confirmed7{0}, create_need7{0};
                std::vector<std::thread> ct7;
                for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                    ct7.emplace_back([&, s]() {
                        auto& ep = shard_endpoints[s];
                        std::string fallback_ip = ep.ip;
                        uint16_t fallback_tcp = (uint16_t)(ep.http_port - 10000);
                        auto& users = call_users7[s];
                        uint32_t nu = (uint32_t)users.size();
                        if (nu == 0) return;

                        // Fetch leaders first
                        ShardoraSDK csdk(ep.ip, ep.http_port);
                        std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> leader_map7;
                        uint32_t leader_cnt7 = 0;
                        csdk.fetchLeaders(leader_map7, leader_cnt7);

                        // First pass: fetch pool_index per contract via shard endpoint
                        std::unordered_map<std::string, int64_t> prepay_nonces;
                        std::unordered_map<std::string, uint32_t> contract_pool7;  // contract_hex → pool_index
                        {
                            std::vector<std::string> keys;
                            for (auto& u : users)
                                for (auto& c : u.contract_addrs)
                                    keys.push_back(c + u.addr_hex);
                            const uint32_t kBN = 300;
                            // First pass via shard endpoint to collect pool_index
                            for (uint32_t off = 0; off < keys.size() && !global_stop; off += kBN) {
                                uint32_t end = std::min(off + kBN, (uint32_t)keys.size());
                                std::vector<std::string> batch(keys.begin() + off, keys.begin() + end);
                                auto r = csdk.batchQueryAccounts(batch);
                                for (auto& k : batch) {
                                    if (r.contains("accounts") && r["accounts"].contains(k)) {
                                        auto& acc = r["accounts"][k];
                                        std::string contract_hex = k.substr(0, 40);
                                        if (!contract_pool7.count(contract_hex) && acc.contains("pool_index")) {
                                            try { contract_pool7[contract_hex] = acc["pool_index"].get<uint32_t>(); }
                                            catch (...) {}
                                        }
                                    }
                                }
                                usleep(10000);
                            }
                            // Second pass: query nonces via each contract's leader
                            std::map<std::pair<std::string,uint16_t>, std::vector<std::string>> ep_keys;
                            for (auto& k : keys) {
                                std::string c = k.substr(0, 40);
                                uint32_t pidx;
                                auto pit = contract_pool7.find(c);
                                if (pit != contract_pool7.end()) pidx = pit->second;
                                else pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                std::string lip = fallback_ip;
                                uint16_t lhttp = (uint16_t)ep.http_port;
                                auto lit = leader_map7.find(pidx);
                                if (lit != leader_map7.end()) {
                                    lip = lit->second.ip;
                                    lhttp = (uint16_t)(lit->second.port + 10000);
                                }
                                ep_keys[{lip, lhttp}].push_back(k);
                            }
                            for (auto& [endpoint, qkeys] : ep_keys) {
                                ShardoraSDK qsdk(endpoint.first, endpoint.second);
                                for (uint32_t off = 0; off < qkeys.size() && !global_stop; off += kBN) {
                                    uint32_t end = std::min(off + kBN, (uint32_t)qkeys.size());
                                    std::vector<std::string> batch(qkeys.begin() + off, qkeys.begin() + end);
                                    auto r = qsdk.batchQueryAccounts(batch);
                                    for (auto& k : batch) {
                                        int64_t n = 0;
                                        if (r.contains("accounts") && r["accounts"].contains(k)) {
                                            auto& acc = r["accounts"][k];
                                            if (acc.contains("nonce")) {
                                                try {
                                                    auto ns = acc["nonce"].get<std::string>();
                                                    std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                                } catch (...) {}
                                            }
                                        }
                                        prepay_nonces[k] = n;
                                    }
                                    usleep(10000);
                                }
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            std::cout << "  Shard " << s << ": fetched "
                                      << prepay_nonces.size() << " prepay nonces for CreateNewItem" << std::endl;
                        }

                        // Build contract→dest cache using on-chain pool_index
                        std::unordered_map<std::string, std::pair<std::string, uint16_t>> contract_dest7;
                        for (auto& u : users) {
                            for (auto& c : u.contract_addrs) {
                                if (contract_dest7.count(c)) continue;
                                uint32_t pidx;
                                auto pit = contract_pool7.find(c);
                                if (pit != contract_pool7.end())
                                    pidx = pit->second;
                                else
                                    pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                auto lit = leader_map7.find(pidx);
                                if (lit != leader_map7.end())
                                    contract_dest7[c] = {lit->second.ip, lit->second.port};
                                else
                                    contract_dest7[c] = {fallback_ip, fallback_tcp};
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            std::cout << "  Shard " << s << ": leader routing " << leader_cnt7
                                      << " pools (on-chain pool_index) for CreateNewItem" << std::endl;
                        }

                        uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        constexpr uint32_t kCreateTps7 = 30000;
                        uint64_t create_sent7 = 0;
                        auto create_t0_7 = std::chrono::steady_clock::now();
                        auto create_tps_print7 = create_t0_7;
                        for (uint32_t ui = 0; ui < nu && !global_stop; ++ui) {
                            auto& u = users[ui];
                            std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                            sec->SetPrivateKey(u.prikey_raw);
                            for (uint32_t ci = 0; ci < (uint32_t)u.contract_addrs.size() && !global_stop; ++ci) {
                                std::string pkey = u.contract_addrs[ci] + u.addr_hex;
                                int64_t& n = prepay_nonces[pkey];
                                auto& dest7 = contract_dest7[u.contract_addrs[ci]];
                                for (uint32_t r = 0; r < kCallRounds7 && !global_stop; ++r) {
                                    std::string hash_raw = make_hash7(s, ui * (uint32_t)u.contract_addrs.size() + ci, r);
                                    std::string input = encode_create7(hash_raw, 1, 0, UINT64_MAX);
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(u.prikey_raw),
                                        common::Encode::HexDecode(u.contract_addrs[ci]),
                                        "call", input, 0, 5000000, 1, (int32_t)s);
                                    if (tcp_enq7(tx, dest7.first, dest7.second)) ++create_ok7;
                                    else { ++create_fail7; --n; }
                                    if (++create_sent7 % 200 == 0) {
                                        auto now7 = std::chrono::steady_clock::now();
                                        double elapsed = std::chrono::duration<double>(now7 - create_t0_7).count();
                                        double expected = (double)create_sent7 / kCreateTps7;
                                        if (elapsed < expected)
                                            usleep((uint32_t)((expected - elapsed) * 1e6));
                                        double since_print = std::chrono::duration<double>(now7 - create_tps_print7).count();
                                        if (since_print >= 1.0) {
                                            double actual_tps = elapsed > 0 ? create_sent7 / elapsed : 0;
                                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                                            std::cout << "  [CreateNewItem shard" << s << "] sent=" << create_sent7
                                                      << " tps=" << (uint32_t)actual_tps << std::endl;
                                            create_tps_print7 = now7;
                                        }
                                    }
                                }
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            double total_elapsed = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - create_t0_7).count();
                            std::cout << "  Shard " << s << ": CreateNewItem sent ok="
                                      << create_ok7.load() << " fail=" << create_fail7.load()
                                      << " avg_tps=" << (uint32_t)(create_sent7 / std::max(total_elapsed, 0.001)) << std::endl;
                        }

                        // ── Verify + resend until all confirmed or 600s timeout ──
                        // Build verify list: prepay_key → expected nonce after kCallRounds7 sends
                        struct VerEntry7 {
                            std::string key;       // contract_hex + user_addr_hex
                            int64_t base_nonce;    // nonce before we started sending
                            uint32_t ui;           // user index in users[]
                            uint32_t ci;           // contract index in u.contract_addrs
                        };
                        std::vector<VerEntry7> ver_list;
                        // base_nonce was prepay_nonces[key] at send time (before ++n)
                        // After kCallRounds7 sends the expected nonce = base_nonce + kCallRounds7
                        for (uint32_t ui = 0; ui < nu; ++ui) {
                            auto& u = users[ui];
                            for (uint32_t ci = 0; ci < (uint32_t)u.contract_addrs.size(); ++ci) {
                                std::string pkey = u.contract_addrs[ci] + u.addr_hex;
                                ver_list.push_back({pkey, prepay_nonces[pkey] - (int64_t)kCallRounds7, ui, ci});
                            }
                        }
                        create_need7.fetch_add(ver_list.size());

                        std::vector<uint32_t> pending;
                        for (uint32_t i = 0; i < ver_list.size(); ++i) pending.push_back(i);
                        uint32_t confirmed_cnt = 0;
                        auto ver_t0 = std::chrono::steady_clock::now();
                        const uint32_t kBN = 300;

                        for (uint32_t rd = 0; !pending.empty() && !global_stop; ++rd) {
                            auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - ver_t0).count();
                            if (elapsed_s >= 600) {
                                std::lock_guard<std::mutex> lk(call_log_mtx7);
                                std::cerr << "  Shard " << s << ": CreateNewItem verify timeout "
                                          << confirmed_cnt << "/" << ver_list.size() << std::endl;
                                break;
                            }

                            // Query current nonces via each contract's leader node
                            std::unordered_map<std::string, int64_t> cur_nonces;
                            {
                                // Group pending keys by leader HTTP endpoint
                                std::map<std::pair<std::string,uint16_t>, std::vector<std::string>> ep_keys;
                                for (uint32_t p : pending) {
                                    const std::string& key = ver_list[p].key;
                                    std::string c = key.substr(0, 40);
                                    uint32_t pidx;
                                    auto pit = contract_pool7.find(c);
                                    if (pit != contract_pool7.end()) pidx = pit->second;
                                    else pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                    std::string lip = fallback_ip;
                                    uint16_t lhttp = (uint16_t)ep.http_port;
                                    auto lit = leader_map7.find(pidx);
                                    if (lit != leader_map7.end()) {
                                        lip = lit->second.ip;
                                        lhttp = (uint16_t)(lit->second.port + 10000);
                                    }
                                    ep_keys[{lip, lhttp}].push_back(key);
                                }
                                for (auto& [endpoint, qkeys] : ep_keys) {
                                    ShardoraSDK qsdk(endpoint.first, endpoint.second);
                                    for (uint32_t off = 0; off < qkeys.size() && !global_stop; off += kBN) {
                                        uint32_t end = std::min(off + kBN, (uint32_t)qkeys.size());
                                        std::vector<std::string> batch(qkeys.begin() + off, qkeys.begin() + end);
                                        auto r = qsdk.batchQueryAccounts(batch);
                                        for (auto& k : batch) {
                                            int64_t n = 0;
                                            if (r.contains("accounts") && r["accounts"].contains(k)) {
                                                auto& acc = r["accounts"][k];
                                                if (acc.contains("nonce")) {
                                                    try {
                                                        auto ns = acc["nonce"].get<std::string>();
                                                        std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                                    } catch (...) {}
                                                }
                                            }
                                            cur_nonces[k] = n;
                                        }
                                        usleep(10000);
                                    }
                                }
                            }

                            // Check confirmed; resend missing txs for those still pending
                            std::vector<uint32_t> next_pend;
                            for (uint32_t p : pending) {
                                auto& ve = ver_list[p];
                                int64_t cur = cur_nonces[ve.key];
                                int64_t target = ve.base_nonce + (int64_t)kCallRounds7;
                                if (cur >= target) {
                                    ++confirmed_cnt;
                                    continue;
                                }
                                next_pend.push_back(p);

                                // Resend missing rounds: from cur to target
                                auto& u = users[ve.ui];
                                auto& dest7 = contract_dest7[u.contract_addrs[ve.ci]];
                                std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                                sec->SetPrivateKey(u.prikey_raw);
                                uint64_t now_ms2 = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                int64_t n = cur;
                                for (int64_t r = cur - ve.base_nonce; r < (int64_t)kCallRounds7 && !global_stop; ++r) {
                                    std::string hash_raw = make_hash7(s, ve.ui * (uint32_t)u.contract_addrs.size() + ve.ci, (uint64_t)r);
                                    std::string input = encode_create7(hash_raw, 1, 0, UINT64_MAX);
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(u.prikey_raw),
                                        common::Encode::HexDecode(u.contract_addrs[ve.ci]),
                                        "call", input, 0, 5000000, 1, (int32_t)s);
                                    tcp_enq7(tx, dest7.first, dest7.second);
                                }
                            }
                            {
                                std::lock_guard<std::mutex> lk(call_log_mtx7);
                                std::cout << "  [shard" << s << " create round " << (rd+1)
                                          << ", " << elapsed_s << "s] "
                                          << confirmed_cnt << "/" << ver_list.size()
                                          << " (" << next_pend.size() << " pending, resent "
                                          << (pending.size() - (ver_list.size() - confirmed_cnt - next_pend.size() > 0 ? 0 : 0))
                                          << ")" << std::endl;
                            }
                            pending = std::move(next_pend);
                            if (!pending.empty())
                                for (int w = 0; w < 100 && !global_stop; ++w) usleep(100000);
                        }
                        create_confirmed7.fetch_add(confirmed_cnt);
                    });
                }
                for (auto& t : ct7) t.join();
                tcp_drain7();
                std::cout << "  CreateNewItem total: ok=" << create_ok7.load()
                          << " fail=" << create_fail7.load() << std::endl;
                std::cout << "[Phase 6a Result] CreateNewItem confirmed: "
                          << create_confirmed7.load() << "/" << create_need7.load() << std::endl;
            }

            // ── Phase 6b: PurchaseItem ────────────────────────────────────────
            // Each user purchases from their OWN contracts (same contracts they prefunded).
            // Prepay key = contract + user.addr_hex, matching Phase 5 prefund.
            std::cout << "\n[Phase 6b] PurchaseItem x" << kPurchaseRounds7
                      << " per user per contract..." << std::endl;
            {
                std::atomic<uint64_t> purchase_ok7{0}, purchase_fail7{0};
                std::atomic<uint64_t> purchase_confirmed7{0}, purchase_need7{0};
                std::vector<std::thread> pt7;
                for (uint32_t s = kConsensusBegin; s <= kMaxShardId; ++s) {
                    pt7.emplace_back([&, s]() {
                        auto& ep = shard_endpoints[s];
                        uint16_t fallback_tcp_p = (uint16_t)(ep.http_port - 10000);
                        auto& users = call_users7[s];
                        uint32_t nu = (uint32_t)users.size();
                        if (nu == 0) return;

                        // Fetch leaders first
                        ShardoraSDK psdk(ep.ip, ep.http_port);
                        std::unordered_map<uint32_t, ShardoraSDK::LeaderInfo> pleader_map7;
                        uint32_t pleader_cnt7 = 0;
                        psdk.fetchLeaders(pleader_map7, pleader_cnt7);

                        // Fetch current prepay nonces (contract + user.addr_hex) via leader nodes
                        std::unordered_map<std::string, int64_t> prepay_nonces;
                        std::unordered_map<std::string, uint32_t> pcontract_pool7;  // contract_hex → pool_index
                        {
                            std::vector<std::string> keys;
                            for (auto& u : users)
                                for (auto& c : u.contract_addrs)
                                    keys.push_back(c + u.addr_hex);
                            const uint32_t kBN = 300;
                            // First pass via shard endpoint to collect pool_index
                            for (uint32_t off = 0; off < keys.size() && !global_stop; off += kBN) {
                                uint32_t end = std::min(off + kBN, (uint32_t)keys.size());
                                std::vector<std::string> batch(keys.begin() + off, keys.begin() + end);
                                auto r = psdk.batchQueryAccounts(batch);
                                for (auto& k : batch) {
                                    if (r.contains("accounts") && r["accounts"].contains(k)) {
                                        auto& acc = r["accounts"][k];
                                        std::string contract_hex = k.substr(0, 40);
                                        if (!pcontract_pool7.count(contract_hex) && acc.contains("pool_index")) {
                                            try { pcontract_pool7[contract_hex] = acc["pool_index"].get<uint32_t>(); }
                                            catch (...) {}
                                        }
                                    }
                                }
                                usleep(10000);
                            }
                            // Second pass: query nonces via each contract's leader
                            std::map<std::pair<std::string,uint16_t>, std::vector<std::string>> pep_init_keys;
                            for (auto& k : keys) {
                                std::string c = k.substr(0, 40);
                                uint32_t pidx;
                                auto pit = pcontract_pool7.find(c);
                                if (pit != pcontract_pool7.end()) pidx = pit->second;
                                else pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                std::string lip = ep.ip;
                                uint16_t lhttp = (uint16_t)ep.http_port;
                                auto lit = pleader_map7.find(pidx);
                                if (lit != pleader_map7.end()) {
                                    lip = lit->second.ip;
                                    lhttp = (uint16_t)(lit->second.port + 10000);
                                }
                                pep_init_keys[{lip, lhttp}].push_back(k);
                            }
                            for (auto& [endpoint, qkeys] : pep_init_keys) {
                                ShardoraSDK qsdk(endpoint.first, endpoint.second);
                                for (uint32_t off = 0; off < qkeys.size() && !global_stop; off += kBN) {
                                    uint32_t end = std::min(off + kBN, (uint32_t)qkeys.size());
                                    std::vector<std::string> batch(qkeys.begin() + off, qkeys.begin() + end);
                                    auto r = qsdk.batchQueryAccounts(batch);
                                    for (auto& k : batch) {
                                        int64_t n = 0;
                                        if (r.contains("accounts") && r["accounts"].contains(k)) {
                                            auto& acc = r["accounts"][k];
                                            if (acc.contains("nonce")) {
                                                try {
                                                    auto ns = acc["nonce"].get<std::string>();
                                                    std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                                } catch (...) {}
                                            }
                                        }
                                        prepay_nonces[k] = n;
                                    }
                                    usleep(10000);
                                }
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            std::cout << "  Shard " << s << ": fetched "
                                      << prepay_nonces.size() << " prepay nonces for PurchaseItem" << std::endl;
                        }

                        // Build contract→dest cache using on-chain pool_index
                        std::unordered_map<std::string, std::pair<std::string, uint16_t>> pcontract_dest7;
                        for (auto& u : users) {
                            for (auto& c : u.contract_addrs) {
                                if (pcontract_dest7.count(c)) continue;
                                uint32_t pidx;
                                auto pit = pcontract_pool7.find(c);
                                if (pit != pcontract_pool7.end())
                                    pidx = pit->second;
                                else
                                    pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                auto lit = pleader_map7.find(pidx);
                                if (lit != pleader_map7.end())
                                    pcontract_dest7[c] = {lit->second.ip, lit->second.port};
                                else
                                    pcontract_dest7[c] = {ep.ip, fallback_tcp_p};
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            std::cout << "  Shard " << s << ": leader routing " << pleader_cnt7
                                      << " pools (on-chain pool_index) for PurchaseItem" << std::endl;
                        }

                        uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        constexpr uint32_t kPurchaseTps7 = 30000;
                        uint64_t purchase_sent7 = 0;
                        auto purchase_t0_7 = std::chrono::steady_clock::now();
                        auto purchase_tps_print7 = purchase_t0_7;
                        for (uint32_t ui = 0; ui < nu && !global_stop; ++ui) {
                            auto& u = users[ui];
                            std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                            sec->SetPrivateKey(u.prikey_raw);
                            for (uint32_t ci = 0; ci < (uint32_t)u.contract_addrs.size() && !global_stop; ++ci) {
                                std::string pkey = u.contract_addrs[ci] + u.addr_hex;
                                int64_t& n = prepay_nonces[pkey];
                                auto& pdest7 = pcontract_dest7[u.contract_addrs[ci]];
                                for (uint32_t r = 0; r < kPurchaseRounds7 && !global_stop; ++r) {
                                    std::string hash_raw = make_hash7(s, ui * (uint32_t)u.contract_addrs.size() + ci, r % kCallRounds7);
                                    std::string input = encode_purchase7(hash_raw, now_ms + r);
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(u.prikey_raw),
                                        common::Encode::HexDecode(u.contract_addrs[ci]),
                                        "call", input, 1, 5000000, 1, (int32_t)s);
                                    if (tcp_enq7(tx, pdest7.first, pdest7.second)) ++purchase_ok7;
                                    else { ++purchase_fail7; --n; }
                                    if (++purchase_sent7 % 200 == 0) {
                                        auto now7 = std::chrono::steady_clock::now();
                                        double elapsed = std::chrono::duration<double>(now7 - purchase_t0_7).count();
                                        double expected = (double)purchase_sent7 / kPurchaseTps7;
                                        if (elapsed < expected)
                                            usleep((uint32_t)((expected - elapsed) * 1e6));
                                        double since_print = std::chrono::duration<double>(now7 - purchase_tps_print7).count();
                                        if (since_print >= 1.0) {
                                            double actual_tps = elapsed > 0 ? purchase_sent7 / elapsed : 0;
                                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                                            std::cout << "  [PurchaseItem shard" << s << "] sent=" << purchase_sent7
                                                      << " tps=" << (uint32_t)actual_tps << std::endl;
                                            purchase_tps_print7 = now7;
                                        }
                                    }
                                }
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lk(call_log_mtx7);
                            double total_elapsed = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - purchase_t0_7).count();
                            std::cout << "  Shard " << s << ": PurchaseItem sent ok="
                                      << purchase_ok7.load() << " fail=" << purchase_fail7.load()
                                      << " avg_tps=" << (uint32_t)(purchase_sent7 / std::max(total_elapsed, 0.001)) << std::endl;
                        }

                        // ── Verify + resend until all confirmed or 600s timeout ──
                        struct PVerEntry7 {
                            std::string key;
                            int64_t base_nonce;
                            uint32_t ui;
                            uint32_t ci;
                        };
                        std::vector<PVerEntry7> pver_list;
                        for (uint32_t ui = 0; ui < nu; ++ui) {
                            auto& u = users[ui];
                            for (uint32_t ci = 0; ci < (uint32_t)u.contract_addrs.size(); ++ci) {
                                std::string pkey = u.contract_addrs[ci] + u.addr_hex;
                                // base = nonce before PurchaseItem sends = prepay_nonces[pkey] - kPurchaseRounds7
                                pver_list.push_back({pkey,
                                    prepay_nonces[pkey] - (int64_t)kPurchaseRounds7,
                                    ui, ci});
                            }
                        }
                        purchase_need7.fetch_add(pver_list.size());

                        std::vector<uint32_t> ppending;
                        for (uint32_t i = 0; i < pver_list.size(); ++i) ppending.push_back(i);
                        uint32_t pconfirmed_cnt = 0;
                        auto pver_t0 = std::chrono::steady_clock::now();
                        const uint32_t kPBN = 300;
                        uint64_t now_ms_p = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        for (uint32_t rd = 0; !ppending.empty() && !global_stop; ++rd) {
                            auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - pver_t0).count();
                            if (elapsed_s >= 600) {
                                std::lock_guard<std::mutex> lk(call_log_mtx7);
                                std::cerr << "  Shard " << s << ": PurchaseItem verify timeout "
                                          << pconfirmed_cnt << "/" << pver_list.size() << std::endl;
                                break;
                            }

                            // Query current nonces via each contract's leader node
                            std::unordered_map<std::string, int64_t> pcur_nonces;
                            {
                                // Group pending keys by leader HTTP endpoint
                                std::map<std::pair<std::string,uint16_t>, std::vector<std::string>> pep_keys;
                                for (uint32_t p : ppending) {
                                    const std::string& key = pver_list[p].key;
                                    std::string c = key.substr(0, 40);
                                    uint32_t pidx;
                                    auto pit = pcontract_pool7.find(c);
                                    if (pit != pcontract_pool7.end()) pidx = pit->second;
                                    else pidx = common::GetAddressPoolIndex(common::Encode::HexDecode(c));
                                    std::string lip = ep.ip;
                                    uint16_t lhttp = (uint16_t)ep.http_port;
                                    auto lit = pleader_map7.find(pidx);
                                    if (lit != pleader_map7.end()) {
                                        lip = lit->second.ip;
                                        lhttp = (uint16_t)(lit->second.port + 10000);
                                    }
                                    pep_keys[{lip, lhttp}].push_back(key);
                                }
                                for (auto& [endpoint, qkeys] : pep_keys) {
                                    ShardoraSDK qsdk(endpoint.first, endpoint.second);
                                    for (uint32_t off = 0; off < qkeys.size() && !global_stop; off += kPBN) {
                                        uint32_t end = std::min(off + kPBN, (uint32_t)qkeys.size());
                                        std::vector<std::string> batch(qkeys.begin() + off, qkeys.begin() + end);
                                        auto r = qsdk.batchQueryAccounts(batch);
                                        for (auto& k : batch) {
                                            int64_t n = 0;
                                            if (r.contains("accounts") && r["accounts"].contains(k)) {
                                                auto& acc = r["accounts"][k];
                                                if (acc.contains("nonce")) {
                                                    try {
                                                        auto ns = acc["nonce"].get<std::string>();
                                                        std::from_chars(ns.data(), ns.data() + ns.size(), n);
                                                    } catch (...) {}
                                                }
                                            }
                                            pcur_nonces[k] = n;
                                        }
                                        usleep(10000);
                                    }
                                }
                            }

                            std::vector<uint32_t> pnext_pend;
                            for (uint32_t p : ppending) {
                                auto& ve = pver_list[p];
                                int64_t cur = pcur_nonces[ve.key];
                                int64_t target = ve.base_nonce + (int64_t)kPurchaseRounds7;
                                if (cur >= target) {
                                    ++pconfirmed_cnt;
                                    continue;
                                }
                                pnext_pend.push_back(p);

                                // Resend missing rounds: from cur to target
                                auto& u = users[ve.ui];
                                auto& pdest7 = pcontract_dest7[u.contract_addrs[ve.ci]];
                                std::shared_ptr<security::Security> sec = std::make_shared<security::Ecdsa>();
                                sec->SetPrivateKey(u.prikey_raw);
                                int64_t n = cur;
                                for (int64_t r = cur - ve.base_nonce; r < (int64_t)kPurchaseRounds7 && !global_stop; ++r) {
                                    std::string hash_raw = make_hash7(s,
                                        ve.ui * (uint32_t)u.contract_addrs.size() + ve.ci,
                                        (uint64_t)(r % kCallRounds7));
                                    std::string input = encode_purchase7(hash_raw, now_ms_p + (uint64_t)r);
                                    auto tx = CreateTransactionWithAttr(sec, ++n,
                                        common::Encode::HexEncode(u.prikey_raw),
                                        common::Encode::HexDecode(u.contract_addrs[ve.ci]),
                                        "call", input, 1, 5000000, 1, (int32_t)s);
                                    tcp_enq7(tx, pdest7.first, pdest7.second);
                                }
                            }
                            {
                                std::lock_guard<std::mutex> lk(call_log_mtx7);
                                std::cout << "  [shard" << s << " purchase round " << (rd+1)
                                          << ", " << elapsed_s << "s] "
                                          << pconfirmed_cnt << "/" << pver_list.size()
                                          << " (" << pnext_pend.size() << " pending)" << std::endl;
                            }
                            ppending = std::move(pnext_pend);
                            if (!ppending.empty())
                                for (int w = 0; w < 100 && !global_stop; ++w) usleep(100000);
                        }
                        purchase_confirmed7.fetch_add(pconfirmed_cnt);
                    });
                }
                for (auto& t : pt7) t.join();
                tcp_drain7();
                std::cout << "  PurchaseItem total: ok=" << purchase_ok7.load()
                          << " fail=" << purchase_fail7.load() << std::endl;
                std::cout << "[Phase 6b Result] PurchaseItem confirmed: "
                          << purchase_confirmed7.load() << "/" << purchase_need7.load() << std::endl;
            }

        }  // end if (compiled7 ok)

        std::cout << "\n=== Mode 7 Complete ===" << std::endl;
        tcp_stop7.store(true);
        tcp_cv7.notify_all();
        if (tcp_sender7.joinable()) tcp_sender7.join();
        transport::TcpTransport::Instance()->Stop();
        return 0;
    }

    // ── Mode 8: CrossShardBase Token AMM ─────────────────────────────────
    // Usage: txcli 8 <funder_shard> <ip> <port>
    //        [users=100] [tokens=5] [amm_pairs=3] [rounds=10] [tps=0]
    //        [ip3:port3] [ip4:port4] [ip5:port5] [ip6:port6]
    //
    //   Phase 0: Compile CrossShardToken + AMMPool
    //   Phase 1: Generate user / token-deployer / AMM-deployer accounts
    //   Phase 2: Fund all accounts from genesis funders (raw TCP, bulk)
    //   Phase 2a: Wait for funder nonces to confirm on-chain
    //   Phase 2b: Verify all funded accounts (full batch check per shard)
    //   Phase 3: Deploy CrossShardToken (one per token deployer, on its own shard)
    //   Phase 4: Deploy AMMPool (one per amm deployer, constructor args = token pair)
    //   Phase 5: crossTransfer tokens to users + verify balanceOf on shadow contract
    //   Phase 6: setGasPrefund for token holders on AMM pool contracts + verify
    if (argv[1][0] == '8') {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        // ── Argument parsing ─────────────────────────────────────────────
        uint32_t funder_shard = (argc >= 3) ? (uint32_t)std::stoul(argv[2]) : 3u;
        std::string base_ip   = (argc >= 4) ? argv[3] : "192.168.25.129";
        uint16_t    base_tcp  = (argc >= 5) ? (uint16_t)std::stoi(argv[4]) : 13001;
        uint32_t kUsers       = (argc >= 6) ? (uint32_t)std::stoul(argv[5]) : 100u;
        uint32_t kTokens      = (argc >= 7) ? (uint32_t)std::stoul(argv[6]) : 5u;
        uint32_t kAmmPairs    = (argc >= 8) ? (uint32_t)std::stoul(argv[7]) : 3u;
        uint32_t kRounds8     = (argc >= 9) ? (uint32_t)std::stoul(argv[8]) : 10u;
        uint32_t kTps8        = (argc >= 10) ? (uint32_t)std::stoul(argv[9]) : 0u;
        (void)kRounds8; (void)kTps8;

        // amm_pairs must not exceed C(tokens, 2)
        if (kTokens >= 2) {
            uint32_t max_pairs = kTokens * (kTokens - 1) / 2;
            if (kAmmPairs > max_pairs) kAmmPairs = max_pairs;
        } else {
            kAmmPairs = 0;
        }
        if (kTokens == 0) { std::cerr << "tokens must be >= 1\n"; return 1; }

        // per-shard HTTP endpoints; override via argv[10..13] as "ip:port"
        struct Ep8 { std::string ip; uint16_t http; };
        std::map<uint32_t, Ep8> eps8;
        for (uint32_t s = 3; s <= 6; ++s)
            eps8[s] = {base_ip, (uint16_t)(20000u + s * 1000u + 1u)};
        for (int i = 10; i < std::min(argc, 14); ++i) {
            uint32_t sid = 3u + (uint32_t)(i - 10);
            std::string arg(argv[i]);
            auto col = arg.find(':');
            if (col != std::string::npos)
                eps8[sid] = {arg.substr(0, col), (uint16_t)std::stoi(arg.substr(col + 1))};
        }

        global_chain_node_ip        = eps8[funder_shard].ip;
        global_chain_node_http_port = eps8[funder_shard].http;
        shardnum = (int)funder_shard;

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "  Mode 8: CrossShardBase Token AMM  —  Step 1 (Accounts + Fund)\n";
        std::cout << "  Funder shard : " << funder_shard << "\n";
        std::cout << "  Users        : " << kUsers << "\n";
        std::cout << "  Tokens       : " << kTokens << "\n";
        std::cout << "  AMM pairs    : " << kAmmPairs << "\n";
        for (auto& [s, e] : eps8)
            std::cout << "  Shard " << s << "      : " << e.ip << ":" << e.http << "\n";
        std::cout << std::string(70, '=') << "\n";

        LoadAllAccounts(funder_shard);
        if (g_prikeys.empty()) {
            std::cerr << "No funder accounts loaded (missing init_accounts" << funder_shard << ")\n";
            return 1;
        }
        std::cout << "Loaded " << g_prikeys.size() << " funder accounts\n";

        SignalRegister();
        WriteDefaultLogConf();

        // ── TCP transport ─────────────────────────────────────────────────
        transport::MultiThreadHandler net_handler8;
        std::shared_ptr<security::Security> sec8 = std::make_shared<security::Ecdsa>();
        auto db_ptr8 = std::make_shared<db::Db>();
        std::string db8_path = db_path + "_mode8";
        { std::string cmd = "rm -rf '" + db8_path + "' 2>/dev/null"; (void)system(cmd.c_str()); }
        if (!db_ptr8->Init(db8_path)) { std::cerr << "init db failed\n"; return 1; }
        if (net_handler8.Init(db_ptr8, sec8) != 0) { std::cerr << "init net handler failed\n"; return 1; }
        if (transport::TcpTransport::Instance()->Init("127.0.0.1:13900", 128, false, &net_handler8) != 0) {
            std::cerr << "init tcp transport failed\n"; return 1;
        }
        if (transport::TcpTransport::Instance()->Start(false) != 0) {
            std::cerr << "start tcp transport failed\n"; return 1;
        }

        ShardoraSDK sdk8(global_chain_node_ip, global_chain_node_http_port);

        // connectivity check
        {
            auto ts = std::make_shared<security::Ecdsa>();
            ts->SetPrivateKey(g_prikeys[0]);
            int64_t tn = sdk8.fetchNonce(common::Encode::HexEncode(ts->GetAddress()));
            if (tn < 0) {
                std::cerr << "  ERROR: Cannot reach " << global_chain_node_ip
                          << ":" << global_chain_node_http_port
                          << " (is shard " << funder_shard << " node running?)\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  HTTP OK  node=" << global_chain_node_ip
                      << ":" << global_chain_node_http_port
                      << "  nonce=" << tn << "\n";
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 0: Compile CrossShardToken + AMMPool
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 0] Compile contracts...\n";

        // CrossShardBase (full inline) + CrossShardToken
        const std::string CROSS_SHARD_TOKEN_SOL = R"SOL(
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

abstract contract CrossShardBase {
    bytes32 private constant IS_CROSS_SHARD_BASE_SLOT =
        0x0c1f51986c7b4d6e0c3e3a3f5a6b7d8e9f0a1b2c3d4e5f6789abcdef01234567;
    address public constant SYSTEM_EXECUTOR_ADDRESS =
        0x53595354454d5f4558454355544F525f56310000;
    bytes private constant DOMAIN_TAG = "AKAVERSE_FEISTEL_V1";
    uint256 private constant MASK_80 = (1 << 80) - 1;

    bool    public IS_ROOT;
    address public BASE_ROOT_ADDRESS;
    address public SYSTEM_EXECUTOR;
    mapping(address => uint256) internal _balances;
    uint256 public totalSupply;

    event CrossTransferOut(address indexed base, address indexed from, address to,
        uint256 amount, uint64 nonce, uint32 toShard, uint32 toPool);
    event CrossTransferIn(address indexed base, address to, uint256 amount, uint64 nonce);
    event CrossStorageOut(address indexed base, bytes32 indexed key, bytes value,
        uint64 nonce, uint32 toShard, uint32 toPool);
    event CrossStorageIn(address indexed base, bytes32 key, bytes value, uint64 version);

    modifier onlySystemExecutor() {
        require(msg.sender == SYSTEM_EXECUTOR, "ONLY_SYSTEM_EXECUTOR"); _;
    }
    modifier onlyBase() { require(IS_ROOT, "ONLY_BASE_SHARD"); _; }

    constructor(address systemExecutor) {
        require(systemExecutor  != address(0), "ZERO_SYSTEM_EXECUTOR");
        SYSTEM_EXECUTOR   = systemExecutor;
        BASE_ROOT_ADDRESS = address(this);
        IS_ROOT           = true;
        _baseInit();
        assembly { sstore(IS_CROSS_SHARD_BASE_SLOT, 1) }
    }

    function _baseInit() internal virtual {}

    function _crossTransfer(address to, uint256 amount, uint32 toShard, uint32 toPool)
            internal returns (uint64 nonce) {
        require(to != address(0), "ZERO_TO");
        require(_balances[msg.sender] >= amount, "INSUFFICIENT_BALANCE");
        _balances[msg.sender] -= amount;
        totalSupply -= amount;
        nonce = 0;
        emit CrossTransferOut(BASE_ROOT_ADDRESS, msg.sender, to, amount, nonce, toShard, toPool);
    }

    function systemExecuteCrossTransfer(address to, uint256 amount, uint64 nonce)
            external onlySystemExecutor {
        require(to != address(0), "ZERO_TO");
        _balances[to] += amount;
        totalSupply += amount;
        emit CrossTransferIn(BASE_ROOT_ADDRESS, to, amount, nonce);
    }

    function _crossSetStorage(bytes32 key, bytes memory value, uint32 toShard, uint32 toPool)
            internal returns (uint64 nonce) {
        nonce = 0;
        emit CrossStorageOut(BASE_ROOT_ADDRESS, key, value, nonce, toShard, toPool);
    }

    function systemExecuteCrossStorage(bytes32 key, bytes calldata value, uint64 version)
            external onlySystemExecutor {
        _crossStorageSet(key, value);
        emit CrossStorageIn(BASE_ROOT_ADDRESS, key, value, version);
    }

    function _crossStorageSet(bytes32 key, bytes calldata value) internal virtual {}

    function balanceOf(address account) external view returns (uint256) {
        return _balances[account];
    }

    function approve(address /*spender*/, uint256 /*amount*/) external pure returns (bool) {
        return true;
    }

    function transferFrom(address from, address to, uint256 amount) external returns (bool) {
        require(_balances[from] >= amount, "INSUFFICIENT");
        _balances[from] -= amount;
        _balances[to]   += amount;
        return true;
    }
}

contract CrossShardToken is CrossShardBase {
    constructor(address sys) CrossShardBase(sys) {}

    function _baseInit() internal override {
        _balances[msg.sender] = 1_000_000 ether;
        totalSupply = 1_000_000 ether;
    }

    function crossTransfer(address to, uint256 amt, uint32 shard, uint32 pool) external {
        _crossTransfer(to, amt, shard, pool);
    }

    function transfer(address to, uint256 amt) external returns (bool) {
        require(_balances[msg.sender] >= amt, "INSUFFICIENT");
        _balances[msg.sender] -= amt;
        _balances[to] += amt;
        return true;
    }
}
)SOL";

        // AMMPool — standard constant-product AMM, no CrossShardBase
        const std::string AMM_POOL_SOL = R"SOL(
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
    event Swap(address indexed user, address tokenIn, uint256 amountIn, uint256 amountOut);
    constructor(address _tokenA, address _tokenB) {
        tokenA = IERC20(_tokenA);
        tokenB = IERC20(_tokenB);
    }
    function addLiquidity(uint256 amountA, uint256 amountB) external returns (uint256 lp) {
        tokenA.transferFrom(msg.sender, address(this), amountA);
        tokenB.transferFrom(msg.sender, address(this), amountB);
        lp = totalLiquidity == 0 ? amountA : (amountA * totalLiquidity) / reserveA;
        reserveA += amountA; reserveB += amountB; totalLiquidity += lp;
        liquidity[msg.sender] += lp;
    }
    function swapAForB(uint256 amountIn, uint256 minOut) external returns (uint256 amountOut) {
        require(amountIn > 0 && reserveA > 0 && reserveB > 0, "invalid");
        amountOut = (amountIn * reserveB) / (reserveA + amountIn);
        require(amountOut >= minOut, "slippage");
        tokenA.transferFrom(msg.sender, address(this), amountIn);
        tokenB.transfer(msg.sender, amountOut);
        reserveA += amountIn; reserveB -= amountOut;
        emit Swap(msg.sender, address(tokenA), amountIn, amountOut);
    }
    function swapBForA(uint256 amountIn, uint256 minOut) external returns (uint256 amountOut) {
        require(amountIn > 0 && reserveA > 0 && reserveB > 0, "invalid");
        amountOut = (amountIn * reserveA) / (reserveB + amountIn);
        require(amountOut >= minOut, "slippage");
        tokenB.transferFrom(msg.sender, address(this), amountIn);
        tokenA.transfer(msg.sender, amountOut);
        reserveB += amountIn; reserveA -= amountOut;
        emit Swap(msg.sender, address(tokenB), amountIn, amountOut);
    }
    function getReserves() external view returns (uint256, uint256) {
        return (reserveA, reserveB);
    }
}
)SOL";

        auto compiled_token = sdk8.compileSolidity(CROSS_SHARD_TOKEN_SOL);
        if (compiled_token["status"] != 0) {
            std::cerr << "  CrossShardToken compile failed: " << compiled_token["msg"] << "\n";
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }
        std::string token_bytecode8 = compiled_token["bytecode"];
        std::cout << "  CrossShardToken: " << token_bytecode8.size() / 2 << " bytes\n";

        auto compiled_amm = sdk8.compileSolidity(AMM_POOL_SOL);
        if (compiled_amm["status"] != 0) {
            std::cerr << "  AMMPool compile failed: " << compiled_amm["msg"] << "\n";
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }
        std::string amm_bytecode8 = compiled_amm["bytecode"];
        std::cout << "  AMMPool:         " << amm_bytecode8.size() / 2 << " bytes\n";
        std::cout << "  [Phase 0] OK\n";

        // ─────────────────────────────────────────────────────────────────
        // Phase 1: Generate accounts
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 1] Generate accounts...\n";

        const std::vector<uint32_t> kShards8 = {3, 4, 5, 6};
        const uint32_t kNumShards8 = 4;

        // Address → shard routing: matches root_to_tx_item.cc RootToTxItem::HandleTx
        //   sharding_id = Hash64(addr) % num_shards + kConsensusShardBeginNetworkId
        auto addr_pool8 = [](const std::string& raw) -> uint32_t {
            return common::GetAddressPoolIndex(raw);
        };
        auto addr_shard8 = [&](const std::string& raw) -> uint32_t {
            uint64_t h = common::Hash::Hash64(raw.substr(0, common::kUnicastAddressLength));
            return (uint32_t)(h % kNumShards8) + network::kConsensusShardBeginNetworkId;
        };

        // ── User accounts — distributed evenly across shards 3-6 ─────────
        struct UserAcct8 {
            std::string prikey;
            std::string addr_hex;
            uint32_t    shard_id;
            uint32_t    pool_idx;
        };
        std::vector<UserAcct8> users8;
        users8.reserve(kUsers);

        {
            uint32_t per_shard = kUsers / kNumShards8;
            if (per_shard == 0) per_shard = 1;

            std::mutex mu;
            std::vector<std::thread> uth;
            for (uint32_t si = 0; si < kNumShards8 && !global_stop; ++si) {
                uth.emplace_back([&, si, per_shard]() {
                    uint32_t target = kShards8[si];
                    uint32_t need   = (si < kNumShards8 - 1)
                                        ? per_shard
                                        : (kUsers - per_shard * (kNumShards8 - 1));
                    std::vector<UserAcct8> local;
                    local.reserve(need);
                    while ((uint32_t)local.size() < need && !global_stop) {
                        std::string pk(32, '\0');
                        for (int j = 0; j < 32; ++j)
                            pk[j] = (char)(common::Random::RandomUint32() % 256);
                        auto s = std::make_shared<security::Ecdsa>();
                        s->SetPrivateKey(pk);
                        std::string addr = s->GetAddress();
                        if (addr_shard8(addr) == target) {
                            local.push_back({pk, common::Encode::HexEncode(addr),
                                             target, addr_pool8(addr)});
                        }
                    }
                    std::lock_guard<std::mutex> lk(mu);
                    for (auto& u : local) users8.push_back(std::move(u));
                });
            }
            for (auto& t : uth) t.join();
        }

        // Print shard distribution + full addresses
        std::cout << "  Users: " << users8.size() << "  (";
        for (uint32_t s : kShards8) {
            uint32_t c = 0;
            for (auto& u : users8) if (u.shard_id == s) ++c;
            std::cout << "s" << s << "=" << c << " ";
        }
        std::cout << ")\n";
        for (uint32_t i = 0; i < (uint32_t)users8.size(); ++i) {
            std::cout << "    [user" << i << "] addr=" << users8[i].addr_hex
                      << " s" << users8[i].shard_id << "\n";
        }

        // ── Token deployers — random signing keys + pre-chosen contract addrs ──
        // Shardora lets the sender choose the contract address freely via the `to`
        // field of a kCreateContract TX, so we pick a random 20-byte address
        // that routes to the desired shard/pool.
        struct TokenDeployer8 {
            std::string prikey;           // signing key (raw)
            std::string addr_hex;         // signer address (hex, for funding)
            uint32_t    signer_shard;
            std::string contract_addr;    // pre-chosen contract address (raw 20B)
            std::string contract_addr_hex;
            uint32_t    contract_shard;
            uint32_t    contract_pool;
        };
        std::vector<TokenDeployer8> tdeps8(kTokens);

        for (uint32_t i = 0; i < kTokens; ++i) {
            // Signer and contract MUST be on the same shard: Shardora's
            // kCreateContract routes on the `to` address, and gas is deducted
            // from the sender on that same shard's pool.
            uint32_t cshard = kShards8[i % kNumShards8];  // spread tokens across shards

            // contract address — random address that routes to cshard
            std::string caddr;
            while (true) {
                caddr.resize(20);
                for (int j = 0; j < 20; ++j) caddr[j] = (char)(common::Random::RandomUint32() % 256);
                if (addr_shard8(caddr) == cshard) break;
            }

            // signing key — must also route to cshard (co-located with contract)
            std::string pk, addr;
            while (true) {
                pk.resize(32);
                for (int j = 0; j < 32; ++j) pk[j] = (char)(common::Random::RandomUint32() % 256);
                auto sec = std::make_shared<security::Ecdsa>();
                sec->SetPrivateKey(pk);
                addr = sec->GetAddress();
                if (addr_shard8(addr) == cshard) break;
            }

            tdeps8[i].prikey            = pk;
            tdeps8[i].addr_hex          = common::Encode::HexEncode(addr);
            tdeps8[i].signer_shard      = cshard;
            tdeps8[i].contract_addr     = caddr;
            tdeps8[i].contract_addr_hex = common::Encode::HexEncode(caddr);
            tdeps8[i].contract_shard    = cshard;
            tdeps8[i].contract_pool     = addr_pool8(caddr);
        }

        std::cout << "  Token deployers: " << kTokens << "\n";
        for (uint32_t i = 0; i < kTokens; ++i) {
            std::cout << "    [token" << i << "] signer=" << tdeps8[i].addr_hex
                      << " s" << tdeps8[i].signer_shard
                      << "  contract=" << tdeps8[i].contract_addr_hex
                      << " s" << tdeps8[i].contract_shard
                      << " pool=" << tdeps8[i].contract_pool << "\n";
        }

        // ── AMM deployers — random signing keys + token pair assignment ───
        struct AmmDeployer8 {
            std::string prikey;
            std::string addr_hex;
            uint32_t    signer_shard;   // == deploy_shard (co-located)
            uint32_t    deployer_pool;  // pool of the deployer addr (contract must match)
            uint32_t    token_a;        // index into tdeps8
            uint32_t    token_b;
            std::string contract_addr_hex;   // filled in after Phase 4 deploy
            std::string token_a_shadow_hex;  // DeriveShardAddress(tokenA_base, shard, pool)
            std::string token_b_shadow_hex;  // DeriveShardAddress(tokenB_base, shard, pool)
        };
        std::vector<AmmDeployer8> adeps8(kAmmPairs);

        {
            // Build sequential unique pairs (a, b) with a < b
            std::vector<std::pair<uint32_t,uint32_t>> pairs;
            for (uint32_t a = 0; a < kTokens; ++a)
                for (uint32_t b = a + 1; b < kTokens; ++b)
                    pairs.push_back({a, b});
            // Fisher-Yates shuffle
            for (uint32_t i = (uint32_t)pairs.size(); i > 1; --i) {
                uint32_t j = common::Random::RandomUint32() % i;
                std::swap(pairs[i-1], pairs[j]);
            }

            for (uint32_t k = 0; k < kAmmPairs; ++k) {
                // Pick a target shard for this AMM deployer; signer must be co-located.
                uint32_t ashard = kShards8[k % kNumShards8];
                std::string pk, addr;
                while (true) {
                    pk.resize(32);
                    for (int j = 0; j < 32; ++j) pk[j] = (char)(common::Random::RandomUint32() % 256);
                    auto sec = std::make_shared<security::Ecdsa>();
                    sec->SetPrivateKey(pk);
                    addr = sec->GetAddress();
                    if (addr_shard8(addr) == ashard) break;
                }
                adeps8[k].prikey        = pk;
                adeps8[k].addr_hex      = common::Encode::HexEncode(addr);
                adeps8[k].signer_shard  = ashard;
                adeps8[k].deployer_pool = addr_pool8(addr);
                adeps8[k].token_a       = pairs[k].first;
                adeps8[k].token_b       = pairs[k].second;
            }
        }

        std::cout << "  AMM deployers: " << kAmmPairs << "\n";
        for (uint32_t k = 0; k < kAmmPairs; ++k) {
            std::cout << "    [amm" << k << "] signer=" << adeps8[k].addr_hex
                      << " s" << adeps8[k].signer_shard
                      << " pool=" << adeps8[k].deployer_pool
                      << "  pair=(token" << adeps8[k].token_a
                      << ",token" << adeps8[k].token_b << ")\n";
        }
        std::cout << "  [Phase 1] OK\n";

        // ─────────────────────────────────────────────────────────────────
        // Phase 2: Fund all accounts (bulk raw-TCP, same pattern as Mode 6)
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 2] Fund all accounts...\n";

        // Flat list: (addr_hex, target_shard) — order: users, token dep, amm dep
        std::vector<std::pair<std::string, uint32_t>> to_fund8;
        to_fund8.reserve(users8.size() + kTokens + kAmmPairs);
        for (auto& u : users8)     to_fund8.push_back({u.addr_hex,     u.shard_id});
        for (auto& d : tdeps8)     to_fund8.push_back({d.addr_hex,     d.signer_shard});
        for (auto& d : adeps8)     to_fund8.push_back({d.addr_hex,     d.signer_shard});
        uint32_t total_fund8 = (uint32_t)to_fund8.size();

        std::cout << "  Accounts to fund: " << total_fund8
                  << "  (users=" << users8.size()
                  << " tkn_dep=" << kTokens
                  << " amm_dep=" << kAmmPairs << ")\n";

        const uint64_t kFundAmt8 = 8000000000ULL;

        // Deduplicate genesis funders
        std::vector<std::string> uniq_funders8;
        { std::set<std::string> seen;
          for (auto& pk : g_prikeys)
              if (seen.insert(pk).second) uniq_funders8.push_back(pk); }
        std::cout << "  Unique funders: " << uniq_funders8.size() << "\n";

        struct FState8 {
            std::string prikey;
            std::shared_ptr<security::Security> sec;
            std::string addr_hex;
            int64_t nonce_start{0};
            int64_t nonce_sent{0};
            std::atomic<uint32_t> sent{0};
            FState8() = default;
            FState8(FState8&& o) noexcept
                : prikey(std::move(o.prikey)), sec(std::move(o.sec)),
                  addr_hex(std::move(o.addr_hex)),
                  nonce_start(o.nonce_start), nonce_sent(o.nonce_sent),
                  sent(o.sent.load()) {}
            FState8& operator=(FState8&&) = delete;
        };
        std::vector<FState8> fstates8;
        {
            ShardoraSDK fqsdk(eps8[funder_shard].ip, eps8[funder_shard].http);
            for (auto& pk : uniq_funders8) {
                FState8 fs;
                fs.prikey = pk;
                fs.sec = std::make_shared<security::Ecdsa>();
                fs.sec->SetPrivateKey(pk);
                std::string addr_raw = fs.sec->GetAddress();
                fs.addr_hex = common::Encode::HexEncode(addr_raw);
                int64_t bal = fqsdk.fetchBalance(fs.addr_hex);
                if (bal <= 0) {
                    std::cout << "  funder " << fs.addr_hex
                              << "  balance=" << bal << ", skipped\n";
                    continue;
                }
                int64_t n = fqsdk.fetchNonce(fs.addr_hex);
                fs.nonce_start = (n >= 0) ? n : 0;
                fs.nonce_sent  = fs.nonce_start;
                std::cout << "  funder " << fs.addr_hex
                          << "  chain_nonce=" << fs.nonce_start
                          << "  balance=" << bal << "\n";
                fstates8.push_back(std::move(fs));
            }
            if (fstates8.empty()) {
                std::cerr << "  FATAL: no valid funders found (check init_accounts"
                          << funder_shard << ")\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }

        std::string fip8 = eps8[funder_shard].ip;
        uint16_t    ftcp8 = eps8[funder_shard].http - 10000;
        uint32_t    nf8   = (uint32_t)fstates8.size();
        if (!nf8) nf8 = 1;

        std::atomic<uint32_t> fund_ok8{0}, fund_fail8{0};

        {
            std::vector<std::thread> fth8;
            for (uint32_t fi = 0; fi < nf8; ++fi) {
                fth8.emplace_back([&, fi]() {
                    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (fd < 0) {
                        for (uint32_t i = fi; i < total_fund8; i += nf8) ++fund_fail8;
                        return;
                    }
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    sockaddr_in sa{};
                    sa.sin_family = AF_INET;
                    sa.sin_port   = htons(ftcp8);
                    ::inet_pton(AF_INET, fip8.c_str(), &sa.sin_addr);
                    if (::connect(fd, (sockaddr*)&sa, sizeof(sa)) != 0) {
                        ::close(fd);
                        for (uint32_t i = fi; i < total_fund8; i += nf8) ++fund_fail8;
                        return;
                    }

                    auto& fs = fstates8[fi];
                    int64_t nonce = fs.nonce_sent;
                    for (uint32_t i = fi; i < total_fund8 && !global_stop; i += nf8) {
                        auto& [addr_hex, tgt] = to_fund8[i];
                        (void)tgt;
                        auto tx = CreateTransactionWithAttr(
                            fs.sec, (uint64_t)(++nonce), fs.prikey,
                            common::Encode::HexDecode(addr_hex),
                            "", "", kFundAmt8, 210000, 1, (int32_t)funder_shard);
                        if (!tx) { ++fund_fail8; --nonce; continue; }

                        tx->header.set_from_public_port(
                            common::GlobalInfo::Instance()->config_public_port());
                        if (!tx->header.has_hash64() || tx->header.hash64() == 0) {
                            std::string hs = tx->header.SerializeAsString();
                            tx->header.set_hash64(common::Hash::Hash64(hs));
                        }
                        std::string payload = tx->header.SerializeAsString();
                        uint32_t    plen    = (uint32_t)payload.size();
                        uint8_t     hdr[4]  = {
                            (uint8_t)(plen & 0xFF),
                            (uint8_t)((plen >> 8) & 0xFF),
                            (uint8_t)((plen >> 16) & 0xFF),
                            0
                        };

                        bool ok = (::send(fd, hdr, 4, MSG_NOSIGNAL) == 4);
                        if (ok) {
                            uint32_t off = 0;
                            while (off < plen) {
                                ssize_t n = ::send(fd, payload.data() + off, plen - off, MSG_NOSIGNAL);
                                if (n <= 0) { ok = false; break; }
                                off += (uint32_t)n;
                            }
                        }

                        if (ok) {
                            ++fund_ok8;
                            ++fs.sent;
                        } else {
                            ++fund_fail8;
                            --nonce;
                            ::close(fd);
                            fd = ::socket(AF_INET, SOCK_STREAM, 0);
                            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                            if (::connect(fd, (sockaddr*)&sa, sizeof(sa)) != 0) {
                                ::close(fd); fd = -1; break;
                            }
                        }
                    }
                    if (fd >= 0) ::close(fd);
                    fs.nonce_sent = nonce;
                });
            }

            // progress reporter
            std::thread prog8([&]() {
                uint32_t last = 0;
                while (fund_ok8.load() + fund_fail8.load() < total_fund8 && !global_stop) {
                    usleep(1000000);
                    uint32_t ok   = fund_ok8.load();
                    uint32_t fail = fund_fail8.load();
                    uint32_t tps  = ok + fail - last;
                    last = ok + fail;
                    std::cout << "  Fund: " << ok << " ok  " << fail << " fail / "
                              << total_fund8 << "  tps=" << tps << "\n";
                }
            });
            for (auto& t : fth8) t.join();
            prog8.join();
        }
        std::cout << "  [Phase 2] sends: " << fund_ok8.load() << " ok  "
                  << fund_fail8.load() << " fail\n";

        // ─────────────────────────────────────────────────────────────────
        // Phase 2a: Wait for funder nonces to confirm on-chain
        // ─────────────────────────────────────────────────────────────────
        // Helper: parse int64 from JSON value that may be string or number
        auto jsonToInt64 = [](const nlohmann::json& v) -> int64_t {
            if (v.is_string()) {
                int64_t n = -1;
                std::string s = v.get<std::string>();
                std::from_chars(s.data(), s.data() + s.size(), n);
                return n;
            }
            try { return v.get<int64_t>(); } catch (...) { return -1; }
        };

        std::cout << "\n[Phase 2a] Verify funder nonces on-chain (max 60s)...\n";
        {
            ShardoraSDK vsdk8(eps8[funder_shard].ip, eps8[funder_shard].http);
            std::vector<std::string> faddrs8;
            for (auto& fs : fstates8) faddrs8.push_back(fs.addr_hex);

            bool nonce_ok8 = false;
            for (int rd = 0; rd < 60 && !global_stop; ++rd) {
                auto r = vsdk8.batchQueryAccounts(faddrs8);
                bool all_match = true;
                uint32_t confirmed = 0;
                if (r.contains("accounts")) {
                    for (auto& fs : fstates8) {
                        if (!r["accounts"].contains(fs.addr_hex)) { all_match = false; continue; }
                        int64_t actual = -1;
                        try { actual = jsonToInt64(r["accounts"][fs.addr_hex]["nonce"]); } catch (...) {}
                        if (actual >= (int64_t)fs.nonce_sent) ++confirmed;
                        else all_match = false;
                    }
                } else { all_match = false; }

                if (all_match) { nonce_ok8 = true; break; }
                if (rd % 30 == 0)
                    std::cout << "  [" << rd << "s] " << confirmed << "/" << fstates8.size()
                              << " funders confirmed nonces...\n";
                usleep(1000000);
            }
            if (nonce_ok8) std::cout << "  Funder nonces confirmed OK\n";
            else {
                std::cout << "  FATAL: nonce check timed out after 60s\n";
                auto r = vsdk8.batchQueryAccounts(faddrs8);
                for (auto& fs : fstates8) {
                    int64_t actual = -1;
                    if (r.contains("accounts") && r["accounts"].contains(fs.addr_hex))
                        try { actual = jsonToInt64(r["accounts"][fs.addr_hex]["nonce"]); } catch (...) {}
                    std::cout << "    " << fs.addr_hex
                              << "  want=" << fs.nonce_sent << " got=" << actual << "\n";
                }
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 2b: Verify ALL funded accounts via batch query per shard (max 60s)
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 2b] Verify all " << total_fund8
                  << " accounts funded (batch query per shard, max 60s)...\n";
        {
            // Group all to_fund8 addresses by shard
            std::map<uint32_t, std::vector<std::string>> shard_addrs8;
            for (auto& [addr, shard] : to_fund8) {
                shard_addrs8[shard].push_back(addr);
            }

            auto parseBalance8 = [](const nlohmann::json& bv) -> uint64_t {
                uint64_t bal = 0;
                try {
                    if (bv.is_string()) {
                        std::string bs = bv.get<std::string>();
                        std::from_chars(bs.data(), bs.data() + bs.size(), bal);
                    } else {
                        bal = bv.get<uint64_t>();
                    }
                } catch (...) {}
                return bal;
            };

            std::vector<std::thread> bal_threads;
            std::mutex bal_mu;
            std::atomic<uint32_t> total_unfunded{0};

            for (auto& [s, addrs] : shard_addrs8) {
                bal_threads.emplace_back([&, s, addrs]() {
                    ShardoraSDK ssdk(eps8[s].ip, eps8[s].http);
                    std::vector<std::string> pending = addrs;

                    for (int rd = 0; rd < 60 && !pending.empty() && !global_stop; ++rd) {
                        auto r = ssdk.batchQueryAccounts(pending);
                        std::vector<std::string> still_pending;
                        if (r.contains("accounts")) {
                            for (auto& a : pending) {
                                uint64_t bal = 0;
                                if (r["accounts"].contains(a))
                                    bal = parseBalance8(r["accounts"][a]["balance"]);
                                if (bal == 0) still_pending.push_back(a);
                            }
                        } else {
                            still_pending = pending;
                        }
                        pending = still_pending;

                        if (!pending.empty() && rd % 20 == 0) {
                            std::lock_guard<std::mutex> lk(bal_mu);
                            std::cout << "  Shard " << s << ": "
                                      << (addrs.size() - pending.size()) << "/"
                                      << addrs.size() << " funded [" << rd << "s]";
                            // Always print stuck addresses so diagnosis is visible
                            // even if the run is interrupted before the 60s timeout.
                            if (rd > 0) {
                                std::cout << "  still-unfunded(" << pending.size() << "):";
                                int print_n = 0;
                                for (auto& a : pending) {
                                    if (print_n++ >= 3) break;
                                    std::cout << " " << a;
                                }
                                if ((int)pending.size() > 3)
                                    std::cout << " ...";
                            }
                            std::cout << "\n";
                        }
                        if (!pending.empty()) usleep(1000000);
                    }

                    std::lock_guard<std::mutex> lk(bal_mu);
                    if (pending.empty()) {
                        std::cout << "  Shard " << s << ": all " << addrs.size()
                                  << " accounts funded OK\n";
                    } else {
                        // Re-query each stuck address individually to separate
                        // batch-query truncation from genuine funding failures.
                        std::vector<std::string> truly_unfunded;
                        for (auto& a : pending) {
                            int64_t bal = ssdk.fetchBalance(a);
                            if (bal > 0) {
                                std::cout << "  Shard " << s << ": " << a
                                          << " balance=" << bal
                                          << " (batch missed, individually OK)\n";
                            } else {
                                truly_unfunded.push_back(a);
                                std::cout << "  Shard " << s << ": " << a
                                          << " balance=0 (genuinely unfunded)\n";
                            }
                        }
                        if (!truly_unfunded.empty()) {
                            std::cout << "  Shard " << s << ": FAILED "
                                      << truly_unfunded.size() << "/"
                                      << addrs.size() << " genuinely unfunded after 60s\n";
                            total_unfunded.fetch_add((uint32_t)truly_unfunded.size());
                        } else {
                            std::cout << "  Shard " << s << ": all " << addrs.size()
                                      << " accounts funded OK (batch had false misses)\n";
                        }
                    }
                });
            }
            for (auto& t : bal_threads) t.join();

            if (total_unfunded.load() > 0) {
                std::cerr << "  FATAL: Phase 2b: " << total_unfunded.load()
                          << " accounts still unfunded. Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  Phase 2b: all " << total_fund8 << " accounts confirmed funded\n";
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 3: Deploy CrossShardToken contracts
        // Each token deployer sends one kCreateContract tx to its pre-chosen
        // contract address on its target shard.  Constructor: (sys, base) where
        // sys = SYSTEM_EXECUTOR_ADDRESS (fixed constant), base = contract_addr.
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 3] Deploy CrossShardToken contracts (" << kTokens << " tokens)...\n";

        // SYSTEM_EXECUTOR_ADDRESS constant from CrossShardBase (20 bytes, hex)
        const std::string kSysExec = "53595354454d5f4558454355544f525f56310000";

        // ABI-encode constructor(address sys): one address arg, 32 bytes (left-padded)
        auto encodeAddr32 = [](const std::string& hex40) -> std::string {
            // left-pad 40-char hex address to 64 chars
            return std::string(24, '0') + hex40;
        };

        {
            const uint64_t kTokenDeployPrefund = 5000000000ULL;
            std::vector<std::thread> tth3;
            std::atomic<uint32_t> tok_ok{0}, tok_fail{0};

            for (uint32_t i = 0; i < kTokens; ++i) {
                tth3.emplace_back([&, i]() {
                    auto& td = tdeps8[i];
                    // constructor arg: only sys (baseRootAddress auto-set to address(this))
                    std::string ctor_args = encodeAddr32(kSysExec);
                    std::string full_code = token_bytecode8 + ctor_args;

                    ShardoraSDK dsdk(eps8[td.signer_shard].ip, eps8[td.signer_shard].http);
                    // pre-fetch nonce so we send with the correct nonce
                    int64_t nonce = dsdk.fetchNonce(td.addr_hex);
                    if (nonce < 0) { ++tok_fail; return; }

                    auto r = dsdk.deployToAddressWithNonce(
                        common::Encode::HexEncode(td.prikey),
                        full_code,
                        td.contract_addr_hex,
                        kTokenDeployPrefund,
                        nonce);
                    if (r.contains("status") && r["status"] == 0) {
                        ++tok_ok;
                    } else {
                        std::cerr << "  [token" << i << "] deploy failed: "
                                  << r.value("msg", "?") << "\n";
                        ++tok_fail;
                    }
                });
            }
            for (auto& t : tth3) t.join();
            std::cout << "  Token deploy sends: " << tok_ok.load() << " ok  "
                      << tok_fail.load() << " fail\n";
        }

        // Phase 3 verify: wait for all token contract addresses to appear on-chain
        std::cout << "\n[Phase 3 verify] Wait for token contracts on-chain (max 60s)...\n";
        {
            std::map<uint32_t, std::vector<std::string>> shard_token_addrs;
            for (uint32_t i = 0; i < kTokens; ++i)
                shard_token_addrs[tdeps8[i].contract_shard].push_back(tdeps8[i].contract_addr_hex);

            std::vector<std::thread> vth3;
            std::mutex vmx3;
            std::atomic<uint32_t> confirmed_tokens{0};
            std::atomic<uint32_t> failed_tokens{0};

            for (auto& [s, addrs] : shard_token_addrs) {
                vth3.emplace_back([&, s, addrs]() {
                    ShardoraSDK vsdk(eps8[s].ip, eps8[s].http);
                    std::vector<std::string> pending = addrs;
                    for (int rd = 0; rd < 60 && !pending.empty() && !global_stop; ++rd) {
                        auto r = vsdk.batchQueryAccounts(pending);
                        std::vector<std::string> still;
                        if (r.contains("accounts")) {
                            for (auto& a : pending) {
                                if (r["accounts"].contains(a)) {
                                    confirmed_tokens.fetch_add(1);
                                } else {
                                    still.push_back(a);
                                }
                            }
                        } else { still = pending; }
                        pending = still;
                        if (!pending.empty() && rd % 10 == 0) {
                            std::lock_guard<std::mutex> lk(vmx3);
                            std::cout << "  Shard " << s << ": " << (addrs.size() - pending.size())
                                      << "/" << addrs.size() << " token contracts confirmed [" << rd << "s]\n";
                        }
                        if (!pending.empty()) usleep(1000000);
                    }
                    std::lock_guard<std::mutex> lk(vmx3);
                    if (pending.empty()) {
                        std::cout << "  Shard " << s << ": all " << addrs.size()
                                  << " token contracts confirmed OK\n";
                    } else {
                        std::cout << "  Shard " << s << ": FAILED " << pending.size()
                                  << "/" << addrs.size() << " token contracts not found after 60s:\n";
                        for (auto& a : pending) std::cout << "    " << a << "\n";
                        failed_tokens.fetch_add((uint32_t)pending.size());
                    }
                });
            }
            for (auto& t : vth3) t.join();

            if (failed_tokens.load() > 0) {
                std::cerr << "  FATAL: Phase 3: " << failed_tokens.load()
                          << " token contracts not confirmed. Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  Phase 3: all " << kTokens << " token contracts deployed OK\n";
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 4: Deploy AMMPool contracts
        // Each AMM deployer sends one kCreateContract tx with a randomly-
        // chosen contract address (any shard). Constructor: (tokenA, tokenB)
        // where tokenA/tokenB are the deployed CrossShardToken contract addrs.
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 4] Deploy AMMPool contracts (" << kAmmPairs << " pools)...\n";

        {
            const uint64_t kAmmDeployPrefund = 5000000000ULL;
            std::vector<std::thread> tth4;
            std::atomic<uint32_t> amm_ok{0}, amm_fail{0};

            for (uint32_t k = 0; k < kAmmPairs; ++k) {
                tth4.emplace_back([&, k]() {
                    auto& ad = adeps8[k];
                    // constructor(address tokenA, address tokenB)
                    // Pass mirror addresses so the AMMPool EVM calls land on the same
                    // shard+pool where crossTransfer created the token mirrors.
                    const std::string& tA_base = tdeps8[ad.token_a].contract_addr_hex;
                    const std::string& tB_base = tdeps8[ad.token_b].contract_addr_hex;
                    evmc::address tA_evmc{}, tB_evmc{};
                    {
                        auto tA_raw = common::Encode::HexDecode(tA_base);
                        auto tB_raw = common::Encode::HexDecode(tB_base);
                        std::memcpy(tA_evmc.bytes, tA_raw.data(), 20);
                        std::memcpy(tB_evmc.bytes, tB_raw.data(), 20);
                    }
                    evmc::address tA_shadow = shardoravm::DeriveShardAddress(
                        tA_evmc, ad.signer_shard, ad.deployer_pool);
                    evmc::address tB_shadow = shardoravm::DeriveShardAddress(
                        tB_evmc, ad.signer_shard, ad.deployer_pool);
                    std::string tA_shadow_hex = common::Encode::HexEncode(
                        std::string(reinterpret_cast<const char*>(tA_shadow.bytes), 20));
                    std::string tB_shadow_hex = common::Encode::HexEncode(
                        std::string(reinterpret_cast<const char*>(tB_shadow.bytes), 20));
                    std::string ctor_args = encodeAddr32(tA_shadow_hex) + encodeAddr32(tB_shadow_hex);
                    std::string full_code = amm_bytecode8 + ctor_args;

                    // Contract address must be on the SAME shard AND pool as the deployer,
                    // so that eps8[shard] node (which handles deployer_pool) can process
                    // prefund TXs for this contract without "contract not local" errors.
                    std::string to_address;
                    for (int attempt = 0; attempt < 200000 && !global_stop; ++attempt) {
                        std::string salt = ad.addr_hex + std::to_string(attempt);
                        std::string cand = utils::keccak256Str(amm_bytecode8 + salt).substr(24);
                        if (!cand.empty()) {
                            std::string raw = common::Encode::HexDecode(cand);
                            if (!raw.empty() &&
                                    addr_shard8(raw) == ad.signer_shard &&
                                    addr_pool8(raw) == ad.deployer_pool) {
                                to_address = cand;
                                break;
                            }
                        }
                    }
                    if (to_address.empty()) { ++amm_fail; return; }

                    ad.contract_addr_hex = to_address;  // store for later
                    ad.token_a_shadow_hex = tA_shadow_hex;
                    ad.token_b_shadow_hex = tB_shadow_hex;
                    {
                        std::string tA_raw2(reinterpret_cast<const char*>(tA_shadow.bytes), 20);
                        std::string tB_raw2(reinterpret_cast<const char*>(tB_shadow.bytes), 20);
                        uint32_t tA_pool = addr_pool8(tA_raw2);
                        uint32_t tB_pool = addr_pool8(tB_raw2);
                        std::cout << "  [amm" << k << "] contract=" << to_address
                                  << " s" << ad.signer_shard << " deployer_pool=" << ad.deployer_pool
                                  << "\n         tokenA_shadow=" << tA_shadow_hex
                                  << " shadow_pool=" << tA_pool
                                  << (tA_pool != ad.deployer_pool ? " MISMATCH!" : "")
                                  << "\n         tokenB_shadow=" << tB_shadow_hex
                                  << " shadow_pool=" << tB_pool
                                  << (tB_pool != ad.deployer_pool ? " MISMATCH!" : "") << "\n";
                    }

                    ShardoraSDK dsdk(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                    int64_t nonce = dsdk.fetchNonce(ad.addr_hex);
                    if (nonce < 0) { ++amm_fail; return; }

                    auto r = dsdk.deployToAddressWithNonce(
                        common::Encode::HexEncode(ad.prikey),
                        full_code,
                        to_address,
                        kAmmDeployPrefund,
                        nonce);
                    if (r.contains("status") && r["status"] == 0) {
                        ++amm_ok;
                    } else {
                        std::cerr << "  [amm" << k << "] deploy failed: "
                                  << r.value("msg", "?") << "\n";
                        ++amm_fail;
                    }
                });
            }
            for (auto& t : tth4) t.join();
            std::cout << "  AMM deploy sends: " << amm_ok.load() << " ok  "
                      << amm_fail.load() << " fail\n";
        }

        // Phase 4 verify: wait for AMM contract addresses on-chain
        std::cout << "\n[Phase 4 verify] Wait for AMM contracts on-chain (max 60s)...\n";
        {
            // Group by deployer's shard (contract address was chosen to match it)
            std::map<uint32_t, std::vector<std::string>> shard_amm;
            for (uint32_t k = 0; k < kAmmPairs; ++k) {
                if (adeps8[k].contract_addr_hex.empty()) continue;
                shard_amm[adeps8[k].signer_shard].push_back(adeps8[k].contract_addr_hex);
            }

            std::vector<std::thread> vth4;
            std::mutex vmx4;
            std::atomic<uint32_t> confirmed_amm{0};
            std::atomic<uint32_t> failed_amm{0};

            for (auto& [s, addrs] : shard_amm) {
                vth4.emplace_back([&, s, addrs]() {
                    ShardoraSDK vsdk(eps8[s].ip, eps8[s].http);
                    std::vector<std::string> pending = addrs;

                    for (int rd = 0; rd < 60 && !pending.empty() && !global_stop; ++rd) {
                        auto r = vsdk.batchQueryAccounts(pending);
                        std::vector<std::string> still;
                        if (r.contains("accounts")) {
                            for (auto& a : pending) {
                                if (r["accounts"].contains(a)) confirmed_amm.fetch_add(1);
                                else still.push_back(a);
                            }
                        } else { still = pending; }
                        pending = still;
                        if (!pending.empty() && rd % 10 == 0) {
                            std::lock_guard<std::mutex> lk(vmx4);
                            std::cout << "  Shard " << s << ": " << (addrs.size() - pending.size())
                                      << "/" << addrs.size() << " AMM contracts confirmed [" << rd << "s]\n";
                        }
                        if (!pending.empty()) usleep(1000000);
                    }
                    std::lock_guard<std::mutex> lk(vmx4);
                    if (pending.empty()) {
                        std::cout << "  Shard " << s << ": all " << addrs.size()
                                  << " AMM contracts confirmed OK\n";
                    } else {
                        std::cout << "  Shard " << s << ": FAILED " << pending.size()
                                  << "/" << addrs.size() << " AMM contracts not found after 60s:\n";
                        for (auto& a : pending) std::cout << "    " << a << "\n";
                        failed_amm.fetch_add((uint32_t)pending.size());
                    }
                });
            }
            for (auto& t : vth4) t.join();

            if (failed_amm.load() > 0) {
                std::cerr << "  FATAL: Phase 4: " << failed_amm.load()
                          << " AMM contracts not confirmed. Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  Phase 4: all " << kAmmPairs << " AMM contracts deployed OK\n";
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 5: Distribute tokens to users via crossTransfer
        // CrossShardToken._baseInit() already minted 1_000_000 ether to the
        // deployer (tx.origin).  Here we distribute 10000 ether to each of
        // max(2, users/kTokens*2) randomly chosen users, then verify
        // balanceOf > 0 on each user's shard after 60s cross-shard delivery.
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n[Phase 5] Distribute tokens to users...\n";

        // ── Pre-Phase 5: Query on-chain shard/pool for each user ──────────
        // Replace locally-computed shard_id/pool_idx with on-chain values so
        // crossTransfer uses the correct destination parameters.
        {
            std::set<uint32_t> unresolved;
            for (uint32_t i = 0; i < (uint32_t)users8.size(); ++i)
                unresolved.insert(i);

            for (int attempt = 0; !unresolved.empty() && attempt < 600 && !global_stop; ++attempt) {
                if (attempt > 0) {
                    usleep(1000000);
                    if (attempt % 30 == 0)
                        std::cout << "  [Phase5 shard-query] waiting... "
                                  << (users8.size() - unresolved.size())
                                  << "/" << users8.size() << " resolved\n";
                }

                // Group unresolved by current shard_id to batch per shard
                std::unordered_map<uint32_t, std::vector<uint32_t>> shard_idxs;
                for (uint32_t idx : unresolved)
                    shard_idxs[users8[idx].shard_id].push_back(idx);

                for (auto& [shard, idxs] : shard_idxs) {
                    auto ep_it = eps8.find(shard);
                    if (ep_it == eps8.end()) continue;
                    ShardoraSDK qsdk(ep_it->second.ip, ep_it->second.http);
                    std::vector<std::string> addrs;
                    addrs.reserve(idxs.size());
                    for (uint32_t idx : idxs)
                        addrs.push_back(users8[idx].addr_hex);
                    auto qres = qsdk.batchQueryAccounts(addrs);
                    if (!qres.contains("status") || qres["status"] != 0) continue;
                    if (!qres.contains("accounts")) continue;
                    for (uint32_t idx : idxs) {
                        auto& u = users8[idx];
                        auto it = qres["accounts"].find(u.addr_hex);
                        if (it == qres["accounts"].end()) continue;
                        auto& acc = *it;
                        if (acc.contains("pool_index"))
                            u.pool_idx = acc["pool_index"].get<uint32_t>();
                        if (acc.contains("sharding_id"))
                            u.shard_id = acc["sharding_id"].get<uint32_t>();
                        unresolved.erase(idx);
                    }
                }
            }

            if (!unresolved.empty()) {
                std::cerr << "  FATAL: Phase 5: " << unresolved.size()
                          << " users not found on-chain after 600s. Aborting.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  [Phase 5] All " << users8.size()
                      << " users resolved on-chain (shard/pool confirmed)\n";
        }

        // Encode uint256 from a 128-bit value (supports amounts up to 2^128)
        auto encodeUint256u128 = [](__uint128_t val) -> std::string {
            std::string res(64, '0');
            for (int j = 63; j >= 0 && val > (__uint128_t)0; --j) {
                res[j] = "0123456789abcdef"[(uint8_t)(val & 0xF)];
                val >>= 4;
            }
            return res;
        };
        // Encode uint32 as 32-byte ABI word
        auto encodeUint32ABI = [](uint32_t val) -> std::string {
            std::string res(64, '0');
            for (int j = 63; j >= 0 && val > 0u; --j) {
                res[j] = "0123456789abcdef"[val & 0xF];
                val >>= 4;
            }
            return res;
        };

        // ABI selectors
        const std::string kXferSel =
            utils::keccak256Str("crossTransfer(address,uint256,uint32,uint32)").substr(0, 8);
        const std::string kBalOfSel =
            utils::keccak256Str("balanceOf(address)").substr(0, 8);

        const uint64_t kLiqAmt7      = 1'000'000'000'000ULL; // 1T per token for AMM liquidity
        const uint64_t kSwapAmt7     = 1'000'000ULL;          // 1M per swap
        const uint32_t kSwapRounds7  = 5;

        // 10000 ether = 10000 * 10^18 (fits in 128 bits, exceeds uint64_t)
        const __uint128_t kPerAmt5 =
            (__uint128_t)1000000000000000000ULL * 10000ULL;

        // Number of recipients per token: max(2, floor(users/kTokens)*2)
        const uint32_t kRcpt5 =
            std::max(2u, (uint32_t)(users8.size() / kTokens) * 2u);

        // Build per-token recipient lists (random, wraps if users < kRcpt5*kTokens)
        std::vector<std::vector<uint32_t>> rcpt5(kTokens);
        {
            std::vector<uint32_t> idxs(users8.size());
            for (uint32_t i = 0; i < (uint32_t)idxs.size(); ++i) idxs[i] = i;
            for (uint32_t i = (uint32_t)idxs.size(); i > 1u; --i) {
                uint32_t j = common::Random::RandomUint32() % i;
                std::swap(idxs[i - 1], idxs[j]);
            }
            for (uint32_t ti = 0; ti < kTokens; ++ti) {
                rcpt5[ti].reserve(kRcpt5);
                for (uint32_t r = 0; r < kRcpt5; ++r)
                    rcpt5[ti].push_back(
                        idxs[(ti * kRcpt5 + r) % (uint32_t)idxs.size()]);
            }
        }
        std::cout << "  Recipients per token: " << kRcpt5
                  << "  per-user amount: 10000 ether (10^22 wei)\n";

        // ── Send crossTransfer TXs (one thread per token) ─────────────────
        // Step 7 (setGasPrefund) must precede step 8 (callContractWithNonce):
        // step=8 validates against the prepayment account (contract+sender),
        // which is created by step=7.  Nonce for step=8 comes from that account.
        const uint64_t kGasPrefund5 = 2000000000ULL;  // 2B tokens covers many calls
        std::atomic<uint32_t> xok5{0}, xfail5{0};
        {
            std::vector<std::thread> xth5;
            for (uint32_t ti = 0; ti < kTokens && !global_stop; ++ti) {
                xth5.emplace_back([&, ti]() {
                    auto& td = tdeps8[ti];
                    std::string pk_hex = common::Encode::HexEncode(td.prikey);

                    ShardoraSDK dsdk(eps8[td.signer_shard].ip,
                                     eps8[td.signer_shard].http);

                    // step=7: create prepayment account (contract_addr+deployer_addr)
                    auto pfres = dsdk.setGasPrefund(
                        pk_hex, td.contract_addr_hex, kGasPrefund5);
                    if (!pfres.contains("status") || pfres["status"] != 0) {
                        std::cerr << "  [token" << ti << "] setGasPrefund failed: "
                                  << pfres.value("msg", "?") << "\n";
                        xfail5.fetch_add((uint32_t)rcpt5[ti].size());
                        return;
                    }

                    // Wait up to 60s for prepayment account to confirm
                    std::string ppkey = td.contract_addr_hex + td.addr_hex;
                    int64_t ppnonce = -1;
                    for (int pw = 0; pw < 60 && !global_stop; ++pw) {
                        usleep(1000000);
                        ppnonce = dsdk.fetchNonce(ppkey);
                        if (ppnonce >= 0) break;
                    }
                    if (ppnonce < 0) {
                        std::cerr << "  [token" << ti
                                  << "] gas prefund not confirmed after 60s\n";
                        xfail5.fetch_add((uint32_t)rcpt5[ti].size());
                        return;
                    }

                    std::cout << "  [token" << ti << "] contract="
                              << td.contract_addr_hex << " s" << td.signer_shard
                              << "  ppnonce=" << ppnonce
                              << "  → " << rcpt5[ti].size() << " TXs\n";

                    for (uint32_t ri = 0;
                         ri < (uint32_t)rcpt5[ti].size() && !global_stop; ++ri) {
                        auto& u = users8[rcpt5[ti][ri]];
                        // ABI: crossTransfer(address,uint256,uint32,uint32)
                        std::string calldata =
                            kXferSel
                            + encodeAddr32(u.addr_hex)
                            + encodeUint256u128(kPerAmt5)
                            + encodeUint32ABI(u.shard_id)
                            + encodeUint32ABI(u.pool_idx);

                        auto r = dsdk.callContractWithNonce(
                            pk_hex, td.contract_addr_hex,
                            calldata, ppnonce + (int64_t)ri);
                        if (r.contains("status") && r["status"] == 0) {
                            xok5.fetch_add(1);
                        } else {
                            std::cerr << "  [Phase5 xfer] FAIL token" << ti
                                      << " base=" << td.contract_addr_hex
                                      << " user=" << u.addr_hex
                                      << " shard=" << u.shard_id
                                      << " pool=" << u.pool_idx
                                      << " err=" << r.value("msg", "?") << "\n";
                            xfail5.fetch_add(1);
                        }
                    }
                    // Also send kLiqAmt7 to each AMM deployer that uses this token,
                    // directly to the AMM's (shard, pool) shadow so addLiquidity works
                    // without a separate pre-transfer step in Phase 7.
                    // Also send kSwapXferAmt to each user at each AMM shadow so swapAForB
                    // can call transferFrom — CrossShardBase.transferFrom only needs balance.
                    {
                        int64_t amm_nonce = ppnonce + (int64_t)rcpt5[ti].size();
                        // AMM deployer liquidity transfers
                        for (uint32_t k = 0; k < kAmmPairs && !global_stop; ++k) {
                            if (adeps8[k].token_a != ti && adeps8[k].token_b != ti) continue;
                            const auto& ad = adeps8[k];
                            std::string cd = kXferSel
                                + encodeAddr32(ad.addr_hex)
                                + encodeUint256u128((__uint128_t)kLiqAmt7)
                                + encodeUint32ABI(ad.signer_shard)
                                + encodeUint32ABI(ad.deployer_pool);
                            auto ra = dsdk.callContractWithNonce(
                                pk_hex, td.contract_addr_hex, cd, amm_nonce);
                            if (ra.contains("status") && ra["status"] == 0) {
                                xok5.fetch_add(1); ++amm_nonce;
                            } else {
                                std::cerr << "  [Phase5 amm-xfer] FAIL token" << ti
                                          << "→amm" << k
                                          << " err=" << ra.value("msg", "?") << "\n";
                                xfail5.fetch_add(1);
                            }
                        }
                        // Swap-user transfers: each user needs kSwapXferAmt on the AMM shadow
                        const uint64_t kSwapXferAmt = kSwapAmt7 * (uint64_t)(kSwapRounds7 + 2);
                        for (uint32_t k = 0; k < kAmmPairs && !global_stop; ++k) {
                            if (adeps8[k].token_a != ti && adeps8[k].token_b != ti) continue;
                            const auto& ad = adeps8[k];
                            for (uint32_t ri = 0; ri < (uint32_t)rcpt5[ti].size() && !global_stop; ++ri) {
                                const auto& u = users8[rcpt5[ti][ri]];
                                // skip users already on the AMM's shard/pool (own shadow == AMM shadow)
                                if (u.shard_id == ad.signer_shard && u.pool_idx == ad.deployer_pool) continue;
                                std::string cd = kXferSel
                                    + encodeAddr32(u.addr_hex)
                                    + encodeUint256u128((__uint128_t)kSwapXferAmt)
                                    + encodeUint32ABI(ad.signer_shard)
                                    + encodeUint32ABI(ad.deployer_pool);
                                auto ra = dsdk.callContractWithNonce(
                                    pk_hex, td.contract_addr_hex, cd, amm_nonce);
                                if (ra.contains("status") && ra["status"] == 0) {
                                    xok5.fetch_add(1); ++amm_nonce;
                                } else {
                                    std::cerr << "  [Phase5 usr-amm-xfer] FAIL token" << ti
                                              << " usr" << rcpt5[ti][ri] << "→amm" << k
                                              << " err=" << ra.value("msg", "?") << "\n";
                                    xfail5.fetch_add(1);
                                }
                            }
                        }
                    }
                });
            }
            for (auto& t : xth5) t.join();
        }
        std::cout << "  crossTransfer sends: " << xok5.load()
                  << " ok  " << xfail5.load() << " fail\n";
        if (xok5.load() == 0) {
            std::cerr << "  FATAL: Phase 5: no transfers succeeded. Aborting.\n";
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }

        // ── Phase 5 verify: poll every 10s until all balances confirmed ──
        // Build a flat list of (token_idx, user_idx) pairs to check.
        struct P5Item { uint32_t ti; uint32_t ri; };
        std::vector<P5Item> pending5;
        for (uint32_t ti = 0; ti < kTokens; ++ti)
            for (uint32_t ri = 0; ri < (uint32_t)rcpt5[ti].size(); ++ri)
                pending5.push_back({ti, ri});
        const uint32_t total5 = (uint32_t)pending5.size();

        std::cout << "\n[Phase 5 verify] Polling balanceOf (10s initial wait, "
                  << "max 240s, " << total5 << " checks)...\n";

        // Initial wait — give cross-shard delivery a head start.
        for (int ws = 0; ws < 10 && !global_stop; ++ws) usleep(1000000);
        if (global_stop) { transport::TcpTransport::Instance()->Stop(); return 1; }

        uint32_t bok5 = 0;
        auto p5_start = std::chrono::steady_clock::now();
        const int kP5MaxSec = 240;

        while (!pending5.empty() && !global_stop) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - p5_start).count();
            if (elapsed >= kP5MaxSec) {
                std::cerr << "  TIMEOUT: Phase 5: " << pending5.size()
                          << "/" << total5 << " balances unconfirmed after "
                          << kP5MaxSec << "s.\n";
                break;
            }

            std::vector<P5Item> still_pending;
            std::mutex rmx5;
            std::atomic<uint32_t> round_ok{0};

            std::vector<std::thread> bth5;
            for (auto& item : pending5) {
                bth5.emplace_back([&, item]() {
                    uint32_t ti = item.ti;
                    uint32_t ri = item.ri;
                    auto& td = tdeps8[ti];
                    auto& u  = users8[rcpt5[ti][ri]];
                    std::string pk_hex = common::Encode::HexEncode(td.prikey);
                    std::string qip  = eps8[u.shard_id].ip;
                    uint16_t   qhttp = eps8[u.shard_id].http;

                    std::string root_raw = common::Encode::HexDecode(td.contract_addr_hex);
                    evmc::address root_evmc{};
                    std::memcpy(root_evmc.bytes, root_raw.data(), 20);
                    evmc::address shadow_evmc = shardoravm::DeriveShardAddress(
                        root_evmc, u.shard_id, u.pool_idx);
                    std::string shadow_hex = common::Encode::HexEncode(
                        std::string(reinterpret_cast<const char*>(shadow_evmc.bytes), 20));

                    ShardoraSDK qsdk(qip, qhttp);
                    auto res = qsdk.queryFunctionSolidity(
                        pk_hex, shadow_hex,
                        "balanceOf", {"address"}, {u.addr_hex}, {"uint256"});
                    std::string rv = (res.contains("status") && res["status"] == 0)
                                     ? res.value("return_value", "") : "";

                    bool found = false;
                    for (char c : rv) if (c != '0') { found = true; break; }
                    if (found) {
                        round_ok.fetch_add(1);
                    } else {
                        std::lock_guard<std::mutex> lk(rmx5);
                        still_pending.push_back(item);
                    }
                });
            }
            for (auto& t : bth5) t.join();

            bok5 += round_ok.load();
            pending5 = std::move(still_pending);

            auto now_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - p5_start).count();
            std::cout << "  [Phase5 verify] " << bok5 << "/" << total5
                      << " confirmed, " << pending5.size() << " pending ["
                      << now_elapsed << "s]\n";

            if (!pending5.empty() && !global_stop) {
                for (int ws = 0; ws < 10 && !global_stop; ++ws) usleep(1000000);
            }
        }
        if (global_stop) { transport::TcpTransport::Instance()->Stop(); return 1; }

        if (!pending5.empty()) {
            std::cerr << "  FATAL: Phase 5: " << pending5.size()
                      << "/" << total5 << " token balances unconfirmed.\n";
            transport::TcpTransport::Instance()->Stop();
            return 1;
        }
        std::cout << "  Phase 5: all " << total5
                  << " user token balances confirmed OK\n";

        // Phase 5 AMM deployer verify: poll until each AMM deployer has kLiqAmt7
        // of both its tokens on the AMM's shadow (shard, pool).
        if (kAmmPairs > 0) {
            std::cout << "\n[Phase 5 AMM verify] Polling AMM deployer token balances (max 120s)...\n";
            auto hx2u64_5 = [](const std::string& h) -> uint64_t {
                uint64_t v = 0;
                size_t start = (h.size() > 16) ? h.size() - 16 : 0;
                for (size_t i = start; i < h.size(); ++i) {
                    char c = h[i];
                    v = v * 16 + (c>='0'&&c<='9' ? c-'0' :
                                  c>='a'&&c<='f' ? c-'a'+10 :
                                  c>='A'&&c<='F' ? c-'A'+10 : 0);
                }
                return v;
            };
            struct AmmBal5 { bool aOk = false, bOk = false; };
            std::vector<AmmBal5> amm_bal5(kAmmPairs);
            for (int rd = 0; rd < 120 && !global_stop; ++rd) {
                uint32_t nc = 0;
                for (uint32_t k = 0; k < kAmmPairs; ++k) {
                    if (amm_bal5[k].aOk && amm_bal5[k].bOk) { ++nc; continue; }
                    const auto& ad = adeps8[k];
                    ShardoraClient qc(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                    if (!amm_bal5[k].aOk) {
                        const auto& tdA = tdeps8[ad.token_a];
                        std::string rs = qc.queryContract(
                            common::Encode::HexEncode(tdA.prikey),
                            ad.token_a_shadow_hex, kBalOfSel + encodeAddr32(ad.addr_hex));
                        if (rs.size() >= 64 && hx2u64_5(rs.substr(0, 64)) >= kLiqAmt7)
                            amm_bal5[k].aOk = true;
                    }
                    if (!amm_bal5[k].bOk) {
                        const auto& tdB = tdeps8[ad.token_b];
                        std::string rs = qc.queryContract(
                            common::Encode::HexEncode(tdB.prikey),
                            ad.token_b_shadow_hex, kBalOfSel + encodeAddr32(ad.addr_hex));
                        if (rs.size() >= 64 && hx2u64_5(rs.substr(0, 64)) >= kLiqAmt7)
                            amm_bal5[k].bOk = true;
                    }
                    if (amm_bal5[k].aOk && amm_bal5[k].bOk) ++nc;
                }
                std::cout << "  [Phase5 AMM verify " << rd << "s] " << nc << "/" << kAmmPairs
                          << " AMM deployers ready\n";
                if (nc == kAmmPairs) break;
                usleep(1000000);
            }
            uint32_t amm_ok5 = 0;
            for (const auto& b : amm_bal5) if (b.aOk && b.bOk) ++amm_ok5;
            if (amm_ok5 < kAmmPairs) {
                std::cerr << "  FATAL: Phase 5 AMM: only " << amm_ok5 << "/" << kAmmPairs
                          << " AMM deployers have both token balances after 120s.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
            std::cout << "  Phase 5 AMM: all " << kAmmPairs
                      << " AMM deployers confirmed OK\n";
        }

        // ── Phase 6: Set prefund for token holders on AMM pool contracts ──
        // For each AMM pool, find users that hold either of its two tokens
        // (from rcpt5), then create a prepayment account (amm_contract +
        // user_addr) on the AMM's shard so they can later call swapAForB /
        // swapBForA without being blocked by a missing prepayment account.
        // ─────────────────────────────────────────────────────────────────
        if (kAmmPairs > 0) {
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "[Phase 6] Set AMM prefund for token holders\n";
        std::cout << std::string(70, '-') << "\n";

        const uint64_t kAmmPrefund6 = 2000000000ULL;  // 2B gas covers many swaps

        // Build flat list of (user_idx, amm_idx) ops
        struct AmmPfItem6 { uint32_t user_idx; uint32_t amm_idx; };
        std::vector<AmmPfItem6> amm_pf6;
        for (uint32_t k = 0; k < kAmmPairs; ++k) {
            std::set<uint32_t> holders;
            for (uint32_t ui : rcpt5[adeps8[k].token_a]) holders.insert(ui);
            for (uint32_t ui : rcpt5[adeps8[k].token_b]) holders.insert(ui);
            for (uint32_t ui : holders) amm_pf6.push_back({ui, k});
        }
        std::cout << "  Prefund ops: " << amm_pf6.size()
                  << "  (" << kAmmPairs << " AMM pools)\n";

        // Send prefunds — one thread per USER so nonces are sequential.
        // Each user may appear in multiple AMM pools (holding both tokens).
        // Running one thread per AMM caused concurrent sends for the same user
        // with the same fetched nonce → second TX rejected as nonce-reuse.
        std::atomic<uint32_t> apf6_ok{0}, apf6_fail{0};
        {
            // Group amm_pf6 entries by user index
            std::unordered_map<uint32_t, std::vector<uint32_t>> user_to_amms;
            for (uint32_t i = 0; i < (uint32_t)amm_pf6.size(); ++i)
                user_to_amms[amm_pf6[i].user_idx].push_back(i);

            std::vector<std::thread> apf6_threads;
            for (auto& [ui, pf_idxs] : user_to_amms) {
                apf6_threads.emplace_back([&, ui, pf_idxs]() {
                    if (global_stop) return;
                    uint32_t user_shard = users8[ui].shard_id;
                    auto ep_it = eps8.find(user_shard);
                    if (ep_it == eps8.end()) {
                        apf6_fail.fetch_add((uint32_t)pf_idxs.size());
                        return;
                    }
                    ShardoraSDK sdk(ep_it->second.ip, ep_it->second.http);
                    std::string pk_hex = common::Encode::HexEncode(users8[ui].prikey);
                    std::string addr_hex = users8[ui].addr_hex;
                    // Fetch nonce once for this user; subsequent TXs increment locally.
                    int64_t cur_nonce = sdk.fetchNonce(addr_hex);
                    if (cur_nonce < 0) {
                        apf6_fail.fetch_add((uint32_t)pf_idxs.size());
                        return;
                    }
                    for (uint32_t idx : pf_idxs) {
                        if (global_stop) break;
                        auto& amm6 = adeps8[amm_pf6[idx].amm_idx];
                        auto r = sdk.setGasPrefundWithNonce(
                            pk_hex, amm6.contract_addr_hex, kAmmPrefund6, cur_nonce);
                        if (r.contains("status") && r["status"] == 0) {
                            apf6_ok.fetch_add(1);
                            ++cur_nonce;  // advance for next AMM
                        } else {
                            apf6_fail.fetch_add(1);
                        }
                    }
                });
            }
            for (auto& th : apf6_threads) th.join();
        }
        std::cout << "  Sends: " << apf6_ok.load()
                  << " ok  " << apf6_fail.load() << " fail\n";

        // Wait for prefund consensus
        std::cout << "  Waiting 10s for prefund consensus...\n";
        for (int ws = 0; ws < 10 && !global_stop; ++ws) usleep(1000000);
        if (global_stop) { transport::TcpTransport::Instance()->Stop(); return 1; }

        // ── Phase 6 verify: poll until all prepayment accounts appear ────
        std::cout << "\n[Phase 6 verify] Polling AMM prepayment accounts (max 60s, "
                  << amm_pf6.size() << " checks)...\n";

        // Prepayment key = amm_contract_addr + user_addr (both hex, 40 chars each)
        std::vector<bool> apf6_confirmed(amm_pf6.size(), false);
        std::vector<uint32_t> apf6_pending;
        for (uint32_t i = 0; i < (uint32_t)amm_pf6.size(); ++i)
            apf6_pending.push_back(i);

        auto p6_start = std::chrono::steady_clock::now();
        const int kP6MaxSec = 60;

        for (int round = 0; !apf6_pending.empty() && !global_stop; ++round) {
            int elapsed6 = (int)std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - p6_start).count();
            if (elapsed6 >= kP6MaxSec) {
                std::cerr << "  TIMEOUT: Phase 6: " << apf6_pending.size()
                          << "/" << amm_pf6.size()
                          << " AMM prefunds unconfirmed after " << kP6MaxSec << "s\n";
                // Print details of up to 3 unconfirmed items for diagnosis
                uint32_t diag_n = std::min((uint32_t)apf6_pending.size(), 3u);
                for (uint32_t di = 0; di < diag_n; ++di) {
                    uint32_t idx = apf6_pending[di];
                    auto& it6  = amm_pf6[idx];
                    auto& amm6 = adeps8[it6.amm_idx];
                    auto& u6   = users8[it6.user_idx];
                    std::string ppkey = amm6.contract_addr_hex + u6.addr_hex;
                    std::cerr << "  [unconfirmed " << di << "]"
                              << " user=" << u6.addr_hex
                              << " s" << u6.shard_id << " pool=" << u6.pool_idx
                              << " amm=" << amm6.contract_addr_hex
                              << " s" << amm6.signer_shard << " pool=" << amm6.deployer_pool
                              << " prepay_key=" << ppkey << "\n";
                }
                break;
            }

            // Group pending by AMM shard for batch query
            std::unordered_map<uint32_t, std::vector<uint32_t>> shard_pend6;
            for (uint32_t idx : apf6_pending)
                shard_pend6[adeps8[amm_pf6[idx].amm_idx].signer_shard].push_back(idx);

            std::vector<uint32_t> next_pend6;
            uint32_t round6_ok = 0;

            for (auto& [ashard, idxs6] : shard_pend6) {
                if (global_stop) break;
                auto ep6 = eps8.find(ashard);
                if (ep6 == eps8.end()) {
                    for (auto i2 : idxs6) next_pend6.push_back(i2);
                    continue;
                }
                ShardoraSDK vsdk6(ep6->second.ip, ep6->second.http);

                std::vector<std::string> bkeys;
                std::vector<uint32_t>   bidxs;
                const uint32_t kBatch6 = 50;
                for (uint32_t j = 0; j <= (uint32_t)idxs6.size() && !global_stop; ++j) {
                    bool flush = (j == (uint32_t)idxs6.size()) ||
                                 (bkeys.size() >= kBatch6);
                    if (j < (uint32_t)idxs6.size()) {
                        uint32_t idx = idxs6[j];
                        bkeys.push_back(adeps8[amm_pf6[idx].amm_idx].contract_addr_hex
                                        + users8[amm_pf6[idx].user_idx].addr_hex);
                        bidxs.push_back(idx);
                    }
                    if (!flush || bkeys.empty()) continue;
                    auto qr6 = vsdk6.batchQueryAccounts(bkeys);
                    if (qr6.contains("status") && qr6["status"] == 0
                            && qr6.contains("accounts")) {
                        for (uint32_t bi = 0; bi < bidxs.size(); ++bi) {
                            if (qr6["accounts"].contains(bkeys[bi])) {
                                apf6_confirmed[bidxs[bi]] = true;
                                ++round6_ok;
                            } else {
                                next_pend6.push_back(bidxs[bi]);
                            }
                        }
                    } else {
                        for (auto i2 : bidxs) next_pend6.push_back(i2);
                    }
                    bkeys.clear(); bidxs.clear();
                }
            }

            apf6_pending = std::move(next_pend6);
            uint32_t total6_confirmed = 0;
            for (bool b : apf6_confirmed) if (b) ++total6_confirmed;

            std::cout << "  [P6 round " << (round + 1) << "] +" << round6_ok
                      << "  confirmed: " << total6_confirmed
                      << "/" << amm_pf6.size()
                      << "  pending: " << apf6_pending.size()
                      << "  [" << elapsed6 << "s]\n";

            if (apf6_pending.empty()) break;

            for (int ws = 0; ws < 10 && !global_stop; ++ws) usleep(1000000);
        }
        if (global_stop) { transport::TcpTransport::Instance()->Stop(); return 1; }

        uint32_t total6_ok = 0;
        for (bool b : apf6_confirmed) if (b) ++total6_ok;
        if (total6_ok < (uint32_t)amm_pf6.size()) {
            std::cerr << "  WARNING: Phase 6: only " << total6_ok << "/"
                      << amm_pf6.size() << " AMM prefund accounts confirmed\n";
            // Print details of up to 3 still-pending items
            uint32_t diag_n = std::min((uint32_t)apf6_pending.size(), 3u);
            for (uint32_t di = 0; di < diag_n; ++di) {
                uint32_t idx = apf6_pending[di];
                auto& it6  = amm_pf6[idx];
                auto& amm6 = adeps8[it6.amm_idx];
                auto& u6   = users8[it6.user_idx];
                std::cerr << "  [unconfirmed " << di << "]"
                          << " user=" << u6.addr_hex
                          << " s" << u6.shard_id << " pool=" << u6.pool_idx
                          << " amm=" << amm6.contract_addr_hex
                          << " s" << amm6.signer_shard << " pool=" << amm6.deployer_pool
                          << " prepay=" << amm6.contract_addr_hex + u6.addr_hex << "\n";
            }
        } else {
            std::cout << "  Phase 6: all " << total6_ok
                      << " AMM prefund accounts confirmed OK\n";
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 7a: transfer liquidity tokens to AMM deployers + set deployer
        //           gas-prefund on AMM contracts (two sub-steps in parallel)
        // Phase 7b: AMM deployers call addLiquidity
        // Phase 7c: Swap stress test (TCP fast path, max concurrency)
        // Phase 7d: Verify reserves and user balances
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "  [Phase 7a] Transfer tokens to deployers + deployer prefund\n";
        std::cout << std::string(70, '-') << "\n";

        const uint64_t kAdepPrefund7 = 5'000'000'000ULL;      // 5B gas for deployer

        // ABI selectors
        const std::string kSel7AddLiq   = utils::keccak256Str("addLiquidity(uint256,uint256)").substr(0, 8);
        const std::string kSel7SwapAB   = utils::keccak256Str("swapAForB(uint256,uint256)").substr(0, 8);
        const std::string kSel7SwapBA   = utils::keccak256Str("swapBForA(uint256,uint256)").substr(0, 8);
        const std::string kSel7Reserves = utils::keccak256Str("getReserves()").substr(0, 8);

        // ABI: selector + two uint256 (fits in uint64)
        auto enc2u64 = [&](const std::string& sel, uint64_t a, uint64_t b) -> std::string {
            return sel + encodeUint256u128((__uint128_t)a) + encodeUint256u128((__uint128_t)b);
        };

        // Build per-token set of holders (for fast membership test later)
        std::vector<std::unordered_set<uint32_t>> holder_set(kTokens);
        for (uint32_t ti = 0; ti < kTokens; ++ti)
            for (uint32_t ui : rcpt5[ti]) holder_set[ti].insert(ui);

        // Build contract → shard map for fast lookup in swap worker
        std::unordered_map<std::string, uint32_t> amm_contract_shard;
        for (const auto& ad : adeps8)
            amm_contract_shard[ad.contract_addr_hex] = ad.signer_shard;

        // AMM deployers setGasPrefund on AMM contracts
        std::atomic<uint32_t> p7a_pf_ok{0}, p7a_pf_fail{0};
        std::vector<std::thread> th7a_pf;
        for (uint32_t k = 0; k < kAmmPairs && !global_stop; ++k) {
            th7a_pf.emplace_back([&, k]() {
                const auto& ad = adeps8[k];
                std::string pk_hex = common::Encode::HexEncode(ad.prikey);
                ShardoraSDK dsdk(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                int64_t nonce = dsdk.fetchNonce(ad.addr_hex);
                if (nonce < 0) { ++p7a_pf_fail; return; }
                auto r = dsdk.setGasPrefundWithNonce(pk_hex, ad.contract_addr_hex, kAdepPrefund7, nonce);
                if (r.contains("status") && r["status"] == 0) ++p7a_pf_ok;
                else { ++p7a_pf_fail; std::cerr << "  [7a-pf amm" << k << "] " << r.dump() << "\n"; }
            });
        }
        for (auto& t : th7a_pf) t.join();
        std::cout << "  [7a] deployer prefund: " << p7a_pf_ok << " ok / " << (p7a_pf_ok+p7a_pf_fail) << "\n";

        // Poll until all deployer prepay accounts confirmed (max 60s)
        std::cout << "  [7a] Polling deployer prepay accounts (max 60s)...\n";
        {
            std::vector<bool> dpf_ok(kAmmPairs, false);
            for (int rd = 0; rd < 60 && !global_stop; ++rd) {
                uint32_t nc = 0;
                for (uint32_t k = 0; k < kAmmPairs; ++k) {
                    if (dpf_ok[k]) { ++nc; continue; }
                    const auto& ad = adeps8[k];
                    ShardoraSDK q(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                    if (q.fetchNonce(ad.contract_addr_hex + ad.addr_hex) >= 0) { dpf_ok[k] = true; ++nc; }
                }
                std::cout << "  [7a " << rd << "s] " << nc << "/" << kAmmPairs << " deployer prepay OK\n";
                if (nc == kAmmPairs) break;
                usleep(1000000);
            }
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 7b: AMM deployers call addLiquidity
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "  [Phase 7b] addLiquidity\n";
        std::cout << std::string(70, '-') << "\n";

        // Hex → uint64_t (take last 16 hex chars = 8 bytes)
        auto hex2u64 = [](const std::string& h) -> uint64_t {
            uint64_t v = 0;
            size_t start = (h.size() > 16) ? h.size() - 16 : 0;
            for (size_t i = start; i < h.size(); ++i) {
                char c = h[i];
                v = v * 16 + (c >= '0' && c <= '9' ? c - '0' :
                              c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                              c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
            }
            return v;
        };

        // Now send addLiquidity for each AMM deployer
        {
            std::atomic<uint32_t> liq_ok{0}, liq_fail{0};
            std::vector<std::thread> th7liq;
            for (uint32_t k = 0; k < kAmmPairs && !global_stop; ++k) {
                th7liq.emplace_back([&, k]() {
                    const auto& ad = adeps8[k];
                    std::string pk_hex = common::Encode::HexEncode(ad.prikey);
                    ShardoraSDK dsdk(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                    // Nonce from prepay account: amm_addr + adep_addr
                    int64_t nonce = dsdk.fetchNonce(ad.contract_addr_hex + ad.addr_hex);
                    if (nonce < 0) { ++liq_fail; std::cerr << "  [7b amm" << k << "] nonce fail\n"; return; }
                    auto r = dsdk.callContractWithNonce(pk_hex, ad.contract_addr_hex,
                                enc2u64(kSel7AddLiq, kLiqAmt7, kLiqAmt7), nonce);
                    if (r.contains("status") && r["status"] == 0) ++liq_ok;
                    else { ++liq_fail; std::cerr << "  [7b amm" << k << "] " << r.dump() << "\n"; }
                });
            }
            for (auto& t : th7liq) t.join();
            std::cout << "  [7b] addLiquidity: " << liq_ok << " ok / " << (liq_ok+liq_fail) << "\n";
        }

        // Poll AMM reserves (max 60s)
        std::cout << "  [7b] Polling AMM reserves (max 60s)...\n";
        {
            std::vector<bool> res_ready(kAmmPairs, false);
            for (int rd = 0; rd < 60 && !global_stop; ++rd) {
                uint32_t nc = 0;
                for (uint32_t k = 0; k < kAmmPairs; ++k) {
                    if (res_ready[k]) { ++nc; continue; }
                    const auto& ad = adeps8[k];
                    ShardoraClient q(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
                    std::string rs = q.queryContract(common::Encode::HexEncode(ad.prikey),
                                                     ad.contract_addr_hex, kSel7Reserves);
                    // Valid ABI response = 128 hex chars; reserveA (first 64) must be non-zero.
                    // Error JSON responses (contain '{', '"') must not be treated as ready.
                    bool valid_abi = rs.size() >= 128;
                    for (size_t ci = 0; ci < 128 && valid_abi; ++ci) {
                        char c = rs[ci];
                        valid_abi = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                    }
                    bool rA_nonzero = false;
                    if (valid_abi)
                        for (size_t ci = 0; ci < 64; ++ci)
                            if (rs[ci] != '0') { rA_nonzero = true; break; }
                    if (rA_nonzero) { res_ready[k] = true; ++nc; }
                }
                std::cout << "  [7b " << rd << "s] " << nc << "/" << kAmmPairs << " pools have reserves\n";
                if (nc == kAmmPairs) break;
                usleep(1000000);
            }

            // Count how many reserves are ready after polling
            uint32_t ready_count = 0;
            for (bool b : res_ready) if (b) ++ready_count;
            if (ready_count < kAmmPairs) {
                std::cerr << "  FATAL: Phase 7b: only " << ready_count << "/" << kAmmPairs
                          << " AMM pools have non-zero reserves after 60s.\n"
                          << "  addLiquidity is reverting — check tokenA/B shadow addresses and deployer balances above.\n";
                transport::TcpTransport::Instance()->Stop();
                return 1;
            }
        }

        // ─────────────────────────────────────────────────────────────────
        // Phase 7c: Swap stress test (TCP fast path, maximum concurrency)
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "  [Phase 7c] Swap stress test  rounds=" << kSwapRounds7
                  << "  swapAmt=" << kSwapAmt7 << "\n";
        std::cout << std::string(70, '-') << "\n";

        // ContractCallOp / NoncedCallGroup pattern (same as Mode 5, local to Phase 7)
        struct SwapOp7  { std::string prikey_hex, contract_addr, caller_addr, input_data; };
        struct SwapGrp7 {
            std::string prikey_hex, caller_addr, contract_addr;
            uint32_t    amm_shard;
            std::vector<std::string> inputs;
        };

        // Build flat swap op list from amm_pf6
        std::vector<SwapOp7> swap_ops7;
        swap_ops7.reserve(amm_pf6.size() * kSwapRounds7);
        for (const auto& pf : amm_pf6) {
            const auto& u  = users8[pf.user_idx];
            const auto& ad = adeps8[pf.amm_idx];
            bool has_a = holder_set[ad.token_a].count(pf.user_idx);
            bool has_b = holder_set[ad.token_b].count(pf.user_idx);
            std::string pk_hex = common::Encode::HexEncode(u.prikey);
            for (uint32_t r = 0; r < kSwapRounds7; ++r) {
                // Alternate direction per round for balance stability
                bool go_ab = has_a && (!has_b || (r % 2 == 0));
                std::string inp = go_ab
                    ? enc2u64(kSel7SwapAB, kSwapAmt7, 0)
                    : enc2u64(kSel7SwapBA, kSwapAmt7, 0);
                if (has_a || has_b)
                    swap_ops7.push_back({pk_hex, ad.contract_addr_hex, u.addr_hex, inp});
            }
        }
        std::cout << "  Total swap ops:   " << swap_ops7.size() << "\n";

        // group_by_prepay: group by (contract_addr + caller_addr)
        std::vector<SwapGrp7> swap_grps7;
        {
            std::unordered_map<std::string, uint32_t> key2idx;
            for (const auto& op : swap_ops7) {
                std::string key = op.contract_addr + op.caller_addr;
                auto it = key2idx.find(key);
                if (it == key2idx.end()) {
                    uint32_t shard = amm_contract_shard.count(op.contract_addr)
                                     ? amm_contract_shard[op.contract_addr] : (uint32_t)shardnum;
                    key2idx[key] = (uint32_t)swap_grps7.size();
                    swap_grps7.push_back({op.prikey_hex, op.caller_addr, op.contract_addr, shard, {}});
                    it = key2idx.find(key);
                }
                swap_grps7[it->second].inputs.push_back(op.input_data);
            }
        }
        std::cout << "  Swap groups:      " << swap_grps7.size() << "\n";

        // TCP sender thread (same pattern as Mode 5)
        struct TcpItem7 { transport::MessagePtr msg; std::string ip; uint16_t port; };
        std::queue<TcpItem7>     tcp_q7;
        std::mutex               tcp_mtx7;
        std::condition_variable  tcp_cv7;
        std::atomic<bool>        tcp_stop7{false};
        std::atomic<uint64_t>    tcp_sent7{0};

        std::thread tcp_sender7([&]() {
            std::vector<TcpItem7> batch;
            batch.reserve(4096);
            while (!tcp_stop7.load()) {
                {
                    std::unique_lock<std::mutex> lk(tcp_mtx7);
                    tcp_cv7.wait_for(lk, std::chrono::milliseconds(1),
                        [&]{ return !tcp_q7.empty() || tcp_stop7.load(); });
                    while (!tcp_q7.empty()) { batch.push_back(std::move(tcp_q7.front())); tcp_q7.pop(); }
                }
                for (auto& item : batch) {
                    transport::TcpTransport::Instance()->Send(item.ip, item.port, item.msg->header);
                    ++tcp_sent7;
                }
                batch.clear();
            }
            std::lock_guard<std::mutex> lk(tcp_mtx7);
            while (!tcp_q7.empty()) {
                auto item = std::move(tcp_q7.front()); tcp_q7.pop();
                transport::TcpTransport::Instance()->Send(item.ip, item.port, item.msg->header);
                ++tcp_sent7;
            }
        });

        auto tcp_enq7 = [&](transport::MessagePtr msg, const std::string& ip, uint16_t port) -> bool {
            if (!msg) return false;
            { std::lock_guard<std::mutex> lk(tcp_mtx7); tcp_q7.push({std::move(msg), ip, port}); }
            tcp_cv7.notify_one();
            return true;
        };

        // Worker thread pool: one slice of groups per thread
        std::atomic<uint64_t> swap_ok7{0}, swap_fail7{0};
        auto swap_start7 = std::chrono::steady_clock::now();
        {
            uint32_t nt = std::min((uint32_t)common::kMaxThreadCount, (uint32_t)swap_grps7.size());
            if (nt == 0) nt = 1;
            uint32_t gpp = (uint32_t)swap_grps7.size() / nt;
            std::vector<std::thread> workers;
            for (uint32_t t = 0; t < nt; ++t) {
                uint32_t s = t * gpp;
                uint32_t e = (t == nt - 1) ? (uint32_t)swap_grps7.size() : s + gpp;
                workers.emplace_back([&, s, e]() {
                    std::unordered_map<std::string, std::shared_ptr<security::Security>> sec_cache;
                    std::unordered_map<uint32_t, std::shared_ptr<ShardoraSDK>>          sdk_cache;
                    for (uint32_t gi = s; gi < e && !global_stop; ++gi) {
                        auto& grp = swap_grps7[gi];
                        // Cache security object
                        auto& sec = sec_cache[grp.prikey_hex];
                        if (!sec) {
                            sec = std::make_shared<security::Ecdsa>();
                            sec->SetPrivateKey(common::Encode::HexDecode(grp.prikey_hex));
                        }
                        // Cache SDK per shard
                        auto& sdk = sdk_cache[grp.amm_shard];
                        if (!sdk) {
                            auto ep = eps8.find(grp.amm_shard);
                            if (ep == eps8.end()) { swap_fail7 += grp.inputs.size(); continue; }
                            sdk = std::make_shared<ShardoraSDK>(ep->second.ip, ep->second.http);
                        }
                        // Fetch prepay nonce once per group (amm_addr + user_addr)
                        std::string ppkey = grp.contract_addr + grp.caller_addr;
                        int64_t nonce = -1;
                        for (int retry = 0; retry < 3 && nonce < 0; ++retry) {
                            nonce = sdk->fetchNonce(ppkey);
                            if (nonce < 0 && retry < 2) usleep(100000);
                        }
                        if (nonce < 0) nonce = 0;
                        // Lookup dest TCP port
                        auto ep = eps8.find(grp.amm_shard);
                        uint16_t tcp_port = (ep != eps8.end()) ? ep->second.http - 10000 : 10001;
                        std::string dest_ip = (ep != eps8.end()) ? ep->second.ip : global_chain_node_ip;
                        // Send all TXs in this group, incrementing nonce locally
                        for (const auto& inp : grp.inputs) {
                            if (global_stop) break;
                            auto tx = CreateTransactionWithAttr(sec, ++nonce,
                                grp.prikey_hex,
                                common::Encode::HexDecode(grp.contract_addr),
                                "call", inp, 0, 5000000, 1, (int32_t)grp.amm_shard);
                            if (tcp_enq7(tx, dest_ip, tcp_port)) ++swap_ok7;
                            else ++swap_fail7;
                        }
                    }
                });
            }
            // Progress monitor
            std::thread prog7([&]() {
                uint64_t total = swap_ops7.size();
                while (swap_ok7.load() + swap_fail7.load() < total && !global_stop) {
                    usleep(2000000);
                    auto el = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - swap_start7).count();
                    std::cout << "  [7c " << el << "s] sent=" << (swap_ok7+swap_fail7)
                              << "/" << total << "  tcp=" << tcp_sent7 << "\n";
                }
            });
            for (auto& w : workers) w.join();
            prog7.join();
        }
        // Drain TCP sender
        tcp_stop7 = true;
        tcp_cv7.notify_all();
        tcp_sender7.join();

        auto swap_elapsed7 = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - swap_start7).count();
        std::cout << "  [7c] sent=" << swap_ok7 << " fail=" << swap_fail7
                  << "  tcp=" << tcp_sent7 << "  elapsed=" << swap_elapsed7 << "ms"
                  << "  tps=" << (tcp_sent7.load() * 1000 / std::max(1LL, (long long)swap_elapsed7)) << "\n";

        // ─────────────────────────────────────────────────────────────────
        // Phase 7d: Verify correctness
        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "  [Phase 7d] Verify (30s settlement wait)\n";
        std::cout << std::string(70, '-') << "\n";
        for (int w7 = 0; w7 < 30 && !global_stop; ++w7) {
            usleep(1000000);
            if (w7 % 10 == 9) std::cout << "  [" << (w7+1) << "s] waiting...\n";
        }

        // Check AMM reserves
        std::cout << "  AMM reserves after swaps:\n";
        uint32_t pools_swapped = 0;
        for (uint32_t k = 0; k < kAmmPairs; ++k) {
            const auto& ad = adeps8[k];
            ShardoraClient q(eps8[ad.signer_shard].ip, eps8[ad.signer_shard].http);
            std::string rs = q.queryContract(common::Encode::HexEncode(ad.prikey),
                                             ad.contract_addr_hex, kSel7Reserves);
            uint64_t rA = 0, rB = 0;
            if (rs.size() >= 128) { rA = hex2u64(rs.substr(0, 64)); rB = hex2u64(rs.substr(64, 64)); }
            bool liq_added = (rA > 0 || rB > 0);
            bool swapped   = liq_added && (rA != kLiqAmt7 || rB != kLiqAmt7);
            if (swapped) ++pools_swapped;
            std::string res_status = !liq_added ? "  ✗ no liquidity (addLiquidity failed)"
                                   : swapped    ? "  ✓ swaps occurred"
                                                : "  – no swaps";
            std::cout << "    [amm" << k << "] s" << ad.signer_shard
                      << " rA=" << rA << " rB=" << rB
                      << res_status << "\n";
        }

        // Check token balances for first 3 users per AMM (sample)
        std::cout << "  Sample user token balances:\n";
        uint32_t checked = 0;
        for (const auto& pf : amm_pf6) {
            if (checked >= 3 * kAmmPairs) break;
            const auto& u  = users8[pf.user_idx];
            const auto& ad = adeps8[pf.amm_idx];
            uint32_t ti = ad.token_a;
            const auto& td = tdeps8[ti];
            // User balance is on the SHADOW contract on the user's shard, not the base contract
            std::string root_raw = common::Encode::HexDecode(td.contract_addr_hex);
            evmc::address root_evmc{};
            std::memcpy(root_evmc.bytes, root_raw.data(), 20);
            evmc::address shadow_evmc = shardoravm::DeriveShardAddress(
                root_evmc, u.shard_id, u.pool_idx);
            std::string shadow_hex = common::Encode::HexEncode(
                std::string(reinterpret_cast<const char*>(shadow_evmc.bytes), 20));
            ShardoraClient q(eps8[u.shard_id].ip, eps8[u.shard_id].http);
            std::string qdata = kBalOfSel + encodeAddr32(u.addr_hex);
            std::string rs = q.queryContract(common::Encode::HexEncode(td.prikey),
                                             shadow_hex, qdata);
            uint64_t bal = rs.size() >= 64 ? hex2u64(rs.substr(0, 64)) : 0;
            std::cout << "    [amm" << pf.amm_idx << " user=" << u.addr_hex.substr(0,8)
                      << "...] token" << ti << " bal=" << bal << "\n";
            ++checked;
        }

        std::cout << "\n  Phase 7 summary:\n";
        std::cout << "    Liquidity:   " << kLiqAmt7 << " per token per pool\n";
        std::cout << "    Swap TXs:    " << tcp_sent7 << " / " << swap_ops7.size() << "\n";
        std::cout << "    TPS:         " << (tcp_sent7.load() * 1000 / std::max(1LL, (long long)swap_elapsed7)) << "\n";
        std::cout << "    Pools with swaps: " << pools_swapped << "/" << kAmmPairs << "\n";

        } // end if (kAmmPairs > 0)

        // ─────────────────────────────────────────────────────────────────
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "  Mode 8 Complete (Phase 0-7)\n";
        std::cout << "  Users:           " << users8.size() << "\n";
        std::cout << "  Token contracts: " << kTokens << "\n";
        for (uint32_t i = 0; i < kTokens; ++i)
            std::cout << "    [token" << i << "] " << tdeps8[i].contract_addr_hex
                      << " s" << tdeps8[i].contract_shard << "\n";
        std::cout << "  AMM contracts:   " << kAmmPairs << "\n";
        for (uint32_t k = 0; k < kAmmPairs; ++k)
            std::cout << "    [amm" << k << "] " << adeps8[k].contract_addr_hex
                      << "  pair=(token" << adeps8[k].token_a
                      << ",token" << adeps8[k].token_b << ")\n";
        std::cout << std::string(70, '=') << "\n";

        transport::TcpTransport::Instance()->Stop();
        return 0;
    }

    return 0;
}
