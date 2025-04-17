#include "contract/contract_ripemd160.h"

#include "common/hash.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "contract/contract_pairing.h"
#include "contract/contract_pki.h"
#include "contract/contract_cl.h"
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
    ZJC_WARN("contract_reencryption TestProxyReEncryption: %s", common::Encode::HexEncode(std::string((char*)res->output_data, 32)).c_str()); \
    return kContractSuccess; \
}

#define GET_KEY_VALUE_FROM_PARAM() \
    uint32_t key_len = 0; \
    if (!common::StringUtil::ToUint32(param.data.substr(6, 2), &key_len) || key_len <= 0) { \
        CONTRACT_ERROR("abe convert key len failed: %s!", param.data.substr(3, 2).c_str()); \
        return kContractError; \
    } \
      \
    auto key = param.data.substr(8, key_len); \
    auto val_start = 8 + key_len; \
    if (val_start >= param.data.size()) { \
        CONTRACT_ERROR("abe val_start error: %d, %d!", val_start, param.data.size()); \
        return kContractError; \
    } \
      \
    std::string val = param.data.substr(val_start, param.data.size() - val_start);


int Ripemd160::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) try {
    CONTRACT_DEBUG("abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());
    if (param.data.empty()) {
        return kContractError;
    }

    CONTRACT_DEBUG("0 1 abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());

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
    CONTRACT_DEBUG("0 2 abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());

    // proxy reencryption
    if (param.data.substr(0, 6) == "tpinit") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.CreatePrivateAndPublicKeys(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprenk") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.CreateReEncryptionKeys(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tpencu") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.EncryptUserMessage(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprenc") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.ReEncryptUserMessage(param, key, val);
        DEFAULT_CALL_RESULT();
    }
    CONTRACT_DEBUG("0 2 abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());

    if (param.data.substr(0, 6) == "mprenc") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.ReEncryptUserMessageWithMember(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    // pki
    if (param.data.substr(0, 6) == "pkipki") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.PkiExtract(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "pkipib") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.IbExtract(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "pkiege") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.EncKeyGen(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "pkidge") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.DecKeyGen(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "pkienc") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.Enc(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "pkidec") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractPki pki;
        pki.Dec(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    // pki cl
    CONTRACT_DEBUG("0 abe contract called decode: %s, src: %s",
        common::Encode::HexDecode(param.data).c_str(), param.data.c_str());
    if (param.data.substr(0, 6) == "clipki") {
        CONTRACT_DEBUG("1 abe contract called decode: %s, src: %s",
            common::Encode::HexDecode(param.data).c_str(), param.data.c_str());
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.PkiExtract(param, key, val);
        CONTRACT_DEBUG("2 abe contract called decode: %s, src: %s",
            common::Encode::HexDecode(param.data).c_str(), param.data.c_str());
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "clipib") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.IbExtract(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "cliege") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.EncKeyGen(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "clidge") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.DecKeyGen(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "clienc") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.Enc(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "clidec") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractCl pki;
        pki.Dec(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tprdec") {
        GET_KEY_VALUE_FROM_PARAM();
        ContractReEncryption proxy_reenc;
        proxy_reenc.Decryption(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    // ars
    if (param.data.substr(0, 6) == "tarscr") {
        GET_KEY_VALUE_FROM_PARAM();
        CreateArsKeys(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tarsps") {
        GET_KEY_VALUE_FROM_PARAM();
        SingleSign(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "tarsas") {
        GET_KEY_VALUE_FROM_PARAM();
        AggSignAndVerify(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 4) == "tars") {
        TestArs(param, "", "");
        DEFAULT_CALL_RESULT();
    }

    // rabpre
    if (param.data.substr(0, 6) == "rabini") {
        GET_KEY_VALUE_FROM_PARAM();
        RabpreInit(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "rabenc") {
        GET_KEY_VALUE_FROM_PARAM();
        RabpreEnc(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "rabdec") {
        GET_KEY_VALUE_FROM_PARAM();
        RabpreDec(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "rabren") {
        GET_KEY_VALUE_FROM_PARAM();
        RabpreReEnc(param, key, val);
        DEFAULT_CALL_RESULT();
    }

    if (param.data.substr(0, 6) == "rabrde") {
        GET_KEY_VALUE_FROM_PARAM();
        RabpreReDec(param, key, val);
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
} catch(std::exception& e) {
    ZJC_ERROR("catch error: %s", e.what());
    DEFAULT_CALL_RESULT();
}

void Ripemd160::SaveCrs(const CRS& crs, std::string* val) {
    char data[10240] = {0};
    long long* ldata = (long long*)data;
    uint32_t idx = 0;
    ldata[idx++] = crs.p;
    ldata[idx++] = crs.g;
    ldata[idx++] = crs.h;
    ldata[idx++] = crs.Z;
    for (uint32_t i = 0; i < crs.A0B0.size(); ++i) {
        ldata[idx++] = crs.A0B0[i];
    }

    for (uint32_t i = 0; i < crs.A1B1.size(); ++i) {
        ldata[idx++] = crs.A1B1[i];
    }

    for (uint32_t i = 0; i < crs.U0W01.size(); ++i) {
        ldata[idx++] = crs.U0W01[i];
    }

    for (uint32_t i = 0; i < crs.U1W10.size(); ++i) {
        ldata[idx++] = crs.U1W10[i];
    }

    *val = std::string(data, idx * sizeof(ldata[0]));
}

void Ripemd160::LoadCrs(const std::string& val, CRS& crs) {
    long long* ldata = (long long*)val.c_str();
    uint32_t idx = 0;
    crs.p = ldata[idx++];
    crs.g = ldata[idx++];
    crs.h = ldata[idx++];
    crs.Z = ldata[idx++];
    crs.A0B0.push_back(ldata[idx++]);
    crs.A0B0.push_back(ldata[idx++]);
    crs.A1B1.push_back(ldata[idx++]);
    crs.A1B1.push_back(ldata[idx++]);
    crs.U0W01.push_back(ldata[idx++]);
    crs.U0W01.push_back(ldata[idx++]);
    crs.U1W10.push_back(ldata[idx++]);
    crs.U1W10.push_back(ldata[idx++]);
}

void Ripemd160::SaveSkPk(
        long long sk, 
        const std::tuple<long long, long long>& pk, 
        std::string* val) {
    char data[10240] = {0};
    long long* ldata = (long long*)data;
    uint32_t idx = 0;
    ldata[idx++] = sk;
    ldata[idx++] = std::get<0>(pk);
    ldata[idx++] = std::get<1>(pk);
    *val = std::string(data, idx * sizeof(ldata[0]));
}

void Ripemd160::LoadSkPk(
        const std::string& val, 
        long long* sk, 
        std::tuple<long long, long long>* pk) {
    long long* ldata = (long long*)val.c_str();
    *sk = ldata[0];
    *pk = std::make_tuple(ldata[1], ldata[2]);
}

void Ripemd160::SaveAgg(
        const std::tuple<long long, long long, long long, long long, long long, long long>& mpk,
        const std::tuple<long long, long long, long long, long long, long long>& hsk0,
        const std::tuple<long long, long long, long long, long long, long long>& hsk1,
        std::string* val) {
    char data[10240] = {0};
    long long* ldata = (long long*)data;
    uint32_t idx = 0;
    ldata[idx++] = std::get<0>(mpk);
    ldata[idx++] = std::get<1>(mpk);
    ldata[idx++] = std::get<2>(mpk);
    ldata[idx++] = std::get<3>(mpk);
    ldata[idx++] = std::get<4>(mpk);
    ldata[idx++] = std::get<5>(mpk);

    ldata[idx++] = std::get<0>(hsk0);
    ldata[idx++] = std::get<1>(hsk0);
    ldata[idx++] = std::get<2>(hsk0);
    ldata[idx++] = std::get<3>(hsk0);
    ldata[idx++] = std::get<4>(hsk0);

    ldata[idx++] = std::get<0>(hsk1);
    ldata[idx++] = std::get<1>(hsk1);
    ldata[idx++] = std::get<2>(hsk1);
    ldata[idx++] = std::get<3>(hsk1);
    ldata[idx++] = std::get<4>(hsk1);

    *val = std::string(data, idx * sizeof(ldata[0]));
}

void Ripemd160::LoadAgg(
        const std::string& val, 
        std::tuple<long long, long long, long long, long long, long long, long long>* mpk,
        std::tuple<long long, long long, long long, long long, long long>* hsk0,
        std::tuple<long long, long long, long long, long long, long long>* hsk1) {
    long long* ldata = (long long*)val.c_str();
    *mpk = std::make_tuple(ldata[0], ldata[1], ldata[2], ldata[3], ldata[4], ldata[5]);
    *hsk0 = std::make_tuple(ldata[6], ldata[7], ldata[8], ldata[9], ldata[10]);
    *hsk1 = std::make_tuple(ldata[11], ldata[12], ldata[13], ldata[14], ldata[15]);
}

int Ripemd160::RabpreInit(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
        ContractArs ars;
    auto line_splits = common::Split<>(value.c_str(), '-');
    if (line_splits.Count() < 2) {
        ZJC_DEBUG("line_splits.Count() < 2");
        return kContractError;
    }

    auto lambda_count = 32;
    if (!common::StringUtil::ToInt32(line_splits[1], &lambda_count)) {
        ZJC_DEBUG("common::StringUtil::ToInt32(line_splits[0], &lambda_count) failed");
        return kContractError;
    }

    auto id = common::Encode::HexDecode(line_splits[0]);
    try {
        // 参数初始化
        auto crs = Rabpre::SETUP(32);
        std::string val;
        SaveCrs(crs, &val);
        auto tmp_key = std::string("rabpre_crs_") + id;
        param.zjc_host->SaveKeyValue(param.from, tmp_key, val);
        // 密钥生成
        auto [sk0, pk0] = Rabpre::KEYGEN(crs, 0);
        std::string sk0_pk0;
        SaveSkPk(sk0, pk0, &sk0_pk0);
        tmp_key = std::string("rabpre_sk0_pk0_") + id;
        param.zjc_host->SaveKeyValue(param.from, tmp_key, sk0_pk0);
        auto [sk1, pk1] = Rabpre::KEYGEN(crs, 1);
        std::string sk1_pk1;
        SaveSkPk(sk1, pk1, &sk1_pk1);
        tmp_key = std::string("rabpre_sk1_pk1_") + id;
        param.zjc_host->SaveKeyValue(param.from, tmp_key, sk1_pk1);
        // 聚合密钥
        auto [mpk, hsk0, hsk1] = Rabpre::AGGREGATE(crs, {pk0, pk1});
        std::string agg;
        SaveAgg(mpk, hsk0, hsk1, &agg);
        tmp_key = std::string("rabpre_agg_") + id;
        param.zjc_host->SaveKeyValue(param.from, tmp_key, agg);

        // // 加密测试
        // long long plaintext = 199; //修改消息
        // auto ct = Rabpre::ENCRYPT(mpk, plaintext, 1);

        // // 解密测试
        // long long decrypted = Rabpre::DEC(ct, sk0, hsk0, mpk);
        // cout << "原始明文: " << plaintext<<endl;
        // cout<<"第一层密文: " <<get<1>(ct)<<endl;
        // cout<< "解密结果: " << decrypted << endl;

        // // 重加密测试
        // auto rk = Rabpre::RKGEN({0,0}, sk1, hsk1, 1,mpk);
        // auto ct_new = Rabpre::REENC(rk, ct);
        // cout<<"重加密密文: "<<get<0>(get<1>(ct_new))<<endl;
        // long long m2 = Rabpre::DECRE(ct_new, sk1, hsk1, mpk);
        // cout << "重加密解密结果: " << m2 << endl;
        ZJC_DEBUG("init RabpreInit success id: %s, lambda_count: %u",
            common::Encode::HexEncode(id).c_str(), lambda_count);
    } catch (const exception& e) {
        cerr << "错误: " << e.what() << endl;
        return 1;
    }
    return kContractSuccess;
}

void Ripemd160::SaveEncVal(
        const std::tuple<int, long long, long long, long long,
        long long, long long, long long, long long,
        long long, long long>& enc,
        std::string* val) {
    char data[10240] = {0};
    long long* ldata = (long long*)data;
    uint32_t idx = 0;
    ldata[idx++] = std::get<0>(enc);
    ldata[idx++] = std::get<1>(enc);
    ldata[idx++] = std::get<2>(enc);
    ldata[idx++] = std::get<3>(enc);
    ldata[idx++] = std::get<4>(enc);
    ldata[idx++] = std::get<5>(enc);
    ldata[idx++] = std::get<6>(enc);
    ldata[idx++] = std::get<7>(enc);
    ldata[idx++] = std::get<8>(enc);
    ldata[idx++] = std::get<9>(enc);
    *val = std::string(data, idx * sizeof(ldata[0]));
}

void Ripemd160::LoadEncVal(
        const std::string& val, 
        std::tuple<int, long long, long long, long long,
        long long, long long, long long, long long,
        long long, long long>* enc) {
    long long* ldata = (long long*)val.c_str();
    *enc = std::make_tuple(static_cast<int>(ldata[0]), ldata[1], ldata[2], ldata[3], 
        ldata[4], ldata[5], ldata[6], ldata[7], 
        ldata[8], ldata[9]);
}

int Ripemd160::RabpreEnc(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto line_splits = common::Split<>(value.c_str(), '-');
    if (line_splits.Count() < 2) {
        ZJC_DEBUG("line_splits.Count() < 2");
        return kContractError;
    }

    int64_t plaintext = 0;
    if (!common::StringUtil::ToInt64(line_splits[1], &plaintext)) {
        ZJC_DEBUG("common::StringUtil::ToInt32(line_splits[0], &lambda_count) failed");
        return kContractError;
    }

    auto id = common::Encode::HexDecode(line_splits[0]);
    std::tuple<long long, long long, long long, long long, long long, long long> mpk;
    std::tuple<long long, long long, long long, long long, long long> hsk0;
    std::tuple<long long, long long, long long, long long, long long> hsk1;
    auto tmp_key = std::string("rabpre_agg_") + id;
    std::string val;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadAgg(val, &mpk, &hsk0, &hsk1);
    auto ct = Rabpre::ENCRYPT(mpk, plaintext, 1);
    std::string enc_val;
    SaveEncVal(ct, &enc_val);
    tmp_key = std::string("rabpre_enc_") + id;
    param.zjc_host->SaveKeyValue(param.from, tmp_key, enc_val);
    ZJC_DEBUG("Rabpre enc success id: %s, plaintext: %ld",
        common::Encode::HexEncode(id).c_str(), plaintext);
    return kContractSuccess;
}

int Ripemd160::RabpreDec(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto id = common::Encode::HexDecode(value);
    auto tmp_key = std::string("rabpre_crs_") + id;
    std::string val;
    if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
        return kContractError;
    }

    CRS crs;
    LoadCrs(val, crs);
    tmp_key = std::string("rabpre_sk0_pk0_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk0;
    std::tuple<long long, long long> pk0;
    LoadSkPk(val, &sk0, &pk0);
    tmp_key = std::string("rabpre_sk1_pk1_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk1;
    std::tuple<long long, long long> pk1;
    LoadSkPk(val, &sk1, &pk1);
    std::tuple<long long, long long, long long, long long, long long, long long> mpk;
    std::tuple<long long, long long, long long, long long, long long> hsk0;
    std::tuple<long long, long long, long long, long long, long long> hsk1;
    tmp_key = std::string("rabpre_agg_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadAgg(val, &mpk, &hsk0, &hsk1);

    std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long> ct;
    tmp_key = std::string("rabpre_enc_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadEncVal(val, &ct);
    long long decrypted = Rabpre::DEC(ct, sk0, hsk0, mpk);
    ZJC_DEBUG("Rabpre enc success id: %s, decrypted: %ld",
        common::Encode::HexEncode(id).c_str(), decrypted);
    return kContractSuccess;
}

void Ripemd160::SaveReenc(
        const std::tuple<
            int, 
            std::tuple<long long, long long>,
            std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long>, 
            long long>& reenc,
        std::string* val) {
    char data[10240] = {0};
    long long* ldata = (long long*)data;
    uint32_t idx = 0;
    ldata[idx++] = std::get<0>(reenc);
    ldata[idx++] = std::get<0>(std::get<1>(reenc));
    ldata[idx++] = std::get<1>(std::get<1>(reenc));
    auto third_val = std::get<2>(reenc);
    ldata[idx++] = std::get<0>(third_val);
    ldata[idx++] = std::get<1>(third_val);
    ldata[idx++] = std::get<2>(third_val);
    ldata[idx++] = std::get<3>(third_val);
    ldata[idx++] = std::get<4>(third_val);
    ldata[idx++] = std::get<5>(third_val);
    ldata[idx++] = std::get<6>(third_val);
    ldata[idx++] = std::get<7>(third_val);
    ldata[idx++] = std::get<8>(third_val);
    ldata[idx++] = std::get<9>(third_val);
    ldata[idx++] = std::get<3>(reenc);
    *val = std::string(data, idx * sizeof(ldata[0]));
}

void Ripemd160::LoadReenc(
    const std::string& val, 
    std::tuple<int, std::tuple<long long, long long>,
        std::tuple<int, long long, long long, long long,
            long long, long long, long long, long long,
            long long, long long>, long long>* reenc) {
    long long* ldata = (long long*)val.c_str();
    *reenc = std::make_tuple(
        static_cast<int>(ldata[0]), 
        std::make_tuple(ldata[1], ldata[2]), 
        std::make_tuple(static_cast<int>(ldata[3]), ldata[4], ldata[5], ldata[6], 
            ldata[7], ldata[8], ldata[9], ldata[10], ldata[11], ldata[12]),
        ldata[13]);
}

int Ripemd160::RabpreReEnc(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto id = common::Encode::HexDecode(value);
    auto tmp_key = std::string("rabpre_crs_") + id;
    std::string val;
    if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
        return kContractError;
    }

    CRS crs;
    LoadCrs(val, crs);
    tmp_key = std::string("rabpre_sk0_pk0_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk0;
    std::tuple<long long, long long> pk0;
    LoadSkPk(val, &sk0, &pk0);
    tmp_key = std::string("rabpre_sk1_pk1_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk1;
    std::tuple<long long, long long> pk1;
    LoadSkPk(val, &sk1, &pk1);
    std::tuple<long long, long long, long long, long long, long long, long long> mpk;
    std::tuple<long long, long long, long long, long long, long long> hsk0;
    std::tuple<long long, long long, long long, long long, long long> hsk1;
    tmp_key = std::string("rabpre_agg_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadAgg(val, &mpk, &hsk0, &hsk1);

    std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long> ct;
    tmp_key = std::string("rabpre_enc_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadEncVal(val, &ct);

    auto rk = Rabpre::RKGEN({0,0}, sk1, hsk1, 1, mpk);
    auto ct_new = Rabpre::REENC(rk, ct);
    std::string reenc_val;
    SaveReenc(ct_new, &reenc_val);
    tmp_key = std::string("rabpre_reenc_") + id;
    param.zjc_host->SaveKeyValue(param.from, tmp_key, reenc_val);
    ZJC_DEBUG("Rabpre reenc success id: %s",
        common::Encode::HexEncode(id).c_str());
    return kContractSuccess;
}

int Ripemd160::RabpreReDec(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto id = common::Encode::HexDecode(value);
    auto tmp_key = std::string("rabpre_crs_") + id;
    std::string val;
    if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
        return kContractError;
    }

    CRS crs;
    LoadCrs(val, crs);
    tmp_key = std::string("rabpre_sk0_pk0_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk0;
    std::tuple<long long, long long> pk0;
    LoadSkPk(val, &sk0, &pk0);
    tmp_key = std::string("rabpre_sk1_pk1_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    long long sk1;
    std::tuple<long long, long long> pk1;
    LoadSkPk(val, &sk1, &pk1);
    std::tuple<long long, long long, long long, long long, long long, long long> mpk;
    std::tuple<long long, long long, long long, long long, long long> hsk0;
    std::tuple<long long, long long, long long, long long, long long> hsk1;
    tmp_key = std::string("rabpre_agg_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadAgg(val, &mpk, &hsk0, &hsk1);
    std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long> ct;
    tmp_key = std::string("rabpre_enc_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);
    LoadEncVal(val, &ct);
    tmp_key = std::string("rabpre_reenc_") + id;
    param.zjc_host->GetKeyValue(param.from, tmp_key, &val);

    std::tuple<int, std::tuple<long long, long long>,
            std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long>, long long> ct_new;
    LoadReenc(val, &ct_new);
    long long m2 = Rabpre::DECRE(ct_new, sk1, hsk1, mpk);
    ZJC_DEBUG("Rabpre redec success id: %s, m2: %ld",
        common::Encode::HexEncode(id).c_str(), m2);
    return kContractSuccess;
}

int Ripemd160::CreateArsKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    ContractArs ars;
    // 初始化公私钥对
    auto line_splits = common::Split<>(value.c_str(), '-');
    if (line_splits.Count() < 2) {
        ZJC_DEBUG("line_splits.Count() < 2");
        return kContractError;
    }

    auto keys_splits = common::Split<10240>(line_splits[0], ',');
    ars.set_ring_size(keys_splits.Count());
    auto ex_splits = common::Split<>(line_splits[1], ',');
    if (ex_splits.Count() < 2) {
        ZJC_DEBUG("ex_splits.Count() < 2");
        return kContractError;
    }

    auto signer_count = 0;
    if (!common::StringUtil::ToInt32(ex_splits[0], &signer_count)) {
        ZJC_DEBUG("common::StringUtil::ToInt32(ex_splits[0], &signer_count) failed");
        return kContractError;
    }

    ars.set_signer_count(signer_count);
    if (signer_count <= 0 || signer_count >= ars.ring_size()) {
        ZJC_DEBUG("signer_count <= 0 || signer_count >= ars.ring_size(): %u, %u", signer_count, ars.ring_size());
        return kContractError;
    }

    auto id = common::Encode::HexDecode(ex_splits[1]);
    // 创建环中的公钥和私钥对
    std::vector<element_t> private_keys(ars.ring_size());
    std::vector<element_t> public_keys(ars.ring_size());
    for (int i = 0; i < ars.ring_size(); ++i) {
        ars.KeyGen(keys_splits[i], private_keys[i], public_keys[i]);
        unsigned char bytes_data[10240] = {0};
        auto len = element_to_bytes(bytes_data, private_keys[i]);
        std::string x_i_str((char*)bytes_data, len);
        len = element_to_bytes_compressed(bytes_data, public_keys[i]);
        std::string y_i_str((char*)bytes_data, len);
        auto tmp_key = id + std::string("ars_create_user_private_key_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, tmp_key, x_i_str);
        tmp_key = id + std::string("ars_create_user_public_key_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, tmp_key, y_i_str);
        element_clear(private_keys[i]);
        element_clear(public_keys[i]);
    }

    auto tmp_key = std::string("ars_create_") + id;
    auto val = common::StringUtil::Format("%u,%u", ars.ring_size(), ars.signer_count());
    param.zjc_host->SaveKeyValue(param.from, tmp_key, val);
    ZJC_DEBUG("init sign success: %s, from: %s, key: %s, ring size: %d, signer_count: %d",
        ex_splits[1], 
        common::Encode::HexEncode(param.from).c_str(), 
        common::Encode::HexEncode(tmp_key).c_str(),
        ars.ring_size(), 
        ars.signer_count());
    return kContractSuccess;
}

int Ripemd160::GetRing(
        const std::string& id,
        const CallParameters& param, 
        ContractArs& ars, 
        std::vector<element_t>& ring) {
    for (auto i = 0; i < ars.ring_size(); ++i) {
        auto key = id + std::string("ars_create_user_public_key_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        element_init_G2(ring[i], ars.get_pairing());  // 公钥初始化为 G2 群中的元素
        element_from_bytes_compressed(ring[i], (unsigned char*)val.c_str());
    }

    return kContractSuccess;
}

int Ripemd160::SingleSign(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto line_splits = common::Split<>(value.c_str(), '-');
    if (line_splits.Count() < 2) {
        ZJC_WARN("line_splits.Count() < 2 failed");
        return kContractError;
    }

    auto id = common::Encode::HexDecode(line_splits[1]);
    auto tmp_key = std::string("ars_create_") + id;
    std::string val;
    if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
        return kContractError;
    }

    auto ring_and_signer_count_splits = common::Split<>(val.c_str(), ',');
    int32_t ring_size = 0;
    if (!common::StringUtil::ToInt32(ring_and_signer_count_splits[0], &ring_size)) {
        ZJC_WARN("ring_size failed key: %s, val: %s", common::Encode::HexEncode(tmp_key).c_str(), val.c_str());
        return kContractError;
    }

    int32_t signer_count = 0;
    if (!common::StringUtil::ToInt32(ring_and_signer_count_splits[1], &signer_count)) {
        ZJC_WARN("signer_count failed: %s", val.c_str());
        return kContractError;
    }

    ContractArs ars;
    ars.set_ring_size(ring_size);
    ars.set_signer_count(signer_count);
    // 设置环的大小和签名者数量
    std::vector<element_t> ring(ars.ring_size());
    if (GetRing(id, param, ars, ring) != kContractSuccess) {
        ZJC_WARN("GetRing failed");
        return kContractError;
    }

    auto splits = common::Split<>(line_splits[0], ',');
    if (splits.Count() < 3) {
        ZJC_WARN("invalid splits count: %s", value.c_str());
        return kContractError;
    }

    int signer_idx = 0;
    if (!common::StringUtil::ToInt32(splits[0], &signer_idx)) {
        ZJC_WARN("invalid splits count: %s", value.c_str());
        return kContractError;
    }

    if (signer_idx < 0 || signer_idx > ars.ring_size()) {
        ZJC_WARN("invalid splits count: %s", value.c_str());
        return kContractError;
    }

    element_t delta_prime;
    element_t y_prime;
    element_init_G1(delta_prime, ars.get_pairing());
    element_init_G2(y_prime, ars.get_pairing());
    element_t private_key;
    element_init_Zr(private_key, ars.get_pairing());
    element_from_bytes(private_key, (unsigned char*)common::Encode::HexDecode(splits[2]).c_str());
    std::vector<element_t> pi_proof(4);
    ars.SingleSign(splits[1], private_key, ring[signer_idx], ring, delta_prime, y_prime, pi_proof);
    tmp_key = std::string("ars_create_single_sign_") + std::to_string(signer_idx);
    val = std::string(splits[1]) + ",";
    unsigned char data[20480] = {0};
    {
        auto len = element_to_bytes_compressed(data, delta_prime);
        val += common::Encode::HexEncode(std::string((char*)data, len)) + ",";
    }

    {
        auto len = element_to_bytes_compressed(data, y_prime);
        val += common::Encode::HexEncode(std::string((char*)data, len)) + ",";
    }

    for (auto &proof : pi_proof) {
        auto len = element_to_bytes(data, proof);
        val += common::Encode::HexEncode(std::string((char*)data, len)) + ",";
        element_clear(proof);
    }

    param.zjc_host->SaveKeyValue(param.from, tmp_key, val);
    ZJC_WARN("single sign success: %d, %s, from: %s, key: %s",
        signer_idx, line_splits[1], 
        common::Encode::HexEncode(param.from).c_str(), tmp_key.c_str());
    element_clear(delta_prime);
    element_clear(y_prime);
    element_clear(private_key);
    // AggSignAndVerify(param, key, line_splits[1]);
    return kContractSuccess;
}

int Ripemd160::AggSignAndVerify(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
        // 聚合签名生成
    auto id = common::Encode::HexDecode(value);
    auto tmp_key = std::string("ars_create_") + id;
    ZJC_DEBUG("get create ars key: %s", common::Encode::HexEncode(tmp_key).c_str());
    std::string val;
    if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
        CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
        return kContractError;
    }

    auto ring_and_signer_count_splits = common::Split<>(val.c_str(), ',');
    int32_t ring_size = 0;
    if (!common::StringUtil::ToInt32(ring_and_signer_count_splits[0], &ring_size)) {
        return kContractError;
    }

    int32_t signer_count = 0;
    if (!common::StringUtil::ToInt32(ring_and_signer_count_splits[1], &signer_count)) {
        return kContractError;
    }

    ContractArs ars;
    ars.set_ring_size(ring_size);
    ars.set_signer_count(signer_count);
    ZJC_DEBUG("success get signer_count: %d, ring_size: %d", signer_count, ring_size);
    element_t agg_signature;
    element_init_G1(agg_signature, ars.get_pairing());
    std::vector<std::string> messages;
    std::vector<element_t> delta_primes(ars.signer_count());
    std::vector<element_t> y_primes(ars.signer_count());
    std::vector<element_t> ring(ars.ring_size());
    if (GetRing(id, param, ars, ring) != kContractSuccess) {
        return kContractError;
    }
    
    std::vector<std::vector<element_t>*> pi_proofs;
    int ret = kContractSuccess;
    int32_t valid_idx = 0;
    for (auto i = 0; i < ars.ring_size(); ++i) {
        auto tmp_key = std::string("ars_create_single_sign_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, tmp_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", tmp_key.c_str());
            continue;
        }

        ZJC_DEBUG("success get single sign key: %s, val: %s, real val: %s", 
            tmp_key.c_str(), common::Encode::HexEncode(val).c_str(), val.c_str());
        auto items = common::Split<1024>(val.c_str(), ',');
        if (items.Count() < 4) {
            ZJC_DEBUG("items.Count() < 4 failed get ars single key: %s", tmp_key.c_str());
            continue;
        }

        messages.push_back(items[0]);
        element_t& delta_prime = delta_primes[valid_idx];
        element_t& y_prime = y_primes[valid_idx];
        element_init_G1(delta_prime, ars.get_pairing());
        element_init_G2(y_prime, ars.get_pairing());
        element_from_bytes_compressed(delta_prime, (unsigned char*)common::Encode::HexDecode(items[1]).c_str());
        element_from_bytes_compressed(y_prime, (unsigned char*)common::Encode::HexDecode(items[2]).c_str());
        std::vector<element_t>* tmp_pi_proof = new std::vector<element_t>(4);
        for (uint32_t j = 3; j < items.Count(); ++j) {
            if (items.SubLen(j) <= 0) {
                break;
            }

            element_t& proof = (*tmp_pi_proof)[j - 3];
            if (j < 5) {
                element_init_G1(proof, ars.get_pairing());
            } else {
                element_init_Zr(proof, ars.get_pairing());
            }

            element_from_bytes(proof, (unsigned char*)common::Encode::HexDecode(items[j]).c_str());
        }

        pi_proofs.push_back(tmp_pi_proof);
        if (pi_proofs.size() == ars.signer_count()) {
            break;
        }

        ++valid_idx;
    }

    if (pi_proofs.size() == ars.signer_count()) {
        ars.AggreSign(messages, y_primes, delta_primes, pi_proofs, ring, agg_signature);
        auto tmp_key = std::string("ars_create_agg_sign");
        unsigned char data[20480] = {0};
        auto len = element_to_bytes_compressed(data, agg_signature);
        auto val = common::Encode::HexEncode(std::string((char*)data, len)) + ",";
        param.zjc_host->SaveKeyValue(param.from, tmp_key, val);
        ZJC_WARN("agg sign success: %s", val.c_str());

        // 聚合签名验证
        bool is_aggregate_valid = ars.AggreVerify(messages, agg_signature, y_primes);
        if (is_aggregate_valid) {
            ZJC_WARN("Aggregate signature verification passed: %s", value.c_str());
        } else {
            ZJC_WARN("Aggregate signature verification failed!");
        }

        element_clear(agg_signature);
    }
    
    for (int32_t i = 0; i < valid_idx; ++i) {
        element_clear(delta_primes[i]);
        element_clear(y_primes[i]);
        auto& item = pi_proofs[i];
        for (uint32_t j = 0; j < 4; ++j) {
            element_clear((*item)[j]);
        }

        delete item;
    }

    for (uint32_t i = 0; i < ring.size(); ++i) {
        element_clear(ring[i]);
    }

    return kContractSuccess;
}

void Ripemd160::TestArs(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    ContractArs ars;
    // 设置环的大小和签名者数量
    const int ring_size = ars.ring_size();
    const int signer_count = ars.signer_count();

    // 创建环中的公钥和私钥对
    std::vector<element_t> private_keys(ring_size);
    std::vector<element_t> public_keys(ring_size);

    // 初始化公私钥对
    for (int i = 0; i < ring_size; ++i) {
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
    std::vector<std::vector<element_t>*> pi_proofs;

    auto& ring = public_keys;
    for (int i = 0; i < signer_count; ++i)
    {
        pi_proofs.push_back(new std::vector<element_t>(4));
        int signer_idx = signers[i];
        element_init_G1(delta_primes[i], ars.get_pairing());
        element_init_G2(y_primes[i], ars.get_pairing());
        ars.SingleSign(messages[i], private_keys[signer_idx], public_keys[signer_idx],
                       ring, delta_primes[i], y_primes[i], *pi_proofs[i]);

        // 打印单个签名的生成内容
        std::cout << "Signer " << signer_idx << " signature details:" << std::endl;
        element_printf("delta_prime: %B\n", delta_primes[i]);
        element_printf("y_prime: %B\n", y_primes[i]);

        // 假设 pi_proofs 是一个 element_s 类型的向量
        std::cout << "pi_proof: ";
        for (const auto &proof : *pi_proofs[i])
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
