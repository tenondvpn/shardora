#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "macros.h"
#include "secp256k1.h"
#include "secp256k1_ecdh.h"
#include "secp256k1_preallocated.h"
#include "secp256k1_recovery.h"
// #include "secp256k1/src/secp256k1.c"

static uint8_t typedarray_width(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array: return 1;
    case napi_uint8_array: return 1;
    case napi_uint8_clamped_array: return 1;
    case napi_int16_array: return 2;
    case napi_uint16_array: return 2;
    case napi_int32_array: return 4;
    case napi_uint32_array: return 4;
    case napi_float32_array: return 4;
    case napi_float64_array: return 8;
    case napi_bigint64_array: return 8;
    case napi_biguint64_array: return 8;
    default: return 0;
  }
}

static void sn_secp256k1_context_preallocated_destroy (napi_env env, void* finalise_data, void* finalise_hint) {
  secp256k1_context_preallocated_destroy(finalise_data);
}

napi_value sn_secp256k1_context_create (napi_env env, napi_callback_info info) {
  SN_ARGV(1, secp256_context_create)

  SN_ARGV_UINT32(flags, 0)

  SN_THROWS(flags != SECP256K1_CONTEXT_VERIFY && flags != SECP256K1_CONTEXT_SIGN && flags != SECP256K1_CONTEXT_DECLASSIFY && flags != SECP256K1_CONTEXT_NONE, "must use one of the 'secp26k1_context'' flags")

  secp256k1_context *ctx = secp256k1_context_create(flags);

  napi_value buf;

  SN_STATUS_THROWS(napi_create_external_buffer(env, sizeof(ctx), ctx, &sn_secp256k1_context_preallocated_destroy, NULL, &buf), "failed to create a n-api buffer")

  return buf;
}

napi_value sn_secp256k1_context_randomize (napi_env env, napi_callback_info info) {
  SN_ARGV(2, secp256k1_context_randomize)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_OPTS_TYPEDARRAY(seed32, 1)

  if (seed32_data != NULL) {
    SN_THROWS(seed32_size != 32, "seed must be 'secp256k1_context_SEEDBYTES' bytes")
  }

  SN_RETURN(secp256k1_context_randomize(ctx, seed32_data), "context could not be randomized")
}

napi_value sn_secp256k1_ec_pubkey_parse (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_pubkey_parse)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)
  SN_ARGV_TYPEDARRAY(input, 2)

  SN_RETURN(secp256k1_ec_pubkey_parse(ctx, pubkey, input_data, input_size), "pubkey could not be parsed")
}

napi_value sn_secp256k1_ec_pubkey_serialize (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ec_pubkey_serialize)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(output, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 2)
  SN_ARGV_UINT32(flags, 3)

  size_t min_len = flags == SECP256K1_EC_COMPRESSED ? 33 : 65;

  SN_THROWS(output_size < min_len, "output buffer must be at least 33/65 bytes for compressed/uncompressed serialisation")
  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")

  SN_CALL(secp256k1_ec_pubkey_serialize(ctx, output_data, &output_size, pubkey, flags), "pubkey could not be serialised")

  napi_value result;
  SN_STATUS_THROWS(napi_create_uint32(env, (uint32_t) output_size, &result), "")
  return result;
}

napi_value sn_secp256k1_ec_seckey_verify (napi_env env, napi_callback_info info) {
  SN_ARGV(2, secp256k1_ec_seckey_verify)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(seckey, 1)

  SN_THROWS(seckey_size != 32, "seckey should be 'secp256k1_SECKEYBYTES' bytes")

  SN_RETURN_BOOLEAN_FROM_1(secp256k1_ec_seckey_verify(ctx, seckey_data))
}

napi_value sn_secp256k1_ec_privkey_negate (napi_env env, napi_callback_info info) {
  SN_ARGV(2, secp256k1_ec_privkey_negate)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(seckey, 1)

  SN_THROWS(seckey_size != 32, "seckey should be 'secp256k1_SECKEYBYTES' bytes")

  SN_RETURN(secp256k1_ec_privkey_negate(ctx, seckey_data), "privkey could not be negated")
}

