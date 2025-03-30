#include <iostream>
#include <vector>
#include <random>
#include <stdexcept>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random.hpp>
#include <boost/multiprecision/miller_rabin.hpp>

namespace mp = boost::multiprecision;
using cpp_int = mp::cpp_int;

// 安全随机数生成器
class SecureRNG {
    boost::random::mt19937_64 engine;
public:
    SecureRNG() {
        std::random_device rd;
        engine.seed(rd());
    }
    
    cpp_int generate(cpp_int min, cpp_int max) {
        boost::random::uniform_int_distribution<cpp_int> dist(min, max);
        return dist(engine);
    }
};

// 密码学参数结构体
struct CRSParams {
    cpp_int p, g, h, Z;
    std::pair<cpp_int, cpp_int> A0B0;
    std::pair<cpp_int, cpp_int> A1B1;
    std::pair<cpp_int, cpp_int> U0W01;
    std::pair<cpp_int, cpp_int> U1W10;
};

// 密钥对结构体
struct KeyPair {
    cpp_int sk;
    std::pair<cpp_int, cpp_int> pk;
};

// 模逆元计算
cpp_int modular_inverse(const cpp_int& a, const cpp_int& m) {
    if (m <= 0) throw std::invalid_argument("Modulus must be positive");
    
    cpp_int old_r = a % m, r = m;
    cpp_int old_s = 1, s = 0;

    while (r != 0) {
        cpp_int quotient = old_r / r;
        std::tie(old_r, r) = std::make_tuple(r, old_r - quotient * r);
        std::tie(old_s, s) = std::make_tuple(s, old_s - quotient * s);
    }

    if (old_r != 1) throw std::runtime_error("Inverse does not exist");
    return (old_s % m + m) % m;
}

// 素数生成
cpp_int generate_prime(unsigned bits) {
    SecureRNG rng;
    cpp_int candidate;
    do {
        candidate = rng.generate(cpp_int(1) << (bits-1), (cpp_int(1) << bits) - 1);
    } while (!mp::miller_rabin_test(candidate, 50));
    return candidate;
}

// 系统初始化
CRSParams SETUP(unsigned security_param) {
    SecureRNG rng;
    CRSParams crs;
    
    // 生成素数参数
    crs.p = generate_prime(security_param);
    crs.g = 2;  // 固定原根
    
    // 生成随机参数
    cpp_int a = rng.generate(2, crs.p-2);
    cpp_int b = rng.generate(2, crs.p-2);
    
    // 计算公共参数
    crs.h = mp::powm(crs.g, b, crs.p);
    crs.Z = mp::powm(crs.g, a, crs.p);
    
    // 生成临时参数
    cpp_int t0 = rng.generate(2, crs.p-2);
    cpp_int t1 = rng.generate(2, crs.p-2);
    crs.A0B0.first = mp::powm(crs.g, t0, crs.p);
    crs.A0B0.second = mp::powm(crs.g, a + t0*b, crs.p);
    crs.A1B1.first = mp::powm(crs.g, t1, crs.p);
    crs.A1B1.second = mp::powm(crs.g, a + t1*b, crs.p);
    
    // 生成承诺参数
    cpp_int u0 = rng.generate(2, crs.p-2);
    cpp_int u1 = rng.generate(2, crs.p-2);
    crs.U0W01.first = mp::powm(crs.g, u0, crs.p);
    crs.U1W10.first = mp::powm(crs.g, u1, crs.p);
    crs.U0W01.second = mp::powm(crs.A0B0.first, u1, crs.p);
    crs.U1W10.second = mp::powm(crs.A1B1.first, u0, crs.p);
    
    return crs;
}

// 密钥生成
KeyPair KEYGEN(const CRSParams& crs, int index) {
    SecureRNG rng;
    KeyPair keys;
    
    // 生成私钥
    keys.sk = rng.generate(2, crs.p-2);
    
    // 计算公钥
    keys.pk.first = mp::powm(crs.g, keys.sk, crs.p);
    
    // 根据索引选择参数
    const auto& A = (index == 0) ? crs.A1B1.first : crs.A0B0.first;
    keys.pk.second = mp::powm(A, crs.g, crs.p);
    
    return keys;
}

// 解密函数实现
cpp_int DECRYPT(const CRSParams& crs, 
               const std::pair<cpp_int, cpp_int>& ciphertext,
               const KeyPair& key0,
               const KeyPair& key1) {
    try {
        const auto& [C1, C2] = ciphertext;
        
        // 计算联合私钥分量
        cpp_int combined_sk = (key0.sk + key1.sk) % (crs.p - 1);
        
        // 计算解密因子
        cpp_int s = mp::powm(C1, combined_sk, crs.p);
        
        // 计算模逆元
        cpp_int s_inv = modular_inverse(s, crs.p);
        
        // 恢复原始消息
        cpp_int message = (C2 * s_inv) % crs.p;
        
        return message;
    } catch (const std::exception& e) {
        throw std::runtime_error("解密失败: " + std::string(e.what()));
    }
}

// 密钥聚合
std::tuple<cpp_int, cpp_int, cpp_int> AGGREGATE(const CRSParams& crs, 
                                              const KeyPair& key1, 
                                              const KeyPair& key2) {
    // 聚合T参数
    cpp_int T_sum = (key1.pk.first * key2.pk.first) % crs.p;
    
    // 提取V参数
    return {T_sum, key1.pk.second, key2.pk.second};
}

// 加密算法
std::pair<cpp_int, cpp_int> ENCRYPT(const CRSParams& crs, 
                                  const std::tuple<cpp_int, cpp_int, cpp_int>& agg_key,
                                  const cpp_int& message) {
    SecureRNG rng;
    
    // 解析聚合密钥
    auto [T_sum, V0, V1] = agg_key;
    
    // 生成临时参数
    cpp_int q0 = rng.generate(2, crs.p-2);
    cpp_int q1 = rng.generate(2, crs.p-2);
    
    // 计算加密参数
    cpp_int h1 = mp::powm(crs.g, q0, crs.p);
    cpp_int h2 = mp::powm(crs.g, q1, crs.p);
    
    // 计算密文
    cpp_int tmp_c1 = mp::powm(T_sum, q0, crs.p) * mp::powm(crs.h, q1, crs.p);
    cpp_int C1 = tmp_c1 % crs.p;
    cpp_int tmp_pow = mp::powm(crs.Z, q0 + q1, crs.p);
    cpp_int tmp_c2 = message * tmp_pow;
    cpp_int C2 = tmp_c2 % crs.p;
    
    return {C1, C2};
}

int test_rabpre_main() {
    try {
        // 系统初始化
        const unsigned SECURITY_BITS = 512;
        auto crs = SETUP(SECURITY_BITS);
        
        // 生成密钥对
        auto key0 = KEYGEN(crs, 0);
        auto key1 = KEYGEN(crs, 1);
        
        // 密钥聚合
        auto agg_key = AGGREGATE(crs, key0, key1);
        
        // 加密测试
        cpp_int message = 123456789;
        auto cipher = ENCRYPT(crs, agg_key, message);
        
        // 输出结果
        std::cout << "加密结果:\n"
                  << "C1 = " << cipher.first << "\n"
                  << "C2 = " << cipher.second << std::endl;

        
        // 解密测试
        cpp_int decrypted = DECRYPT(crs, cipher, key0, key1);
        
        std::cout << "\n原始消息: " << message
                  << "\n解密结果: " << decrypted << std::endl;
                  
        // 验证解密正确性
        if (message == decrypted) {
            std::cout << "解密验证成功!" << std::endl;
        } else {
            std::cerr << "解密验证失败!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "系统错误: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
