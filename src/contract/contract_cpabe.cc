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

void ContractCpabe::initialize_keys(
        const std::string& pk_str, 
        const std::string& master_key_str, 
        PublicKey &publicKey, 
        MasterKey &masterKey) {
    auto pk_splits = common::Split<>(pk_str.c_str(), ',');
    BN_CTX* ctx = BN_CTX_new();
    BIGNUM* p = BN_new();
    BN_hex2bn(&p, pk_splits[0]);
    publicKey.p = BIGNUM_ptr(p, BN_free);
    BIGNUM* g = BN_new();
    BN_hex2bn(&g, pk_splits[1]);
    publicKey.g = BIGNUM_ptr(g, BN_free);
    BIGNUM* h = BN_new();
    BN_hex2bn(&h, pk_splits[2]);
    publicKey.h = BIGNUM_ptr(h, BN_free);

    if (!master_key_str.empty()) {
        BIGNUM* alpha = BN_new();
        BN_hex2bn(&alpha, master_key_str.c_str());
        masterKey.alpha = BIGNUM_ptr(alpha, BN_free);
    }

    BN_CTX_free(ctx);
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
    return true;
}

int ContractCpabe::generate_private_and_public_key(
        const std::string& des_file, 
        const std::string& pk_file) {
    // 初始化公钥和主密钥
    PublicKey initPublicKey;
    MasterKey initMasterKey;

    initialize_keys(initPublicKey, initMasterKey);
    std::cout << "init public key: " << initPublicKey.to_string() << std::endl;
    std::cout << "int master key: " << initMasterKey.to_string() << std::endl;

    PublicKey publicKey;
    MasterKey masterKey;
    initialize_keys(initPublicKey.to_string(), initMasterKey.to_string(), publicKey, masterKey);
    std::cout << "public key: " << publicKey.to_string() << std::endl;
    std::cout << "master key: " << masterKey.to_string() << std::endl;

    // 定义第三个用户属性（复杂策略匹配成功）
    std::vector<std::string> user_attributes_complex = {"X", "Y", "Z"};
    UserPrivateKey userPrivateKey_complex;
    generate_user_private_key(publicKey, user_attributes_complex, userPrivateKey_complex);

    if (!des_file.empty()) {
        FILE* fd = fopen(des_file.c_str(), "w");
        std::string private_str;
        for (uint32_t i = 0; i < userPrivateKey_complex.attributes.size(); ++i) {
            std::cout << "user PrivateKey: " << i << ", " << userPrivateKey_complex.attributes[i].to_string() << std::endl;
            private_str += userPrivateKey_complex.attributes[i].to_string() + ",";
        }

        auto des_str = publicKey.to_string() + "-" + masterKey.to_string() + "-" + private_str;
        fwrite(des_str.c_str(), 1, des_str.size(), fd);
        fclose(fd);

        FILE* pk_fd = fopen(pk_file.c_str(), "w");
        fwrite(publicKey.to_string().c_str(), 1, publicKey.to_string().size(), pk_fd);
        fclose(pk_fd);
    }

    return 0;
}

int ContractCpabe::encrypt(
        const std::string& des_file, 
        const std::string& public_key, 
        const std::string& policy, 
        const std::string& plan_text) {
    // 加密消息
    PublicKey publicKey;
    MasterKey masterKey;
    initialize_keys(public_key, "", publicKey, masterKey);
    CipherText cipher3 = encrypt(publicKey, plan_text, policy);
    FILE* fd = fopen(des_file.c_str(), "w");
    auto des_str = cipher3.to_string();
    fwrite(des_str.c_str(), 1, des_str.size(), fd);
    fclose(fd);
    std::cout << "cipher text: " << cipher3.to_string() << std::endl;
}