napi_value sn_secp256k1_ec_pubkey_negate (napi_env env, napi_callback_info info) {
  SN_ARGV(2, secp256k1_ec_pubkey_negate)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)

  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")

  SN_RETURN(secp256k1_ec_pubkey_negate(ctx, pubkey), "pubkey could not be negated")
}

napi_value sn_secp256k1_ec_pubkey_create (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_pubkey_create)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)
  SN_ARGV_TYPEDARRAY(seckey, 2)

  SN_THROWS(seckey_size != 32, "seckey should be 'secp256k1_SECKEYBYTES' bytes")
  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")

  SN_RETURN(secp256k1_ec_pubkey_create(ctx, pubkey, seckey_data), "could not generate public key")
}

napi_value sn_secp256k1_ec_privkey_tweak_add (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_privkey_tweaka_add)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(seckey, 1)
  SN_ARGV_TYPEDARRAY(tweak, 2)

  SN_THROWS(seckey_size != 32, "pubkey must be 'secp256k1_SECKEYBYTES' bytes")
  SN_THROWS(tweak_size != 32, "tweak must be 'secp256k1_ec_TWEAKBYTES' bytes")

  SN_RETURN(secp256k1_ec_privkey_tweak_add(ctx, seckey_data, tweak_data), "could not generate public key")
}

napi_value sn_secp256k1_ec_pubkey_tweak_add (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_pubkey_tweak_add)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)
  SN_ARGV_TYPEDARRAY(tweak, 2)

  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")
  SN_THROWS(tweak_size != 32, "tweak must be 'secp256k1_ec_TWEAKBYTES' bytes")

  SN_RETURN(secp256k1_ec_pubkey_tweak_add(ctx, pubkey, tweak_data), "could not generate public key")
}

napi_value sn_secp256k1_ec_privkey_tweak_mul (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_privkey_tweaka_mul)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(seckey, 1)
  SN_ARGV_TYPEDARRAY(tweak, 2)

  SN_THROWS(seckey_size != 32, "pubkey must be 'secp256k1_SECKEYBYTES' bytes")
  SN_THROWS(tweak_size != 32, "tweak must be 'secp256k1_ec_TWEAKBYTES' bytes")

  SN_RETURN(secp256k1_ec_privkey_tweak_mul(ctx, seckey_data, tweak_data), "could not generate public key")
}

napi_value sn_secp256k1_ec_pubkey_tweak_mul (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_pubkey_tweak_mul)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)
  SN_ARGV_TYPEDARRAY(tweak, 2)

  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")
  SN_THROWS(tweak_size != 32, "tweak must be 'secp256k1_ec_TWEAKBYTES' bytes")

  SN_RETURN(secp256k1_ec_pubkey_tweak_mul(ctx, pubkey, tweak_data), "could not generate public key")
}

napi_value sn_secp256k1_ec_pubkey_combine (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ec_pubkey_tweak_mul)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubnonce, 1)

  uint32_t batch_length;
  napi_get_array_length(env, argv[2], &batch_length);
  SN_THROWS(batch_length > 100, "batch too long, number of keys should be <= 100 ")

  const secp256k1_pubkey* pubkeys[100];

  for (uint32_t i = 0; i < batch_length; i++) {
    napi_value element;
    napi_get_element(env, argv[2], i, &element);
    SN_TYPEDARRAY_ASSERT(pubkey, element, "pubkey should be passed as a TypedArray")
    SN_BUFFER_CAST(secp256k1_pubkey *, pubkey, element)
    SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")
    pubkeys[i] = pubkey;
  }

  int success = secp256k1_ec_pubkey_combine(ctx, pubnonce, pubkeys, batch_length);
  SN_THROWS(success != 1, "could not combine public keys")

  return NULL;
}

napi_value sn_secp256k1_ecdsa_signature_parse_der (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_signature_parse_der)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(input, 2)

  SN_RETURN(secp256k1_ecdsa_signature_parse_der(ctx, sig, input_data, input_size), "signature could not be parsed")
}

