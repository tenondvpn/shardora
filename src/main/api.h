/**
 * Shardora C++ SDK & Server Implementation
 * Replaces Python Django logic with high-performance C++.
 */

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>
#include <memory>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <random>
#include <charconv>
#include <system_error>
// ==========================================
// External Dependencies
// ==========================================
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>

// Cryptography Headers
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <openssl/evp.h>
#include "common/encode.h"
#include "common/hash.h"
#include "security/ecdsa/ecdsa.h"

namespace shardora {

using json = nlohmann::json;

// ==========================================
// 1. Utilities
// ==========================================
namespace utils {
    std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        size_t start = (hex.length() >= 2 && hex.substr(0, 2) == "0x") ? 2 : 0;
        for (unsigned int i = start; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }

    std::string bytesToHex(const std::vector<uint8_t>& bytes) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : bytes) ss << std::setw(2) << (int)b;
        return ss.str();
    }

    std::vector<uint8_t> longToBytesLittleEndian(uint64_t val) {
        std::vector<uint8_t> bytes(8);
        for (int i = 0; i < 8; ++i) {
            bytes[i] = (val >> (i * 8)) & 0xFF;
        }
        return bytes;
    }

    std::string keccak256(const std::vector<uint8_t>& input) {
        std::string str((const char*)input.data(), input.size());
        std::string hash = common::Hash::keccak256(str);
        return bytesToHex(std::vector<uint8_t>(hash.begin(), hash.end()));
    }
    
    std::string keccak256Str(const std::string& input) {
        std::vector<uint8_t> vec(input.begin(), input.end());
        return keccak256(vec);
    }
}

// ==========================================
// 2. Constants & Structs
// ==========================================

struct Keypair {
    std::vector<uint8_t> skbytes;
    std::vector<uint8_t> pkbytes;
    std::string account_id;
};

struct Sign {
    std::string r;
    std::string s;
    int v;
};

// ==========================================
// 3. ABI Helper & Decoder
// ==========================================
class ABIHelper {
public:
    // Extract return types for a function from full ABI JSON
    static std::vector<std::string> getReturnTypesFromABI(const std::string& abi_json_str, const std::string& func_name) {
        std::vector<std::string> types;
        try {
            auto abi = json::parse(abi_json_str);
            const json* abi_array = &abi;
            // Handle solc combined-json output format if necessary, here assumes array
            
            for (const auto& item : *abi_array) {
                if (item.contains("type") && item["type"] == "function" && 
                    item.contains("name") && item["name"] == func_name) {
                    if (item.contains("outputs")) {
                        for (const auto& output : item["outputs"]) {
                            types.push_back(output["type"].get<std::string>());
                        }
                    }
                    return types;
                }
            }
        } catch (...) {}
        return types;
    }
};

class ABIDecoder {
private:
    static std::string hexToASCII(const std::string& hex) {
        std::string ascii = "";
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string part = hex.substr(i, 2);
            char ch = (char)stoul(part, nullptr, 16);
            if (ch != 0) ascii += ch;
        }
        return ascii;
    }
public:
    // Decode raw hex string based on types
    static json decode(std::string hex_data, const std::vector<std::string>& output_types) {
        json result = json::array();
        if (hex_data.substr(0, 2) == "0x") hex_data = hex_data.substr(2);
        if (hex_data.empty()) return result;

        for (size_t i = 0; i < output_types.size(); ++i) {
            std::string type = output_types[i];
            size_t chunk_start = i * 64; 

            if (chunk_start + 64 > hex_data.length()) break;
            std::string chunk = hex_data.substr(chunk_start, 64);

            if (type == "uint256" || type == "uint" || type == "int") {
                try {
                    size_t first_nonzero = chunk.find_first_not_of('0');
                    if (first_nonzero == std::string::npos) result.push_back(0);
                    else if (chunk.length() - first_nonzero > 16) result.push_back("0x" + chunk.substr(first_nonzero));
                    else result.push_back(std::stoull(chunk, nullptr, 16));
                } catch (...) { result.push_back("0x" + chunk); }
            } 
            else if (type == "bool") {
                result.push_back((chunk.back() == '1' || chunk.back() == 'e')); // simple check
            } 
            else if (type == "address") {
                result.push_back("0x" + chunk.substr(24));
            } 
            else if (type == "string" || type == "bytes") {
                try {
                    uint64_t offset_chars = std::stoull(chunk, nullptr, 16) * 2;
                    if (offset_chars + 64 <= hex_data.length()) {
                        uint64_t data_len_chars = std::stoull(hex_data.substr(offset_chars, 64), nullptr, 16) * 2;
                        if (offset_chars + 64 + data_len_chars <= hex_data.length()) {
                            std::string data_hex = hex_data.substr(offset_chars + 64, data_len_chars);
                            if (type == "string") result.push_back(hexToASCII(data_hex));
                            else result.push_back("0x" + data_hex);
                        }
                    }
                } catch (...) { result.push_back("Error decoding dynamic type"); }
            }
            else {
                result.push_back(chunk);
            }
        }
        return result;
    }
};

