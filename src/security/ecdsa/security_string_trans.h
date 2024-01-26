#pragma once

#include <mutex>
#include <memory>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>

#include "common/utils.h"
#include "common/spin_mutex.h"
#include "security/ecdsa/curve.h"

namespace zjchain {

namespace security {

class SecurityStringTrans {
public:
    static SecurityStringTrans* Instance();
    std::shared_ptr<BIGNUM> StringToBignum(const std::string& src);
    void BignumToString(
            const std::shared_ptr<BIGNUM>& value,
            std::string& dst);
    std::shared_ptr<EC_POINT> StringToEcPoint(
        const Curve& curve,
        const std::string& src);
    void EcPointToString(
        const Curve& curve,
        const std::shared_ptr<EC_POINT>& value,
        bool compress,
        std::string& dst);

private:
    SecurityStringTrans() {}
    ~SecurityStringTrans() {}

    common::SpinMutex bitnum_mutex_;
    common::SpinMutex ecpoint_mutex_;

    DISALLOW_COPY_AND_ASSIGN(SecurityStringTrans);
};

}  // namespace security

}  // namespace zjchain
