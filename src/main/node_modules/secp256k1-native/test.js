const test = require('tape')
const vectors = require('./vectors.json')
// const elliptic = require('elliptic')
const secp256k1 = require('./')

test('check constants', function (t) {
  t.equal(secp256k1.secp256k1_SECKEYBYTES, 32, 'secp256k1_SECKEYBYTES')
  t.equal(secp256k1.secp256k1_PUBKEYBYTES, 64, 'secp256k1_PUBKEYBYTES')
  t.equal(secp256k1.secp256k1_ec_TWEAKBYTES, 32, 'secp256k1_ec_TWEAKBYTES')
  t.equal(secp256k1.secp256k1_ecdsa_SIGBYTES, 64, 'secp256k1_ecdsa_SIGBYTES')
  t.equal(secp256k1.secp256k1_ecdsa_recoverable_SIGBYTES, 65, 'secp256k1_ecdsa_SIGBYTES')
  t.equal(secp256k1.secp256k1_ecdsa_COMPACTBYTES, 64, 'secp256k1_ecdsa_COMPACTBYTES')
  t.equal(secp256k1.secp256k1_ecdsa_MSGYBYTES, 32, 'secp256k1_ecdsa_MSGYBYTES')
  t.equal(secp256k1.secp256k1_context_VERIFY, 257, 'secp256k1_context_VERIFY')
  t.equal(secp256k1.secp256k1_context_SIGN, 513, 'secp256k1_context_SIGN')
  t.equal(secp256k1.secp256k1_context_DECLASSIFY, 1025, 'secp256k1_context_DECLASSIFY')
  t.equal(secp256k1.secp256k1_context_NONE, 1, 'secp256k1_context_NONE')
  t.equal(secp256k1.secp256k1_ec_COMPRESSED, 258, 'secp256k1_ec_COMPRESSED')
  t.equal(secp256k1.secp256k1_ec_UNCOMPRESSED, 2, 'secp256k1_ec_UNCOMPRESSED')
  t.equal(secp256k1.secp256k1_tag_pubkey_EVEN, 2, 'secp256k1_tag_pubkey_EVEN')
  t.equal(secp256k1.secp256k1_tag_pubkey_ODD, 3, 'secp256k1_tag_pubkey_ODD')
  t.equal(secp256k1.secp256k1_tag_pubkey_UNCOMPRESSED, 4, 'secp256k1_tag_pubkey_UNCOMPRESSED')
  t.equal(secp256k1.secp256k1_tag_pubkey_HYBRID_EVEN, 6, 'secp256k1_tag_pubkey_HYBRID_EVEN')
  t.equal(secp256k1.secp256k1_tag_pubkey_HYBRID_ODD, 7, 'secp256k1_tag_pubkey_HYBRID_ODD')
  t.end()
})

test('context create', t => {
  t.throws(() => secp256k1.secp256k1_context_create(39))
  t.throws(() => secp256k1.secp256k1_context_create(null))
  t.throws(() => secp256k1.secp256k1_context_create('null'))
  t.throws(() => secp256k1.secp256k1_context_create([1]))

  t.doesNotThrow(() => secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_NONE))
  t.doesNotThrow(() => secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_VERIFY))
  t.doesNotThrow(() => secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN))
  t.doesNotThrow(() => secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_DECLASSIFY))

  t.end()
})

test('context randomize', t => {
  var noneCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_NONE)
  var verifyCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_VERIFY)
  var signCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
  var declassifyCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_DECLASSIFY)

  var seed = Buffer.alloc(32, 1)

  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(noneCtx, null))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(verifyCtx, null))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(signCtx, null))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(declassifyCtx, null))

  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(noneCtx, seed))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(verifyCtx, seed))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(signCtx, seed))
  t.doesNotThrow(() => secp256k1.secp256k1_context_randomize(declassifyCtx, seed))

  t.throws(() => secp256k1.secp256k1_context_randomize(noneCtx, Buffer.alloc(33)))
  t.throws(() => secp256k1.secp256k1_context_randomize(noneCtx, 'string'))
  t.throws(() => secp256k1.secp256k1_context_randomize(noneCtx, 0xff))
  t.throws(() => secp256k1.secp256k1_context_randomize(noneCtx, [2, 3, 4]))

  t.end()
})

test('ec seckey verify', t => {
  var ctx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_VERIFY)

  var noneCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_NONE)
  var signCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
  var declassifyCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_DECLASSIFY)

  t.ok(secp256k1.secp256k1_ec_seckey_verify(ctx, Buffer.alloc(32, 1)))
  t.ok(() => secp256k1.secp256k1_ec_seckey_verify(noneCtx, Buffer.alloc(32, 0xab)))
  t.ok(() => secp256k1.secp256k1_ec_seckey_verify(signCtx, Buffer.alloc(32, 0xab)))
  t.ok(() => secp256k1.secp256k1_ec_seckey_verify(declassifyCtx, Buffer.alloc(32, 0xab)))
  
  t.notOk(secp256k1.secp256k1_ec_seckey_verify(ctx, Buffer.alloc(32, 0xff)))

  t.throws(() => secp256k1.secp256k1_ec_seckey_verify(ctx, Buffer.alloc(16, 0xab)))
  t.throws(() => secp256k1.secp256k1_ec_seckey_verify(ctx, 'string'))
  t.throws(() => secp256k1.secp256k1_ec_seckey_verify(ctx, 0xff))
  t.throws(() => secp256k1.secp256k1_ec_seckey_verify(ctx, [2, 3, 4]))

  t.end()
})