// ==========================================
// 4. Shardora Client
// ==========================================
class ShardoraClient {
public:
    int64_t fetchNonce(const std::string& address) {
        auto& cli = getOrCreateClient();
        httplib::Params params;
        params.emplace("address", address);
        auto res = cli.Post("/query_account", params);
        if (res && res->status == 200) {
            try {
                json info = json::parse(res->body);
                if (info.contains("nonce")) {
                    int64_t nonce = -1;
                    auto str = info["nonce"].get<std::string>();
                    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), nonce);
                    if (ec != std::errc()) {
                        std::cerr << "fetchNonce parse error: addr=" << address.substr(0,32)
                                  << " nonce_str=\"" << str << "\"" 
                                  << " host=" << node_host_ << ":" << node_port_ << std::endl;
                        return -1;
                    }

                    return nonce;
                }
                // Server returned 200 but no "nonce" field — account may not exist yet
                return 0;
            } catch (std::exception& e) {
                // Non-JSON response (e.g. "get address failed") — treat as nonce=0
                return 0; 
            }
        }

        if (res) {
            std::cerr << "fetchNonce http error: addr=" << address.substr(0,32)
                      << " status=" << res->status
                      << " host=" << node_host_ << ":" << node_port_ << std::endl;
        } else {
            auto err = getOrCreateClient().get_openssl_verify_result();
            std::cerr << "fetchNonce connection failed: addr=" << address.substr(0,32)
                      << " host=" << node_host_ << ":" << node_port_
                      << " ssl_err=" << err << std::endl;
            // Reset persistent client on connection failure so next call reconnects
            resetClient();
        }
        return -1; 
    }

    ShardoraClient(
            const std::string& node_host = "127.0.0.1", 
            int node_port = 23001)
            : node_host_(node_host), node_port_(node_port) {
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }
    ~ShardoraClient() { secp256k1_context_destroy(ctx); }

    // Persistent SSL client for connection reuse (avoids repeated TLS handshakes)
    httplib::SSLClient& getOrCreateClient() {
        if (!persistent_cli_) {
            persistent_cli_ = std::make_unique<httplib::SSLClient>(node_host_, node_port_);
            persistent_cli_->enable_server_certificate_verification(false);
            persistent_cli_->set_connection_timeout(3, 0);
            persistent_cli_->set_read_timeout(5, 0);
            persistent_cli_->set_keep_alive(true);
        }
        return *persistent_cli_;
    }

    void resetClient() {
        persistent_cli_.reset();
    }

    Keypair getKeypair(const std::string& privKeyHex) {
        Keypair kp;
        kp.skbytes = utils::hexToBytes(privKeyHex);
        
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(ctx, &pubkey, kp.skbytes.data())) {
            throw std::runtime_error("Invalid private key");
        }

        // Modification 1: Compressed public key length is fixed at 33 bytes 
        // (1 byte prefix + 32 bytes X coordinate)
        size_t len = 33; 
        kp.pkbytes.resize(len);

        // Modification 2: Use the SECP256K1_EC_COMPRESSED flag for serialization
        secp256k1_ec_pubkey_serialize(ctx, kp.pkbytes.data(), &len, &pubkey, SECP256K1_EC_COMPRESSED);

        // Address generation logic (Note: This will change the generated address!)
        // The logic here is to remove the 1st byte prefix (0x02 or 0x03) 
        // and hash the remaining 32 bytes of the X coordinate.
        std::vector<uint8_t> pubKeyNoPrefix(kp.pkbytes.begin() + 1, kp.pkbytes.end());
        std::string hash = utils::keccak256(pubKeyNoPrefix);
        kp.account_id = hash.substr(hash.length() - 40);
        
        return kp;
    }

    Sign signMessage(security::Ecdsa& ecdsa, uint64_t nonce, const std::string& to, uint64_t amount,
                    uint64_t gas_limit, uint64_t gas_price, int step,
                    const std::string& contract_bytes, const std::string& input,
                    uint64_t prefund, const std::string& key, const std::string& val) {
        std::string message;
        message.append(std::string((char*)&nonce, sizeof(nonce)));
        message.append(ecdsa.GetPublicKey());
        message.append(common::Encode::HexDecode(to));
        message.append(std::string((char*)&amount, sizeof(amount)));
        message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
        message.append(std::string((char*)&gas_price, sizeof(gas_price)));
        uint64_t tmp_step = step;
        message.append(std::string((char*)&tmp_step, sizeof(tmp_step)));
        if (!contract_bytes.empty()) message.append(common::Encode::HexDecode(contract_bytes));
        if (!input.empty()) message.append(common::Encode::HexDecode(input));
        // Only append prefund when non-zero, matching shardora_sdk.py behaviour.
        if (prefund > 0) message.append(std::string((char*)&prefund, sizeof(prefund)));

        std::string h_str = common::Hash::keccak256(message);
        std::string sign;
        ecdsa.Sign(h_str, &sign);
        return {sign.substr(0, 32), sign.substr(32, 32), sign[64]};
    }

    bool transfer(const std::string& private_key, std::string to, uint64_t amount,
                int64_t nonce = -1, int step = 0, std::string contract_bytes = "",
                std::string input = "", std::string key = "", std::string val = "",
                uint64_t prefund = 0, bool check_tx_valid = true) {
        try {
            httplib::SSLClient cli(node_host_, node_port_);
            cli.enable_server_certificate_verification(false);
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(common::Encode::HexDecode(private_key));
            if (nonce == -1) nonce = fetchNonce(common::Encode::HexEncode(ecdsa.GetAddress()));
            if (nonce == -1) return false;
            nonce++;
            // Must match shardora_sdk.py: gas_limit=5000000, gas_price=1
            uint64_t gas_limit = 5000000lu;
            uint64_t gas_price = 1llu;
            Sign sig = signMessage(ecdsa, nonce, to, amount, gas_limit, gas_price, step, contract_bytes, input, prefund, key, val);
            httplib::Params params;
            params.emplace("nonce", std::to_string(nonce));
            params.emplace("pubkey", common::Encode::HexEncode(ecdsa.GetPublicKey()));
            params.emplace("to", to);
            params.emplace("type", std::to_string(step));
            params.emplace("amount", std::to_string(amount));
            params.emplace("gas_limit", std::to_string(gas_limit));
            params.emplace("gas_price", std::to_string(gas_price));
            params.emplace("shard_id", "0");
            if (!key.empty()) params.emplace("key", key);
            if (!val.empty()) params.emplace("val", val);
            if (prefund > 0) params.emplace("prefund", std::to_string(prefund));
            params.emplace("sign_r", common::Encode::HexEncode(sig.r));
            params.emplace("sign_s", common::Encode::HexEncode(sig.s));
            params.emplace("sign_v", std::to_string((uint8_t)sig.v));
            if (!contract_bytes.empty()) params.emplace("bytes_code", contract_bytes);
            if (!input.empty()) params.emplace("input", input);
            auto res = cli.Post("/transaction", params);
            return (res && res->status == 200);
        } catch (std::exception& e) {
            std::cout << "transfer failed: " << e.what() << std::endl;
            return false;
        }
    }
    std::string queryContract(const std::string& private_key, const std::string& contract_address, const std::string& input_data) {
        try {
            httplib::SSLClient cli(node_host_, node_port_);
            cli.enable_server_certificate_verification(false);
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(common::Encode::HexDecode(private_key));
            httplib::Params params;
            params.emplace("input", input_data);
            params.emplace("address", contract_address);
            params.emplace("from", common::Encode::HexEncode(ecdsa.GetAddress()));
            auto res = cli.Post("/abi_query_contract", params);
            if (res && res->status == 200) return res->body;
            return "";
        } catch (...) { return ""; }
    }

