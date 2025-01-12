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

// 将 BIGNUM 转换为十六进制字符串
std::string ContractCpabe::bn_to_hex(const BIGNUM* bn) {
    if (!bn) return "";
    char* hex_str = BN_bn2hex(bn);
    if (!hex_str) return "";
    std::string result(hex_str);
    OPENSSL_free(hex_str);
    return result;
}

// 日志记录函数
void ContractCpabe::log_message(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    time_t time_now = std::chrono::system_clock::to_time_t(now);
    tm* localTime = localtime(&time_now);
    std::cout << message << std::endl;
}

// 判断策略是否匹配
bool ContractCpabe::policy_matches(const std::string& policy_str, const std::vector<std::string>& user_attributes) {
    PolicyParser parser(policy_str);
    std::unique_ptr<PolicyNode> policy_tree = parser.parse();
    return policy_tree->evaluate(user_attributes);
}

// 初始化密钥
void ContractCpabe::initialize_keys(PublicKey &publicKey, MasterKey &masterKey) {
    BN_CTX* ctx = BN_CTX_new();

    // 生成一个大质数 p（512位用于测试，实际应用中应使用2048位或更大）
    if (!BN_generate_prime_ex(publicKey.p.get(), 512, 1, NULL, NULL, NULL)) {
        std::cerr << "Error: Failed to generate prime p." << std::endl;
        log_message("密钥生成失败：无法生成质数 p。");
        exit(1);
    }

    // 设置生成元 g = 2
    if (!BN_set_word(publicKey.g.get(), 2)) {
        std::cerr << "Error: Failed to set generator g." << std::endl;
        log_message("密钥生成失败：无法设置生成元 g。");
        exit(1);
    }

    // 生成主密钥 alpha，1 < alpha < p-1
    if (!BN_rand_range(masterKey.alpha.get(), publicKey.p.get())) {
        std::cerr << "Error: Failed to generate alpha." << std::endl;
        log_message("密钥生成失败：无法生成 alpha。");
        exit(1);
    }

    // 确保 alpha != 0
    if (BN_is_zero(masterKey.alpha.get())) {
        // 设置 alpha = 1
        masterKey.alpha.reset(BN_new());
        if (!BN_set_word(masterKey.alpha.get(), 1)) {
            std::cerr << "Error: Failed to set alpha to 1." << std::endl;
            log_message("密钥生成失败：无法设置 alpha。");
            exit(1);
        }
    }

    // 计算 h = g^alpha mod p
    if (!BN_mod_exp(publicKey.h.get(), publicKey.g.get(), masterKey.alpha.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate h." << std::endl;
        log_message("密钥生成失败：无法计算 h。");
        exit(1);
    }

    BN_CTX_free(ctx);
    log_message("密钥初始化成功。");
}

// 生成用户私钥
void ContractCpabe::generate_user_private_key(const PublicKey &publicKey, const std::vector<std::string>& attributes, UserPrivateKey &userPrivateKey) {
    userPrivateKey.attributes.clear();
    BN_CTX* ctx = BN_CTX_new(); // 创建 BN_CTX

    for (const std::string &attr : attributes) {
        BIGNUM* key = BN_new();

        // 生成一个随机密钥，1 < key < p-1
        if (!BN_rand_range(key, publicKey.p.get())) {
            BN_free(key);
            std::cerr << "Error: Failed to generate user private key." << std::endl;
            log_message("用户密钥生成失败：无法生成用户私钥。");
            BN_CTX_free(ctx);
            exit(1);
        }

        // 确保 key != 0
        if (BN_is_zero(key)) {
            BN_free(key);
            key = BN_new();
            if (!BN_set_word(key, 1)) { // 设置为1作为默认值
                std::cerr << "Error: Failed to set user key to 1." << std::endl;
                log_message("用户密钥生成失败：无法设置用户密钥。");
                BN_CTX_free(ctx);
                exit(1);
            }
        }

        userPrivateKey.attributes.emplace_back(attr, key);
    }

    BN_CTX_free(ctx);
    log_message("用户私钥生成成功。");
}

// 加密函数
CipherText ContractCpabe::encrypt(const PublicKey &publicKey, const std::string &message, const std::string &policy) {
    BN_CTX* ctx = BN_CTX_new();
    CipherText cipher;
    cipher.policy = policy;

    // 将消息转换为 BIGNUM
    BIGNUM_ptr m(BN_bin2bn(reinterpret_cast<const unsigned char*>(message.c_str()), message.size(), BN_new()), BN_free);
    if (!m) {
        std::cerr << "Error: BN_bin2bn failed to convert message to BIGNUM." << std::endl;
        log_message("加密失败：无法将消息转换为 BIGNUM。");
        exit(1);
    }

    // 生成随机数 r，1 < r < p-1
    BIGNUM_ptr r(BN_new(), BN_free);
    if (!BN_rand_range(r.get(), publicKey.p.get())) {
        std::cerr << "Error: Failed to generate random r." << std::endl;
        log_message("加密失败：无法生成随机数 r。");
        exit(1);
    }

    // 确保 r != 0
    if (BN_is_zero(r.get())) {
        if (!BN_set_word(r.get(), 1)) {
            std::cerr << "Error: Failed to set r to 1." << std::endl;
            log_message("加密失败：无法设置 r。");
            exit(1);
        }
    }

    // 计算 C1 = g^r mod p
    if (!BN_mod_exp(cipher.C1.get(), publicKey.g.get(), r.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate C1." << std::endl;
        log_message("加密失败：无法计算 C1。");
        exit(1);
    }

    // 计算 h^r mod p
    BIGNUM_ptr hr(BN_new(), BN_free);
    if (!BN_mod_exp(hr.get(), publicKey.h.get(), r.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate h^r." << std::endl;
        log_message("加密失败：无法计算 h^r。");
        exit(1);
    }

    // 计算 C2 = m * h^r mod p
    if (!BN_mod_mul(cipher.C2.get(), m.get(), hr.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate C2." << std::endl;
        log_message("加密失败：无法计算 C2。");
        exit(1);
    }

    // 清理
    BN_CTX_free(ctx);
    log_message("加密成功。");
    return std::move(cipher);
}

// 解密函数
bool ContractCpabe::decrypt(const PublicKey &publicKey, const MasterKey &masterKey, const UserPrivateKey &userPrivateKey, const CipherText &cipher, std::string &decrypted_message) {
    BN_CTX* ctx = BN_CTX_new();
    bool has_access = false;

    // 提取用户属性
    std::vector<std::string> user_attrs;
    for (const auto& akp : userPrivateKey.attributes) {
        user_attrs.push_back(akp.attribute);
    }

    // 检查用户是否满足访问策略
    has_access = policy_matches(cipher.policy, user_attrs);

    if (!has_access) {
        log_message("解密失败，策略不匹配。");
        BN_CTX_free(ctx);
        return false;
    }

    // 计算 s = C1^alpha mod p
    BIGNUM_ptr s(BN_new(), BN_free);
    if (!BN_mod_exp(s.get(), cipher.C1.get(), masterKey.alpha.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate s = C1^alpha mod p." << std::endl;
        log_message("解密失败：无法计算 s。");
        exit(1);
    }

    // 计算 s^{-1} mod p
    BIGNUM_ptr s_inv(BN_new(), BN_free);
    if (!BN_mod_inverse(s_inv.get(), s.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate s^{-1} mod p." << std::endl;
        log_message("解密失败：无法计算 s 的逆元。");
        exit(1);
    }

    // 计算 m = C2 * s^{-1} mod p
    BIGNUM_ptr m(BN_new(), BN_free);
    if (!BN_mod_mul(m.get(), cipher.C2.get(), s_inv.get(), publicKey.p.get(), ctx)) {
        std::cerr << "Error: Failed to calculate decrypted message m." << std::endl;
        log_message("解密失败：无法计算解密后的消息。");
        exit(1);
    }

    // 将 BIGNUM m 转换为字符串
    int num_bytes = BN_num_bytes(m.get());
    std::unique_ptr<unsigned char[]> buffer(new unsigned char[num_bytes]);
    BN_bn2bin(m.get(), buffer.get());

    decrypted_message.assign(reinterpret_cast<char*>(buffer.get()), num_bytes);

    // 清理
    BN_CTX_free(ctx);
    log_message("解密成功。");
    return true;
}

int ContractCpabe::test_cpabe() {
    // 初始化公钥和主密钥
    PublicKey publicKey;
    MasterKey masterKey;

    initialize_keys(publicKey, masterKey);

    // 定义第一个用户属性（匹配策略）
    std::vector<std::string> user_attributes_success = {"X", "Y"};
    UserPrivateKey userPrivateKey_success;
    generate_user_private_key(publicKey, user_attributes_success, userPrivateKey_success);

    // 定义第二个用户属性（不匹配策略）
    std::vector<std::string> user_attributes_failure = {"A", "B"};
    UserPrivateKey userPrivateKey_failure;
    generate_user_private_key(publicKey, user_attributes_failure, userPrivateKey_failure);

    // 定义第三个用户属性（复杂策略匹配成功）
    std::vector<std::string> user_attributes_complex = {"X", "Y", "Z"};
    UserPrivateKey userPrivateKey_complex;
    generate_user_private_key(publicKey, user_attributes_complex, userPrivateKey_complex);

    // 定义第四个用户属性（复杂策略匹配失败）
    std::vector<std::string> user_attributes_complex_fail = {"X", "Z"};
    UserPrivateKey userPrivateKey_complex_fail;
    generate_user_private_key(publicKey, user_attributes_complex_fail, userPrivateKey_complex_fail);

    // 显示公钥信息
    std::cout << "公钥 (p, g, h): {"
        << bn_to_hex(publicKey.p.get()) << ", "
        << bn_to_hex(publicKey.g.get()) << ", "
        << bn_to_hex(publicKey.h.get()) << "}" << std::endl;

    // 显示主密钥信息（通常不应公开，但为了测试展示）
    std::cout << "主密钥 (alpha): " << bn_to_hex(masterKey.alpha.get()) << std::endl;

    // ----------- 测试用例1：策略匹配成功 -----------
    std::cout << "\n--- 测试用例1：策略匹配成功 ---" << std::endl;
    // 显示用户属性
    std::cout << "用户属性列表：[";
    for (size_t i = 0; i < userPrivateKey_success.attributes.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << userPrivateKey_success.attributes[i].attribute;
    }
    std::cout << "]" << std::endl;

    // 加密消息
    std::string message1 = "Hello, CP-ABE!";
    std::cout << "待加密密文：" << message1 <<std::endl ;
    std::string policy1 = "X OR Y";  // 访问策略
    CipherText cipher1 = encrypt(publicKey, message1, policy1);

    // 显示密文
    std::cout << "密文:" << std::endl;
    std::cout << "C1: " << bn_to_hex(cipher1.C1.get()) << std::endl;
    std::cout << "C2: " << bn_to_hex(cipher1.C2.get()) << std::endl;

    // 解密消息
    std::string decrypted_message1;
    if (decrypt(publicKey, masterKey, userPrivateKey_success, cipher1, decrypted_message1)) {
        std::cout << "解密后的消息: " << decrypted_message1 << std::endl;
    } else {
        std::cout << "解密失败，策略不匹配！" << std::endl;
    }

    // ----------- 测试用例2：策略匹配失败 -----------
    std::cout << "\n--- 测试用例2：策略匹配失败 ---" << std::endl;
    // 显示用户属性
    std::cout << "用户属性列表：[";
    for (size_t i = 0; i < userPrivateKey_failure.attributes.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << userPrivateKey_failure.attributes[i].attribute;
    }
    std::cout << "]" << std::endl;

    // 加密消息
    std::string message2 = "你猜猜我是不是可以解密的";
    std::string policy2 = "Doctor OR Engineer";  // 访问策略
    CipherText cipher2 = encrypt(publicKey, message2, policy2);

    // 显示密文
    std::cout << "密文:" << std::endl;
    std::cout << "C1: " << bn_to_hex(cipher2.C1.get()) << std::endl;
    std::cout << "C2: " << bn_to_hex(cipher2.C2.get()) << std::endl;

    // 解密消息
    std::string decrypted_message2;
    if (decrypt(publicKey, masterKey, userPrivateKey_failure, cipher2, decrypted_message2)) {
        std::cout << "解密后的消息: " << decrypted_message2 << std::endl;
    } else {
        std::cout << "解密失败，策略不匹配！" << std::endl;
    }

    // ----------- 测试用例3：复杂策略匹配成功 -----------
    std::cout << "\n--- 测试用例3：复杂策略匹配成功 ---" << std::endl;
    // 显示用户属性
    std::cout << "用户属性列表：[";
    for (size_t i = 0; i < userPrivateKey_complex.attributes.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << userPrivateKey_complex.attributes[i].attribute;
    }
    std::cout << "]" << std::endl;

    // 加密消息
    std::string message3 = "Complex Policy Test!";
    std::cout << "待加密密文3：" << message3 <<std::endl;
    std::string policy3 = "(X AND Y) OR Z";  // 访问策略
    CipherText cipher3 = encrypt(publicKey, message3, policy3);

    // 显示密文
    std::cout << "密文:" << std::endl;
    std::cout << "C1: " << bn_to_hex(cipher3.C1.get()) << std::endl;
    std::cout << "C2: " << bn_to_hex(cipher3.C2.get()) << std::endl;

    // 解密消息
    std::string decrypted_message3;
    if (decrypt(publicKey, masterKey, userPrivateKey_complex, cipher3, decrypted_message3)) {
        std::cout << "解密后的消息: " << decrypted_message3 << std::endl;
    } else {
        std::cout << "解密失败，策略不匹配！" << std::endl;
    }

    // ----------- 测试用例4：复杂策略匹配失败 -----------
    std::cout << "\n--- 测试用例4：复杂策略匹配失败 ---" << std::endl;
    // 显示用户属性
    std::cout << "用户属性列表：[";
    for (size_t i = 0; i < userPrivateKey_complex_fail.attributes.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << userPrivateKey_complex_fail.attributes[i].attribute;
    }
    std::cout << "]" << std::endl;

    // 加密消息
    std::string message4 = "Another Complex Policy Test!";
    std::string policy4 = "X AND (Y OR NOT Z)";  // 访问策略
    CipherText cipher4 = encrypt(publicKey, message4, policy4);

    // 显示密文
    std::cout << "密文:" << std::endl;
    std::cout << "C1: " << bn_to_hex(cipher4.C1.get()) << std::endl;
    std::cout << "C2: " << bn_to_hex(cipher4.C2.get()) << std::endl;

    // 解密消息
    std::string decrypted_message4;
    if (decrypt(publicKey, masterKey, userPrivateKey_complex_fail, cipher4, decrypted_message4)) {
        std::cout << "解密后的消息: " << decrypted_message4 << std::endl;
    } else {
        std::cout << "解密失败，策略不匹配！" << std::endl;
    }

    return 0;
}

}  // namespace contract

}  // namespace shardora
