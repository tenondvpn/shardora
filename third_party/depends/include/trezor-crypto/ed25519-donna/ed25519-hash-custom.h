/*
	a custom hash must have a 512bit digest and implement:

	struct ed25519_hash_context;

	void ed25519_hash_init(ed25519_hash_context *ctx);
	void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen);
	void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash);
	void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen);
*/

#ifndef ED25519_HASH_CUSTOM
#define ED25519_HASH_CUSTOM

#include "sha2.h"

#define ed25519_hash_context SHA512_CTX
#define ed25519_hash_init(ctx) sha512_Init(ctx)
#define ed25519_hash_update(ctx, in, inlen) sha512_Update((ctx), (in), (inlen))
#define ed25519_hash_final(ctx, hash) sha512_Final((ctx), (hash))
#define ed25519_hash(hash, in, inlen) sha512_Raw((in), (inlen), (hash))

#endif // ED25519_HASH_CUSTOM