private:
    secp256k1_context* ctx;
    std::unique_ptr<httplib::SSLClient> persistent_cli_;
public:
    std::string node_host_ = "127.0.0.1";
    int node_port_ = 23001;
};

// ==========================================
// 5. Solidity Compiler
// ==========================================
class SolidityCompiler {
public:
    static json compile(const std::string& sourceCode) {
        std::string filename = "temp_contract.sol";
        std::ofstream out(filename);
        out << sourceCode;
        out.close();
        // Redirect stderr to /dev/null so warnings don't corrupt the JSON output.
        std::string cmd = "solc --combined-json abi,bin " + filename + " 2>/dev/null";
        std::string result_json;
        char buffer[128];
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) throw std::runtime_error("popen() failed!");
        while (fgets(buffer, sizeof buffer, pipe) != nullptr) result_json += buffer;
        pclose(pipe);
        // Trim leading/trailing whitespace
        auto start = result_json.find('{');
        if (start != std::string::npos) result_json = result_json.substr(start);
        try {
            json raw = json::parse(result_json);
            if (raw.contains("contracts")) {
                for (auto& el : raw["contracts"].items()) return {{"abi", el.value()["abi"]}, {"bin", el.value()["bin"]}};
            }
            return nullptr;
        } catch (...) { throw std::runtime_error("Compile failed: " + result_json); }
    }
};

