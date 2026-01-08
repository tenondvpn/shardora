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
        unsigned int md_len;
        unsigned char md_value[EVP_MAX_MD_SIZE];
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        const EVP_MD* md = EVP_sha3_256(); // Ensure OpenSSL version matches chain requirement
        EVP_DigestInit_ex(mdctx, md, nullptr);
        EVP_DigestUpdate(mdctx, input.data(), input.size());
        EVP_DigestFinal_ex(mdctx, md_value, &md_len);
        EVP_MD_CTX_free(mdctx);
        return bytesToHex(std::vector<uint8_t>(md_value, md_value + md_len));
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
        httplib::Client cli(node_host_, node_port_);
        httplib::Params params;
        params.emplace("address", address);
        auto res = cli.Post("/query_account", params);
        if (res && res->status == 200) {
            try {
                std::cout << res->body << std::endl;
                json info = json::parse(res->body);
                if (info.contains("nonce")) {
                    int64_t nonce = -1;
                    auto str = info["nonce"].get<std::string>();
                    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), nonce);
                    if (ec != std::errc()) {
                        std::cout << "nonce invalid:" << str << std::endl;
                        return -1;
                    }

                    return nonce;
                }
            } catch (std::exception& e) {
                std::cout << "catch error fetch nonce failed: " << address << ": " << e.what() << std::endl;
                return -1; 
            }
        }

        std::cout << "fetch nonce failed: " << res->status << ", " << res->body << std::endl;
        return -1; 
    }

    ShardoraClient(
            const std::string& node_host = "127.0.0.1", 
            int node_port = 23001)
            : node_host_(node_host), node_port_(node_port) {
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }
    ~ShardoraClient() { secp256k1_context_destroy(ctx); }

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
                    uint64_t prepay, const std::string& key, const std::string& val) {
        std::string message;
        message.append(std::string((char*)&nonce, sizeof(nonce)));
        message.append(ecdsa.GetPublicKey());
        message.append(to);
        message.append(std::string((char*)&amount, sizeof(amount)));
        message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
        message.append(std::string((char*)&gas_price, sizeof(gas_price)));
        uint64_t tmp_step = step;
        message.append(std::string((char*)&tmp_step, sizeof(tmp_step)));
        message.append(contract_bytes);
        message.append(input);
        message.append(std::string((char*)&prepay, sizeof(prepay)));
        if (!key.empty()) {
            message.append(key);
            if (!val.empty()) {
                message.append(val);
            }
        }

        std::string h_str = common::Hash::keccak256(message);
        std::string sign;
        ecdsa.Sign(h_str, &sign);
        std::cout << "hash: " << common::Encode::HexEncode(h_str) << ", " << common::Encode::HexEncode(message) 
            << ", sign: " << common::Encode::HexEncode(sign) << std::endl;
        return {sign.substr(0, 32), sign.substr(32, 32), sign[64]};
    }

    bool transfer(const std::string& private_key, std::string to, uint64_t amount, 
                int64_t nonce = -1, int step = 0, std::string contract_bytes = "", 
                std::string input = "", std::string key = "", std::string val = "", 
                uint64_t prepayment = 0, bool check_tx_valid = true) {
        try {
            httplib::Client cli(node_host_, node_port_);
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(common::Encode::HexDecode(private_key));
            if (nonce == -1) nonce = fetchNonce(common::Encode::HexEncode(ecdsa.GetAddress()));
            if (nonce == -1) return false;
            uint64_t gas_limit = 9999999lu;
            uint64_t gas_price = 1llu;
            Sign sig = signMessage(ecdsa, nonce, to, amount, gas_limit, gas_price, step, contract_bytes, input, prepayment, key, val);
            httplib::Params params;
            params.emplace("nonce", std::to_string(nonce));
            params.emplace("pubkey", common::Encode::HexEncode(ecdsa.GetPublicKey()));
            params.emplace("to", common::Encode::HexEncode(to));
            params.emplace("type", std::to_string(step));
            params.emplace("amount", std::to_string(amount));
            params.emplace("gas_limit", std::to_string(gas_limit));
            params.emplace("gas_price", std::to_string(gas_price));
            params.emplace("shard_id", "3");
            params.emplace("key", key);
            params.emplace("val", val);
            params.emplace("pepay", std::to_string(prepayment));
            params.emplace("sign_r", sig.r);
            params.emplace("sign_s", sig.s);
            params.emplace("sign_v", std::to_string(sig.v));
            if (!contract_bytes.empty()) params.emplace("bytes_code", contract_bytes);
            if (!input.empty()) params.emplace("input", input);
            auto res = cli.Post("/transaction", params);
            std::cout << res->body << std::endl;
            return (res && res->status == 200);
        } catch (std::exception& e) { 
            std::cout << "transfer failed: " << e.what() << std::endl; 
            return false; 
        }
    }
    std::string queryContract(const std::string& private_key, const std::string& contract_address, const std::string& input_data) {
        try {
            httplib::Client cli(node_host_, node_port_);
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
        std::string cmd = "solc --combined-json abi,bin " + filename + " 2>&1";
        std::string result_json;
        char buffer[128];
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) throw std::runtime_error("popen() failed!");
        while (fgets(buffer, sizeof buffer, pipe) != nullptr) result_json += buffer;
        pclose(pipe);
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
                    if (base.find("bytes") == 0 || base == "address") encoded += encodeBytes(item);
                    else if (base.find("string") == 0) encoded += encodeBytes(utils::bytesToHex(std::vector<uint8_t>(item.begin(), item.end())));
                    else if (base.find("bool") == 0) encoded += encodeBool(item);
                    else encoded += encodeInt(item);
                }
            } else {
                if (type.find("bytes") == 0 || type == "address") encoded += encodeBytes(arg);
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

    json compileSolidity(const std::string& source_code) {
        try {
            json compiled = SolidityCompiler::compile(source_code);
            if (compiled.is_null()) return {{"status", 1}, {"msg", "Compile returned empty"}};
            return {{"status", 0}, {"abi", compiled["abi"]}, {"bytecode", compiled["bin"]}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }

    json setGasPrepayment(const std::string& private_key, const std::string& address, uint64_t prepayment) {
        if (client.transfer(private_key, address, 0, -1, 7, "", "", "", "", prepayment, true)) return {{"status", 0}, {"msg", "ok"}};
        return {{"status", 1}, {"msg", "set gas prepayment failed"}};
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
            if (client.transfer(private_key, address, amount, -1, 8, "", input_data)) return {{"status", 0}, {"msg", "ok"}};
            return {{"status", 1}, {"msg", "call function failed"}};
        } catch (const std::exception& e) { return {{"status", 1}, {"msg", e.what()}}; }
    }

    json deploySolidity(const std::string& private_key, const std::string& bytecode, uint64_t amount, uint64_t prepayment, 
                        int code_type, const std::vector<std::string>& fn_types, const std::vector<std::string>& fn_args) {
        try {
            if (fn_types.size() != fn_args.size()) return {{"status", 1}, {"msg", "len mismatch"}};
            std::string full_payload = bytecode + encodeArgs(fn_types, fn_args);
            std::string to_address = utils::keccak256Str(full_payload + std::to_string(rand())).substr(24);
            if (client.transfer(private_key, to_address, amount, -1, (code_type != 0) ? 14 : 6, full_payload, "", "", "", prepayment, true)) {
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
//     // 1. 初始化 SDK 实例
//     shardora::ShardoraSDK sdk;
    
//     // 初始化随机种子 (用于模拟生成地址)
//     srand(time(0));

//     std::cout << "=============================================" << std::endl;
//     std::cout << "      Shardora C++ SDK Client Demo           " << std::endl;
//     std::cout << "=============================================" << std::endl;

//     // -------------------------------------------------
//     // 场景配置: 模拟账户和合约源码
//     // -------------------------------------------------
//     std::string private_key = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"; // 32字节 Hex 私钥
    
//     // 一个简单的存储合约，包含构造函数、写方法和读方法
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
//     // Step 1: 编译 Solidity (Compile)
//     // -------------------------------------------------
//     std::cout << "\n[Step 1] Compiling Solidity..." << std::endl;
//     shardora::json compile_res = sdk.compileSolidity(source_code);

//     if (compile_res["status"] != 0) {
//         std::cerr << "Compile Failed: " << compile_res["msg"] << std::endl;
//         return 1;
//     }
    
//     std::string bytecode = compile_res["bytecode"];
//     // 注意：compileSolidity 返回的 abi 是 JSON 对象，我们需要 dump 成字符串给 queryContractWithABI 使用
//     std::string abi_json_str = compile_res["abi"].dump(); 
    
//     std::cout << "-> Compilation Success!" << std::endl;
//     std::cout << "-> Bytecode Length: " << bytecode.length() << std::endl;

//     // -------------------------------------------------
//     // Step 2: 部署合约 (Deploy)
//     // 构造函数参数: (uint256: 12345, string: "ShardoraAdmin")
//     // -------------------------------------------------
//     std::cout << "\n[Step 2] Deploying Contract..." << std::endl;
    
//     std::vector<std::string> constructor_types = {"uint256", "string"};
//     std::vector<std::string> constructor_args = {"12345", "ShardoraAdmin"};
    
//     shardora::json deploy_res = sdk.deploySolidity(
//         private_key, 
//         bytecode, 
//         0,      // amount (转账金额)
//         50000,  // gas_prepayment (预付 Gas)
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
//     // Step 3: 设置 Gas 预付 (Set Gas Prepayment)
//     // -------------------------------------------------
//     std::cout << "\n[Step 3] Setting Gas Prepayment..." << std::endl;
    
//     shardora::json prepay_res = sdk.setGasPrepayment(
//         private_key, 
//         contract_address, 
//         10000 // 补充 Gas
//     );

//     std::cout << "-> Set Prepayment: " << prepay_res["msg"] << std::endl;

//     // -------------------------------------------------
//     // Step 4: 调用合约函数 (Call Function)
//     // 调用 set(88888) 修改状态
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
//     // Step 5: 查询合约 - 方式 A (手动指定返回类型)
//     // 查询 ownerName (期待返回 string)
//     // -------------------------------------------------
//     std::cout << "\n[Step 5A] Querying 'ownerName' (Manual Decode)..." << std::endl;
    
//     // ownerName 是 public 变量，会自动生成 getter
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
//     // Step 6: 查询合约 - 方式 B (ABI 自动解码)
//     // 调用 get() 返回 (uint256, string)
//     // -------------------------------------------------
//     std::cout << "\n[Step 6B] Querying 'get()' (Auto ABI Decode)..." << std::endl;

//     shardora::json query_res_auto = sdk.queryContractWithABI(
//         private_key,
//         contract_address,
//         abi_json_str, // 传入 Step 1 编译生成的 ABI 字符串
//         "get",        // 函数名
//         {},           // 输入类型 (get 无参数)
//         {}            // 输入参数
//     );

//     if (query_res_auto["status"] == 0) {
//         std::cout << "-> Decoded Result: " << query_res_auto["decoded_response"].dump(4) << std::endl;
//         // 预期输出: [88888, "ShardoraAdmin"]
//     } else {
//         std::cerr << "-> Query Failed: " << query_res_auto["msg"] << std::endl;
//     }

//     std::cout << "\n=============================================" << std::endl;
//     std::cout << "           Demo Completed                    " << std::endl;
//     std::cout << "=============================================" << std::endl;

//     return 0;
// }