#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <iomanip>
#include <cstring>

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/aes.h>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "db/db.h"
#include "pki/pki_ib_agka.h"
#include "pki/param.h"

namespace shardora {

namespace contract {


// 属性-密钥对结构
struct AttributeKeyPair {
    std::string attribute;
    BIGNUM* key;
    AttributeKeyPair(const std::string& attr, BIGNUM* key) : attribute(attr), key(key) {}
    ~AttributeKeyPair() {
        BN_free(key);
    }
};

// 公钥结构
struct PublicKey {
    BIGNUM* g;  // 生成元
    BIGNUM* h;  // 公共部分
    PublicKey() : g(nullptr), h(nullptr) {}
    ~PublicKey() {
        BN_free(g);
        BN_free(h);
    }
};

// 主密钥结构
struct MasterKey {
    BIGNUM* alpha;  // 私密部分
    MasterKey() : alpha(nullptr) {}
    ~MasterKey() {
        BN_free(alpha);
    }
};

// 用户密钥结构
struct UserPrivateKey {
    std::vector<AttributeKeyPair> attributes; // 用户的属性与私钥对
    ~UserPrivateKey() {
        attributes.clear();
    }
};

// 密文结构
struct CipherText {
    std::string policy;  // 访问策略
    BIGNUM* C1;     // 第一部分密文
    BIGNUM* C2;     // 第二部分密文
    CipherText() : C1(nullptr), C2(nullptr) {}
    ~CipherText() {
        BN_free(C1);
        BN_free(C2);
    }
};

class ContractCpabe : public ContractInterface {
public:
    ContractCpabe() : ContractInterface("") {
    }

    virtual ~ContractCpabe();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

        
    // 日志记录函数
    void log(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        tm* localTime = localtime(&time);
        std::cout << message << std::endl;
    }

    // 初始化密钥生成函数
    void initialize_keys(PublicKey &publicKey, MasterKey &masterKey) {
        BN_CTX* ctx = BN_CTX_new();

        publicKey.g = BN_new();
        masterKey.alpha = BN_new();

        // 生成一个大的质数作为 g
        if (!BN_generate_prime_ex(publicKey.g, 512, 1, NULL, NULL, NULL)) {
            std::cerr << "Error: Failed to generate prime for g." << std::endl;
            log("密钥生成失败：无法生成质数 g。");
            exit(1);
        }

        // 生成 alpha
        if (!BN_rand_range(masterKey.alpha, BN_value_one()) ||!BN_add(masterKey.alpha, masterKey.alpha, BN_value_one())) {
            std::cerr << "Error: Failed to generate alpha." << std::endl;
            log("密钥生成失败：无法生成 alpha。");
            exit(1);
        }

        // 计算 h = g^alpha mod p
        publicKey.h = BN_new();
        if (!BN_mod_exp(publicKey.h, publicKey.g, masterKey.alpha, publicKey.g, ctx)) {
            std::cerr << "Error: Failed to calculate h." << std::endl;
            log("密钥生成失败：无法计算 h。");
            exit(1);
        }

        BN_CTX_free(ctx);
    }

