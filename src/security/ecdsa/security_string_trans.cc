#include "security/ecdsa/security_string_trans.h"

#include "common/encode.h"
#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

SecurityStringTrans* SecurityStringTrans::Instance() {
    static SecurityStringTrans ins;
    return &ins;
}

std::shared_ptr<BIGNUM> SecurityStringTrans::StringToBignum(const std::string& src) {
    if (src.empty()) {
        return nullptr;
    }
    common::AutoSpinLock guard(bitnum_mutex_);
    return std::shared_ptr<BIGNUM>(
            BN_bin2bn((unsigned char*)(src.c_str()), src.size(), NULL),
            BN_clear_free);
}

void SecurityStringTrans::BignumToString(
        const std::shared_ptr<BIGNUM>& value,
        std::string& dst) {
    common::AutoSpinLock guard(bitnum_mutex_);
    const int kSrcBnSize = BN_num_bytes(value.get());
    if (dst.size() < static_cast<uint32_t>(kSrcBnSize)) {
        dst.resize(kSrcBnSize);
    }

    if (BN_bn2bin(
            value.get(),
            (unsigned char*)(&(dst[0]))) != kSrcBnSize) {
        CRYPTO_ERROR("BN_bn2bin failed");
        dst.clear();
    }
}

std::shared_ptr<EC_POINT> SecurityStringTrans::StringToEcPoint(
        const Curve& curve,
        const std::string& src) {
    if (src.empty()) {
        return nullptr;
    }

    std::shared_ptr<BIGNUM> bnvalue = StringToBignum(src);
    if (bnvalue == nullptr) {
        CRYPTO_ERROR("BIGNUMSerialize::GetNumber failed");
        return nullptr;
    }

    common::AutoSpinLock guard(ecpoint_mutex_);
    std::unique_ptr<BN_CTX, void(*)(BN_CTX*)> ctx(BN_CTX_new(), BN_CTX_free);
    if (ctx == nullptr) {
        CRYPTO_ERROR("Memory allocation failure");
        return nullptr;
    }

    auto ret = std::shared_ptr<EC_POINT>(
        EC_POINT_bn2point(
                curve.group_.get(),
                bnvalue.get(),
                NULL,
                ctx.get()),
        EC_POINT_clear_free);
    return ret;
}

void SecurityStringTrans::EcPointToString(
        const Curve& curve,
        const std::shared_ptr<EC_POINT>& value,
        bool compress,
        std::string& dst) {
    std::shared_ptr<BIGNUM> bnvalue;
    {
        common::AutoSpinLock guard(ecpoint_mutex_);
        std::unique_ptr<BN_CTX, void(*)(BN_CTX*)> ctx(BN_CTX_new(), BN_CTX_free);
        if (ctx == nullptr) {
            CRYPTO_ERROR("Memory allocation failure");
            return;
        }

        auto tag = compress ? POINT_CONVERSION_COMPRESSED : POINT_CONVERSION_UNCOMPRESSED;
        bnvalue.reset(
            EC_POINT_point2bn(
                    curve.group_.get(),
                    value.get(),
                    tag,
                    NULL,
                    ctx.get()),
            BN_clear_free);
    }

    if (bnvalue == nullptr) {
        CRYPTO_ERROR("EC_POINT_point2bn failed");
        return;
    }

    SecurityStringTrans::Instance()->BignumToString(bnvalue, dst);
}

}  // namespace security

}  // namespace zjchain