// ==========================================
// 6. Shardora SDK (Facade)
// ==========================================
class ShardoraSDK {
private:
    ShardoraClient client;

    std::string padTo32Bytes(std::string hex, bool isNumeric = true) {
        if (hex.length() >= 2 && hex.substr(0, 2) == "0x") hex = hex.substr(2);
        if (hex.length() > 64) return hex; 
        std::string padding(64 - hex.length(), '0');
        return isNumeric ? (padding + hex) : (hex + padding);
    }
    std::string encodeInt(const std::string& val) {
        try { std::stringstream ss; ss << std::hex << std::stoull(val); return padTo32Bytes(ss.str(), true); } catch (...) { return std::string(64, '0'); }
    }
    std::string encodeBool(const std::string& val) {
        return padTo32Bytes((val != "false" && val != "0") ? "1" : "0", true);
    }
    std::string encodeBytes(const std::string& val) { return padTo32Bytes(val, false); }

    std::string encodeArgs(const std::vector<std::string>& types, const std::vector<std::string>& args) {
        std::string encoded = "";
        for (size_t i = 0; i < types.size(); ++i) {
            std::string type = types[i];
            std::string arg = args[i];
            if (type.length() > 2 && type.substr(type.length() - 2) == "[]") {
                std::stringstream ss(arg);
                std::string item, base = type.substr(0, type.length() - 2);
                while (std::getline(ss, item, '-')) {
                    if (base == "address") encoded += padTo32Bytes(item, true);
                    else if (base.find("bytes") == 0) encoded += encodeBytes(item);
                    else if (base.find("string") == 0) encoded += encodeBytes(utils::bytesToHex(std::vector<uint8_t>(item.begin(), item.end())));
                    else if (base.find("bool") == 0) encoded += encodeBool(item);
                    else encoded += encodeInt(item);
                }
            } else {
                if (type == "address") encoded += padTo32Bytes(arg, true);
                else if (type.find("bytes") == 0) encoded += encodeBytes(arg);
                else if (type == "string") encoded += encodeBytes(utils::bytesToHex(std::vector<uint8_t>(arg.begin(), arg.end())));
                else if (type == "bool") encoded += encodeBool(arg);
                else encoded += encodeInt(arg);
            }
        }
        return encoded;
    }

public:
    ShardoraSDK(const std::string& node_host = "127.0.0.1", int node_port = 23001) : client(node_host, node_port) {}
    int64_t fetchNonce(const std::string& address) {
        return client.fetchNonce(address); 
    }