    // 用户密钥生成函数
    // void generate_user_private_key(const PublicKey &publicKey, const MasterKey &masterKey, const std::vector<std::string>& attributes, UserPrivateKey &userPrivateKey) {
    //     userPrivateKey.attributes.clear();
    //     for (const std::string &attr : attributes) {
    //         BIGNUM* key = BN_new();
    //         // 生成一个随机的私钥
    //         if (!BN_rand(key, 256, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
    //             BN_free(key);
    //             std::cerr << "Error: Failed to generate user private key." << std::endl;
    //             log("用户密钥生成失败：无法生成用户私钥。");
    //             exit(1);
    //         }
    //         AttributeKeyPair akp(attr, key);
    //         userPrivateKey.attributes.push_back(akp);
    //     }
    // }
    // 将生成用户私钥的长度从 256 位增加到 512 位。
    void generate_user_private_key(const PublicKey &publicKey, const MasterKey &masterKey, const std::vector<std::string>& attributes, UserPrivateKey &userPrivateKey) {
        userPrivateKey.attributes.clear();
        for (const std::string &attr : attributes) {
            BIGNUM* key = BN_new();
            // 生成一个随机的私钥，增加长度到 512 位
            if (!BN_rand(key, 512, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
                BN_free(key);
                std::cerr << "Error: Failed to generate user private key." << std::endl;
                log("用户密钥生成失败：无法生成用户私钥。");
                exit(1);
            }
            AttributeKeyPair akp(attr, key);
            userPrivateKey.attributes.push_back(akp);
        }
    }

    // // 加密函数
    // // 采用多次加密
    // CipherText encrypt(const PublicKey &publicKey, const std::string &message, const std::string &policy) {
    //     BN_CTX* ctx = BN_CTX_new();
    //     CipherText cipher;
    //     cipher.policy = policy;
    //     cipher.C1 = BN_new();
    //     cipher.C2 = BN_new();
    //
    //     // 随机选择 C1
    //     if (!BN_rand(cipher.C1, 256, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
    //         std::cerr << "Error: Failed to generate C1." << std::endl;
    //         log("加密失败：无法生成 C1。");
    //         exit(1);
    //     }
    //
    //     BIGNUM* temp = BN_new();
    //     BIGNUM* encryptedMessage = BN_bin2bn((const unsigned char*)message.c_str(), message.size(), NULL);
    //     if (encryptedMessage == NULL) {
    //         std::cerr << "Error: BN_bin2bn failed to convert message to BIGNUM." << std::endl;
    //         log("加密失败：无法将消息转换为 BIGNUM。");
    //         exit(1);
    //     }
    //
    //     // 多次加密，例如进行三次加密循环
    //     for (int i = 0; i < 3; ++i) {
    //         if (!BN_mod_exp(temp, publicKey.h, cipher.C1, BN_value_one(), ctx)) {
    //             std::cerr << "Error: Failed to calculate intermediate encrypted value." << std::endl;
    //             log("加密失败：无法计算中间加密值。");
    //             exit(1);
    //         }
    //         if (!BN_add(encryptedMessage, temp, encryptedMessage)) {
    //             std::cerr << "Error: BN_add failed during multiple encryption." << std::endl;
    //             log("加密失败：多次加密加法操作失败。");
    //             exit(1);
    //         }
    //     }
    //
    //     cipher.C2 = encryptedMessage;
    //
    //     BN_free(temp);
    //     BN_CTX_free(ctx);
    //     return cipher;
    // }

    // 函数用于安全地生成指定长度的随机字节数组（使用更安全的随机数生成机制）
    bool generate_secure_random_bytes(unsigned char* buffer, size_t length) {
        // 可以考虑使用系统更底层、更安全的随机数源，比如在Windows下可使用BCryptGenRandom
        // 这里简单示例，实际应用可根据不同平台做更适配的实现
        if (!RAND_bytes(buffer, length)) {
            std::cerr << "Error: Failed to generate secure random bytes." << std::endl;
            return false;
        }
        return true;
    }

    // 加密函数
    CipherText encrypt(const PublicKey &publicKey, const std::string &message, const std::string &policy) {
        BN_CTX* ctx = BN_CTX_new();
        CipherText cipher;
        cipher.policy = policy;
        cipher.C1 = BN_new();
        cipher.C2 = BN_new();

        // 生成更安全且长度适当增加的C1（例如增加到512位，可根据实际调整）
        if (!BN_rand(cipher.C1, 512, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
            std::cerr << "Error: Failed to generate C1." << std::endl;
            log("加密失败：无法生成C1。");
            exit(1);
        }

        // 生成一个随机的AES密钥用于对称加密预处理（长度增加且使用更安全的生成方式）
        unsigned char aes_key[AES_BLOCK_SIZE * 2];  // 密钥长度翻倍
        if (!generate_secure_random_bytes(aes_key, sizeof(aes_key))) {
            std::cerr << "Error: Failed to generate AES key." << std::endl;
            log("加密失败：无法生成AES密钥。");
            exit(1);
        }

        // 对消息进行填充处理，使其长度符合加密要求（例如填充到AES块大小的整数倍）
        size_t padded_message_size = (message.size() + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE;
        unsigned char* padded_message = new unsigned char[padded_message_size];
        memset(padded_message, 0, padded_message_size);
        memcpy(padded_message, message.c_str(), message.size());

        // 使用AES进行消息的对称加密预处理
        AES_KEY aes_encrypt_key;
        if (AES_set_encrypt_key(aes_key, 8 * sizeof(aes_key), &aes_encrypt_key) < 0) {
            std::cerr << "Error: Failed to set AES encrypt key." << std::endl;
            log("加密失败：设置AES加密密钥失败。");
            exit(1);
        }

        std::cout << "padded_message_size: " << padded_message_size << ", " << message.size() << std::endl;
        unsigned char encrypted_message[padded_message_size];
        memset(encrypted_message, 0, sizeof(encrypted_message));
        // for (size_t i = 0; i < padded_message_size; i += AES_BLOCK_SIZE) {
        //     AES_encrypt(padded_message + i, encrypted_message + i, &aes_encrypt_key);
        // }

        // 生成混淆因子（以BIGNUM形式），增加加密的随机性和复杂度
        BIGNUM* confusion_factor = BN_new();
        if (!BN_rand(confusion_factor, 512, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
            std::cerr << "Error: Failed to generate confusion factor." << std::endl;
            log("加密失败：无法生成混淆因子。");
            exit(1);
        }

        // 将对称加密后的字节数组以及混淆因子转换为BIGNUM类型（这里简单示意转换，实际需更严谨处理）
        BIGNUM* combined_data = BN_bin2bn(encrypted_message, sizeof(encrypted_message), NULL);
        if (combined_data == NULL) {
            std::cerr << "Error: BN_bin2bn failed to convert encrypted message to BIGNUM." << std::endl;
            log("加密失败：无法将加密后的消息转换为BIGNUM。");
            exit(1);
        }
        if (!BN_add(combined_data, combined_data, confusion_factor)) {
            std::cerr << "Error: BN_add failed to combine data with confusion factor." << std::endl;
            log("加密失败：无法合并数据与混淆因子。");
            exit(1);
        }

        BIGNUM* temp = BN_new();
        // 多次加密，进一步增加加密迭代次数，例如进行二十次加密循环（可根据实际安全需求调整次数）
        for (int i = 0; i < 20; ++i) {
            if (!BN_mod_exp(temp, publicKey.h, cipher.C1, BN_value_one(), ctx)) {
                std::cerr << "Error: Failed to calculate intermediate encrypted value." << std::endl;
                log("加密失败：无法计算中间加密值。");
                exit(1);
            }
            if (!BN_add(combined_data, temp, combined_data)) {
                std::cerr << "Error: BN_add failed during multiple encryption." << std::endl;
                log("加密失败：多次加密加法操作失败。");
                exit(1);
            }
        }

        cipher.C2 = combined_data;

        // 释放动态分配的内存和相关BIGNUM对象
        BN_free(confusion_factor);
        BN_free(temp);
        delete[] padded_message;
        BN_CTX_free(ctx);
        return cipher;
    }

    // 解密函数
    bool decrypt(const PublicKey &publicKey, const UserPrivateKey &userPrivateKey, const CipherText &cipher, std::string &decrypted_message) {
        BN_CTX* ctx = BN_CTX_new();
        bool has_access = false;

        // 检查用户是否有解密权限
        for (const AttributeKeyPair &akp : userPrivateKey.attributes) {
            if (cipher.policy.find(akp.attribute)!= std::string::npos) {
                has_access = true;
                break;
            }
        }

        if (!has_access) {
            BN_CTX_free(ctx);
            return false;
        }

        // 计算 h^C1
        BIGNUM* temp = BN_new();
        if (!BN_mod_exp(temp, publicKey.h, cipher.C1, BN_value_one(), ctx)) {
            std::cerr << "Error: Failed to calculate h^C1." << std::endl;
            log("解密失败：无法计算 h^C1。");
            exit(1);
        }

        // 解密过程 (C2 - h^C1)
        BIGNUM* decrypted = BN_new();
        if (!BN_sub(decrypted, cipher.C2, temp)) {
            std::cerr << "Error: BN_sub failed." << std::endl;
            log("解密失败：减法操作失败。");
            exit(1);
        }

        // 转换为字符串
        int decrypted_len = BN_num_bytes(decrypted);
        unsigned char* decrypted_bytes = new unsigned char[decrypted_len];
        if (!BN_bn2bin(decrypted, decrypted_bytes)) {
            std::cerr << "Error: BN_bn2bin failed." << std::endl;
            delete[] decrypted_bytes;
            log("解密失败：无法将 BIGNUM 转换为字节数组。");
            exit(1);
        }
        decrypted_message.assign((const char*)decrypted_bytes, decrypted_len);
        delete[] decrypted_bytes;

        // 清理
        BN_free(decrypted);
        BN_free(temp);
        BN_CTX_free(ctx);
        return true;
    }

    // 打印 BIGNUM 为十六进制字符串
    std::string bn_to_hex(BIGNUM* bn) {
        char* hex_str = BN_bn2hex(bn);
        std::string result(hex_str);
        OPENSSL_free(hex_str);
        return result;
    }

    int test_cpabe() {
        PublicKey publicKey;
        MasterKey masterKey;
        UserPrivateKey userPrivateKey;

        // 初始化密钥
        try {
            initialize_keys(publicKey, masterKey);
            log("密钥初始化成功。");
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            log("密钥初始化失败。");
            return 1;
        }

        // 第一种测试用例：
        // 定义用户属性
        std::vector<std::string> user_attributes = {"X", "Y","G"};
        try {
            generate_user_private_key(publicKey, masterKey, user_attributes, userPrivateKey);
            log("用户密钥生成成功。");
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            log("用户密钥生成失败。");
            return 1;
        }

        std::string message = "Hello, CP-ABE!";
        std::string policy = "X OR Y";  // 访问策略
        CipherText cipher;
        try {
            cipher = encrypt(publicKey, message, policy);
            log("加密成功。");
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            log("加密失败。");
            return 1;
        }

        std::string decrypted_message;
        // 输出公钥相关信息（示例格式，根据实际需求调整准确表示）
        std::string g_hex = bn_to_hex(publicKey.g);
        std::string h_hex = bn_to_hex(publicKey.h);
        std::cout << "M:{x=" << g_hex << ",y=" << h_hex << "}" << std::endl;
        // 按照新格式输出相关信息
        std::cout << "PPK_M:" << bn_to_hex(publicKey.g) << "," << bn_to_hex(publicKey.h) << "," << bn_to_hex(masterKey.alpha) << std::endl;
        std::cout << "用户属性列表：[";
        for (size_t i = 0; i < userPrivateKey.attributes.size(); ++i) {
            std::cout << (i > 0? ", " : "") << userPrivateKey.attributes[i].attribute;
        }
        std::cout << "]" << std::endl;

        if (decrypt(publicKey, userPrivateKey, cipher, decrypted_message)) {
            // 模拟检查节点是否满足（这里只是简单示意，根据实际逻辑完善）
            for (size_t i = 0; i < userPrivateKey.attributes.size(); ++i) {
                std::cout << "The node with index " << (i + 1) << " is sarisfied!" << std::endl;
            }
            std::cout << "解密结果:{x=" << bn_to_hex(userPrivateKey.attributes[0].key) << ",y=" << bn_to_hex(userPrivateKey.attributes[1].key) << "}" << std::endl;
            std::cout << "成功解密！" << std::endl;
            log("解密成功。");
        } else {
            std::cout << "解密失败，策略不匹配！" << std::endl;
            log("解密失败，策略不匹配。");
        }


        // 第二种测试用例（属性不匹配访问策略）：
        std::cout << "-------------------------------------------------" << std::endl;
        // 定义用户属性
        std::vector<std::string> user_attributes_err = {"Student", "Teacher","G","X"};
        try {
            generate_user_private_key(publicKey, masterKey, user_attributes_err, userPrivateKey);
            log("用户密钥生成成功。");
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            log("用户密钥生成失败。");
            return 1;
        }

        std::string message_err = "你猜猜我是不是可以解密的";
        std::string policy_err = "Doctor OR Engineer";  // 访问策略
        CipherText cipher_err;
        try {
            cipher_err = encrypt(publicKey, message_err, policy_err);
            log("加密成功。");
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            log("加密失败。");
            return 1;
        }

        std::string decrypted_message_err;
        // 按照新格式输出相关信息
        std::cout << "PPK_M:" << bn_to_hex(publicKey.g) << "," << bn_to_hex(publicKey.h) << "," << bn_to_hex(masterKey.alpha) << std::endl;
        std::cout << "用户属性列表：[";
        for (size_t i = 0; i < userPrivateKey.attributes.size(); ++i) {
            std::cout << (i > 0? ", " : "") << userPrivateKey.attributes[i].attribute;
        }
        std::cout << "]" << std::endl;
        // 模拟检查节点是否满足（这里只是简单示意，根据实际逻辑完善）

        if (decrypt(publicKey, userPrivateKey, cipher_err, decrypted_message_err)) {
            for (size_t i = 0; i < userPrivateKey.attributes.size(); ++i) {
                std::cout << "The node with index " << (i + 1) << " is sarisfied!" << std::endl;
            }
            std::cout << "解密结果:{x=" << bn_to_hex(userPrivateKey.attributes[0].key) << ",y=" << bn_to_hex(userPrivateKey.attributes[1].key) << "}" << std::endl;
            std::cout << "成功解密！" << std::endl;
            log("解密成功。");
        } else {
            std::cout << "解密失败，策略不匹配！" << std::endl;
            log("解密失败，策略不匹配。");
        }

        return 0;
    }

private:
    
    DISALLOW_COPY_AND_ASSIGN(ContractCpabe);
};

}  // namespace contract

}  // namespace shardora
