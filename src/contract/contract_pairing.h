#pragma once

#include <vector>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "cpppbc/PBC.h"
#include "db/db.h"

namespace zjchain {

namespace contract {

class PbcParing : public ContractInterface {
public:
    PbcParing(const std::string& create_address, const std::string& pairing_param);
    virtual ~PbcParing();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    void SetCount(int32_t count) {
        count_ = count;
    }

    void SaveData();
    void DecryptData();

    void TestEnc() {
        SetupParams();
        KeyGen();
        GenTk();
        Encrypt();
    }

    void TestDec() {
        Transform();
    }

// private:
    int SetupParams();
    int KeyGen();
    int GenTk();
    int Encrypt();
    int Decrypt();
    int Transform();
    int TransformAll();

    uint64_t gas_cast_{ 3000llu };

    int32_t count_{ 20 };

    // setup params
    G1 g_;
    Zr alpha_;
    Zr a_;
    G1 g1_;
    GT gt_;
    std::vector<G1> init_zrs_;

    // keygen
    G1 msk_;
    Zr s_;
    G1 R_;
    G1 L_;
    std::vector<G1> Rx_;

    // gen tk
    Zr o_;
    G1 R1_;
    G1 L1_;
    std::vector<G1> Rx1_;

    // encrypt
    GT k_;
    Zr r_;
    std::vector<Zr> tlist_;
    std::vector<Zr> llist_;
    GT C_;
    G1 C1_;
    std::vector<G1> clist_;
    std::vector<G1> dlist_;

    // transform
    std::vector<GT> trans_list_;
    std::vector<Zr> wlist_;
    GT t_;

    // decrypt
    GT decrypt_k_;

    Pairing pairing_;

    uint64_t used_times_[16] = { 0 };

    std::shared_ptr<db::Db> db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(PbcParing);
};

}  // namespace contract

}  // namespace zjchain