napi_value sn_secp256k1_ecdsa_signature_parse_compact (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_signature_parse_compact)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(input64, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(input64_size != 64, "input64 must be 'secp256k1_ecdsa_COMPACTBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_signature_parse_compact(ctx, sig, input64_data), "signature could not be parsed")
}

napi_value sn_secp256k1_ecdsa_signature_serialize_der (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_signature_serialize_der)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(output, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_signature_serialize_der(ctx, output_data, &output_size, sig), "could not serialise signature")
}

napi_value sn_secp256k1_ecdsa_signature_serialize_compact (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_signature_serialize_compact)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(output64, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(output64_size != 64, "output must be 'secp256k1_ecdsa_COMPACTBYTES'")

  SN_RETURN(secp256k1_ecdsa_signature_serialize_compact(ctx, output64_data, sig), "could not serialise signature")
}

napi_value sn_secp256k1_ecdsa_signature_normalize (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_signature_normalize)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sigout, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sigin, 2)

  SN_THROWS(sigout_size != sizeof(secp256k1_ecdsa_signature), "sigout must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(sigin_size != sizeof(secp256k1_ecdsa_signature), "sigin must be 'secp256k1_ecdsa_SIGBYTES' bytes")

  SN_RETURN_BOOLEAN_FROM_1(secp256k1_ecdsa_signature_normalize(ctx, sigout, sigin))
}

napi_value sn_secp256k1_ecdsa_verify (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ecdsa_verify)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(msg32, 2)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 3)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(msg32_size != 32, "msg32 should be 'secp256k1_ecdsa_MSGBYTES' bytes")
  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")

  SN_RETURN_BOOLEAN_FROM_1(secp256k1_ecdsa_verify(ctx, sig, msg32_data, pubkey))
}

napi_value sn_secp256k1_ecdsa_sign (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ecdsa_sign)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(msg32, 2)
  SN_ARGV_TYPEDARRAY(seckey, 3)

  SN_THROWS(seckey_size != 32, "seckey should be 'secp256k1_SECKEYBYTES' bytes")
  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(msg32_size != 32, "msg32 should be 'secp256k1_ecdsa_MSGBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_sign(ctx, sig, msg32_data, seckey_data, NULL, NULL), "signature could not be generated")
}

napi_value sn_secp256k1_ecdsa_recoverable_signature_parse_compact (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ecdsa_recoverable_signature_parse_compact)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_recoverable_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(input64, 2)
  SN_ARGV_UINT32(recid, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_recoverable_signature), "sig must be 'secp256k1_ecdsa_recoverable_SIGBYTES' bytes")
  SN_THROWS(input64_size != 64, "input64 must be 'secp256k1_ecdsa_COMPACTBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, sig, input64_data, recid), "signature could not be parsed")
}

napi_value sn_secp256k1_ecdsa_recoverable_signature_serialize_compact (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_recoverable_signature_serialize_compact)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(output64, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_recoverable_signature *, sig, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_recoverable_signature), "sig must be 'secp256k1_ecdsa_recoverable_SIGBYTES' bytes")
  SN_THROWS(output64_size != 64, "output must be 'secp256k1_ecdsa_COMPACTBYTES'")

  int recid;
  SN_CALL(secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, output64_data, &recid, sig), "could not serialise signature")

  napi_value result;
  SN_STATUS_THROWS(napi_create_uint32(env, (uint32_t) recid, &result), "")
  return result;
}

napi_value sn_secp256k1_ecdsa_recoverable_signature_convert (napi_env env, napi_callback_info info) {
  SN_ARGV(3, secp256k1_ecdsa_recoverable_signature_convert)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_signature *, sig, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_recoverable_signature *, sigin, 2)

  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_signature), "sig must be 'secp256k1_ecdsa_SIGBYTES' bytes")
  SN_THROWS(sigin_size != sizeof(secp256k1_ecdsa_recoverable_signature), "sig must be 'secp256k1_ecdsa_recoverable_SIGBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_recoverable_signature_convert(ctx, sig, sigin), "could not convert signature")
}

