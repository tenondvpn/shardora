#include "contract/contract_ripemd160.h"

#include "common/hash.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "contract/contract_pairing.h"
#include "pbc/pbc.h"
#include "zjcvm/zjc_host.h"

namespace zjchain {

namespace contract {

Ripemd160::Ripemd160(const std::string& create_address)
        : ContractInterface(create_address) {}

Ripemd160::~Ripemd160() {}

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

    int64_t gas_used = ComputeGasUsed(600, 120, param.data.size());
    if (res->gas_left < gas_used) {
        return kContractError;
    }

    std::string ripemd160 = common::Hash::ripemd160(param.data);
    res->output_data = new uint8_t[32];
    memcpy((void*)res->output_data, ripemd160.c_str(), ripemd160.size());
    res->output_size = 32;
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_used;
    return kContractSuccess;
}

int Ripemd160::CheckDecrytParamsValid(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {

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
        AddAllParams(param, val);
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

void Ripemd160::AddAllParams(const CallParameters& param, const std::string& val) {
    common::Split<1024> lines(val.c_str(), '\n', val.size());
    for (uint32_t i = 0; i < lines.Count(); ++i) {
        common::Split<> items(lines[i], ':', lines.SubLen(i));
        if (items.Count() != 2) {
            continue;
        }

        std::string key = std::string("abe_") + items[0];
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

}  // namespace zjchain
