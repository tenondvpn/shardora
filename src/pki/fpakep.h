#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

#include <gmssl/sm2.h>
#include <gmssl/error.h>
#include <gmssl/rand.h>
#include <gmssl/sm4.h>
#include <gmssl/sm3.h>

#include "common/log.h"
#include "common/encode.h"

namespace shardora {

namespace fpakep {

class Fpakep {
public:
    // 字符串拼接函数，使用append方法
    std::string concatenateStrings(const std::string& str1, const std::string& str2) {
        std::string result = str1;
        result.append(str2);
        return result;
    }

    // PKCS#7填充
    std::vector<uint8_t> pkcs7_pad(const std::vector<uint8_t>& data, size_t block_size) {
        size_t padding_size = block_size - (data.size() % block_size);
        std::vector<uint8_t> padded_data(data);
        padded_data.insert(padded_data.end(), padding_size, static_cast<uint8_t>(padding_size));
        return padded_data;
    }

    // PKCS#7去填充
    std::vector<uint8_t> pkcs7_unpad(const std::vector<uint8_t>& data) {
        if (data.empty()) return data;
        size_t padding_size = data.back();
        if (padding_size > 16) return data; // Invalid padding
        return std::vector<uint8_t>(data.begin(), data.end() - padding_size);
    }

    // SM4加密
    void sm4_encrypt_h(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& key, std::vector<uint8_t>& ciphertext) {
        SM4_KEY sm4_key;
        sm4_set_encrypt_key(&sm4_key, key.data());

        size_t block_size = 16; // SM4块大小为16字节
        std::vector<uint8_t> padded_plaintext = pkcs7_pad(plaintext, block_size); // 填充明文

        size_t num_blocks = padded_plaintext.size() / block_size;

        ciphertext.resize(num_blocks * block_size, 0); // 初始化密文为零

        for (size_t i = 0; i < num_blocks; ++i) {
            sm4_encrypt(&sm4_key, padded_plaintext.data() + i * block_size, ciphertext.data() + i * block_size);
        }
    }

    // // SM4解密
    // void sm4_decrypt_h(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key, std::vector<uint8_t>& plaintext) {
    //     SM4_KEY sm4_key;
    //     sm4_set_decrypt_key(&sm4_key, key.data());

    //     size_t block_size = 16; // SM4块大小为16字节
    //     size_t num_blocks = ciphertext.size() / block_size;

    //     plaintext.resize(ciphertext.size()); // 初始化明文为零

    //     for (size_t i = 0; i < num_blocks; ++i) {
    //         sm4_decrypt(&sm4_key, ciphertext.data() + i * block_size, plaintext.data() + i * block_size);
    //     }

    //     plaintext = pkcs7_unpad(plaintext); // 去掉填充
    // }

    // SM3哈希计算
    void compute_sm3_hash(const std::string& message, uint8_t hash[32]) {
        SM3_CTX ctx;
        sm3_init(&ctx);
        sm3_update(&ctx, reinterpret_cast<const uint8_t*>(message.data()), message.size());
        sm3_finish(&ctx, hash);
    }

    //打印SM3摘要
    void print_hash(const uint8_t hash[32]) {
        std::cout << "SM3 Hash (hex): ";
        for (int i = 0; i < 32; ++i) {
            std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(hash[i]);
        }
        std::cout << std::dec << std::endl; // 还原为十进制输出
    }

    // 生成SM2密钥对，允许手动设置私钥并根据私钥生成公钥
    bool generate_sm2_keypair(SM2_KEY& key, const uint8_t private_key[32]) {
        // 设置私钥
        memcpy(key.private_key, private_key, 32);

        // 根据私钥生成公钥
        if (sm2_key_generate(reinterpret_cast<SM2_KEY*>(&key)) != 1) {
            std::cerr << "Failed to generate public key from private key." << std::endl;
            return false;
        }

        return true;
    }

