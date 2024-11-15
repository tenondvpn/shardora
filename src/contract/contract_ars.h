#pragma once

#include <vector>
#include <iostream>
#include <vector>
#include <memory>
#include <pbc/pbc.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "cpppbc/PBC.h"
#include "db/db.h"

namespace shardora {

namespace contract {

class ContractArs : public ContractInterface {
public:
    ContractArs(const std::string &create_address, const std::string &pairing_param);
    virtual ~ContractArs();
    virtual int call(
        const CallParameters &param,
        uint64_t gas,
        const std::string &origin_address,
        evmc_result *res);
    // 初始化系统参数
    ContractArs();
    // 密钥生成
    void KeyGen(element_t &x_i, element_t &y_i);
    // 将一个 element_t 类型的群元素转换为字符串格式
    std::string element_to_string(element_t &element);
    // 单签名生成
    void SingleSign(const std::string &message, element_t &x_i, element_t &y_i,
                    const std::vector<element_t *> &ring, element_t &delta_prime_i,
                    element_t &y_prime_i, std::vector<element_s> &pi_i);
    // 聚合签名生成
    void AggreSign(const std::vector<std::string> &messages, std::vector<element_t> &y_primes,
                   std::vector<element_t> &delta_primes, std::vector<std::vector<element_s>> &pi_i,
                   const std::vector<element_t *> &ring, element_t &agg_signature);
    // 聚合签名验证
    bool AggreVerify(const std::vector<std::string> &messages, element_t &agg_signature,
                     std::vector<element_t> &y_primes);
    // Sigma 证明验证
    bool VerifyProof(std::vector<element_s> &pi, element_t &y_prime,
                     element_t &delta_prime, const std::string &message,
                     const std::vector<element_t *> &ring, element_t &y_i);
    pairing_t &get_pairing();

private:
    // 私有哈希函数，将消息哈希为群元素
    void hash_to_group(element_t &result, const std::string &message);
    pairing_t pairing;
    element_t G, H;
    size_t q;

};

}  // namespace contract

}  // namespace shardora