test('ec pubkey', t => {
  var ctx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)

  const pubkey = Buffer.alloc(64)
  const pubkeyFromRaw = Buffer.alloc(64)
  const pubkeySerialise = Buffer.alloc(65)
  const privKey = Buffer.from('c70df8568f2c56f38a68049608fccfff9a46fc0991f11b12ac4759f5c067f9e7', 'hex')
  const expect = Buffer.from('04611e760c3adf5452217943797d57b8ec95253a5d6daf2dc8e7e95eeb2db69c53381d4519eb0bb4d0e15af982393f2b3104370573cd7ad897dee2baddd623ec99', 'hex')
  const badPrivKey = Buffer.alloc(32, 0xff)

  t.throws(() => secp256k1.secp256k1_ec_pubkey_create(ctx, pubkey, badPrivKey))
  t.doesNotThrow(() => secp256k1.secp256k1_ec_pubkey_create(ctx, pubkey, privKey))
  t.doesNotThrow(() => secp256k1.secp256k1_ec_pubkey_serialize(ctx, pubkeySerialise, pubkey, secp256k1.secp256k1_ec_UNCOMPRESSED))
  t.doesNotThrow(() => secp256k1.secp256k1_ec_pubkey_parse(ctx, pubkeyFromRaw, expect))

  t.same(pubkeyFromRaw, pubkey)
  t.same(pubkeySerialise, expect)

  var buf = Buffer.alloc(1000)
  var bytesWritten = secp256k1.secp256k1_ec_pubkey_serialize(ctx, buf.subarray(700), pubkey, secp256k1.secp256k1_ec_UNCOMPRESSED)
  t.same(buf.subarray(700, 700 + bytesWritten), expect)

  t.end()
})