    // 打印公私钥
    void print_sm2_key(const SM2_KEY& key) {
        std::cout << "Private Key (hex): ";
        for (int i = 0; i < 32; ++i) {
            printf("%02x", key.private_key[i]);
        }
        std::cout << std::endl;

        std::cout << "Public Key (hex): ";
        for (int i = 0; i < 64; ++i) {
            printf("%02x", key.public_key.X[i]);
            if (i == 31) std::cout << " "; // 分隔x和y
        }
        for (int i = 0; i < 64; ++i) {
            printf("%02x", key.public_key.Y[i]);
        }
        std::cout << std::endl;
    }

    // SM2加密
    std::vector<uint8_t> fp_sm2_encrypt(const SM2_KEY& key, const std::vector<uint8_t>& plaintext) {
        std::vector<uint8_t> ciphertext(plaintext.size() + 128, 0); // 最大开销
        size_t ciphertext_len = 0;

        if (sm2_encrypt(&key, plaintext.data(), plaintext.size(), ciphertext.data(), &ciphertext_len) != 1) {
            std::cerr << "SM2 encryption failed." << std::endl;
            return {};
        }

        ciphertext.resize(ciphertext_len);
        return ciphertext;
    }

    // SM2解密
    std::vector<uint8_t> fp_sm2_decrypt(const SM2_KEY& key, const std::vector<uint8_t>& ciphertext) {
        std::vector<uint8_t> plaintext(ciphertext.size(), 0);
        size_t plaintext_len = 0;

        if (sm2_decrypt(&key, ciphertext.data(), ciphertext.size(), plaintext.data(), &plaintext_len) != 1) {
            std::cerr << "SM2 decryption failed." << std::endl;
            return {};
        }

        plaintext.resize(plaintext_len);
        return plaintext;
    }

    int InitPrivateAndPublicKey(const std::string& init_str, const std::string& des_file) {
        SM2_KEY sm2_key;

        // 示例明文
        std::string plaintext_str_S0 = "This is a test for SM4 encryption about S.";
        std::string plaintext_str_PW = "This is a test for SM4 encryption about PW.";
        //plaintext_str_X1作为另外一方的密文X1
        std::string plaintext_str_X1 = "This is a test for HASH(SM3) encryption about X1.";
        std::string plaintext_str = concatenateStrings(plaintext_str_S0, plaintext_str_PW);
        std::vector<uint8_t> sm4_plaintext(plaintext_str.begin(), plaintext_str.end());
        std::string plaintext_str_M = "This is a test for SM2 encryption about MESSAGE.";
        std::vector<uint8_t> sm2_plaintext(plaintext_str_M.begin(), plaintext_str_M.end());
        // 示例密钥（必须为16字节）
        std::vector<uint8_t> sm4_key = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 }; // 16字节密钥


        //ciphertext作为H（X0,X1）当中的X0
        std::vector<uint8_t> sm4_ciphertext;
        std::vector<uint8_t> decrypted_plaintext;

        // 加密
        sm4_encrypt_h(sm4_plaintext, sm4_key, sm4_ciphertext);

        //将vector<uint8_t>转换为string
        std::string plaintext_str_X0(sm4_ciphertext.begin(), sm4_ciphertext.end());
        std::string plaintext_str_X0_x1 = concatenateStrings(plaintext_str_X0, plaintext_str_X1);

        //生成摘要
        uint8_t sm3_hash[32];
        compute_sm3_hash(plaintext_str_X0_x1, sm3_hash);
        
        print_hash(sm3_hash);

        // Generate the key pair based on the provided private key
        if (!generate_sm2_keypair(sm2_key, sm3_hash)) {
            return -1; // 密钥生成失败
        }

