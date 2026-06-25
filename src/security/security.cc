#include "security/security.h"

#include "security/ecdsa/ecdsa.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"

namespace shardora {

namespace security {

std::string Security::GetAddressWithPublicKey(const std::string& pubkey) {
    if (pubkey.size() >= 128u) {
        security::Oqs oqs;
        return oqs.GetAddress(pubkey);
    }

     if (pubkey.size() == 64u) {
        security::GmSsl gmssl;
        return gmssl.GetAddress(pubkey);
    }

    security::Ecdsa ecdsa;
    return ecdsa.GetAddress(pubkey);
}


}  // namespace security

}  // namespace shardora
