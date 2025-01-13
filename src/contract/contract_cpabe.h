#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <iomanip>
#include <cstring>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cctype>

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/aes.h>

#include "common/encode.h"
#include "common/split.h"
#include "common/tick.h"
#include "contract/contract_interface.h"
#include "db/db.h"
#include "pki/pki_ib_agka.h"
#include "pki/param.h"

namespace shardora {

namespace contract {


// 将 BIGNUM 转换为十六进制字符串
inline static std::string bn_to_hex(const BIGNUM* bn) {
    if (!bn) return "";
    char* hex_str = BN_bn2hex(bn);
    if (!hex_str) return "";
    std::string result(hex_str);
    OPENSSL_free(hex_str);
    return result;
}

// 定义一个智能指针类型，用于管理 BIGNUM 对象
using BIGNUM_ptr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;

// 结构体：属性-密钥对
struct AttributeKeyPair {
    std::string attribute;
    BIGNUM_ptr key;

    AttributeKeyPair(const std::string& attr, BIGNUM* key_val)
        : attribute(attr), key(key_val, BN_free) {}
    std::string to_string() {
        return common::Encode::HexEncode(attribute) + "," + bn_to_hex(key.get());
    }
};

// 结构体：公钥
struct PublicKey {
    BIGNUM_ptr p;  // 模数
    BIGNUM_ptr g;  // 生成元
    BIGNUM_ptr h;  // 公钥部分

    PublicKey()
        : p(BN_new(), BN_free),
          g(BN_new(), BN_free),
          h(BN_new(), BN_free) {}

    // 禁止拷贝
    PublicKey(const PublicKey&) = delete;
    PublicKey& operator=(const PublicKey&) = delete;

    // 允许移动
    PublicKey(PublicKey&&) noexcept = default;
    PublicKey& operator=(PublicKey&&) noexcept = default;
    std::string to_string() {
        return bn_to_hex(p.get()) + "," + 
            bn_to_hex(g.get()) + "," +
            bn_to_hex(h.get());
    }
};

// 结构体：主密钥
struct MasterKey {
    BIGNUM_ptr alpha;  // 主密钥

    MasterKey() : alpha(BN_new(), BN_free) {}

    // 禁止拷贝
    MasterKey(const MasterKey&) = delete;
    MasterKey& operator=(const MasterKey&) = delete;

    // 允许移动
    MasterKey(MasterKey&&) noexcept = default;
    MasterKey& operator=(MasterKey&&) noexcept = default;
    std::string to_string() {
        return bn_to_hex(alpha.get());
    }
};

// 结构体：用户私钥
struct UserPrivateKey {
    std::vector<AttributeKeyPair> attributes; // 用户属性-密钥对

    // 默认构造
    UserPrivateKey() = default;

    // 禁止拷贝
    UserPrivateKey(const UserPrivateKey&) = delete;
    UserPrivateKey& operator=(const UserPrivateKey&) = delete;

    // 允许移动
    UserPrivateKey(UserPrivateKey&&) noexcept = default;
    UserPrivateKey& operator=(UserPrivateKey&&) noexcept = default;

};

// 结构体：密文
struct CipherText {
    std::string policy;  // 访问策略
    BIGNUM_ptr C1;  // 密文部分1
    BIGNUM_ptr C2;  // 密文部分2

    CipherText()
        : C1(BN_new(), BN_free),
          C2(BN_new(), BN_free) {}

    // 禁止拷贝
    CipherText(const CipherText&) = delete;
    CipherText& operator=(const CipherText&) = delete;

    // 允许移动
    CipherText(CipherText&&) noexcept = default;
    CipherText& operator=(CipherText&&) noexcept = default;
    std::string to_string() {
        return common::Encode::HexEncode(policy) + "," + bn_to_hex(C1.get()) + "," + bn_to_hex(C2.get());
    }
};


// 策略节点基类
struct PolicyNode {
    virtual ~PolicyNode() {}
    virtual bool evaluate(const std::vector<std::string>& user_attributes) const = 0;
};

// 属性节点
struct AttributeNode : public PolicyNode {
    std::string attribute;

    AttributeNode(const std::string& attr) : attribute(attr) {}

    bool evaluate(const std::vector<std::string>& user_attributes) const override {
        return find(user_attributes.begin(), user_attributes.end(), attribute) != user_attributes.end();
    }
};

// 操作符节点
struct OperatorNode : public PolicyNode {
    enum OperatorType { AND, OR, NOT } op;
    std::vector<std::unique_ptr<PolicyNode>> children;

    OperatorNode(OperatorType oper) : op(oper) {}

