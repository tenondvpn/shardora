// #pragma once

// #include <iostream>
// #include <vector>
// #include <string>
// #include <ctime>
// #include <iomanip>
// #include <cstring>

// #include <openssl/bn.h>
// #include <openssl/rand.h>
// #include <openssl/evp.h>
// #include <openssl/aes.h>

// #include "common/tick.h"
// #include "contract/contract_interface.h"
// #include "db/db.h"
// #include "pki/pki_ib_agka.h"
// #include "pki/param.h"

// namespace shardora {

// namespace contract {

// // 公钥结构
// struct PublicKey {
//     BIGNUM* g;
//     BIGNUM* h;
//     PublicKey() : g(nullptr), h(nullptr) {
//         g = BN_new();
//         h = BN_new();
//     }
//     ~PublicKey() {
//         BN_free(g);
//         BN_free(h);
//     }
// };

// // 主密钥结构
// struct MasterKey {
//     BIGNUM* alpha;
//     MasterKey() : alpha(nullptr) {
//         alpha = BN_new();
//     }
//     ~MasterKey() {
//         BN_free(alpha);
//     }
// };

// // 用户密钥结构（属性-私钥对）
// using AttributeKeyPair = std::pair<std::string, BIGNUM*>;
// struct UserPrivateKey {
//     std::vector<AttributeKeyPair> attributes;
//     ~UserPrivateKey() {
//         for (const auto& [_, key] : attributes) {
//             BN_free(key);
//         }
//     }
// };

// // 密文结构
// struct CipherText {
//     std::string policy;
//     BIGNUM* C1;
//     BIGNUM* C2;
//     CipherText() : C1(nullptr), C2(nullptr) {
//         C1 = BN_new();
//         C2 = BN_new();
//     }
//     ~CipherText() {
//         BN_free(C1);
//         BN_free(C2);
//     }
// };

// class ContractCpabe : public ContractInterface {
// public:
//     ContractCpabe() : ContractInterface("") {
//     }

//     virtual ~ContractCpabe();
//     virtual int call(
//         const CallParameters& param,
//         uint64_t gas,
//         const std::string& origin_address,
//         evmc_result* res);

    

//     // 日志记录函数
//     void log(const std::string& message);
//     // 初始化密钥生成函数
//     void initialize_keys(PublicKey& publicKey, MasterKey& masterKey);
//     // 用户密钥生成函数
//     void generate_user_private_key(
//         const PublicKey& publicKey, 
//         const MasterKey& masterKey, 
//         const std::vector<std::string>& attributes, UserPrivateKey& userPrivateKey);
//     // 加密函数
//     CipherText encrypt(
//         const PublicKey& publicKey, 
//         const std::string& message, 
//         const std::string& policy);
//     // 将字节数组转换为十六进制字符串
//     std::string bytesToHex(const unsigned char* data, size_t len);
//     // 解密函数
//     bool decrypt(
//         const PublicKey& publicKey, 
//         const UserPrivateKey& userPrivateKey, 
//         const CipherText& cipher, 
//         std::string& decrypted_message);
//     int test_cpabe();


// private:
    
//     DISALLOW_COPY_AND_ASSIGN(ContractCpabe);
// };

// }  // namespace contract

// }  // namespace shardora