napi_value sn_secp256k1_ecdsa_sign_recoverable (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ecdsa_sign_recoverable)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_recoverable_signature *, sig, 1)
  SN_ARGV_TYPEDARRAY(msg32, 2)
  SN_ARGV_TYPEDARRAY(seckey, 3)

  SN_THROWS(seckey_size != 32, "seckey should be 'secp256k1_SECKEYBYTES' bytes")
  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_recoverable_signature), "sig must be 'secp256k1_ecdsa_recoverable_SIGBYTES' bytes")
  SN_THROWS(msg32_size != 32, "msg32 should be 'secp256k1_ecdsa_MSGBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_sign_recoverable(ctx, sig, msg32_data, seckey_data, NULL, NULL), "signature could not be generated")
}

napi_value sn_secp256k1_ecdsa_recover (napi_env env, napi_callback_info info) {
  SN_ARGV(4, secp256k1_ecdsa_recover)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, pubkey, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_ecdsa_recoverable_signature *, sig, 2)
  SN_ARGV_TYPEDARRAY(msg32, 3)

  SN_THROWS(pubkey_size != sizeof(secp256k1_pubkey), "pubkey should be 'secp256k1_PUBKEYBYTES' bytes")
  SN_THROWS(sig_size != sizeof(secp256k1_ecdsa_recoverable_signature), "sig must be 'secp256k1_ecdsa_recoverable_SIGBYTES' bytes")
  SN_THROWS(msg32_size != 32, "msg32 should be 'secp256k1_ecdsa_MSGBYTES' bytes")

  SN_RETURN(secp256k1_ecdsa_recover(ctx, pubkey, sig, msg32_data), "public key could not be recovered")
}

napi_value sn_secp256k1_ecdh (napi_env env, napi_callback_info info) {
  SN_ARGV(5, secp256k1_ecdh)

  SN_ARGV_BUFFER_CAST(secp256k1_context *, ctx, 0)
  SN_ARGV_TYPEDARRAY(output, 1)
  SN_ARGV_BUFFER_CAST(secp256k1_pubkey *, point, 2)
  SN_ARGV_TYPEDARRAY(scalar, 3)
  SN_ARGV_OPTS_TYPEDARRAY_PTR(data, 4)

  SN_THROWS(output_size != 32, "output should be 'secp256k1_ecdh_BYTES' bytes")
  SN_THROWS(point_size != sizeof(secp256k1_pubkey), "pubkey must be 'secp256k1_PUBKEYBYTES' bytes")
  SN_THROWS(scalar_size != 32, "scalar should be 'secp256k1_ecdh_SCALARBYTES' bytes")

  SN_RETURN(secp256k1_ecdh(ctx, output_data, point, scalar_data, NULL, data), "ecdh could not be completed")
}

