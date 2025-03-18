#include "security/ecdsa/ecdsa.h"

#include <gmssl/sm2_recover.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "security/gmssl/gmssl.h"

namespace shardora {

namespace security {

int GmSsl::SetPrivateKey(const std::string& prikey) {
    assert(prikey.size() == 32);
    str_prikey_ = prikey;
    prikey_ = std::make_shared<SM2_KEY>();
    // 设置私钥
    memcpy(prikey_->private_key, prikey.c_str(), 32);
    // 根据私钥生成公钥
    if (sm2_key_generate(prikey_.get()) != 1) {
        ZJC_ERROR("Failed to generate public key from private key.");
        return kSecurityError;
    }

    str_pk_ = std::string((char*)prikey_->public_key.x, 32) + std::string((char*)prikey_->public_key.y, 32);
    str_addr_ = common::Hash::sm3(str_pk_).substr(0, 20);
    return kSecuritySuccess;
}

int GmSsl::Sign(const std::string &hash, std::string *sign) {
	SM2_SIGNATURE sig;
	sm2_do_sign(prikey_.get(), (uint8_t*)hash.c_str(), &sig);
    *sign = std::string((char*)sig.r, 32) + std::string((char*)sig.s, 32);
    return kSecuritySuccess;
}

int GmSsl::Verify(const std::string& hash, const std::string& str_pk, const std::string& sign) {
    assert(sign.size() == 64);
    assert(hash.size() == 32);
	SM2_SIGNATURE sig;
    memcpy(sig.r, sign.c_str(), 32);
    memcpy(sig.s, sign.c_str() + 32, 32);
	SM2_POINT points[4];
	size_t points_cnt;
	sm2_signature_to_public_key_points(&sig, (uint8_t*)hash.c_str(), points, &points_cnt);
    auto tmp_pk = std::string((char*)points[1].x, 32) + std::string((char*)points[1].y, 32);
    if (memcmp(tmp_pk.c_str(), str_pk.c_str(), tmp_pk.size()) != 0) {
        ZJC_DEBUG("sign get pk: %s, src pk: %s", 
            common::Encode::HexEncode(tmp_pk).c_str(), 
            common::Encode::HexEncode(str_pk).c_str());
        printf("sign get pk: %s, src pk: %s\n", 
            common::Encode::HexEncode(tmp_pk).c_str(), 
            common::Encode::HexEncode(str_pk).c_str());
        return kSecurityError;
    }

    return kSecuritySuccess;
}

std::string GmSsl::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    ZJC_FATAL("invalid!");
    return "";
}

std::string GmSsl::Recover(
        const std::string& sign,
        const std::string& hash) {
    ZJC_FATAL("invalid!");
    return "";
}

const std::string& GmSsl::GetAddress() const {
    return str_addr_;
}

std::string GmSsl::GetAddress(const std::string& pubkey) {
    return common::Hash::sm3(pubkey).substr(0, 20);
}

const std::string& GmSsl::GetPublicKey() const {
    return str_pk_;
}

const std::string& GmSsl::GetPublicKeyUnCompressed() const {
    return str_pk_;
}

int GmSsl::Encrypt(const std::string& msg, const std::string& key, std::string* out) {
    ZJC_FATAL("invalid!");
    return -1;
}

int GmSsl::Decrypt(const std::string& msg, const std::string& key, std::string* out) {
    ZJC_FATAL("invalid!");
    return -1;
}

bool GmSsl::IsValidPublicKey(const std::string& pubkey) {
    return pubkey.size() == 96;
}

std::string GmSsl::UnicastAddress(const std::string& src_address) {
    ZJC_FATAL("invalid");
    return "";
}

}  // namespace security

}  // namespace shardora
