#include "security/ecdsa/ecdsa.h"

#include "common/encode.h"
#include "security/ecdsa/crypto.h"
#include "security/ecdsa/secp256k1.h"
#include "security/ecdsa/security_string_trans.h"

namespace zjchain {

namespace security {

int Ecdsa::SetPrivateKey(const std::string& prikey) {
    str_prikey_ = prikey;
    prikey_ = std::make_shared<PrivateKey>(prikey);
    if (pubkey_.FromPrivateKey(curve_, *prikey_.get()) != kSecuritySuccess) {
        return kSecurityError;
    }

    if (ecdh_key_.Init(&curve_, prikey_.get(), &pubkey_) != kSecuritySuccess) {
        return kSecurityError;
    }

    str_addr_ = Secp256k1::Instance()->ToAddressWithPublicKey(curve_, pubkey_.str_pubkey());
    return kSecuritySuccess;
}

int Ecdsa::Sign(const std::string& hash, std::string* sign) {
    if (!Secp256k1::Instance()->Secp256k1Sign(hash, *prikey_.get(), sign)) {
        return kSecurityError;
    }

    // CRYPTO_DEBUG("signed hash: %s, sign: %s",
    //     common::Encode::HexEncode(hash).c_str(),
    //     common::Encode::HexEncode(*sign).c_str());
    return kSecuritySuccess;
}

int Ecdsa::Verify(const std::string& hash, const std::string& str_pk, const std::string& sign) {
    if (!Secp256k1::Instance()->Secp256k1Verify(hash, str_pk, sign)) {
        CRYPTO_ERROR("verify sig failed! hash: %s, pk: %s, sign: %s",
            common::Encode::HexEncode(hash).c_str(),
            common::Encode::HexEncode(str_pk).c_str(),
            common::Encode::HexEncode(sign).c_str());
        return kSecurityError;
    }

    return kSecuritySuccess;
}

std::string Ecdsa::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    return Secp256k1::Instance()->GetSign(r, s, v);
}

std::string Ecdsa::Recover(
        const std::string& sign,
        const std::string& hash) {
    return Secp256k1::Instance()->Recover(sign, hash, true);
}

const std::string& Ecdsa::GetAddress() const {
    return str_addr_;
}

std::string Ecdsa::GetAddress(const std::string& pubkey) {
//     std::string addr;
//     if (pk_addr_map_.get(pubkey, &addr)) {
//         return addr;
//     }
// 
    return Secp256k1::Instance()->ToAddressWithPublicKey(curve_, pubkey);
//     pk_addr_map_.add(pubkey, addr);
//     return addr;
}

const std::string& Ecdsa::GetPublicKey() const {
    return pubkey_.str_pubkey();
}

const std::string& Ecdsa::GetPublicKeyUnCompressed() const {
    return pubkey_.str_pubkey_uncompressed();
}

int Ecdsa::Encrypt(const std::string& msg, const std::string& key, std::string* out) {
    return security::Crypto::Instance()->GetEncryptData(
        key,
        msg,
        out);
}

int Ecdsa::Decrypt(const std::string& msg, const std::string& key, std::string* out) {
    return security::Crypto::Instance()->GetDecryptData(
        key,
        msg,
        out);
}

int Ecdsa::GetEcdhKey(const std::string& peer_pubkey, std::string* ecdh_key) {
    PublicKey pk(curve_);
    if (pk.Deserialize(peer_pubkey) != kSecuritySuccess) {
        return kSecurityError;
    }

    return ecdh_key_.CreateKey(pk, ecdh_key);
}

int Ecdsa::GetEcdhKey(const PublicKey& pk, std::string* ecdh_key) {
    return ecdh_key_.CreateKey(pk, ecdh_key);
}

bool Ecdsa::IsValidPublicKey(const std::string& pubkey) {
    auto ptr = SecurityStringTrans::Instance()->StringToEcPoint(curve_, pubkey);
    return ptr != nullptr;
}

std::string Ecdsa::UnicastAddress(const std::string& src_address) {
    return Secp256k1::Instance()->UnicastAddress(src_address);
}

}  // namespace security

}  // namespace zjchain
