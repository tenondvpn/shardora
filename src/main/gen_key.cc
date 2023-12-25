#include <iostream>
#include "security/ecdsa/private_key.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/curve.h"

int main(int argc, char** argv) {
    zjchain::security::Curve curve;
    zjchain::security::PublicKey pubkey(curve);
    zjchain::security::PrivateKey prikey(argv[0]);

    pubkey.FromPrivateKey(curve, prikey);
    std::cout << pubkey.str_pubkey_uncompressed() << std::endl;
    
    return 0;
}
