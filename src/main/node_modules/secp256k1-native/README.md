# secp256k1-native
[![Build Status](https://travis-ci.org/chm-diederichs/secp256k1-native.svg?branch=master)](https://travis-ci.org/chm-diederichs/secp256k1-native)

Low-level bindings for bitcoin's secp256k1 elliptic curve [library](https://github.com/bitcoin-core/secp256k1).

## Usage
```js
const crypto = require('crypto')
const secp256k1 = require('secp256k1-native')

const signCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
const verifyCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_VERIFY)

let seckey
do {
  seckey = crypto.randomBytes(32)
} while (!secp256k1.secp256k1_ec_seckey_verify(signCtx, seckey))

const pubkey = Buffer.alloc(64)
secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkey, seckey)

const msg = Buffer.from('Hello, World!')
const msg32 = crypto.createHash('sha256').update(msg).digest()

const sig = Buffer.alloc(64)
secp256k1.secp256k1_ecdsa_sign(signCtx, sig, msg32, seckey)

const result = secp256k1.secp256k1_ecdsa_verify(verifyCtx, sig, msg32, pubkey) ? 'valid' : 'invalid'
console.log('signature is', result)
// signature is valid
```

## API


### Constants

- `secp256k1_SECKEYBYTES = 32`
- `secp256k1_PUBKEYBYTES = 64`
- `secp256k1_ec_TWEAKBYTES = 32`
- `secp256k1_ecdsa_SIGBYTES = 64`
- `secp256k1_ecdsa_recoverable_SIGBYTES = 65`
- `secp256k1_ecdsa_COMPACTBYTES = 64`
- `secp256k1_ecdsa_MSGYBYTES = 32`


### Context


A context is needed in order to store blinding fators used to protect against side-channel attacks.


#### `var ctx = secp256k1.secp256k1_context_create(flags)`

Create a new context in order to protect from side-channel attacks. Returns ctx as a `buffer`.

Flags:
- `secp256k1.secp256k1_context_SIGN`
- `secp256k1.secp256k1_context_VERIFY`
- `secp256k1.secp256k1_context_NONE`
- `secp256k1.secp256k1_context_DECLASSIFY`


#### `secp256k1.secp256k1_context_randomize(ctx, [seed32])`

Randomize a given context. `seed32` may be used to pass 32 bytes of entropy or else it should be passed as `null`.


### Keys

#### `secp256k1.secp256k1_ec_pubkey_parse(ctx, pubkey, input)`

Parse a 33 or 65 byte secp256k1 public key into the 64 byte internal representation used by the library. The result shall be written to `pubkey`, which should be a 64 byte `buffer`. Any `ctx` may be used.


#### `const size = secp256k1.secp256k1_ec_pubkey_serialize(ctx, output, pubkey, flags)`

Serialise a secp256k1 public key to either its 33 or 65 byte representation, the result shall be written to `output` and the function returns the number of bytes written.

Flags:
- `secp256k1.secp256k1_ec_COMPRESSED`
- `secp256k1.secp256k1_ec_UNCOMPRESSED`


#### `secp256k1.secp256k1_ec_sekey_verify(ctx, seckey)`

Verify `seckey` is a valid secp256k1 scalar, ie. less than the curve order. Any `ctx` may be used.


#### `secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkey, seckey)`

Compute the public key for a given secp256k1 private key. `seckey` must be a valid secp256k1 scalar and `pubkey` should be a `buffer` of 64 bytes in length. `signCtx` must have been created with the `secp256k1_context_SIGN` flag.


#### `secp256k1.secp256k1_ec_privkey_negate(ctx, seckey)`

Negate a secp256k1 private key in place. For scalar, `x`, compute `n - x`, where `n` is secp256k1's order. Any `ctx` may be used.


#### `secp256k1.secp256k1_ec_pubkey_negate(ctx, pubkey)`

Negate a secp2561 public key in place. Any `ctx` may be used.


### Key tweaking


#### `sn_secp256k1_ec_privkey_tweak_add(ctx, seckey, tweak)`

Tweak a secp256k1 private key in place, by adding a 32 byte `tweak` to it. Any `ctx` may be used


#### `sn_secp256k1_ec_privkey_tweak_mul(ctx, seckey, tweak)`

Tweak a secp256k1 private key in place, by multiplying with a 32 byte `tweak`. Any `ctx` may be used


#### `sn_secp256k1_ec_privkey_tweak_add(verifyCtx, pubkey, tweak)`

Tweak a secp256k1 public key in place, by adding a `tweak * G` to it, where `G` is the generator. A `VERIFY` context must be used. `pubkey` should be a parsed 64 byte internal representation.


#### `sn_secp256k1_ec_privkey_tweak_mul(verifyCtx, pubkey, tweak)`

Tweak a secp256k1 public key in place, by multiplying with a 32 byte `tweak`. A `VERIFY` context must be used. `pubkey` should be a parsed 64 byte internal representation.


### Signatures


#### `secp256k1.secp256k1_ecdsa_signature_parse_der(ctx, sig, input)`

Parse a DER-encoded ECDSA signature into its internal representation. `sig` should be a 64 byte `buffer`.


#### `secp256k1.secp256k1_ecdsa_signature_parse_compact(ctx, sig, input64)`

Parse a 64 byte ECDSA signature into its internal representation. `sig` should be a 64 byte `buffer`.


#### `secp256k1.secp256k1_ecdsa_signature_serialize_der(ctx, output, sig)`

Serialise `sig` to `output` as a DER-encoded ECDSA signature.


#### `secp256k1.secp256k1_ecdsa_signature_serialize_compact(ctx, output64, sig)`

Serialise `sig` to `output` as a compact 64 byte ECDSA signature.


#### `const hasChanged = secp256k1.secp256k1_ecdsa_signature_normalize(ctx, sigout, sigin)`

Normalise a compact-encoded ECDSA signature to it's lower-S form. Returns a boolean as to whether the signature was changed.


#### `secp256k1.secp256k1_ecdsa_sign(ctx, sig, msg32, seckey)`

Compute an ECDSA signature for `seckey` against `msg32`, which should be the 32 byte hash of a given message. A `SIGN` context should be used.


#### `secp256k1.secp256k1_ecdsa_verify(ctx, sig, msg32, pubkey)`

Verify an ECDSA signature against a `pubkey` and `msg32`. A `VERIFY` context should be used.


### Recoverable signatures

The public key is recoverable from the signature.


#### `secp256k1.secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, sig, input64, recid)`

Given the recovery id, `recid`, parse a 64 byte ECDSA recoverable signature into its 65 byte internal representation. `sig` should be a 65 byte `buffer`.


#### `const recid = secp256k1.secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, output, sig)`

Serialize a recoverable ECDSA signature to `output` in its 64 byte compact-encoded from and return the recovery ID.


#### `secp256k1.secp256k1_ecdsa_recoverable_signature_convert(ctx, sigout, sigin)`

Convert an ECDSA recoverable signature to an ECDSA signature. `sigin` should be `secp256k1_ecdsa_recoverable_SIGBYTES` and `sigout` should be `secp256k1_ecdsa_SIGBYTES`.


#### `secp256k1.secp256k1_ecdsa_sign_recoverable(ctx, sig, msg32, seckey)`

Compute an ECDSA recoverable signature for `seckey` against `msg32`, which should be the 32 byte hash of a given message. A `SIGN` context should be used and `sig` should be `secp256k1_ecdsa_recoverable_SIGBYTES`.


#### `secp256k1.secp256k1_ecdsa_recover(ctx, pubkey, sig, msg32)`

Recover the secp256k1 public key used to create `sig` from `msg32`. A `VERIFY` context should be used and `sig` should be `secp256k1_ecdsa_recoverable_SIGBYTES`.


### Elliptic Curve Diffie-Hellman


#### `secp256k1_ecdh(ctx, output, point, scalar, [data])`

Compute a shared secret by performing an ECDH between a public key, `point`, and a secret key, `scalar`, with optional `data`. `output` should be a 32 byte `buffer` and any context may be used. 
