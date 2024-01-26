#pragma once

#include "common/utils.h"
#include "big_num/bignum_utils.h"

namespace zjchain {

namespace bignum {

class Snark {
public:
    static Snark* Instance();
    std::string AltBn128PairingProduct(const std::string& in);
    std::string AltBn128G1Add(const std::string& in);
    std::string AltBn128G1Mul(const std::string& in);

private:
    Snark() {}
    ~Snark() {}
    void InitLibSnark();
    libff::bigint<libff::alt_bn128_q_limbs> ToLibsnarkBigint(const std::string& in_x);
    std::string FromLibsnarkBigint(libff::bigint<libff::alt_bn128_q_limbs> const& b);
    libff::alt_bn128_Fq DecodeFqElement(const std::string& data);
    libff::alt_bn128_G1 DecodePointG1(const std::string& data);
    std::string EncodePointG1(libff::alt_bn128_G1 p);
    libff::alt_bn128_Fq2 DecodeFq2Element(const std::string& data);
    libff::alt_bn128_G2 DecodePointG2(const std::string& data);

    DISALLOW_COPY_AND_ASSIGN(Snark);
};

};  // namespace bignum

};  // namespace zjchain