static napi_value create_secp256k1_native(napi_env env) {
  napi_value exports;
  assert(napi_create_object(env, &exports) == napi_ok);

  SN_EXPORT_UINT32(secp256k1_SECKEYBYTES, 32)
  SN_EXPORT_UINT32(secp256k1_ec_TWEAKBYTES, 32)
  SN_EXPORT_UINT32(secp256k1_PUBKEYBYTES, sizeof(secp256k1_pubkey))
  SN_EXPORT_UINT32(secp256k1_ecdsa_SIGBYTES, sizeof(secp256k1_ecdsa_signature))
  SN_EXPORT_UINT32(secp256k1_ecdsa_recoverable_SIGBYTES, sizeof(secp256k1_ecdsa_recoverable_signature))
  SN_EXPORT_UINT32(secp256k1_ecdsa_COMPACTBYTES, 64)
  SN_EXPORT_UINT32(secp256k1_ecdsa_MSGYBYTES, 32)
  SN_EXPORT_UINT32(secp256k1_context_VERIFY, SECP256K1_CONTEXT_VERIFY)
  SN_EXPORT_UINT32(secp256k1_context_SIGN, SECP256K1_CONTEXT_SIGN)
  SN_EXPORT_UINT32(secp256k1_context_DECLASSIFY, SECP256K1_CONTEXT_DECLASSIFY)
  SN_EXPORT_UINT32(secp256k1_context_NONE, SECP256K1_CONTEXT_NONE)
  SN_EXPORT_UINT32(secp256k1_context_SEEDBYTES, 32)
  SN_EXPORT_UINT32(secp256k1_ec_COMPRESSED, SECP256K1_EC_COMPRESSED)
  SN_EXPORT_UINT32(secp256k1_ec_UNCOMPRESSED, SECP256K1_EC_UNCOMPRESSED)
  SN_EXPORT_UINT32(secp256k1_tag_pubkey_EVEN, SECP256K1_TAG_PUBKEY_EVEN)
  SN_EXPORT_UINT32(secp256k1_tag_pubkey_ODD, SECP256K1_TAG_PUBKEY_ODD)
  SN_EXPORT_UINT32(secp256k1_tag_pubkey_UNCOMPRESSED, SECP256K1_TAG_PUBKEY_UNCOMPRESSED)
  SN_EXPORT_UINT32(secp256k1_tag_pubkey_HYBRID_EVEN, SECP256K1_TAG_PUBKEY_HYBRID_EVEN)
  SN_EXPORT_UINT32(secp256k1_tag_pubkey_HYBRID_ODD, SECP256K1_TAG_PUBKEY_HYBRID_ODD)

  SN_EXPORT_FUNCTION(secp256k1_context_create, sn_secp256k1_context_create)
  SN_EXPORT_FUNCTION(secp256k1_context_randomize, sn_secp256k1_context_randomize)
  SN_EXPORT_FUNCTION(secp256k1_ec_seckey_verify, sn_secp256k1_ec_seckey_verify)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_create, sn_secp256k1_ec_pubkey_create)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_parse, sn_secp256k1_ec_pubkey_parse)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_serialize, sn_secp256k1_ec_pubkey_serialize)
  SN_EXPORT_FUNCTION(secp256k1_ec_privkey_negate, sn_secp256k1_ec_privkey_negate)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_negate, sn_secp256k1_ec_pubkey_negate)
  SN_EXPORT_FUNCTION(secp256k1_ec_privkey_tweak_add, sn_secp256k1_ec_privkey_tweak_add)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_tweak_add, sn_secp256k1_ec_pubkey_tweak_add)
  SN_EXPORT_FUNCTION(secp256k1_ec_privkey_tweak_mul, sn_secp256k1_ec_privkey_tweak_mul)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_tweak_mul, sn_secp256k1_ec_pubkey_tweak_mul)
  SN_EXPORT_FUNCTION(secp256k1_ec_pubkey_combine, sn_secp256k1_ec_pubkey_combine)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_signature_parse_der, sn_secp256k1_ecdsa_signature_parse_der)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_signature_parse_compact, sn_secp256k1_ecdsa_signature_parse_compact)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_signature_serialize_der, sn_secp256k1_ecdsa_signature_serialize_der)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_signature_serialize_compact, sn_secp256k1_ecdsa_signature_serialize_compact)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_signature_normalize, sn_secp256k1_ecdsa_signature_normalize)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_verify, sn_secp256k1_ecdsa_verify)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_sign, sn_secp256k1_ecdsa_sign)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_recoverable_signature_parse_compact, sn_secp256k1_ecdsa_recoverable_signature_parse_compact)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_recoverable_signature_serialize_compact, sn_secp256k1_ecdsa_recoverable_signature_serialize_compact)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_recoverable_signature_convert, sn_secp256k1_ecdsa_recoverable_signature_convert)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_sign_recoverable, sn_secp256k1_ecdsa_sign_recoverable)
  SN_EXPORT_FUNCTION(secp256k1_ecdsa_recover, sn_secp256k1_ecdsa_recover)
  SN_EXPORT_FUNCTION(secp256k1_ecdh, sn_secp256k1_ecdh)

  return exports;
}

static napi_value Init(napi_env env, napi_value exports) {
  return create_secp256k1_native(env);
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

