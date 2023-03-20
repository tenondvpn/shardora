#include "security/ecdsa/secp256k1.h"

#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <cassert>

#include <ethash/keccak.hpp>
#include "common/string_utils.h"
#include "common/encode.h"
#include "common/hash.h"
#include "security/ecdsa/crypto_utils.h"
#include "security/ecdsa/private_key.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/security_string_trans.h"

namespace zjchain {

namespace security {

Secp256k1::Secp256k1() {
    ctx_ = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
}

Secp256k1::~Secp256k1() {}

Secp256k1* Secp256k1::Instance() {
    static Secp256k1 ins;
    return &ins;
}

typedef uint8_t byte;
struct SignatureStruct
{
    byte r[32];
    byte s[32];
    byte v = 0;
};

using u256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
static const u256 c_secp256k1n("115792089237316195423570985008687907852837564279074904382605163141518161494337");
inline void U256ToByteArray(u256 _val, byte* o_out)
{
    for (auto i = 32; i != 0; _val >>= 8, i--)
    {
        u256 v = _val & (u256)0xff;
        o_out[i - 1] = (uint8_t)v;
    }
}

inline bool ByteArrayLess(byte* l, byte* r) {
    for (unsigned i = 0; i < 32; ++i) {
        if (l[i] < r[i]) {
            return true;
        } else if (l[i] > r[i]) {
            return false;
        }
    }
        
    return false;
}

bool Secp256k1::Sign(const std::string& hash, const PrivateKey& privkey, std::string* sign) {
    secp256k1_ecdsa_signature sig;
    secp256k1_ecdsa_sign(
        getCtx(),
        &sig,
        (const uint8_t*)hash.c_str(),
        (const uint8_t*)privkey.private_key().c_str(),
        NULL,
        NULL);
    uint8_t data[kSignatureSize];
    secp256k1_ecdsa_signature_serialize_compact(getCtx(), data, &sig);
    *sign = std::string((char*)data, sizeof(data));
    return true;
}

bool Secp256k1::Verify(
        const std::string& hash,
        const PublicKey& pubkey,
        const std::string& sign) {
    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_signature_parse_compact(
            getCtx(),
            &sig,
            (uint8_t*)sign.c_str()) != 1) {
        return false;
    }

    if (secp256k1_ecdsa_verify(
            getCtx(),
            &sig,
            (uint8_t*)hash.c_str(),
            pubkey.pubkey()) != 1) {
        return false;
    }

    return true;
}

std::string Secp256k1::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    secp256k1_ecdsa_recoverable_signature sig;
    memcpy(sig.data, r.c_str(), 32);
    memcpy(sig.data + 32, s.c_str(), 32);
    sig.data[64] = v;
    uint8_t data[kSignatureSize];
    int tmp_v = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(getCtx(), data, &tmp_v, &sig);
    SignatureStruct& ss = *reinterpret_cast<SignatureStruct*>(data);
    ss.v = static_cast<byte>(tmp_v);
    byte tmp[32] = { 0 };
    U256ToByteArray(c_secp256k1n / 2, tmp);
    if (ByteArrayLess(tmp, ss.s)) {
        ss.v = static_cast<byte>(ss.v ^ 1);
        std::array<byte, 32>& tmp_arr = *reinterpret_cast<std::array<byte, 32>*>(ss.s);
        auto tmp_val = c_secp256k1n - u256(tmp_arr);
        U256ToByteArray(tmp_val, ss.s);
    }

    std::string sign((char*)data, sizeof(data));
    sign[64] = char(tmp_v);
    return sign;
}

bool Secp256k1::Secp256k1Sign(
        const std::string& msg,
        const PrivateKey& privkey,
        std::string* sign) {
    secp256k1_ecdsa_recoverable_signature sig;
    if (secp256k1_ecdsa_sign_recoverable(
            getCtx(),
            &sig,
            (const uint8_t*)msg.c_str(),
            (const uint8_t*)privkey.private_key().c_str(),
            NULL,
            NULL) != 1) {
        return false;
    }
    uint8_t data[kSignatureSize];
    int v = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(getCtx(), data, &v, &sig);
    SignatureStruct& ss = *reinterpret_cast<SignatureStruct*>(data);
    ss.v = static_cast<byte>(v);
    byte tmp[32] = { 0 };
    U256ToByteArray(c_secp256k1n / 2, tmp);
    if (ByteArrayLess(tmp, ss.s)) {
        ss.v = static_cast<byte>(ss.v ^ 1);
        std::array<byte,32>& tmp_arr = *reinterpret_cast<std::array<byte, 32>*>(ss.s);
        auto tmp_val = c_secp256k1n - u256(tmp_arr);
        U256ToByteArray(tmp_val, ss.s);
        ZJC_DEBUG("DDDDDDDDDDDDDDD");
        assert(false);
    }

    *sign = std::string((char*)data, sizeof(data));
    (*sign)[64] = char(v);
    return true;
}

bool Secp256k1::Secp256k1Verify(
        const std::string& msg,
        const PublicKey& pubkey,
        const std::string& sign) {
    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_signature_parse_compact(
            getCtx(),
            &sig,
            (const uint8_t*)sign.c_str()) != 1) {
        return false;
    }

    if (secp256k1_ecdsa_verify(
            getCtx(),
            &sig,
            (uint8_t*)msg.c_str(),
            pubkey.pubkey()) != 1) {
        return false;
    }

    return true;
}

