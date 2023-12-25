#include <iostream>
#include "security/ecdsa/private_key.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/curve.h"
#include <common/encode.h>

int main(int argc, char** argv) {
    zjchain::security::Curve curve;
    zjchain::security::PublicKey pubkey(curve);
    zjchain::security::PrivateKey prikey(argv[0]);

    pubkey.FromPrivateKey(curve, prikey);
    std::cout << zjchain::common::Encode::HexEncode(pubkey.str_pubkey()) << std::endl;
    
    return 0;
}