    // Query account balance via /query_account. Works for both normal (20-byte)
    // and prepayment (40-byte: contract+user) addresses.
    // Returns balance >= 0 on success, -1 on failure.
    int64_t fetchBalance(const std::string& hex_address) {
        httplib::SSLClient cli(client.node_host_, client.node_port_);
        cli.enable_server_certificate_verification(false);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(5);
        httplib::Params params;
        params.emplace("address", hex_address);
        auto res = cli.Post("/query_account", params);
        if (res && res->status == 200) {
            try {
                // /query_account returns protobuf-to-JSON which has "balance" as a string
                json info = json::parse(res->body);
                if (info.contains("balance")) {
                    int64_t balance = 0;
                    auto bs = info["balance"].get<std::string>();
                    std::from_chars(bs.data(), bs.data() + bs.size(), balance);
                    return balance;
                }
            } catch (...) {}
        }
        return -1;
    }

    // Batch query multiple accounts at once.
    // Input:  vector of hex-encoded addresses.
    // Output: JSON with "accounts" (found) and "not_found" (missing).
    // Returns: { "status":0, "accounts": { addr: {nonce, balance} }, "not_found": [...] }
    json batchQueryAccounts(const std::vector<std::string>& addresses) {
        if (addresses.empty()) {
            return {{"status", 1}, {"msg", "empty addresses"}};
        }

        // Adaptive batch size: 80-char prepayment addresses need smaller batches
        // to stay under uWebSockets ~16KB body limit.
        // Normal (40-char): 500 × 41 = ~20KB → use 300
        // Prepayment (80-char): 50 × 81 = ~4KB → safe
        size_t avg_addr_len = addresses.empty() ? 40 : addresses[0].size();
        const size_t kBatchSize = (avg_addr_len > 50) ? 100 : 300;
        json merged;
        merged["status"] = 0;
        merged["msg"] = "ok";
        merged["accounts"] = json::object();
        merged["not_found"] = json::array();
        merged["partial"] = false;
        uint32_t batch_ok = 0;
        uint32_t batch_fail = 0;
        json batch_errors = json::array();

        for (size_t offset = 0; offset < addresses.size(); offset += kBatchSize) {
            size_t end = std::min(offset + kBatchSize, addresses.size());
            std::string addr_list;
            for (size_t i = offset; i < end; ++i) {
                if (!addr_list.empty()) addr_list += ",";
                addr_list += addresses[i];
            }

            httplib::SSLClient cli(client.node_host_, client.node_port_);
            cli.enable_server_certificate_verification(false);
            cli.set_connection_timeout(10);
            cli.set_read_timeout(30);
            httplib::Params params;
            params.emplace("addresses", addr_list);
            auto res = cli.Post("/batch_query_accounts", params);
            if (!res) {
                ++batch_fail;
                batch_errors.push_back("connection failed (no response) offset=" + std::to_string(offset));
                continue;
            }
            if (res->status != 200) {
                ++batch_fail;
                batch_errors.push_back("HTTP " + std::to_string(res->status) + " offset=" + std::to_string(offset));
                continue;
            }

            try {
                json batch_res = json::parse(res->body);
                if (!batch_res.contains("status") || batch_res["status"] != 0) {
                    ++batch_fail;
                    batch_errors.push_back(batch_res.value("msg", "unknown error"));
                    continue;
                }

                ++batch_ok;
                if (batch_res.contains("accounts")) {
                    for (auto& [k, v] : batch_res["accounts"].items()) {
                        merged["accounts"][k] = v;
                    }
                }
                if (batch_res.contains("not_found")) {
                    for (auto& addr : batch_res["not_found"]) {
                        merged["not_found"].push_back(addr);
                    }
                }
            } catch (std::exception& e) {
                ++batch_fail;
                batch_errors.push_back(std::string("parse error: ") + e.what());
            }
        }

        if (batch_ok == 0) {
            merged["status"] = 1;
            merged["msg"] = batch_errors.empty() ? "batch query failed" : batch_errors[0].get<std::string>();
            return merged;
        }

        if (batch_fail > 0) {
            merged["partial"] = true;
            merged["batch_errors"] = batch_errors;
        }
        return merged;
    }