    bool evaluate(const std::vector<std::string>& user_attributes) const override {
        if (op == AND) {
            for (const auto& child : children) {
                if (!child->evaluate(user_attributes)) return false;
            }
            return true;
        } else if (op == OR) {
            for (const auto& child : children) {
                if (child->evaluate(user_attributes)) return true;
            }
            return false;
        } else if (op == NOT) {
            if (children.size() != 1) return false; // NOT 应只有一个子节点
            return !children[0]->evaluate(user_attributes);
        }
        return false;
    }
};

// 解析策略字符串为策略树
class PolicyParser {
public:
    PolicyParser(const std::string& policy_str) : policy(policy_str), pos(0) {}

    std::unique_ptr<PolicyNode> parse() {
        auto node = parse_expression();
        skip_whitespace();
        if (pos != policy.length()) {
            std::cerr << "Error: Unexpected token at position " << pos << std::endl;
            exit(1);
        }
        return node;
    }

private:
    std::string policy;
    size_t pos;

    // 跳过空格
    void skip_whitespace() {
        while (pos < policy.length() && isspace(policy[pos])) pos++;
    }

    // 匹配并消耗预期的字符串
    bool match(const std::string& token) {
        skip_whitespace();
        size_t len = token.length();
        if (policy.compare(pos, len, token) == 0) {
            pos += len;
            return true;
        }
        return false;
    }

    // 匹配并消耗单个字符
    bool match_char(char expected) {
        skip_whitespace();
        if (pos < policy.length() && policy[pos] == expected) {
            pos++;
            return true;
        }
        return false;
    }

    // 解析表达式（处理 OR）
    std::unique_ptr<PolicyNode> parse_expression() {
        auto node = parse_term();
        while (true) {
            skip_whitespace();
            if (match("OR")) {
                auto or_node = std::make_unique<OperatorNode>(OperatorNode::OR);
                or_node->children.push_back(std::move(node));
                or_node->children.push_back(parse_term());
                node = std::move(or_node);
            } else {
                break;
            }
        }
        return node;
    }

    // 解析项（处理 AND）
    std::unique_ptr<PolicyNode> parse_term() {
        auto node = parse_factor();
        while (true) {
            skip_whitespace();
            if (match("AND")) {
                auto and_node = std::make_unique<OperatorNode>(OperatorNode::AND);
                and_node->children.push_back(std::move(node));
                and_node->children.push_back(parse_factor());
                node = std::move(and_node);
            } else {
                break;
            }
        }
        return node;
    }

    // 解析因子（处理 NOT 和括号）
    std::unique_ptr<PolicyNode> parse_factor() {
        skip_whitespace();
        if (match("NOT")) {
            auto not_node = std::make_unique<OperatorNode>(OperatorNode::NOT);
            not_node->children.push_back(parse_factor());
            return not_node;
        } else if (match_char('(')) {
            auto node = parse_expression();
            if (!match_char(')')) {
                std::cerr << "Error: Expected ')' at position " << pos << std::endl;
                exit(1);
            }
            return node;
        } else {
            return parse_attribute();
        }
    }

    // 解析属性
    std::unique_ptr<PolicyNode> parse_attribute() {
        skip_whitespace();
        size_t start = pos;
        while (pos < policy.length() && (isalnum(policy[pos]) || policy[pos] == '_')) pos++;
        if (start == pos) {
            std::cerr << "Error: Expected attribute at position " << pos << std::endl;
            exit(1);
        }
        std::string attr = policy.substr(start, pos - start);
        return std::make_unique<AttributeNode>(attr);
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
    void log_message(const std::string& message);
    // 判断策略是否匹配
    bool policy_matches(const std::string& policy_str, const std::vector<std::string>& user_attributes);

    // 初始化密钥
    void initialize_keys(PublicKey &publicKey, MasterKey &masterKey);
    void initialize_keys(
        const std::string& pk_str, 
        const std::string& master_key_str, 
        PublicKey &publicKey, 
        MasterKey &masterKey);
    // 生成用户私钥
    void generate_user_private_key(
        const PublicKey &publicKey, 
        const std::vector<std::string>& attributes, 
        UserPrivateKey &userPrivateKey);
    // 加密函数
    CipherText encrypt(
        const PublicKey &publicKey,
        const std::string &message, 
        const std::string &policy);
    // 解密函数
    bool decrypt(
        const PublicKey &publicKey, 
        const MasterKey &masterKey, 
        const UserPrivateKey &userPrivateKey, 
        const CipherText &cipher, 
        std::string &decrypted_message);
    int test_cpabe();

private:
    
    DISALLOW_COPY_AND_ASSIGN(ContractCpabe);
};

}  // namespace contract

}  // namespace shardora
