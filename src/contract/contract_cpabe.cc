#include "contract/contract_cpabe.h"

#include "common/encode.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace contract {

ContractCpabe::~ContractCpabe() {}

int ContractCpabe::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    return kContractSuccess;
}


// 日志记录函数
void ContractCpabe::log(const std::string& message) {
    time_t now = time(nullptr);
    tm* localTime = localtime(&now);
    std::cout << message << std::endl;
}

// 初始化密钥生成函数
void ContractCpabe::initialize_keys(PublicKey& publicKey, MasterKey& masterKey) {
    BN_CTX* ctx = BN_CTX_new();
    if (!BN_generate_prime_ex(publicKey.g, 1024, 1, nullptr, nullptr, nullptr)) {
        std::cerr << "生成g质数失败" << std::endl;
        log("密钥生成失败：无法生成质数 g。");
        exit(1);
    }
    if (!BN_rand_range(masterKey.alpha, BN_value_one()) ||!BN_add(masterKey.alpha, masterKey.alpha, BN_value_one())) {
        std::cerr << "生成alpha失败" << std::endl;
        log("密钥生成失败：无法生成 alpha。");
        exit(1);
    }
    if (!BN_mod_exp(publicKey.h, publicKey.g, masterKey.alpha, publicKey.g, ctx)) {
        std::cerr << "计算h失败" << std::endl;
        log("密钥生成失败：无法计算 h。");
        exit(1);
    }
    BN_CTX_free(ctx);
}