    // Fetch leader routing table: returns map of pool_index -> {ip, port}
    // Each pool has its own leader. Client sends tx directly to the leader
    // of the sender's pool: leaders[GetAddressPoolIndex(from_addr)]
    struct LeaderInfo {
        std::string ip;
        uint16_t port;
    };

    bool fetchLeaders(
            std::unordered_map<uint32_t, LeaderInfo>& leaders,
            uint32_t& leader_count) {
        httplib::SSLClient cli(client.node_host_, client.node_port_);
        cli.enable_server_certificate_verification(false);
        auto res = cli.Post("/query_leaders");
        if (!res || res->status != 200) {
            return false;
        }

        try {
            json info = json::parse(res->body);
            if (!info.contains("status") || info["status"] != 0) {
                return false;
            }

            leader_count = info.value("pool_count", 0u);
            if (leader_count == 0 || !info.contains("leaders")) {
                return false;
            }

            for (auto& [key, val] : info["leaders"].items()) {
                uint32_t pool_idx = std::stoi(key);
                LeaderInfo li;
                li.ip = val["ip"].get<std::string>();
                li.port = val["port"].get<uint16_t>();
                if (li.ip == "0.0.0.0" || li.port == 0) {
                    continue;  // Skip invalid entries
                }
                leaders[pool_idx] = li;
            }

            return !leaders.empty();
        } catch (std::exception& e) {
            std::cerr << "fetchLeaders parse error: " << e.what() << std::endl;
            return false;
        }
    }