bool Secp256k1::Secp256k1Verify(
        const std::string& msg,
        const std::string& pubkey,
        const std::string& sign) {
    auto recover_pk = Recover(sign, msg, pubkey.size() == kPublicCompressKeySize);
    return memcmp(pubkey.c_str() + 1, recover_pk.c_str(), pubkey.size() - 1) == 0;
}

std::string Secp256k1::Recover(const std::string& sign, const std::string& hash, bool compressed) {
    int v = sign[64];
    if (v > 3) {
        CRYPTO_ERROR("sign invalid: %u", v);
        return "";
    }

    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(
            getCtx(),
            &sig,
            (const uint8_t*)sign.c_str(),
            v)) {
        return "";
    }

    secp256k1_pubkey raw_pubkey;
    if (!secp256k1_ecdsa_recover(getCtx(), &raw_pubkey, &sig, (uint8_t*)hash.c_str())) {
        return "";
    }

    if (compressed) {
        std::array<uint8_t, 33> serialized_pubkey;
        size_t serialized_pubkey_size = serialized_pubkey.size();
        secp256k1_ec_pubkey_serialize(
            getCtx(),
            serialized_pubkey.data(),
            &serialized_pubkey_size,
            &raw_pubkey,
            SECP256K1_EC_COMPRESSED);
        return std::string((char*)&serialized_pubkey[1], 32);
    } else {
        std::array<uint8_t, 65> serialized_pubkey;
        size_t serialized_pubkey_size = serialized_pubkey.size();
        secp256k1_ec_pubkey_serialize(
            getCtx(), serialized_pubkey.data(), &serialized_pubkey_size,
            &raw_pubkey, SECP256K1_EC_UNCOMPRESSED);
        return std::string((char*)&serialized_pubkey[1], 64);
    }
    
}

std::string Secp256k1::RecoverForContract(const std::string& sign, const std::string& hash) {
    std::string str_v = sign.substr(0, 32);
    std::string str_sig = sign.substr(32, sign.size() - 32);
    uint8_t v = (uint8_t)str_v[31] - (uint8_t)27;
    str_sig += (char)(v);
    if (v > 3) {
        return "";
    }

    secp256k1_ecdsa_recoverable_signature raw_sig;
    if (secp256k1_ecdsa_recoverable_signature_parse_compact(
        getCtx(),
            &raw_sig,
            (uint8_t*)str_sig.c_str(),
            v) != 1) {
        return "";
    }

    secp256k1_pubkey raw_pubkey;
    if (!secp256k1_ecdsa_recover(getCtx(), &raw_pubkey, &raw_sig, (uint8_t*)hash.c_str())) {
        return "";
    }

    std::array<uint8_t, 65> serialized_pubkey;
    size_t serialized_pubkey_size = serialized_pubkey.size();
    secp256k1_ec_pubkey_serialize(
        getCtx(), serialized_pubkey.data(), &serialized_pubkey_size,
        &raw_pubkey, SECP256K1_EC_UNCOMPRESSED);
    return std::string((char*)&serialized_pubkey[1], 64);
}

std::string Secp256k1::sha3(const std::string& input) {
    ethash::hash256 h = ethash::keccak256((uint8_t*)input.c_str(), input.size());
    return std::string((char*)h.bytes, 32);
}

std::string Secp256k1::ToPublicFromCompressed(const std::string& in_pubkey) {
    auto* ctx = getCtx();
    secp256k1_pubkey raw_pubkey;
    if (!secp256k1_ec_pubkey_parse(
            ctx,
            &raw_pubkey,
            (uint8_t*)in_pubkey.c_str(),
            in_pubkey.size())) {
        return "";
    }

    std::array<uint8_t, 65> serialized_pubkey;
    auto serialized_pubkey_size = serialized_pubkey.size();
    secp256k1_ec_pubkey_serialize(
        ctx,
        serialized_pubkey.data(),
        &serialized_pubkey_size,
        &raw_pubkey,
        SECP256K1_EC_UNCOMPRESSED);
    assert(serialized_pubkey_size == serialized_pubkey.size());
    assert(serialized_pubkey[0] == 0x04);
    return std::string((char*)serialized_pubkey.data(), serialized_pubkey.size());
}

std::string Secp256k1::ToAddressWithPublicKey(
        const Curve& curve,
        const std::string& pub_key) {
//     auto ptr = SecurityStringTrans::Instance()->StringToEcPoint(curve, pub_key);
//     if (ptr == nullptr) {
//         return "";
//     }

    if (pub_key.size() == kPublicCompressKeySize) {
        return UnicastAddress(common::Hash::keccak256(
            ToPublicFromCompressed(pub_key).substr(1, 64)));
    }

    if (pub_key.size() == kPublicKeyUncompressSize) {
        return UnicastAddress(common::Hash::keccak256(pub_key.substr(1, 64)));
    }

    assert(pub_key.size() == kPublicKeyUncompressSize - 1);
    return UnicastAddress(common::Hash::keccak256(pub_key));
}

}  // namespace security

}  // namespace zjchain
