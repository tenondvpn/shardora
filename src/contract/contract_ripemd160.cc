#include "contract/contract_ripemd160.h"

#include "common/hash.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "contract/contract_ars.h"
#include "contract/contract_pairing.h"
#include "contract/contract_reencryption.h"
#include "pbc/pbc.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace contract {

Ripemd160::Ripemd160(const std::string& create_address)
        : ContractInterface(create_address) {}

Ripemd160::~Ripemd160() {}

#define DEFAULT_CALL_RESULT() { \
    res->output_data = new uint8_t[32]; \
    memset((void*)res->output_data, 0, 32); \
    res->output_size = 32; \
    memcpy(res->create_address.bytes, \
        create_address_.c_str(), \
        sizeof(res->create_address.bytes)); \
    res->gas_left -= 1000; \
    ZJC_DEBUG("TestProxyReEncryption: %s", common::Encode::HexEncode(std::string((char*)res->output_data, 32)).c_str()); \
    return kContractSuccess; \
}

int Ripemd160::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    CONTRACT_ERROR("abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());
    if (param.data.empty()) {
        return kContractError;
    }

    if (param.data.substr(0, 3) == "add") {
        return AddParams(param, gas, origin_address, res);
    }

    if (param.data.substr(0, 5) == "check") {
        return CheckDecrytParamsValid(param, gas, origin_address, res);
    }

    if (param.data.substr(0, 7) == "decrypt") {
        return Decrypt(param, gas, origin_address, res);
    }

    if (param.data.substr(0, 5) == "readd") {
        return AddReEncryptionParam(param, gas, origin_address, res);
    }

    // proxy reencryption
    if (param.data.substr(0, 6) == "tpinit") {
        ContractReEncryption proxy_reenc;
        proxy_reenc.CreatePrivateAndPublicKeys(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprenk") {
        ContractReEncryption proxy_reenc;
        proxy_reenc.CreateReEncryptionKeys(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tpencu") {
        ContractReEncryption proxy_reenc;
        proxy_reenc.EncryptUserMessage(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprenc") {
        ContractReEncryption proxy_reenc;
        proxy_reenc.ReEncryptUserMessage(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprdec") {
        ContractReEncryption proxy_reenc;
        proxy_reenc.Decryption(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    // ars
    if (param.data.substr(0, 4) == "tars") {
        TestArs();
        DEFAULT_CALL_RESULT();
    }

    int64_t gas_used = ComputeGasUsed(600, 120, param.data.size());
    if (res->gas_left < gas_used) {
        return kContractError;
    }

    std::string ripemd160 = common::Hash::ripemd160(param.data);
    res->output_data = new uint8_t[32];
    memset((void*)res->output_data, 0, 32);
    memcpy((void*)res->output_data, ripemd160.c_str(), ripemd160.size());
    res->output_size = 32;
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_used;
    ZJC_DEBUG("ripemd160: %s", common::Encode::HexEncode(std::string((char*)res->output_data, 32)).c_str());
    return kContractSuccess;
}

void Ripemd160::TestArs() {
    ContractArs ars;
    // 设置环的大小和签名者数量
    const int ring_size = 3;
    const int signer_count = 2;

    // 创建环中的公钥和私钥对
    std::vector<element_t> private_keys(ring_size);
    std::vector<element_t> public_keys(ring_size);

    // 初始化公私钥对
    for (int i = 0; i < ring_size; ++i)
    {
        element_init_Zr(private_keys[i], ars.get_pairing()); // 私钥初始化为 Zr 群中的元素
        element_init_G2(public_keys[i], ars.get_pairing());  // 公钥初始化为 G2 群中的元素
        ars.KeyGen(private_keys[i], public_keys[i]);
    }

    // 选择签名人（假设是第 0, 1 位成员）
    std::vector<int> signers = {0, 1};
    std::vector<std::string> messages = {"01", "10"};

    // 为每位签名者生成单个签名
    std::vector<element_t> delta_primes(signer_count);
    std::vector<element_t> y_primes(signer_count);
    std::vector<std::vector<element_s>> pi_proofs(signer_count);

    std::vector<element_t *> ring;
    for (auto &pub_key : public_keys)
    {
        ring.push_back(&pub_key); // 将公钥指针添加到 ring 中
    }

    for (int i = 0; i < signer_count; ++i)
    {
        int signer_idx = signers[i];
        element_init_G1(delta_primes[i], ars.get_pairing());
        element_init_G2(y_primes[i], ars.get_pairing());

        ars.SingleSign(messages[i], private_keys[signer_idx], public_keys[signer_idx],
                       ring, delta_primes[i], y_primes[i], pi_proofs[i]);

        // 打印单个签名的生成内容
        std::cout << "Signer " << signer_idx << " signature details:" << std::endl;
        element_printf("delta_prime: %B\n", delta_primes[i]);
        element_printf("y_prime: %B\n", y_primes[i]);

        // 假设 pi_proofs 是一个 element_s 类型的向量
        std::cout << "pi_proof: ";
        for (const auto &proof : pi_proofs[i])
        {
            element_printf("%B\n ", &proof);
        }
        std::cout << std::endl;
    }

    // 聚合签名生成
    element_t agg_signature;
    element_init_G1(agg_signature, ars.get_pairing());
    ars.AggreSign(messages, y_primes, delta_primes, pi_proofs, ring, agg_signature);

    // 用于检验错误输入所生成的结果
    // std::vector<std::string> messages1 = {"010", "101"};
    //  element_t agg_signature1;
    //  element_init_G1(agg_signature1, ars.get_pairing());
    //  element_random(agg_signature1);

    // 聚合签名验证
    bool is_aggregate_valid = ars.AggreVerify(messages, agg_signature, y_primes);
    if (is_aggregate_valid)
    {
        ZJC_DEBUG("Aggregate signature verification passed!");
    }
    else
    {
        ZJC_DEBUG("Aggregate signature verification failed!");
    }

    // 清理资源
    for (int i = 0; i < ring_size; ++i)
    {
        element_clear(private_keys[i]);
        element_clear(public_keys[i]);
    }
    for (int i = 0; i < signer_count; ++i)
    {
        element_clear(delta_primes[i]);
        element_clear(y_primes[i]);
    }
    element_clear(agg_signature);
}

int Ripemd160::CheckDecrytParamsValid(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {

    return kContractSuccess;
}

int Ripemd160::AddReEncryptionParam(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (res->gas_left <= 2300) {
        CONTRACT_ERROR("re encryption gas failed: %d", res->gas_left);
        return kContractError;
    }

    uint32_t key_len = 0;
    if (!common::StringUtil::ToUint32(param.data.substr(3, 2), &key_len) || key_len <= 0) {
        CONTRACT_ERROR("re encryption convert key len failed: %s!", param.data.substr(3, 2).c_str());
        return kContractError;
    }

    auto key = "reenc_" + param.data.substr(5, key_len);
    auto val_start = 5 + key_len;
    if (val_start >= param.data.size()) {
        CONTRACT_ERROR("re encryption val_start error: %d, %d!", val_start, param.data.size());
        return kContractError;
    }

    std::string val = param.data.substr(val_start, param.data.size() - val_start);
    int64_t gas_used = ComputeGasUsed(0, 5000, key.size() + (val.size() / 2));
    if (res->gas_left < gas_used) {
        res->gas_left = 0;
        CONTRACT_ERROR("re encryption gas failed: res->gas_left: %d < gas_used: %d",
            res->gas_left, gas_used);
        return kContractError;
    }

    if (key == "reenc_all") {
        AddAllParams("reenc_", param, val);
    } else {
        param.zjc_host->SaveKeyValue(param.from, key, val);
    }

    res->output_data = new uint8_t[32];
    memset((void*)res->output_data, 0, 32);
    res->output_size = 32;
    res->gas_left -= gas_used;
    CONTRACT_ERROR("re encryption save key value success, gas: %lu, %s: %s",
        gas_used, key.c_str(), val.c_str());
    return kContractSuccess;
}

int Ripemd160::AddParams(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (res->gas_left <= 2300) {
        CONTRACT_ERROR("abe gas failed: %d", res->gas_left);
        return kContractError;
    }

    uint32_t key_len = 0;
    if (!common::StringUtil::ToUint32(param.data.substr(3, 2), &key_len) || key_len <= 0) {
        CONTRACT_ERROR("abe convert key len failed: %s!", param.data.substr(3, 2).c_str());
        return kContractError;
    }

    auto key = "abe_" + param.data.substr(5, key_len);
    auto val_start = 5 + key_len;
    if (val_start >= param.data.size()) {
        CONTRACT_ERROR("abe val_start error: %d, %d!", val_start, param.data.size());
        return kContractError;
    }

    std::string val = param.data.substr(val_start, param.data.size() - val_start);
    int64_t gas_used = ComputeGasUsed(0, 5000, key.size() + (val.size() / 2));
    if (res->gas_left < gas_used) {
        res->gas_left = 0;
        CONTRACT_ERROR("abe gas failed: res->gas_left: %d < gas_used: %d",
            res->gas_left, gas_used);
        return kContractError;
    }

    if (key == "abe_all") {
        AddAllParams("abe_", param, val);
    } else {
        param.zjc_host->SaveKeyValue(param.from, key, val);
    }

    res->output_data = new uint8_t[32];
    memset((void*)res->output_data, 0, 32);
    res->output_size = 32;
    res->gas_left -= gas_used;
    CONTRACT_ERROR("abe save key value success, gas: %lu, %s: %s",
        gas_used, key.c_str(), val.c_str());
    return kContractSuccess;
}

void Ripemd160::AddAllParams(
        const std::string& prev, 
        const CallParameters& param, 
        const std::string& val) {
    common::Split<1024> lines(val.c_str(), '\n', val.size());
    for (uint32_t i = 0; i < lines.Count(); ++i) {
        common::Split<> items(lines[i], ':', lines.SubLen(i));
        if (items.Count() != 2) {
            continue;
        }

        std::string key = prev + items[0];
        param.zjc_host->SaveKeyValue(
            param.from,
            key,
            common::Encode::HexDecode(items[1]));
    }
}

int Ripemd160::GetValue(
        const CallParameters& param,
        const std::string& key,
        std::string* val,
        evmc_result* res) {
    if (param.zjc_host->GetKeyValue(param.from, key, val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", key.c_str());
        return kContractError;
    }

    int64_t gas_used = ComputeGasUsed(0, 2100, key.size() + val->size());
    if (res->gas_left <= gas_used) {
        res->gas_left = 0;
        CONTRACT_ERROR("abe gas failed: %d", res->gas_left);
        return kContractError;
    }

    res->gas_left -= gas_used;
    return kContractSuccess;
}

int Ripemd160::Decrypt(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    // get all keys to compute pbc
    uint32_t param_len = 0;
    if (!common::StringUtil::ToUint32(param.data.substr(7, 4), &param_len) || param_len <= 0) {
        CONTRACT_ERROR("abe convert key len failed: %s!", param.data.substr(7, 4).c_str());
        return kContractError;
    }

    std::string paring_param;
    if (GetValue(param, "abe_c0", &paring_param, res) != kContractSuccess) {
        CONTRACT_ERROR("get data c0 failed!");
        return kContractError;
    }

    PbcParing pbc_pairing("", paring_param);
    pbc_pairing.count_ = param_len;
    CONTRACT_DEBUG("get count: %d", pbc_pairing.count_);
    std::string o;
    if (GetValue(param, "abe_o", &o, res) != kContractSuccess) {
        CONTRACT_ERROR("get data 0 failed!");
        return kContractError;
    }

    pbc_pairing.o_ = Zr(pbc_pairing.pairing_, (void*)o.c_str(), o.size());
    std::string c_str;
    if (GetValue(param, "abe_c", &c_str, res) != kContractSuccess) {
        CONTRACT_ERROR("get data c failed!");
        return kContractError;
    }

    pbc_pairing.C_ = GT(pbc_pairing.pairing_, (void*)c_str.c_str(), c_str.size());
    std::string c1_str;
    if (GetValue(param, "abe_c1", &c1_str, res) != kContractSuccess) {
        CONTRACT_ERROR("get data c1 failed!");
        return kContractError;
    }

    pbc_pairing.C1_ = G1(pbc_pairing.pairing_, (void*)c1_str.c_str(), c1_str.size());
    std::string l1_str;
    if (GetValue(param, "abe_l1", &l1_str, res) != kContractSuccess) {
        CONTRACT_ERROR("get data l1 failed!");
        return kContractError;
    }

    pbc_pairing.L1_ = G1(pbc_pairing.pairing_, (void*)l1_str.c_str(), l1_str.size());
    std::string r1_str;
    if (GetValue(param, "abe_r1", &r1_str, res) != kContractSuccess) {
        CONTRACT_ERROR("get data l1 failed!");
        return kContractError;
    }

    pbc_pairing.R1_ = G1(pbc_pairing.pairing_, (void*)r1_str.c_str(), r1_str.size());
    pbc_pairing.clist_.clear();
    pbc_pairing.dlist_.clear();
    pbc_pairing.Rx1_.clear();
    for (int32_t i = 0; i < pbc_pairing.count_; i++) {
        std::string c_str;
        if (GetValue(param, "abe_c_" + std::to_string(i), &c_str, res) != kContractSuccess) {
            CONTRACT_ERROR("get data c failed!");
            return kContractError;
        }

        pbc_pairing.clist_.push_back(G1(pbc_pairing.pairing_, (void*)c_str.c_str(), c_str.size()));
        std::string d_str;
        if (GetValue(param, "abe_d_" + std::to_string(i), &d_str, res) != kContractSuccess) {
            CONTRACT_ERROR("get data d failed!");
            return kContractError;
        }

        pbc_pairing.dlist_.push_back(G1(pbc_pairing.pairing_, (void*)d_str.c_str(), d_str.size()));
        std::string r_str;
        if (GetValue(param, "abe_r_" + std::to_string(i), &r_str, res) != kContractSuccess) {
            CONTRACT_ERROR("get data d failed!");
            return kContractError;
        }

        pbc_pairing.Rx1_.push_back(G1(pbc_pairing.pairing_, (void*)r_str.c_str(), r_str.size()));
    }

    auto gas_used = pbc_prepair_cast_ + 
        pbc_pairing.count_ * (2 * pbc_exp_cast_ + 2 * pbc_pairing_cast_) + 
        pbc_exp_cast_ + 
        pbc_pairing_cast_;
    if (res->gas_left < (int64_t)gas_used) {
        CONTRACT_ERROR("abe gas failed: res->gas_left: %d < gas_used: %d",
            res->gas_left, gas_used);
        return kContractError;
    }

    res->gas_left -= gas_used;
    pbc_pairing.TransformAll();
    pbc_pairing.Decrypt();
    res->output_data = new uint8_t[32];
    memset((void*)res->output_data, 0, 32);
    res->output_size = 32;
    CONTRACT_DEBUG("over call\n");
    return kContractSuccess;
}

int Ripemd160::TestPbc(const std::string& param) {
    PbcParing pbc_pairing("", param);
    CallParameters p;
    pbc_pairing.call(p, 0, "", nullptr);
    CONTRACT_DEBUG("over pbc\n");
    return 0;
}

}  // namespace contract

}  // namespace shardora
