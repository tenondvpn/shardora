/*
 * curve25519.h
 * 
 * Generic 64-bit integer implementation of Curve25519 ECDH
 * adapted from Matthijs van Duin's public domain
 * C implementation version 200608242056, which is based on
 * work by Daniel J. Bernstein (http://cr.yp.to/ecdh.html),
 * which is also in public domain.
 * 
 */

#ifndef __XCURVE25519_H__
#define __XCURVE25519_H__

#include <stddef.h>

typedef unsigned char uint8;
typedef long int64;
typedef uint8 key25519[32];   // any type of key

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Curve25519 shared secret computation for key agreement
 *
 * @param k [in] your private key
 * @param P [in] peer's public key
 * @param Z [out] shared secret (needs hashing before use, may overlap with k and P)
 */
void secret25519(const key25519 k, const key25519 P, key25519 Z);

/*
 * Curve25519 key-pair generation from 32 random bytes
 *
 * @param k [in] 32 bytes of random data, [out] your private key
 * @param P [out] your public key
 */
void keygen25519(key25519 k, key25519 P);
#ifdef __cplusplus
}
#endif
#endif  // !__XCURVE25519_H__

/////////////////////////////////////////////////////////////////////////////


