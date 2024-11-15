#include "contract/contract_ars.h"

namespace shardora {

namespace contract {

ContractArs::ContractArs(
    const std::string& create_address, 
    const std::string& pairing_param): ContractInterface("") {}

ContractArs::~ContractArs() {}

int ContractArs::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    return kContractSuccess;
}

// 初始化系统参数
ContractArs::ContractArs() : ContractInterface("") {
    std::string param("type a\n"
        "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
        "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
        "r 730750818665451621361119245571504901405976559617\n"
        "exp2 159\n"
        "exp1 107\n"
        "sign1 1\n"
        "sign0 1");
    pairing_init_set_buf(pairing, param.c_str(), param.size());
    element_init_G1(G, pairing);
    element_random(G); // 使用随机生成的非单位元生成元
    unsigned char bytes_data[2048];
    auto len = element_to_bytes_compressed(bytes_data, G);
    std::string g_data = std::string((const char*)bytes_data, len);
    element_from_bytes_compressed(G, (unsigned char*)g_data.c_str());
    element_init_G2(H, pairing);
    element_random(H); // 使用随机生成的非单位元生成元
    q = pairing_length_in_bytes_x_only_G1(pairing);
    char data[10240] = {0};
    element_snprintf(data, sizeof(data), "G: %B, H: %B, bytes: %s", G, H, common::Encode::HexEncode(g_data).c_str());
    ZJC_DEBUG("init paring ars: %s", data);
}

// 密钥生成
void ContractArs::KeyGen(element_t &x_i, element_t &y_i) {
    element_init_Zr(x_i, pairing);
    element_init_G2(y_i, pairing);

    element_random(x_i);
    element_printf("H (generator of G2): %B\n", H);
    element_printf("Private key x_i: %B\n", x_i);
    element_pow_zn(y_i, H, x_i);

    // 调试输出密钥对
    std::cout << "Generated key pair:" << std::endl;
    element_printf("Private key x_i: %B\n", x_i);
    element_printf("Public key y_i: %B\n", y_i);
}

// 将一个 element_t 类型的群元素转换为字符串格式
std::string ContractArs::element_to_string(element_t &element) {
    unsigned char buffer[1024];
    int length = element_to_bytes(buffer, element);
    std::stringstream hex_stream;
    for (int i = 0; i < length; i++)
    {
        hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }
    return hex_stream.str();
}

// 单签名生成
void ContractArs::SingleSign(
        const std::string &message, element_t &x_i, element_t &y_i,
        const std::vector<element_t *> &ring, element_t &delta_prime_i,
        element_t &y_prime_i, std::vector<element_s> &pi_i) {
    element_t delta_i, H_m, r_i;
    element_init_G1(delta_i, pairing);
    element_init_G1(H_m, pairing);
    element_init_Zr(r_i, pairing);

    hash_to_group(H_m, message);
    element_pow_zn(delta_i, H_m, x_i);

    element_random(r_i);
    element_init_G1(delta_prime_i, pairing);
    element_pow_zn(delta_prime_i, H_m, r_i);
    element_add(delta_prime_i, delta_i, delta_prime_i);

    element_init_G2(y_prime_i, pairing);
    element_pow_zn(y_prime_i, H, r_i);
    element_printf("y_prime_i: %B\n", y_prime_i);
    element_printf("y_i: %B\n", y_i);
    element_add(y_prime_i, y_i, y_prime_i);
    element_printf("y_prime_i: %B\n", y_prime_i);

    element_t x_prime, r_prime, t1, t2, c, s1, s2;
    element_init_Zr(x_prime, pairing);
    element_init_Zr(r_prime, pairing);
    element_init_G1(t1, pairing);
    element_init_G1(t2, pairing);
    element_init_G1(c, pairing);
    element_init_Zr(s1, pairing);
    element_init_Zr(s2, pairing);

    element_random(x_prime);
    element_random(r_prime);
    element_pow_zn(t1, H, x_prime);
    element_pow_zn(t2, H, r_prime);

    // 构建哈希输入，包括 t1, t2, y_prime_i, 以及整个环 ring
    std::string hash_input = element_to_string(t1) + element_to_string(t2) + element_to_string(y_prime_i);
    for (const auto &y : ring) {
        hash_input += element_to_string(*y); // 将所有 y_i 依次加入 hash_input 中
    }

    // 将 hash_input 哈希到 G1 群中得到挑战值 c
    hash_to_group(c, hash_input);

    element_pow_zn(s1, c, x_i);
    element_add(s1, s1, x_prime);

    element_pow_zn(s2, c, r_i);
    element_add(s2, s2, r_prime);

    pi_i.clear();
    pi_i.push_back(*t1);
    pi_i.push_back(*t2);
    pi_i.push_back(*s1);
    pi_i.push_back(*s2);

    // 调试输出签名生成的值
    std::cout << "SingleSign generated signature for message \"" << message << "\":" << std::endl;
    element_printf("delta_prime_i: %B\n", delta_prime_i);
    element_printf("y_prime_i: %B\n", y_prime_i);
    element_printf("t1: %B\n", t1);
    element_printf("t2: %B\n", t2);
    element_printf("s1: %B\n", s1);
    element_printf("s2: %B\n", s2);
}

// 聚合签名生成
void ContractArs::AggreSign(
        const std::vector<std::string> &messages, std::vector<element_t> &y_primes,
        std::vector<element_t> &delta_primes, std::vector<std::vector<element_s>> &pi_i,
        const std::vector<element_t *> &ring, element_t &agg_signature) {
    element_t product;
    element_init_G1(product, pairing);
    element_set1(product);
    for (size_t i = 0; i < messages.size(); ++i) {
        element_t H_m;
        element_init_G1(H_m, pairing);
        hash_to_group(H_m, messages[i]);

        element_t lhs, rhs;
        element_init_GT(lhs, pairing);
        element_init_GT(rhs, pairing);
        pairing_apply(lhs, delta_primes[i], H, pairing); // e(δ'_i, H)
        pairing_apply(rhs, H_m, y_primes[i], pairing);   // e(H(m_i), y'_i)

        std::cout << "Message: " << messages[i] << std::endl;
        element_printf("lhs (e(delta'_i, H)): %B\n", lhs);
        element_printf("rhs (e(H(m_i), y'_i)): %B\n", rhs);

        if (element_cmp(lhs, rhs) != 0) {
            std::cerr << "BLS signature verification failed for message " << i << std::endl;
            element_clear(H_m);
            element_clear(lhs);
            element_clear(rhs);
            return;
        }

        element_clear(lhs);
        element_clear(rhs);
        bool proof_valid = false;
        for (const auto &y : ring) {
            if (VerifyProof(pi_i[i], y_primes[i], delta_primes[i], messages[i], ring, *y)) {
                proof_valid = true;
                break;
            }
        }

        if (!proof_valid) {
            std::cerr << "Proof verification failed for message " << i << std::endl;
            element_clear(H_m);
            return;
        }

        element_mul(product, product, delta_primes[i]);
        element_clear(H_m);
    }

    element_init_G1(agg_signature, pairing);
    element_set(agg_signature, product);
    element_clear(product);

    std::cout << "Aggregate signature generated: ";
    element_printf("%B\n", agg_signature);
}

// 聚合签名验证
bool ContractArs::AggreVerify(
        const std::vector<std::string> &messages, 
        element_t &agg_signature,
        std::vector<element_t> &y_primes) {
    element_t product, H_m;
    element_init_GT(product, pairing);
    element_set1(product);

    for (size_t i = 0; i < messages.size(); ++i) {
        element_init_G1(H_m, pairing);
        hash_to_group(H_m, messages[i]);

        element_t temp;
        element_init_GT(temp, pairing);
        pairing_apply(temp, H_m, y_primes[i], pairing);
        element_mul(product, product, temp);
        element_clear(temp);

        std::cout << "Message: " << messages[i] << std::endl;
        element_printf("Current product: %B\n", product);
    }

    element_t left_side;
    element_init_GT(left_side, pairing);
    pairing_apply(left_side, agg_signature, H, pairing);

    std::cout << "Aggregate signature verification result: ";
    element_printf("%B\n", left_side);

    bool is_valid = element_cmp(left_side, product) == 0;

    element_clear(product);
    element_clear(left_side);

    return is_valid;
}

// Sigma 证明验证
bool ContractArs::VerifyProof(
        std::vector<element_s> &pi, element_t &y_prime,
        element_t &delta_prime, const std::string &message,
        const std::vector<element_t *> &ring, element_t &y_i) {
    element_t c, t1, t2, s1, s2, H_m;
    element_init_G1(c, pairing);
    element_init_G1(t1, pairing);
    element_init_G1(t2, pairing);
    element_init_Zr(s1, pairing);
    element_init_Zr(s2, pairing);
    element_init_G1(H_m, pairing);

    hash_to_group(H_m, message);

    element_set(t1, &pi[0]);
    element_set(t2, &pi[1]);
    element_set(s1, &pi[2]);
    element_set(s2, &pi[3]);

    std::cout << "Verifying proof for message \"" << message << "\":" << std::endl;
    element_printf("t1: %B\n", t1);
    element_printf("t2: %B\n", t2);
    element_printf("s1: %B\n", s1);
    element_printf("s2: %B\n", s2);

    std::string hash_input = element_to_string(t1) + element_to_string(t2) + element_to_string(y_prime);
    for (const auto &y : ring) {
        hash_input += element_to_string(*y);
    }

    hash_to_group(c, hash_input);

    element_t left1, right1, left2, right2, diff;
    element_init_G1(left1, pairing);
    element_init_G1(right1, pairing);
    element_init_G1(left2, pairing);
    element_init_G1(right2, pairing);
    element_init_G1(diff, pairing);

    element_pow_zn(left1, H, s1);
    element_pow_zn(right1, y_i, c);
    element_add(right1, t1, right1);

    element_pow_zn(left2, H, s2);
    element_sub(diff, y_prime, y_i);
    element_pow_zn(right2, diff, c);
    element_add(right2, t2, right2);

    std::cout << "Proof verification comparisons:" << std::endl;
    element_printf("left1: %B, right1: %B\n", left1, right1);
    element_printf("left2: %B, right2: %B\n", left2, right2);

    bool result = element_cmp(left1, right1) == 0 && element_cmp(left2, right2) == 0;

    element_clear(c);
    element_clear(t1);
    element_clear(t2);
    element_clear(s1);
    element_clear(s2);
    element_clear(H_m);
    element_clear(left1);
    element_clear(right1);
    element_clear(left2);
    element_clear(right2);
    element_clear(diff);

    return result;
}

pairing_t& ContractArs::get_pairing() {
    return pairing;
}

// 私有哈希函数，将消息哈希为群元素
void ContractArs::hash_to_group(element_t &result, const std::string &message) {
    // Step 1: 将字符串转换为二进制格式
    std::string binary_representation;
    for (char c : message) {
        for (int i = 7; i >= 0; --i) {
            binary_representation += ((c >> i) & 1) ? '1' : '0';
        }
    }

    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256_ctx;

    // Step 2: 初始化和更新 SHA256 上下文
    SHA256_Init(&sha256_ctx);
    SHA256_Update(&sha256_ctx, reinterpret_cast<const unsigned char *>(binary_representation.data()), binary_representation.size());
    SHA256_Final(md, &sha256_ctx);

    // Step 3: 将哈希转换为群元素
    element_from_hash(result, md, SHA256_DIGEST_LENGTH);

    // Step 4: 输出调试信息
    std::cout << "for message \"" << message << "\":" << std::endl;
    std::cout << "Hashing binary representation of message \"" << binary_representation << "\" to group element: ";
    element_printf("%B\n", result);
}

}  // namespace contract

}  // namespace shardora
