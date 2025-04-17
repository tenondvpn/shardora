#pragma once

#include "contract/contract_ars.h"
#include "contract/contract_interface.h"
#include "common/tick.h"
#include "pki/rabpre.h"

namespace shardora {

namespace contract {

class Ripemd160 : public ContractInterface {
public:
    Ripemd160(const std::string& create_address);
    virtual ~Ripemd160();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    int CheckDecrytParamsValid(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int AddParams(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int Decrypt(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int TestPbc(const std::string& param);
    int GetValue(
        const CallParameters& param,
        const std::string& key,
        std::string* val,
        evmc_result* res);
    void AddAllParams(
        const std::string& prev,
        const CallParameters& param,
        const std::string& val);
    int AddReEncryptionParam(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

    // ars
    int CreateArsKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int SingleSign(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int GetRing(
        const std::string& id,
        const CallParameters& param, 
        ContractArs& ars, 
        std::vector<element_t>& ring);
    int AggSignAndVerify(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    void TestArs(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);

    int RabpreInit(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    void SaveCrs(const CRS& crs, std::string* val);
    void LoadCrs(const std::string& val, CRS& crs);
    void SaveSkPk(
        long long sk, 
        const std::tuple<long long, long long>& pk, 
        std::string* val);
    void LoadSkPk(
        const std::string& val, 
        long long* sk, 
        std::tuple<long long, long long>* pk);
    void SaveAgg(
        std::tuple<long long, long long, long long, long long, long long, long long>& mpk,
        std::tuple<long long, long long, long long, long long, long long>& hsk0,
        std::tuple<long long, long long, long long, long long, long long>& hsk1,
        std::string* val);
    void LoadAgg(
        const std::string& val, 
        std::tuple<long long, long long, long long, long long, long long, long long>* mpk,
        std::tuple<long long, long long, long long, long long, long long>* hsk0,
        std::tuple<long long, long long, long long, long long, long long>* hsk1);
    int RabpreEnc(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    void SaveEncVal(
        const std::tuple<int, long long, long long, long long,
        long long, long long, long long, long long,
        long long, long long>& enc,
        std::string* val);
    void LoadEncVal(
        const std::string& val, 
        std::tuple<int, long long, long long, long long,
        long long, long long, long long, long long,
        long long, long long>* enc);
    int RabpreDec(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int RabpreReEnc(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    void SaveReenc(const std::tuple<int, std::tuple<long long, long long>,
        std::tuple<int, long long, long long, long long,
              long long, long long, long long, long long,
              long long, long long>, long long>& reenc,
            std::string* val);
    void LoadReenc(
        const std::string& val, 
        std::tuple<int, std::tuple<long long, long long>,
            std::tuple<int, long long, long long, long long,
                long long, long long, long long, long long,
                long long, long long>, long long>* reenc);
    int RabpreReDec(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);

    uint64_t gas_cast_{ 3000llu };

    uint64_t pbc_prepair_cast_{ 168164llu };
    uint64_t pbc_exp_cast_{ 279344llu };
    uint64_t pbc_pairing_cast_{ 46308llu };

    DISALLOW_COPY_AND_ASSIGN(Ripemd160);
};

}  // namespace contract

}  // namespace shardora