int ContractCpabe::decrypt(
        const std::string& pk_file, 
        const std::string& cipher_str) {
    FILE* fd = fopen(pk_file.c_str(), "r");
    char file_data[1024*1024];
    fread(file_data, 1, sizeof(file_data), fd);
    fclose(fd);
    auto splits = common::Split<>(file_data, '-');
    if (splits.Count() != 3) {
        return 1;
    }

    BN_CTX* ctx = BN_CTX_new();
    PublicKey publicKey;
    MasterKey masterKey;
    initialize_keys(splits[0], splits[1], publicKey, masterKey);
    UserPrivateKey userPrivateKey;
    auto prikey_splits = common::Split<>(splits[2], ',');
    for (auto i = 0; i < prikey_splits.Count(); ++i) {
        auto tmp_splits = common::Split<>(prikey_splits[i], ':');
        BIGNUM* key = BN_new();
        BN_hex2bn(&key, tmp_splits[1]);
        userPrivateKey.attributes.emplace_back(common::Encode::HexDecode(tmp_splits[0]), key);
    }

    std::cout << "public key: " << publicKey.to_string() << std::endl;
    std::cout << "master key: " << masterKey.to_string() << std::endl;
    // 解密消息
    auto cipher_splits = common::Split<>(cipher_str.c_str(), ',');
    CipherText cipher;
    cipher.policy = common::Encode::HexDecode(cipher_splits[0]);
    BIGNUM* c1 = BN_new();
    BIGNUM* c2 = BN_new();
    BN_hex2bn(&c1, cipher_splits[1]);
    BN_hex2bn(&c2, cipher_splits[2]);
    cipher.C1.reset(c1);
    cipher.C2.reset(c2);
    std::string decrypted_message;
    if (decrypt(publicKey, masterKey, userPrivateKey, cipher, decrypted_message)) {
        std::cout << "dec message: " << decrypted_message << std::endl;
    } else {
        std::cout << "dec invalid." << std::endl;
    }

    BN_CTX_free(ctx);
    return 0;
}

int ContractCpabe::test_cpabe(const std::string& des_file) {
    // 初始化公钥和主密钥
    PublicKey initPublicKey;
    MasterKey initMasterKey;

    initialize_keys(initPublicKey, initMasterKey);
    std::cout << "init public key: " << initPublicKey.to_string() << std::endl;
    std::cout << "int master key: " << initMasterKey.to_string() << std::endl;

    PublicKey publicKey;
    MasterKey masterKey;
    initialize_keys(initPublicKey.to_string(), initMasterKey.to_string(), publicKey, masterKey);
    std::cout << "public key: " << publicKey.to_string() << std::endl;
    std::cout << "master key: " << masterKey.to_string() << std::endl;

    // 定义第三个用户属性（复杂策略匹配成功）
    std::vector<std::string> user_attributes_complex = {"X", "Y", "Z"};
    UserPrivateKey userPrivateKey_complex;
    generate_user_private_key(publicKey, user_attributes_complex, userPrivateKey_complex);

    if (!des_file.empty()) {
        FILE* fd = fopen(des_file.c_str(), "w");
        std::string private_str;
        for (uint32_t i = 0; i < userPrivateKey_complex.attributes.size(); ++i) {
            std::cout << "user PrivateKey: " << i << ", " << userPrivateKey_complex.attributes[i].to_string() << std::endl;
            private_str += userPrivateKey_complex.attributes[i].to_string() + ",";
        }

        auto des_str = publicKey.to_string() + "-" + masterKey.to_string() + "-" + private_str;
        des_str += "-" + 
        fwrite(des_str.c_str(), 1, des_str.size(), fd);
        fclose(fd);
    }

    // 加密消息
    std::string message3 = "Complex Policy Test!";
    std::string policy3 = "(X AND Y) OR Z";  // 访问策略
    std::cout << "plain text: " << message3 <<std::endl;
    std::cout << "policy: " << policy3 <<std::endl;
    CipherText cipher3 = encrypt(publicKey, message3, policy3);
    std::cout << "cipher text: " << cipher3.to_string() << std::endl;

    // 解密消息
    std::string decrypted_message3;
    if (decrypt(publicKey, masterKey, userPrivateKey_complex, cipher3, decrypted_message3)) {
        std::cout << "dec message: " << decrypted_message3 << std::endl;
    } else {
        std::cout << "dec invalid." << std::endl;
    }

    return 0;
}

}  // namespace contract

}  // namespace shardora