    json compileSolidity(const std::string& source_code) {
        try {
            json compiled = SolidityCompiler::compile(source_code);
            if (compiled.is_null()) return {{"status", 1}, {"msg", "Compile returned empty"}};
            return {{"status", 0}, {"abi", compiled["abi"]}, {"bytecode", compiled["bin"]}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }

    json setGasPrefund(const std::string& private_key, const std::string& address, uint64_t prefund) {
        if (client.transfer(private_key, address, 0, -1, 7, "", "", "", "", prefund, true)) return {{"status", 0}, {"msg", "ok"}};
        return {{"status", 1}, {"msg", "set gas prefund failed"}};
    }

    json callFunctionSolidity(const std::string& private_key, const std::string& address, uint64_t amount, 
                                const std::string& func_name, const std::vector<std::string>& fn_types, const std::vector<std::string>& fn_args) {
        try {
            if (fn_types.size() != fn_args.size()) return {{"status", 1}, {"msg", "len mismatch"}};
            std::string signature = func_name + "(";
            for (size_t i = 0; i < fn_types.size(); ++i) signature += fn_types[i] + ((i < fn_types.size() - 1) ? "," : "");
            signature += ")";
            std::string selector = utils::keccak256Str(signature).substr(0, 8);
            std::string input_data = selector + encodeArgs(fn_types, fn_args);
            // For contract calls (step 8), nonce comes from prepayment account: contract_addr + caller_addr
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(common::Encode::HexDecode(private_key));
            std::string caller_addr = common::Encode::HexEncode(ecdsa.GetAddress());
            std::string prepay_addr = address + caller_addr;
            int64_t nonce = client.fetchNonce(prepay_addr);
            if (nonce < 0) nonce = 0;  // first call on this prepayment account
            if (client.transfer(private_key, address, amount, nonce, 8, "", input_data)) return {{"status", 0}, {"msg", "ok"}};
            return {{"status", 1}, {"msg", "call function failed"}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }

    json deploySolidity(const std::string& private_key, const std::string& bytecode, uint64_t amount, uint64_t prefund, 
                        int code_type, const std::vector<std::string>& fn_types, const std::vector<std::string>& fn_args) {
        try {
            if (fn_types.size() != fn_args.size()) return {{"status", 1}, {"msg", "len mismatch"}};
            std::string full_payload = bytecode + encodeArgs(fn_types, fn_args);
            // Use private_key + atomic counter for unique contract address (thread-safe)
            static std::atomic<uint64_t> deploy_counter{0};
            uint64_t cnt = deploy_counter.fetch_add(1);
            std::string salt = private_key + std::to_string(cnt) + 
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            std::string to_address = utils::keccak256Str(full_payload + salt).substr(24);
            if (client.transfer(private_key, to_address, amount, -1, (code_type != 0) ? 14 : 6, full_payload, "", "", "", prefund, true)) {
                return {{"status", 0}, {"msg", "ok"}, {"id", to_address}};
            }
            return {{"status", 1}, {"msg", "create contract failed"}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }

    json queryFunctionSolidity(const std::string& private_key, const std::string& address, const std::string& func_name,
                                const std::vector<std::string>& fn_types, const std::vector<std::string>& fn_args,
                                const std::vector<std::string>& return_types = {}) {
        try {
            std::string signature = func_name + "(";
            for (size_t i = 0; i < fn_types.size(); ++i) signature += fn_types[i] + ((i < fn_types.size() - 1) ? "," : "");
            signature += ")";
            std::string input_data = utils::keccak256Str(signature).substr(0, 8) + encodeArgs(fn_types, fn_args);
            std::string res_text = client.queryContract(private_key, address, input_data);
            if (res_text.empty()) return {{"status", 1}, {"msg", "query failed"}};
            
            json decoded_values = res_text;
            if (!return_types.empty()) decoded_values = ABIDecoder::decode(res_text, return_types);
            return {{"status", 0}, {"msg", "ok"}, {"return_value", res_text}, {"decoded", decoded_values}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }
    
    json queryContractWithABI(const std::string& private_key, const std::string& address, const std::string& abi_json,
                                const std::string& func_name, const std::vector<std::string>& fn_types, const std::vector<std::string>& fn_args) {
            std::vector<std::string> return_types = ABIHelper::getReturnTypesFromABI(abi_json, func_name);
            return queryFunctionSolidity(private_key, address, func_name, fn_types, fn_args, return_types);
    }
};

}

// int main() {
//     // 1. Initialize SDK instance
//     shardora::ShardoraSDK sdk;
    
//     // Initialize random seed (for simulating address generation)
//     srand(time(0));

//     std::cout << "=============================================" << std::endl;
//     std::cout << "      Shardora C++ SDK Client Demo           " << std::endl;
//     std::cout << "=============================================" << std::endl;

//     // -------------------------------------------------
//     // Scenario configuration: Simulate account and contract source code
//     // -------------------------------------------------
//     std::string private_key = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"; // 32-byte Hex private key
    
//     // A simple storage contract with constructor, write method and read method
//     std::string source_code = R"(
//         contract SimpleStorage {
//             uint256 public storedData;
//             string public ownerName;

//             constructor(uint256 initialValue, string memory _name) {
//                 storedData = initialValue;
//                 ownerName = _name;
//             }

//             function set(uint256 x) public {
//                 storedData = x;
//             }

//             function get() public view returns (uint256, string memory) {
//                 return (storedData, ownerName);
//             }
//         }
//     )";

//     // -------------------------------------------------
//     // Step 1: Compile Solidity
//     // -------------------------------------------------
//     std::cout << "\n[Step 1] Compiling Solidity..." << std::endl;
//     shardora::json compile_res = sdk.compileSolidity(source_code);

//     if (compile_res["status"] != 0) {
//         std::cerr << "Compile Failed: " << compile_res["msg"] << std::endl;
//         return 1;
//     }
    
//     std::string bytecode = compile_res["bytecode"];
//     // Note: The abi returned by compileSolidity is a JSON object, we need to dump it to a string for use with queryContractWithABI
//     std::string abi_json_str = compile_res["abi"].dump(); 
    
//     std::cout << "-> Compilation Success!" << std::endl;
//     std::cout << "-> Bytecode Length: " << bytecode.length() << std::endl;

//     // -------------------------------------------------
//     // Step 2: Deploy Contract
//     // Constructor parameters: (uint256: 12345, string: "ShardoraAdmin")
//     // -------------------------------------------------
//     std::cout << "\n[Step 2] Deploying Contract..." << std::endl;
    
//     std::vector<std::string> constructor_types = {"uint256", "string"};
//     std::vector<std::string> constructor_args = {"12345", "ShardoraAdmin"};
    
//     shardora::json deploy_res = sdk.deploySolidity(
//         private_key, 
//         bytecode, 
//         0,      // amount (transfer amount)
//         50000,  // gas_prefund (prepaid Gas)
//         0,      // code_type (0=Contract)
//         constructor_types, 
//         constructor_args
//     );

//     if (deploy_res["status"] != 0) {
//         std::cerr << "Deploy Failed: " << deploy_res["msg"] << std::endl;
//         return 1;
//     }

//     std::string contract_address = deploy_res["id"];
//     std::cout << "-> Deploy Success! Contract ID: " << contract_address << std::endl;

//     // -------------------------------------------------
//     // Step 3: Set Gas Prefund
//     // -------------------------------------------------
//     std::cout << "\n[Step 3] Setting Gas Prefund..." << std::endl;
    
//     shardora::json prefund_res = sdk.setGasPrefund(
//         private_key, 
//         contract_address, 
//         10000 // Supplement Gas
//     );

//     std::cout << "-> Set Prefund: " << prefund_res["msg"] << std::endl;

//     // -------------------------------------------------
//     // Step 4: Call Contract Function
//     // Call set(88888) to modify state
//     // -------------------------------------------------
//     std::cout << "\n[Step 4] Calling function 'set(88888)'..." << std::endl;
    
//     std::vector<std::string> call_types = {"uint256"};
//     std::vector<std::string> call_args = {"88888"};

//     shardora::json call_res = sdk.callFunctionSolidity(
//         private_key, 
//         contract_address, 
//         0,       // amount
//         "set",   // function name
//         call_types, 
//         call_args
//     );

//     std::cout << "-> Call Result: " << call_res["msg"] << std::endl;

//     // -------------------------------------------------
//     // Step 5: Query Contract - Method A (Manually specify return type)
//     // Query ownerName (expect to return string)
//     // -------------------------------------------------
//     std::cout << "\n[Step 5A] Querying 'ownerName' (Manual Decode)..." << std::endl;
    
//     // ownerName is a public variable, getter is automatically generated
//     shardora::json query_res_manual = sdk.queryFunctionSolidity(
//         private_key,
//         contract_address,
//         "ownerName",
//         {}, // No input types
//         {}, // No input args
//         {"string"} // Expected return type
//     );

//     std::cout << "-> Raw Result: " << query_res_manual["return_value"] << std::endl;
//     if (query_res_manual.contains("decoded")) {
//         std::cout << "-> Decoded Result: " << query_res_manual["decoded"].dump() << std::endl;
//     }

//     // -------------------------------------------------
//     // Step 6: Query Contract - Method B (Auto ABI Decode)
//     // Call get() returns (uint256, string)
//     // -------------------------------------------------
//     std::cout << "\n[Step 6B] Querying 'get()' (Auto ABI Decode)..." << std::endl;

//     shardora::json query_res_auto = sdk.queryContractWithABI(
//         private_key,
//         contract_address,
//         abi_json_str, // Pass the ABI string generated in Step 1
//         "get",        // Function name
//         {},           // Input types (get has no parameters)
//         {}            // Input parameters
//     );

//     if (query_res_auto["status"] == 0) {
//         std::cout << "-> Decoded Result: " << query_res_auto["decoded_response"].dump(4) << std::endl;
//         // Expected output: [88888, "ShardoraAdmin"]
//     } else {
//         std::cerr << "-> Query Failed: " << query_res_auto["msg"] << std::endl;
//     }

//     std::cout << "\n=============================================" << std::endl;
//     std::cout << "           Demo Completed                    " << std::endl;
//     std::cout << "=============================================" << std::endl;

//     return 0;
// }