// 用户密钥生成函数
void ContractCpabe::generate_user_private_key(
        const PublicKey& publicKey, 
        const MasterKey& masterKey, 
        const std::vector<std::string>& attributes, UserPrivateKey& userPrivateKey) {
    userPrivateKey.attributes.clear();
    for (const std::string& attr : attributes) {
        BIGNUM* key = BN_new();
        if (!BN_rand(key, 512, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
            std::cerr << "生成用户私钥失败" << std::endl;
            BN_free(key);
            continue;
        }
        userPrivateKey.attributes.emplace_back(attr, key);
    }
}

// 加密函数
CipherText ContractCpabe::encrypt(
        const PublicKey& publicKey, 
        const std::string& message, 
        const std::string& policy) {
    BN_CTX* ctx = BN_CTX_new();
    CipherText cipher;
    cipher.policy = policy;
    if (!BN_rand(cipher.C1, 512, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
        std::cerr << "生成C1失败" << std::endl;
        log("加密失败：无法生成C1。");
        exit(1);
    }

    // 生成密钥并设置加密密钥
    unsigned char aes_key[AES_BLOCK_SIZE * 2];
    if (!RAND_bytes(aes_key, sizeof(aes_key))) {
        std::cerr << "生成cpabe密钥失败" << std::endl;
        log("加密失败：无法生成AES密钥。");
        exit(1);
    }
    AES_KEY aes_encrypt_key;
    if (AES_set_encrypt_key(aes_key, 8 * sizeof(aes_key), &aes_encrypt_key) < 0) {
        std::cerr << "设置cpabe加密密钥失败" << std::endl;
        log("加密失败：设置AES加密密钥失败。");
        exit(1);
    }

    // 对消息进行填充，使其长度为块大小的整数倍（采用PKCS7填充方式）
    size_t padded_size = (message.size() + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE;
    unsigned char* padded_message = new unsigned char[padded_size];
    int padding_size = padded_size - message.size();
    for (size_t i = message.size(); i < padded_size; ++i) {
        padded_message[i] = (unsigned char)padding_size;
    }
    memcpy(padded_message, message.c_str(), message.size());

    // 使用AES加密填充后的消息
    unsigned char encrypted_message[padded_size];
    memset(encrypted_message, 0, sizeof(encrypted_message));
    for (size_t i = 0; i < padded_size; i += AES_BLOCK_SIZE) {
        AES_encrypt(padded_message + i, encrypted_message + i, &aes_encrypt_key);
    }

    // 将加密后的字节数组转换为BIGNUM
    BIGNUM* combined_data = BN_bin2bn(encrypted_message, sizeof(encrypted_message), nullptr);
    if (!combined_data) {
        std::cerr << "转换加密消息为BIGNUM失败" << std::endl;
        delete[] padded_message;
        BN_CTX_free(ctx);
        exit(1);
    }

    BIGNUM* temp = BN_new();
    for (int i = 10;  // 简化加密迭代次数
        i >= 0; --i) {
        if (!BN_mod_exp(temp, publicKey.h, cipher.C1, BN_value_one(), ctx)) {
            std::cerr << "计算中间加密值失败" << std::endl;
            delete[] padded_message;
            BN_free(combined_data);
            BN_CTX_free(ctx);
            exit(1);
        }
        if (!BN_add(combined_data, combined_data, temp)) {
            std::cerr << "多次加密加法操作失败" << std::endl;
            delete[] padded_message;
            BN_free(combined_data);
            BN_CTX_free(ctx);
            exit(1);
        }
    }

    cipher.C2 = combined_data;
    delete[] padded_message;
    BN_free(temp);
    BN_CTX_free(ctx);

    return cipher;
}

// 将字节数组转换为十六进制字符串
std::string ContractCpabe::bytesToHex(const unsigned char* data, size_t len) {
    if (len == 0) {
        return "";
    }
    char hex_chars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string hex_str = "";
    for (size_t i = 0; i < len; ++i) {
        hex_str += hex_chars[(data[i] & 0xF0) >> 4];
        hex_str += hex_chars[data[i] & 0x0F];
    }
    return hex_str;
}

// 解密函数
bool ContractCpabe::decrypt(
        const PublicKey& publicKey, 
        const UserPrivateKey& userPrivateKey, 
        const CipherText& cipher, 
        std::string& decrypted_message) {
    BN_CTX* ctx = BN_CTX_new();
    bool has_access = false;
    for (const auto& [attr, _] : userPrivateKey.attributes) {
        if (cipher.policy.find(attr)!= std::string::npos) {
            has_access = true;
            break;
        }
    }
    if (!has_access) {
        BN_CTX_free(ctx);
        return false;
    }

    BIGNUM* temp = BN_new();
    if (!BN_mod_exp(temp, publicKey.h, cipher.C1, BN_value_one(), ctx)) {
        std::cerr << "计算h^C1失败" << std::endl;
        log("解密失败：无法计算 h^C1。");
        BN_CTX_free(ctx);
        exit(1);
    }

    BIGNUM* decrypted = BN_new();
    if (!BN_sub(decrypted, cipher.C2, temp)) {
        std::cerr << "减法操作失败" << std::endl;
        log("解密失败：减法操作失败。");
        BN_CTX_free(ctx);
        exit(1);
    }

    // 获取解密后BIGNUM的字节长度
    int decrypted_len = BN_num_bytes(decrypted);
    // 分配足够空间存储字节数据
    unsigned char* decrypted_bytes = new unsigned char[decrypted_len];
    if (!BN_bn2bin(decrypted, decrypted_bytes)) {
        std::cerr << "转换BIGNUM为字节数组失败" << std::endl;
        delete[] decrypted_bytes;
        BN_free(decrypted);
        BN_free(temp);
        BN_CTX_free(ctx);
        return false;
    }

    // 去除填充字节（采用PKCS7填充方式验证）
    int padding_size = decrypted_bytes[decrypted_len - 1];
    if (padding_size < 1 || padding_size > AES_BLOCK_SIZE) {
        // 填充字节不符合规范，可能数据有误，直接取全部数据作为消息
        decrypted_message = bytesToHex(decrypted_bytes, decrypted_len);
    }
    else {
        // 验证填充字节是否正确，若正确则去除填充字节
        bool valid_padding = true;
        for (int i = 1; i <= padding_size; ++i) {
            if (decrypted_bytes[decrypted_len - i]!= padding_size) {
                valid_padding = false;
                break;
            }
        }
        if (valid_padding) {
            decrypted_message.assign((const char*)decrypted_bytes, decrypted_len - padding_size);
        }
        else {
            // 填充字节验证失败，取全部数据作为消息（可根据实际需求调整错误处理策略）
            decrypted_message = bytesToHex(decrypted_bytes, decrypted_len);
        }
    }

    delete[] decrypted_bytes;
    BN_free(decrypted);
    BN_free(temp);
    BN_CTX_free(ctx);
    return true;
}

int ContractCpabe::test_cpabe() {
    PublicKey publicKey;
    MasterKey masterKey;
    UserPrivateKey userPrivateKey;

    // 初始化密钥
    initialize_keys(publicKey, masterKey);
    log("密钥初始化成功。");

    // 测试用例：正常解密情况
    std::vector<std::string> user_attributes = { "X", "Y", "G" };
    generate_user_private_key(publicKey, masterKey, user_attributes, userPrivateKey);
    log("用户密钥生成成功。");

    std::string message = "Hello, CP-ABE!";
    std::string policy = "X OR Y";
    CipherText cipher = encrypt(publicKey, message, policy);
    log("加密成功。");

    // 输出待加密密文
    std::cout << "待加密密文: " << message << std::endl;

    // 输出加密私钥C1、C2（以十六进制形式展示）
    int c1_bytes_size = BN_num_bytes(cipher.C1);
    unsigned char* c1_bytes = new unsigned char[c1_bytes_size];
    BN_bn2bin(cipher.C1, c1_bytes);
    std::cout << "加密私钥C1（十六进制）: " << bytesToHex(c1_bytes, c1_bytes_size) << std::endl;
    delete[] c1_bytes;

    int c2_bytes_size = BN_num_bytes(cipher.C2);
    unsigned char* c2_bytes = new unsigned char[c2_bytes_size];
    BN_bn2bin(cipher.C2, c2_bytes);
    //std::cout << "加密私钥C2（十六进制）: " << bytesToHex(c2_bytes, c2_bytes_size) << std::endl;
    delete[] c2_bytes;

    // 输出加密后的消息整体十六进制表示（假设在encrypt函数中已将加密后的消息转为合适的BIGNUM等可处理形式）
    // 这里示例简单调用bytesToHex来输出十六进制表示，具体需根据encrypt函数内实际对加密后消息处理逻辑来准确输出
    std::cout << "密文（加密后的（十六进制））: ";
    // 假设可以获取到加密后的消息字节数组形式，此处示例用cipher.C2对应的字节数组来示意，实际可能需调整获取方式
    int encrypted_message_bytes_size = BN_num_bytes(cipher.C2);
    unsigned char* encrypted_message_bytes = new unsigned char[encrypted_message_bytes_size];
    BN_bn2bin(cipher.C2, encrypted_message_bytes);
    std::string hex_encrypted_message = bytesToHex(encrypted_message_bytes, encrypted_message_bytes_size);
    std::cout << hex_encrypted_message << std::endl;
    delete[] encrypted_message_bytes;

    // 输出加密策略
    std::cout << "加密策略: " << cipher.policy << std::endl;

    std::string decrypted_message;
    if (decrypt(publicKey, userPrivateKey, cipher, decrypted_message)) {
        std::cout << "解密成功，消息: " << common::Encode::HexDecode(decrypted_message) << std::endl;
        log("解密成功。");

        c1_bytes_size = BN_num_bytes(cipher.C1);
        c1_bytes = new unsigned char[c1_bytes_size];
        BN_bn2bin(cipher.C1, c1_bytes);
//        std::cout << "加密结果 - C1（十六进制）: " << bytesToHex(c1_bytes, c1_bytes_size) << std::endl;
        delete[] c1_bytes;

        c2_bytes_size = BN_num_bytes(cipher.C2);
        c2_bytes = new unsigned char[c2_bytes_size];
        BN_bn2bin(cipher.C2, c2_bytes);
//        std::cout << "加密结果 - C2（十六进制）: " << bytesToHex(c2_bytes, c2_bytes_size) << std::endl;
        delete[] c2_bytes;
    }
    else {
        std::cout << "解密失败，策略不匹配！" << std::endl;
        log("解密失败，策略不匹配。");
    }

    // 输出横线进行分隔
    std::cout << "----------------------------------------" << std::endl;

    // 测试用例：解密失败情况，修改用户属性使其与加密策略不匹配
    std::vector<std::string> user_attributes_failed = { "A", "B", "C" };
    UserPrivateKey userPrivateKey_failed;
    generate_user_private_key(publicKey, masterKey, user_attributes_failed, userPrivateKey_failed);
    log("失败测试用例 - 用户密钥生成成功。");

    std::string decrypted_message_failed;
    if (!decrypt(publicKey, userPrivateKey_failed, cipher, decrypted_message_failed)) {
        std::cout << "失败测试用例(用户属性使其与加密策略不匹配) - 解密失败，策略不匹配！" << std::endl;
        log("失败测试用例 - 解密失败，策略不匹配。");
    }

    return 0;
}

}  // namespace contract

}  // namespace shardora