test('ec key vectors', t => {
  var signCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
  var verifyCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_VERIFY)
  var noneCtx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_NONE)
  
  for (let vector of vectors.pubkey) {
    const privkey = Buffer.from(vector.privkey, 'hex')
    const pubkey = Buffer.alloc(64)
    const pubkeyCompressed = Buffer.alloc(33)
    const pubkeyUncompressed = Buffer.alloc(65)
    const tweak = Buffer.from(vector.tweak, 'hex')
    const sig = Buffer.alloc(64)
    const message = Buffer.from(vectors.message, 'hex')

    const privkey32 = Buffer.alloc(32)
    const pubkey33 = Buffer.alloc(33)
    const pubkey64 = Buffer.alloc(64)
    const pubkey65 = Buffer.alloc(65)
    const pubkeyCheck = Buffer.alloc(64)
    const sig64 = Buffer.alloc(64)
    const sig65 = Buffer.alloc(65)
    const recoverableSig65 = Buffer.alloc(65)

    secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkey, privkey)
    secp256k1.secp256k1_ec_pubkey_serialize(signCtx, pubkeyCompressed, pubkey, secp256k1.secp256k1_ec_COMPRESSED)
    secp256k1.secp256k1_ec_pubkey_serialize(signCtx, pubkeyUncompressed, pubkey, secp256k1.secp256k1_ec_UNCOMPRESSED)
    secp256k1.secp256k1_ec_pubkey_parse(signCtx, pubkey64, Buffer.from(vector.pubkey, 'hex'))

    t.same(pubkeyCompressed, Buffer.from(vector.pubkey, 'hex'))
    t.same(pubkeyUncompressed, Buffer.from(vector.uncompressed, 'hex'))
    t.same(pubkey, pubkey64)

    // sign

    secp256k1.secp256k1_ecdsa_sign(signCtx, sig64, message, privkey)
    secp256k1.secp256k1_ecdsa_sign_recoverable(signCtx, recoverableSig65, message, privkey)
    secp256k1.secp256k1_ecdsa_signature_parse_compact(signCtx, sig, Buffer.from(vector.signature, 'hex'))

    t.ok(secp256k1.secp256k1_ecdsa_verify(verifyCtx, sig, message, pubkey))

    sig65.set(sig)
    sig65[64] = vector.recid

    t.same(recoverableSig65, sig65, 'sig matches ref')

    secp256k1.secp256k1_ecdsa_recover(verifyCtx, pubkeyCheck, sig65, message)
    secp256k1.secp256k1_ec_pubkey_serialize(verifyCtx, pubkey33, pubkeyCheck, secp256k1.secp256k1_ec_COMPRESSED)

    t.same(pubkey33, Buffer.from(vector.pubkey, 'hex'), 'public key recovered')

    secp256k1.secp256k1_ecdsa_recoverable_signature_convert(signCtx, sig65.slice(0, 64), sig65)

    t.ok(secp256k1.secp256k1_ecdsa_verify(verifyCtx, sig64, message, pubkey), 'ecdsa signature valid')
    t.same(sig64, sig64.slice(0, 64), 'ecdsa signature matches ref')

    // negate
    pubkey64.set(pubkey)
    privkey32.set(privkey)

    secp256k1.secp256k1_ec_privkey_negate(signCtx, privkey32)
    secp256k1.secp256k1_ec_pubkey_negate(signCtx, pubkey64)
    secp256k1.secp256k1_ec_pubkey_serialize(signCtx, pubkey33, pubkey64, secp256k1.secp256k1_ec_COMPRESSED)
    secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkeyCheck, privkey32)

    t.same(privkey32, Buffer.from(vector.negatePriv, 'hex'), 'priv negate')
    t.same(pubkey33, Buffer.from(vector.negate, 'hex'), 'pub negate')
    t.same(pubkey64, pubkeyCheck, 'check negate')
    

    // tweak add
    pubkey64.set(pubkey)
    privkey32.set(privkey)

    secp256k1.secp256k1_ec_privkey_tweak_add(signCtx, privkey32, tweak)
    secp256k1.secp256k1_ec_pubkey_tweak_add(verifyCtx, pubkey64, tweak)
    secp256k1.secp256k1_ec_pubkey_serialize(verifyCtx, pubkey33, pubkey64, secp256k1.secp256k1_ec_COMPRESSED)
    secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkeyCheck, privkey32)

    t.same(privkey32, Buffer.from(vector.privkeyAdd, 'hex'), 'priv tweak add')
    t.same(pubkey33, Buffer.from(vector.pubkeyAdd, 'hex'), 'pub tweak add')
    t.same(pubkey64, pubkeyCheck, 'check tweak add')

    // tweak add
    pubkey64.set(pubkey)
    privkey32.set(privkey)

    secp256k1.secp256k1_ec_privkey_tweak_mul(signCtx, privkey32, tweak)
    secp256k1.secp256k1_ec_pubkey_tweak_mul(verifyCtx, pubkey64, tweak)
    secp256k1.secp256k1_ec_pubkey_serialize(verifyCtx, pubkey33, pubkey64, secp256k1.secp256k1_ec_COMPRESSED)
    secp256k1.secp256k1_ec_pubkey_create(signCtx, pubkeyCheck, privkey32)

    t.same(privkey32, Buffer.from(vector.privkeyMul, 'hex'), 'priv tweak mul')
    t.same(pubkey33, Buffer.from(vector.pubkeyMul, 'hex'), 'pub tweak mul')
    t.same(pubkey64, pubkeyCheck, 'check tweak mul')
  }

  t.end()
})

test('ec key combine vectors', t => {
  var ctx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
  
  const pubkeys = vectors.pubkey.map(vec => Buffer.from(vec.pubkey, 'hex'))
  for (let vector of vectors.combine) {
    const pubs = []

    for (let i of vector.indices) {
      buf = Buffer.alloc(64)
      secp256k1.secp256k1_ec_pubkey_parse(ctx, buf, pubkeys[i])
      pubs.push(buf)
    }

    const pubkey33 = Buffer.alloc(33)
    const pubkey64 = Buffer.alloc(64)

    secp256k1.secp256k1_ec_pubkey_combine(ctx, pubkey64, pubs)
    secp256k1.secp256k1_ec_pubkey_serialize(ctx, pubkey33, pubkey64, secp256k1.secp256k1_ec_COMPRESSED)

    t.same(pubkey33, Buffer.from(vector.combine, 'hex'))
  }

  var tooMany = new Array(101)
  var key = Buffer.alloc(64)
  secp256k1.secp256k1_ec_pubkey_parse(ctx, buf, pubkeys[1])

  for (let i = 0; i < 101; i++) {
    tooMany[i] = key
  }

  t.throws(() => secp256k1.secp256k1_ec_pubkey_combine(ctx, key, tooMany))

  t.end()
})

test('ecdh vectors', t => {
  var ctx = secp256k1.secp256k1_context_create(secp256k1.secp256k1_context_SIGN)
  
  for (let vector of vectors.ecdh) {
    const secret = Buffer.alloc(32)
    const pubkey64 = Buffer.alloc(64)
    const data = vector.data ? Buffer.from(vector.data, 'hex') : null

    secp256k1.secp256k1_ec_pubkey_parse(ctx, pubkey64, Buffer.from(vector.pub, 'hex'))
    secp256k1.secp256k1_ecdh(ctx, secret, pubkey64, Buffer.from(vector.scalar, 'hex'), data)

    t.same(secret, Buffer.from(vector.ecdh, 'hex'))
  }

  t.end()
})