        // 打印公私钥
        print_sm2_key(sm2_key);
         // 加密
        uint8_t out[64];
        sm2_z256_point_to_bytes(&sm2_key.public_key, out);
        uint8_t prikey[32];
        sm2_z256_to_bytes(sm2_key.private_key, prikey);
        std::string private_key = common::Encode::HexEncode(std::string((char*)prikey, sizeof(prikey)));
        std::string public_key = common::Encode::HexEncode(std::string((char*)out, sizeof(out)));
        std::cout << "private key: " << private_key << std::endl;
        std::cout << "public key: " << public_key << std::endl;
        FILE* fd = fopen(des_file.c_str(), "w");
        if (fd == NULL) {
            return 1;
        }

        std::string content = private_key + "," + public_key;
        fwrite(content.c_str(), 1, content.size(), fd);
        fclose(fd);
        return 0;
    }

    int Encrypt(const std::string& data, const std::string& pubkey, const std::string& des_file) {
        SM2_KEY sm2_key;
        auto hex_pk = common::Encode::HexDecode(pubkey);
        uint8_t out[64];
        memcpy(out, hex_pk.c_str(), sizeof(out));
        // sm2_z256_point_from_bytes(&sm2_key.public_key, out);
        std::vector<uint8_t> uint8_hash(data.begin(), data.end());
        std::vector<uint8_t> sm2_ciphertext = fp_sm2_encrypt(sm2_key, uint8_hash);
        if (sm2_ciphertext.empty()) {
            return 1; // 加密失败
        }

        // 输出密文
        std::cout << "sm2 Ciphertext (hex): ";
        for (const auto& byte : sm2_ciphertext) {
            printf("%02x", byte);
        }
        std::cout << std::endl;

        std::string sign(sm2_ciphertext.begin(), sm2_ciphertext.end());
        FILE* fd = fopen(des_file.c_str(), "w");
        if (fd == NULL) {
            return 1;
        }

        std::string hex_sign = common::Encode::HexEncode(sign);
        fwrite(hex_sign.c_str(), 1, hex_sign.size(), fd);
        fclose(fd);
        return 0;
    }

    int Decrypt(const std::string& sign, const std::string& private_key) {
        SM2_KEY sm2_key;
        // auto hex_pk = common::Encode::HexDecode(pubkey);
        // uint8_t out[64];
        // memcpy(out, hex_pk.c_str(), sizeof(out));
        // sm2_z256_point_from_bytes(&sm2_key.public_key, out);
        uint8_t prikey[32];
        auto hex_prik = common::Encode::HexDecode(private_key);
        memcpy(prikey, hex_prik.c_str(), sizeof(prikey));
        sm2_z256_from_bytes(sm2_key.private_key, prikey);
        std::vector<uint8_t> uint8_sign(sign.begin(), sign.end());
        // 解密
        std::vector<uint8_t> verify_hash = fp_sm2_decrypt(sm2_key, uint8_sign);
        std::string verify_str(verify_hash.begin(), verify_hash.end());
        std::cout<< "decrypt: " << verify_str << std::endl;
        return 0;
    }

    int test_fpakep() {

        SM2_KEY sm2_key;

        // 示例明文
        std::string plaintext_str_S0 = "This is a test for SM4 encryption about S.";
        std::string plaintext_str_PW = "This is a test for SM4 encryption about PW.";
        //plaintext_str_X1作为另外一方的密文X1
        std::string plaintext_str_X1 = "This is a test for HASH(SM3) encryption about X1.";
        std::string plaintext_str = concatenateStrings(plaintext_str_S0, plaintext_str_PW);
        std::vector<uint8_t> sm4_plaintext(plaintext_str.begin(), plaintext_str.end());
        std::string plaintext_str_M = "This is a test for SM2 encryption about MESSAGE.";
        std::vector<uint8_t> sm2_plaintext(plaintext_str_M.begin(), plaintext_str_M.end());
        // 示例密钥（必须为16字节）
        std::vector<uint8_t> sm4_key = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 }; // 16字节密钥


        //ciphertext作为H（X0,X1）当中的X0
        std::vector<uint8_t> sm4_ciphertext;
        std::vector<uint8_t> decrypted_plaintext;

        // 加密
        sm4_encrypt_h(sm4_plaintext, sm4_key, sm4_ciphertext);

        //将vector<uint8_t>转换为string
        std::string plaintext_str_X0(sm4_ciphertext.begin(), sm4_ciphertext.end());
        std::string plaintext_str_X0_x1 = concatenateStrings(plaintext_str_X0, plaintext_str_X1);

        //生成摘要
        uint8_t sm3_hash[32];
        compute_sm3_hash(plaintext_str_X0_x1, sm3_hash);
        
        print_hash(sm3_hash);

        // Generate the key pair based on the provided private key
        if (!generate_sm2_keypair(sm2_key, sm3_hash)) {
            return -1; // 密钥生成失败
        }

        // 打印公私钥
        print_sm2_key(sm2_key);

        // 加密
        uint8_t out[64];
        sm2_z256_point_to_bytes(&sm2_key.public_key, out);
        uint8_t prikey[32];
        sm2_z256_to_bytes(sm2_key.private_key, prikey);
        std::string private_key = common::Encode::HexEncode(std::string((char*)prikey, sizeof(prikey)));
        std::string public_key = common::Encode::HexEncode(std::string((char*)out, sizeof(out)));
        std::cout << "private key: " << private_key << std::endl;
        std::cout << "public key: " << public_key << std::endl;
        sm2_z256_point_from_bytes(&sm2_key.public_key, out);
        std::string hash("a4cd1f1f9fd2d85514f93a9426500b68b43c72a59f2b267d45455269071c0761");
        std::vector<uint8_t> uint8_hash(hash.begin(), hash.end());
        
        std::vector<uint8_t> sm2_ciphertext = fp_sm2_encrypt(sm2_key, uint8_hash);
        if (sm2_ciphertext.empty()) {
            return -1; // 加密失败
        }

        // 输出密文
        std::cout << "sm2 Ciphertext (hex): ";
        for (const auto& byte : sm2_ciphertext) {
            printf("%02x", byte);
        }
        std::cout << std::endl;

        std::string sign(sm2_ciphertext.begin(), sm2_ciphertext.end());
        Decrypt(sign, private_key);
        // 解密
        std::vector<uint8_t> sm2_decrypted_plaintext = fp_sm2_decrypt(sm2_key, sm2_ciphertext);
        if (sm2_decrypted_plaintext.empty()) {
            return -1; // 解密失败
        }

        // 输出解密后的明文
        std::string sm2_decrypted_str(sm2_decrypted_plaintext.begin(), sm2_decrypted_plaintext.end());
        std::cout << "sm2 Decrypted plaintext: " << sm2_decrypted_str << std::endl;

        // 验证解密结果
        if (sm2_decrypted_str == hash) {
            std::cout << "SM2 Decryption successful: The decrypted text matches the original plaintext." << std::endl;
        }
        else {
            std::cout << "SM2 Decryption failed: The decrypted text does not match the original plaintext." << std::endl;
        }


        /*sm4解密测试
        // 输出密文
        std::cout << "Ciphertext (hex): ";
        for (const auto& byte : ciphertext) {
            printf("%02x", byte);
        }
        std::cout << std::endl;

        // 解密
        sm4_decrypt_h(ciphertext, sm4_key, decrypted_plaintext);

        // 输出解密后的明文
        std::string sm4_decrypted_str(decrypted_plaintext.begin(), decrypted_plaintext.end());
        std::cout << "SM4 Decrypted plaintext: " << sm4_decrypted_str << std::endl;

        // 验证解密结果
        if (sm4_decrypted_str == plaintext_str) {
            std::cout << "SM4 Decryption successful: The decrypted text matches the original plaintext." << std::endl;
        }
        else {
            std::cout << "SM4 Decryption failed: The decrypted text does not match the original plaintext." << std::endl;
        }
        */
        return 0;
    }

};

}

}
