# Patch-overlapping JavaScript context after the fix

## `lib/jwe/decrypt.js` (changed lines (55, 192))

```javascript
const { inflateRawSync } = require('zlib')

const base64url = require('../help/base64url')
const getKey = require('../help/get_key')
const { KeyStore } = require('../jwks')
const errors = require('../errors')
const { check, decrypt, keyManagementDecrypt } = require('../jwa')
const JWK = require('../jwk')

const { createSecretKey } = require('../help/key_object')
const generateCEK = require('./generate_cek')
const validateHeaders = require('./validate_headers')
const { detect: resolveSerialization } = require('./serializers')

const SINGLE_RECIPIENT = new Set(['compact', 'flattened'])

const combineHeader = (prot = {}, unprotected = {}, header = {}) => {
  if (typeof prot === 'string') {
    prot = base64url.JSON.decode(prot)
  }

  const p2s = prot.p2s || unprotected.p2s || header.p2s
  const apu = prot.apu || unprotected.apu || header.apu
  const apv = prot.apv || unprotected.apv || header.apv
  const iv = prot.iv || unprotected.iv || header.iv
  const tag = prot.tag || unprotected.tag || header.tag

  return {
    ...prot,
    ...unprotected,
    ...header,
    ...(typeof p2s === 'string' ? { p2s: base64url.decodeToBuffer(p2s) } : undefined),
    ...(typeof apu === 'string' ? { apu: base64url.decodeToBuffer(apu) } : undefined),
    ...(typeof apv === 'string' ? { apv: base64url.decodeToBuffer(apv) } : undefined),
    ...(typeof iv === 'string' ? { iv: base64url.decodeToBuffer(iv) } : undefined),
    ...(typeof tag === 'string' ? { tag: base64url.decodeToBuffer(tag) } : undefined)
  }
}

const validateAlgorithms = (algorithms, option) => {
  if (algorithms !== undefined && (!Array.isArray(algorithms) || algorithms.some(s => typeof s !== 'string' || !s))) {
    throw new TypeError(`"${option}" option must be an array of non-empty strings`)
  }

  if (!algorithms) {
    return undefined
  }

  return new Set(algorithms)
}

/*
 * @public
 */
const jweDecrypt = (skipValidateHeaders, serialization, jwe, key, { crit = [], complete = false, keyManagementAlgorithms, contentEncryptionAlgorithms, maxPBES2Count = 10000, inflateRawSyncLimit = 250000 } = {}) => {
  key = getKey(key, true)

  keyManagementAlgorithms = validateAlgorithms(keyManagementAlgorithms, 'keyManagementAlgorithms')
  contentEncryptionAlgorithms = validateAlgorithms(contentEncryptionAlgorithms, 'contentEncryptionAlgorithms')

  if (!Array.isArray(crit) || crit.some(s => typeof s !== 'string' || !s)) {
    throw new TypeError('"crit" option must be an array of non-empty strings')
  }

  if (!serialization) {
    serialization = resolveSerialization(jwe)
  }

  let alg, ciphertext, enc, encryptedKey, iv, opts, prot, tag, unprotected, cek, aad, header

  // treat general format with one recipient as flattened
  // skips iteration and avoids multi errors in this case
  if (serialization === 'general' && jwe.recipients.length === 1) {
    serialization = 'flattened'
    const { recipients, ...root } = jwe
    jwe = { ...root, ...recipients[0] }
  }

  if (SINGLE_RECIPIENT.has(serialization)) {
    if (serialization === 'compact') { // compact serialization format
      ([prot, encryptedKey, iv, ciphertext, tag] = jwe.split('.'))
    } else { // flattened serialization format
      ({ protected: prot, encrypted_key: encryptedKey, iv, ciphertext, tag, unprotected, aad, header } = jwe)
    }

    if (!skipValidateHeaders) {
      validateHeaders(prot, unprotected, [{ header }], true, crit)
    }

    opts = combineHeader(prot, unprotected, header)

    ;({ alg, enc } = opts)

    if (keyManagementAlgorithms && !keyManagementAlgorithms.has(alg)) {
      throw new errors.JOSEAlgNotWhitelisted('key management algorithm not whitelisted')
    }

    if (contentEncryptionAlgorithms && !contentEncryptionAlgorithms.has(enc)) {
      throw new errors.JOSEAlgNotWhitelisted('content encryption algorithm not whitelisted')
    }

    if (key instanceof KeyStore) {
      const keystore = key
      let keys
      if (opts.alg === 'dir') {
        keys = keystore.all({ kid: opts.kid, alg: opts.enc, key_ops: ['decrypt'] })
      } else {
        keys = keystore.all({ kid: opts.kid, alg: opts.alg, key_ops: ['unwrapKey'] })
      }
      switch (keys.length) {
        case 0:
          throw new errors.JWKSNoMatchingKey()
        case 1:
          // treat the call as if a Key instance was passed in
          // skips iteration and avoids multi errors in this case
          key = keys[0]
          break
        default: {
          const errs = []
          for (const key of keys) {
            try {
              return jweDecrypt(true, serialization, jwe, key, {
                crit,
                complete,
                contentEncryptionAlgorithms: contentEncryptionAlgorithms ? [...contentEncryptionAlgorithms] : undefined,
                keyManagementAlgorithms: keyManagementAlgorithms ? [...keyManagementAlgorithms] : undefined
              })
            } catch (err) {
              errs.push(err)
              continue
            }
          }

          const multi = new errors.JOSEMultiError(errs)
          if ([...multi].some(e => e instanceof errors.JWEDecryptionFailed)) {
            throw new errors.JWEDecryptionFailed()
          }
          throw multi
        }
      }
    }

    check(key, ...(alg === 'dir' ? ['decrypt', enc] : ['keyManagementDecrypt', alg]))

    if (alg.startsWith('PBES2')) {
      if (opts && opts.p2c > maxPBES2Count) {
        throw new errors.JWEInvalid('JOSE Header "p2c" (PBES2 Count) out is of acceptable bounds')
      }
    }

    try {
      if (alg === 'dir') {
        cek = JWK.asKey(key, { alg: enc, use: 'enc' })
      } else if (alg === 'ECDH-ES') {
        const unwrapped = keyManagementDecrypt(alg, key, undefined, opts)
        cek = JWK.asKey(createSecretKey(unwrapped), { alg: enc, use: 'enc' })
      } else {
        const unwrapped = keyManagementDecrypt(alg, key, base64url.decodeToBuffer(encryptedKey), opts)
        cek = JWK.asKey(createSecretKey(unwrapped), { alg: enc, use: 'enc' })
      }
    } catch (err) {
      // To mitigate the attacks described in RFC 3218, the
      // recipient MUST NOT distinguish between format, padding, and length
      // errors of encrypted keys.  It is strongly recommended, in the event
      // of receiving an improperly formatted key, that the recipient
      // substitute a randomly generated CEK and proceed to the next step, to
      // mitigate timing attacks.
      cek = generateCEK(enc)
    }

    let adata
    if (aad) {
      adata = Buffer.concat([
        Buffer.from(prot || ''),
        Buffer.from('.'),
        Buffer.from(aad)
      ])
    } else {
      adata = Buffer.from(prot || '')
    }

    try {
      iv = base64url.decodeToBuffer(iv)
    } catch (err) {}
    try {
      tag = base64url.decodeToBuffer(tag)
    } catch (err) {}

    let cleartext = decrypt(enc, cek, base64url.decodeToBuffer(ciphertext), { iv, tag, aad: adata })

    if (opts.zip) {
      cleartext = inflateRawSync(cleartext, { maxOutputLength: inflateRawSyncLimit })
    }

    if (complete) {
      const result = { cleartext, key, cek }
      if (aad) result.aad = aad
      if (header) result.header = header
      if (unprotected) result.unprotected = unprotected
      if (prot) result.protected = base64url.JSON.decode(prot)
      return result
    }

    return cleartext
  }

  validateHeaders(jwe.protected, jwe.unprotected, jwe.recipients.map(({ header }) => ({ header })), true, crit)

  // general serialization format
  const { recipients, ...root } = jwe
  const errs = []
  for (const recipient of recipients) {
    try {
      return jweDecrypt(true, 'flattened', { ...root, ...recipient }, key, {
        crit,
        complete,
        contentEncryptionAlgorithms: contentEncryptionAlgorithms ? [...contentEncryptionAlgorithms] : undefined,
        keyManagementAlgorithms: keyManagementAlgorithms ? [...keyManagementAlgorithms] : undefined
      })
    } catch (err) {
      errs.push(err)
      continue
    }
  }

  const multi = new errors.JOSEMultiError(errs)
  if ([...multi].some(e => e instanceof errors.JWEDecryptionFailed)) {
    throw new errors.JWEDecryptionFailed()
  } else if ([...multi].every(e => e instanceof errors.JWKSNoMatchingKey)) {
    throw new errors.JWKSNoMatchingKey()
  }
  throw multi
}

module.exports = jweDecrypt.bind(undefined, false, undefined)
```

## `test/jwe/sanity.test.js` (changed lines (2, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641))

```javascript
const test = require('ava')
const crypto = require('crypto')

const base64url = require('../../lib/help/base64url')
const { JWKS, JWK: { generateSync }, JWE, errors } = require('../..')

test('keyManagementAlgorithms option be an array of strings', t => {
  ;[{}, new Object(), false, null, Infinity, 0, '', Buffer.from('foo')].forEach((val) => { // eslint-disable-line no-new-object
    t.throws(() => {
      JWE.decrypt({
        header: { alg: 'HS256' },
        payload: 'foo',
        ciphertext: 'bar'
      }, generateSync('oct'), { keyManagementAlgorithms: val })
    }, { instanceOf: TypeError, message: '"keyManagementAlgorithms" option must be an array of non-empty strings' })
    t.throws(() => {
      JWE.decrypt({
        header: { alg: 'HS256' },
        payload: 'foo',
        ciphertext: 'bar'
      }, generateSync('oct'), { keyManagementAlgorithms: [val] })
    }, { instanceOf: TypeError, message: '"keyManagementAlgorithms" option must be an array of non-empty strings' })
  })
})

test('contentEncryptionAlgorithms option be an array of strings', t => {
  ;[{}, new Object(), false, null, Infinity, 0, '', Buffer.from('foo')].forEach((val) => { // eslint-disable-line no-new-object
    t.throws(() => {
      JWE.decrypt({
        header: { alg: 'HS256' },
        payload: 'foo',
        ciphertext: 'bar'
      }, generateSync('oct'), { contentEncryptionAlgorithms: val })
    }, { instanceOf: TypeError, message: '"contentEncryptionAlgorithms" option must be an array of non-empty strings' })
    t.throws(() => {
      JWE.decrypt({
        header: { alg: 'HS256' },
        payload: 'foo',
        ciphertext: 'bar'
      }, generateSync('oct'), { contentEncryptionAlgorithms: [val] })
    }, { instanceOf: TypeError, message: '"contentEncryptionAlgorithms" option must be an array of non-empty strings' })
  })
})

test('compact parts length check', t => {
  t.throws(() => {
    JWE.decrypt('', generateSync('oct'))
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
  t.throws(() => {
    JWE.decrypt('...', generateSync('oct'))
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
  t.throws(() => {
    JWE.decrypt('.....', generateSync('oct'))
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
})

test('JWE no alg specified but cannot resolve', t => {
  const k1 = generateSync('oct', undefined, { key_ops: ['sign'] })
  t.throws(() => {
    JWE.encrypt('foo', k1)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'could not resolve a usable "alg" for a recipient' })
})

test('JWE no alg/enc specified (multi recipient)', t => {
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(generateSync('RSA'))
  encrypt.recipient(generateSync('oct', 256))
  if (!('electron' in process.versions)) {
    encrypt.recipient(generateSync('EC'))
  }

  const jwe = encrypt.encrypt('general')
  t.is(jwe.unprotected, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { enc: 'A128CBC-HS256' })
  t.deepEqual(jwe.recipients[0].header, { alg: 'RSA-OAEP' })
  if (!('electron' in process.versions)) {
    t.deepEqual(jwe.recipients[1].header, { alg: 'A256KW' })
    const { epk, ...rest } = jwe.recipients[2].header
    t.deepEqual(rest, { alg: 'ECDH-ES+A128KW' })
  } else {
    const { alg, ...rest } = jwe.recipients[1].header
    t.is(alg, 'A256GCMKW')
    t.true('iv' in rest)
    t.true('tag' in rest)
  }
})

test('JWE no alg/enc specified (multi recipient) with per-recipient headers', t => {
  const encrypt = new JWE.Encrypt('foo')
  const k1 = generateSync('RSA', undefined, { kid: 'kid_1' })
  encrypt.recipient(k1, { kid: k1.kid })
  const k2 = generateSync('oct', 256, { kid: 'kid_3' })
  encrypt.recipient(k2, { kid: k2.kid })
  if (!('electron' in process.versions)) {
    const k3 = generateSync('EC', undefined, { kid: 'kid_2' })
    encrypt.recipient(k3, { kid: k3.kid })
  }

  const jwe = encrypt.encrypt('general')
  t.is(jwe.unprotected, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { enc: 'A128CBC-HS256' })
  t.deepEqual(jwe.recipients[0].header, { alg: 'RSA-OAEP', kid: 'kid_1' })
  if (!('electron' in process.versions)) {
    t.deepEqual(jwe.recipients[1].header, { alg: 'A256KW', kid: 'kid_3' })
    const { epk, ...rest } = jwe.recipients[2].header
    t.deepEqual(rest, { alg: 'ECDH-ES+A128KW', kid: 'kid_2' })
  } else {
    const { alg, kid, ...rest } = jwe.recipients[1].header
    t.is(alg, 'A256GCMKW')
    t.is(kid, 'kid_3')
    t.true('iv' in rest)
    t.true('tag' in rest)
  }
})

test('JWE no alg/enc specified (single rsa), no protected header', t => {
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(generateSync('RSA'))

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'RSA-OAEP', enc: 'A128CBC-HS256' })
})

test('JWE no alg/enc specified (single rsa), with protected header', t => {
  const k = generateSync('RSA', undefined, { kid: 'jwk key id' })
  const encrypt = new JWE.Encrypt('foo', { kid: k.kid })
  encrypt.recipient(k)

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'RSA-OAEP', enc: 'A128CBC-HS256', kid: 'jwk key id' })
})

test('JWE no alg specified (single rsa), with protected header', t => {
  const k = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo', { enc: 'A256CBC-HS512' })
  encrypt.recipient(k)

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'RSA-OAEP', enc: 'A256CBC-HS512' })
})

test('JWE no alg specified (single rsa), with unprotected header', t => {
  const k = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo', undefined, undefined, { enc: 'A256CBC-HS512' })
  encrypt.recipient(k)

  const jwe = encrypt.encrypt('flattened')
  t.deepEqual(jwe.unprotected, { enc: 'A256CBC-HS512' })
  t.is(jwe.header, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'RSA-OAEP' })
})

test('JWE no alg/enc specified (single oct)', t => {
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(generateSync('oct', 128))

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  if (!('electron' in process.versions)) {
    t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'A128KW', enc: 'A128CBC-HS256' })
  } else {
    const { iv, tag, ...rest } = base64url.JSON.decode(jwe.protected)
    t.deepEqual(rest, { alg: 'A128GCMKW', enc: 'A128CBC-HS256' })
    t.truthy(iv)
    t.truthy(tag)
  }
})

test('JWE no alg/enc specified (single ec)', t => {
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(generateSync('EC'))

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  const { epk, ...rest } = base64url.JSON.decode(jwe.protected)
  t.deepEqual(rest, { alg: 'ECDH-ES', enc: 'A128CBC-HS256' })
})

test('JWE no alg/enc specified (only on a key)', t => {
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(generateSync('RSA', undefined, { alg: 'RSA1_5', use: 'enc' }))

  const jwe = encrypt.encrypt('flattened')
  t.is(jwe.unprotected, undefined)
  t.is(jwe.header, undefined)
  t.deepEqual(base64url.JSON.decode(jwe.protected), { alg: 'RSA1_5', enc: 'A128CBC-HS256' })
})

test('aes_cbc_hmac_sha2 decrypt iv check (missing)', t => {
  const k = generateSync('oct', undefined, { alg: 'A128CBC-HS256' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  delete encrypted.iv
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
})

test('aes_cbc_hmac_sha2 decrypt iv check (invalid length)', t => {
  const k = generateSync('oct', undefined, { alg: 'A128CBC-HS256' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  encrypted.iv = base64url.encode(base64url.decodeToBuffer(encrypted.iv).slice(1))
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'invalid iv' })
})

test('aes_cbc_hmac_sha2 decrypt tag check (missing)', t => {
  const k = generateSync('oct', undefined, { alg: 'A128CBC-HS256' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  delete encrypted.tag
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
})

test('aes_cbc_hmac_sha2 decrypt tag check (invalid length)', t => {
  const k = generateSync('oct', undefined, { alg: 'A128CBC-HS256' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  encrypted.tag = base64url.encode(base64url.decodeToBuffer(encrypted.tag).slice(1))
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'invalid tag' })
})

test('aes_gcm decrypt iv check (missing)', t => {
  const k = generateSync('oct', 128, { alg: 'A128GCM' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  delete encrypted.iv
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
})

test('aes_gcm decrypt iv check (invalid length)', t => {
  const k = generateSync('oct', 128, { alg: 'A128GCM' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  encrypted.iv = base64url.encode(base64url.decodeToBuffer(encrypted.iv).slice(1))
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'invalid iv' })
})

test('aes_gcm decrypt tag check (missing)', t => {
  const k = generateSync('oct', 128, { alg: 'A128GCM' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  delete encrypted.tag
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE malformed or invalid serialization' })
})

test('aes_gcm decrypt tag check (invalid length)', t => {
  const k = generateSync('oct', 128, { alg: 'A128GCM' })
  const encrypted = JWE.encrypt.flattened('foo', k, { alg: 'dir', enc: k.alg })
  encrypted.tag = base64url.encode(base64url.decodeToBuffer(encrypted.tag).slice(1))
  t.throws(() => {
    JWE.decrypt(encrypted, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'invalid tag' })
})

test('JWE encrypt accepts buffer', t => {
  const k = generateSync('oct')
  JWE.encrypt(Buffer.from('foo'), k)
  t.pass()
})

test('JWE encrypt accepts string', t => {
  const k = generateSync('oct')
  JWE.encrypt('foo', k)
  t.pass()
})

test('JWE encrypt rejects other', t => {
  const k = generateSync('oct')
  ;[[], {}, false, true, undefined, null, Infinity, 0].forEach((val) => {
    t.throws(() => {
      JWE.encrypt(val, k)
    }, { instanceOf: TypeError, message: 'cleartext argument must be a Buffer or a string' })
  })
})

test('JWE encrypt protectedHeader rejects non objects if provided', t => {
  const k = generateSync('oct')
  ;[[], false, true, null, Infinity, 0, Buffer.from('foo')].forEach((val) => {
    t.throws(() => {
      JWE.encrypt('foo', k, val)
    }, { instanceOf: TypeError, message: 'protectedHeader argument must be a plain object when provided' })
  })
})

test('JWE encrypt unprotectedHeader rejects non objects if provided', t => {
  ;[[], false, true, null, Infinity, 0, Buffer.from('foo')].forEach((val) => {
    t.throws(() => {
      new JWE.Encrypt('foo', undefined, undefined, val) // eslint-disable-line no-new
    }, { instanceOf: TypeError, message: 'unprotectedHeader argument must be a plain object when provided' })
  })
})

test('JWE encrypt per-recipient header rejects non objects if provided', t => {
  const k = generateSync('oct')
  const enc = new JWE.Encrypt('foo')
  ;[[], false, true, null, Infinity, 0, Buffer.from('foo')].forEach((val) => {
    t.throws(() => {
      enc.recipient(k, val)
    }, { instanceOf: TypeError, message: 'header argument must be a plain object when provided' })
  })
})

test('JWE encrypt aad rejects non buffers and non strings', t => {
  ;[[], false, true, null, Infinity, 0].forEach((val) => {
    t.throws(() => {
      new JWE.Encrypt('foo', undefined, val) // eslint-disable-line no-new
    }, { instanceOf: TypeError, message: 'aad argument must be a Buffer or a string when provided' })
  })
})

test('JWE must have recipients', t => {
  const encrypt = new JWE.Encrypt('foo')
  t.throws(() => {
    encrypt.encrypt('compact')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'missing recipients' })
})

test('JWE valid serialization must be provided', t => {
  ;[[], false, true, null, Infinity, 0, 'foo', ''].forEach((val) => {
    const encrypt = new JWE.Encrypt('foo')
    t.throws(() => {
      encrypt.encrypt(val)
    }, { instanceOf: TypeError, message: 'serialization must be one of "compact", "flattened", "general"' })
  })
})

test('JWE compact does not support multiple recipients', t => {
  const k = generateSync('oct')
  const k2 = generateSync('EC')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k)
  encrypt.recipient(k2)
  t.throws(() => {
    encrypt.encrypt('compact')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Compact Serialization doesn\'t support multiple recipients, JWE unprotected headers or AAD' })
})

test('JWE compact does not support unprotected header', t => {
  const k = generateSync('oct')
  t.throws(() => {
    JWE.encrypt('foo', k, undefined, undefined, { foo: 1 })
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Compact Serialization doesn\'t support multiple recipients, JWE unprotected headers or AAD' })
})

test('JWE compact does not support aad', t => {
  const k = generateSync('oct')
  t.throws(() => {
    JWE.encrypt('foo', k, undefined, 'aad')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Compact Serialization doesn\'t support multiple recipients, JWE unprotected headers or AAD' })
})

test('JWE flattened does not support multiple recipients', t => {
  const k = generateSync('oct')
  const k2 = generateSync('EC')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k)
  encrypt.recipient(k2)
  t.throws(() => {
    encrypt.encrypt('flattened')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'Flattened JWE JSON Serialization doesn\'t support multiple recipients' })
})

test('JWE must only have one Content Encryption algorithm (encrypt)', t => {
  const k = generateSync('oct')
  const k2 = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k, { enc: 'A128CBC-HS256' })
  encrypt.recipient(k2, { enc: 'A128GCM' })
  t.throws(() => {
    encrypt.encrypt('general')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'there must only be one Content Encryption algorithm' })
})

test('JWE must only have one Content Encryption algorithm (decrypt)', t => {
  const k = generateSync('oct')
  const k2 = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k, { enc: 'A128GCM' })
  encrypt.recipient(k2, { enc: 'A128GCM' })
  const jwe = encrypt.encrypt('general')
  t.throws(() => {
    jwe.recipients[0].header.enc = 'A128CBC-HS256'
    JWE.decrypt(jwe, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'there must only be one Content Encryption algorithm' })
})

test('JWE must have a Content Encryption algorithm (decrypt)', t => {
  const k = generateSync('oct')
  const k2 = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k, { enc: 'A128GCM' })
  encrypt.recipient(k2, { enc: 'A128GCM' })
  const jwe = encrypt.encrypt('general')
  t.throws(() => {
    delete jwe.recipients[0].header.enc
    delete jwe.recipients[1].header.enc
    JWE.decrypt(jwe, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'missing Content Encryption algorithm' })
})

test('JWE oct dir is only usable with a single recipient', t => {
  const k = generateSync('oct', undefined, { alg: 'A128CBC-HS256', use: 'enc' })
  const k2 = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k, { alg: 'dir' })
  encrypt.recipient(k2)
  t.throws(() => {
    encrypt.encrypt('general')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'dir and ECDH-ES alg may only be used with a single recipient' })
})

test('JWE EC ECDH-ES is only usable with a single recipient', t => {
  const k = generateSync('EC', undefined, { alg: 'ECDH-ES', use: 'enc' })
  const k2 = generateSync('RSA')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k, { alg: 'ECDH-ES' })
  encrypt.recipient(k2)
  t.throws(() => {
    encrypt.encrypt('general')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'dir and ECDH-ES alg may only be used with a single recipient' })
})

test('JWE prot, unprot and per-recipient headers must be disjoint', t => {
  const k = generateSync('oct')
  t.throws(() => {
    const encrypt = new JWE.Encrypt('foo', { foo: 1 }, undefined, { foo: 2 })
    encrypt.recipient(k)
    encrypt.encrypt('flattened')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Shared Protected, JWE Shared Unprotected and JWE Per-Recipient Header Parameter names must be disjoint' })
  t.throws(() => {
    const encrypt = new JWE.Encrypt('foo', { foo: 1 })
    encrypt.recipient(k, { foo: 2 })
    encrypt.encrypt('flattened')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Shared Protected, JWE Shared Unprotected and JWE Per-Recipient Header Parameter names must be disjoint' })
  t.throws(() => {
    const encrypt = new JWE.Encrypt('foo', undefined, undefined, { foo: 1 })
    encrypt.recipient(k, { foo: 2 })
    encrypt.encrypt('flattened')
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: 'JWE Shared Protected, JWE Shared Unprotected and JWE Per-Recipient Header Parameter names must be disjoint' })
})

test('JWE decrypt keyManagementAlgorithms whitelist', t => {
  const k = generateSync('oct', 128)
  const jwe = JWE.encrypt('foo', k, { alg: 'A128GCMKW' })
  JWE.decrypt(jwe, k, { keyManagementAlgorithms: ['A128GCMKW', 'A192GCMKW'] })

  t.throws(() => {
    JWE.decrypt(jwe, k, { keyManagementAlgorithms: ['A192GCMKW'] })
  }, { instanceOf: errors.JOSEAlgNotWhitelisted, code: 'ERR_JOSE_ALG_NOT_WHITELISTED', message: 'key management algorithm not whitelisted' })
})

test('JWE decrypt keyManagementAlgorithms whitelist with a keystore', t => {
  const k = generateSync('oct')
  const k2 = generateSync('oct', 128)
  const ks = new JWKS.KeyStore(k, k2)

  const jwe = JWE.encrypt('foo', k2, { alg: 'A128GCMKW' })
  JWE.decrypt(jwe, ks, { keyManagementAlgorithms: ['A128GCMKW', 'A192GCMKW'] })

  t.throws(() => {
    JWE.decrypt(jwe, ks, { keyManagementAlgorithms: ['A192GCMKW'] })
  }, { instanceOf: errors.JOSEAlgNotWhitelisted, code: 'ERR_JOSE_ALG_NOT_WHITELISTED' })
})

test('JWE decrypt contentEncryptionAlgorithms whitelist', t => {
  const k = generateSync('oct')
  const jwe = JWE.encrypt('foo', k, { alg: 'dir' })
  JWE.decrypt(jwe, k, { contentEncryptionAlgorithms: ['A128CBC-HS256'] })

  t.throws(() => {
    JWE.decrypt(jwe, k, { contentEncryptionAlgorithms: ['PBES2-HS384+A192KW'] })
  }, { instanceOf: errors.JOSEAlgNotWhitelisted, code: 'ERR_JOSE_ALG_NOT_WHITELISTED' })
})

test('JWE decrypt keyManagementAlgorithms whitelist (multi-recipient)', t => {
  const k = generateSync('oct')
  const k2 = generateSync('RSA')

  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k)
  encrypt.recipient(k2)
  const jwe = encrypt.encrypt('general')

  JWE.decrypt(jwe, k, { keyManagementAlgorithms: ['electron' in process.versions ? 'A256GCMKW' : 'A256KW'] })
  JWE.decrypt(jwe, k2, { keyManagementAlgorithms: ['RSA-OAEP'] })
  let err

  err = t.throws(() => {
    JWE.decrypt(jwe, k, { keyManagementAlgorithms: ['RSA-OAEP'] })
  }, { instanceOf: errors.JOSEMultiError, code: 'ERR_JOSE_MULTIPLE_ERRORS' })
  ;[...err].forEach((e, i) => {
    if (i === 0) {
      t.is(e.constructor, errors.JOSEAlgNotWhitelisted)
    } else {
      t.is(e.constructor, errors.JWKKeySupport)
    }
  })

  err = t.throws(() => {
    JWE.decrypt(jwe, k2, { keyManagementAlgorithms: ['electron' in process.versions ? 'A256GCMKW' : 'A256KW'] })
  }, { instanceOf: errors.JOSEMultiError, code: 'ERR_JOSE_MULTIPLE_ERRORS' })
  ;[...err].forEach((e, i) => {
    if (i === 0) {
      t.is(e.constructor, errors.JWKKeySupport)
    } else {
      t.is(e.constructor, errors.JOSEAlgNotWhitelisted)
    }
  })
})

test('JWE "zip" must be integrity protected', t => {
  const k = generateSync('oct')

  t.throws(() => {
    JWE.encrypt.flattened('foo', k, undefined, undefined, { zip: 'DEF' })
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: '"zip" Header Parameter MUST be integrity protected' })
})

test('JWE "zip" only DEF is supported', t => {
  const k = generateSync('oct')

  t.throws(() => {
    JWE.encrypt.flattened('foo', k, { zip: 'FOO' })
  }, { instanceOf: errors.JOSENotSupported, code: 'ERR_JOSE_NOT_SUPPORTED', message: 'only "DEF" compression algorithm is supported' })
})

test('JWE "zip" must be integrity protected (decrypt)', t => {
  const k = generateSync('oct')
  const jwe = JWE.encrypt.flattened('foo', k, { zip: 'DEF' })
  const prot = base64url.JSON.decode(jwe.protected)
  delete prot.zip
  jwe.protected = base64url.JSON.encode(prot)
  jwe.unprotected = { zip: 'DEF' }

  t.throws(() => {
    JWE.decrypt(jwe, k)
  }, { instanceOf: errors.JWEInvalid, code: 'ERR_JWE_INVALID', message: '"zip" Header Parameter MUST be integrity protected' })
})

test('JWE "zip" only DEF is supported (decrypt)', t => {
  const k = generateSync('oct')
  const jwe = JWE.encrypt.flattened('foo', k, { zip: 'DEF' })
  const prot = base64url.JSON.decode(jwe.protected)
  prot.zip = 'foo'
  jwe.protected = base64url.JSON.encode(prot)

  t.throws(() => {
    JWE.decrypt(jwe, k)
  }, { instanceOf: errors.JOSENotSupported, code: 'ERR_JOSE_NOT_SUPPORTED', message: 'only "DEF" compression algorithm is supported' })
})

test('JWE keystore match multi but fails with decryption error', t => {
  const k = generateSync('oct')
  const ks = new JWKS.KeyStore(generateSync('oct'), generateSync('oct'))
  const jwe = JWE.encrypt('foo', k)

  t.throws(() => {
    JWE.decrypt(jwe, ks)
  }, { instanceOf: errors.JWEDecryptionFailed, code: 'ERR_JWE_DECRYPTION_FAILED' })
})

test('JWE general fails with decryption error', t => {
  const k = generateSync('oct')
  const k2 = generateSync('oct')
  const k3 = generateSync('oct')
  const encrypt = new JWE.Encrypt('foo')
  encrypt.recipient(k)
  encrypt.recipient(k2)
  const jwe = encrypt.encrypt('general')

  t.throws(() => {
    JWE.decrypt(jwe, k3)
  }, { instanceOf: errors.JWEDecryptionFailed, code: 'ERR_JWE_DECRYPTION_FAILED' })
})

test('"sig" key is not usable for signing', t => {
  const k = generateSync('oct', 256, { use: 'sig' })
  t.throws(() => {
    JWE.encrypt('foo', k)
  }, { instanceOf: TypeError, message: 'a key with "use":"sig" is not usable for encryption' })
})

test('"enc" value must be supported error', t => {
  const k = generateSync('oct', 256)
  t.throws(() => {
    JWE.encrypt('foo', k, { alg: 'dir', enc: 'foo' })
  }, { instanceOf: errors.JOSENotSupported, message: 'unsupported encrypt alg: foo' })
})

test('"enc" value must be supported error (when no alg was specified)', t => {
  const k = generateSync('oct', 256)
  t.throws(() => {
    JWE.encrypt('foo', k, { enc: 'foo' })
  }, { instanceOf: errors.JOSENotSupported, message: 'unsupported encrypt alg: foo' })
})

if (!('electron' in process.versions)) {
  test('decrypt PBES2 p2c limit', t => {
    const k = generateSync('oct', 256)
    const jwe = JWE.encrypt('foo', k, { alg: 'PBES2-HS256+A128KW' })
    t.throws(() => {
      JWE.decrypt(jwe, k, { maxPBES2Count: 1000 })
    }, { instanceOf: errors.JWEInvalid, message: 'JOSE Header "p2c" (PBES2 Count) out is of acceptable bounds' })
  })
}

test('Compressed JWE output length limit', t => {
  const k = generateSync('oct', 256)
  {
    const jwe = JWE.encrypt(crypto.randomBytes(250000), k, { alg: 'dir', enc: 'A128CBC-HS256', zip: 'DEF' })
    t.notThrows(() => {
      JWE.decrypt(jwe, k)
    })
  }
  {
    const jwe = JWE.encrypt(crypto.randomBytes(250000 + 1), k, { alg: 'dir', enc: 'A128CBC-HS256', zip: 'DEF' })
    t.throws(() => {
      JWE.decrypt(jwe, k)
    })
  }
  {
    const jwe = JWE.encrypt(crypto.randomBytes(1001), k, { alg: 'dir', enc: 'A128CBC-HS256', zip: 'DEF' })
    t.throws(() => {
      JWE.decrypt(jwe, k, { inflateRawSyncLimit: 1000 })
    })
  }
})
```

## `types/index.d.ts` (changed lines (405,))

```javascript
/// <reference types="node" />
// TypeScript Version: 3.6

import { KeyObject, PrivateKeyInput, PublicKeyInput } from 'crypto';

export type use = 'sig' | 'enc';
export type keyOperation = 'sign' | 'verify' | 'encrypt' | 'decrypt' | 'wrapKey' | 'unwrapKey' | 'deriveKey';
export interface BasicParameters {
  alg?: string;
  use?: use;
  kid?: string;
  key_ops?: keyOperation[];
}
export interface KeyParameters extends BasicParameters {
  x5c?: string[];
  x5t?: string;
  'x5t#S256'?: string;
}
export type ECCurve = 'P-256' | 'secp256k1' | 'P-384' | 'P-521';
export type OKPCurve = 'Ed25519' | 'Ed448' | 'X25519' | 'X448';
export type Curves = OKPCurve | ECCurve;
export type keyType = 'RSA' | 'EC' | 'OKP' | 'oct';
export type asymmetricKeyObjectTypes = 'private' | 'public';
export type keyObjectTypes = asymmetricKeyObjectTypes | 'secret';
export type KeyInput = PrivateKeyInput | PublicKeyInput | string | Buffer;
export type ProduceKeyInput = JWK.Key | KeyObject | KeyInput | JWKOctKey | JWKRSAKey | JWKECKey | JWKOKPKey;
export type ConsumeKeyInput = ProduceKeyInput | JWKS.KeyStore;
export type NoneKey = JWK.NoneKey;
export type EmbeddedJWK = JWK.EmbeddedJWK;
export type EmbeddedX5C = JWK.EmbeddedX5C;
export type EmbeddedVerifyKeys = EmbeddedJWK | EmbeddedX5C;
export type ProduceKeyInputWithNone = ProduceKeyInput | NoneKey;
export type ConsumeKeyInputWithNone = ConsumeKeyInput | NoneKey;

export interface JWKOctKey extends BasicParameters { // no x5c
  kty: 'oct';
  k?: string;
}

export interface JWKECKey extends KeyParameters {
  kty: 'EC';
  crv: ECCurve;
  x: string;
  y: string;
  d?: string;
}

export interface JWKOKPKey extends KeyParameters {
  kty: 'OKP';
  crv: OKPCurve;
  x: string;
  d?: string;
}

export interface JWKRSAKey extends KeyParameters {
  kty: 'RSA';
  e: string;
  n: string;
  d?: string;
  p?: string;
  q?: string;
  dp?: string;
  dq?: string;
  qi?: string;
}

export type JSONWebKey = JWKRSAKey | JWKOKPKey | JWKECKey | JWKOctKey;

export interface JSONWebKeySet {
  keys: JSONWebKey[];
}

export interface ImportOptions {
  calculateMissingRSAPrimes?: boolean;
}

export namespace JWK {
  interface pemEncodingOptions {
    type?: string;
    cipher?: string;
    passphrase?: string;
  }

  interface Key {
    readonly private: boolean;
    readonly public: boolean;
    readonly secret: boolean;
    readonly type: keyObjectTypes;

    readonly kty: keyType;
    readonly alg?: string;
    readonly use?: use;
    readonly key_ops?: ReadonlyArray<keyOperation>;
    readonly kid: string;
    readonly thumbprint: string;
    readonly x5c?: ReadonlyArray<string>;
    readonly x5t?: string;
    readonly 'x5t#S256'?: string;
    readonly keyObject: KeyObject;

    readonly crv?: ECCurve | OKPCurve;
    readonly d?: string;
    readonly dp?: string;
    readonly dq?: string;
    readonly e?: string;
    readonly k?: string;
    readonly n?: string;
    readonly p?: string;
    readonly q?: string;
    readonly qi?: string;
    readonly x?: string;
    readonly y?: string;

    toPEM(private?: boolean, encoding?: pemEncodingOptions): string;

    algorithms(operation?: keyOperation): Set<string>;
  }

  interface RSAKey extends Key {
    readonly secret: false;
    readonly type: asymmetricKeyObjectTypes;

    readonly kty: 'RSA';

    readonly e: string;
    readonly n: string;
    readonly d?: string;
    readonly p?: string;
    readonly q?: string;
    readonly dp?: string;
    readonly dq?: string;
    readonly qi?: string;

    readonly crv: undefined;
    readonly k: undefined;
    readonly x: undefined;
    readonly y: undefined;

    toJWK(private?: boolean): JWKRSAKey;
  }

  interface ECKey extends Key {
    readonly secret: false;
    readonly type: asymmetricKeyObjectTypes;

    readonly kty: 'EC';

    readonly crv: ECCurve;
    readonly x: string;
    readonly y: string;
    readonly d?: string;

    readonly dp: undefined;
    readonly dq: undefined;
    readonly e: undefined;
    readonly k: undefined;
    readonly n: undefined;
    readonly p: undefined;
    readonly q: undefined;
    readonly qi: undefined;

    toJWK(private?: boolean): JWKECKey;
  }

  interface OKPKey extends Key {
    readonly secret: false;
    readonly type: asymmetricKeyObjectTypes;

    readonly kty: 'OKP';

    readonly crv: OKPCurve;
    readonly x: string;
    readonly d?: string;

    readonly dp: undefined;
    readonly dq: undefined;
    readonly e: undefined;
    readonly k: undefined;
    readonly n: undefined;
    readonly p: undefined;
    readonly q: undefined;
    readonly qi: undefined;
    readonly y: undefined;

    toJWK(private?: boolean): JWKOKPKey;
  }

  interface OctKey extends Key {
    readonly private: false;
    readonly public: false;
    readonly secret: true;
    readonly type: 'secret';

    readonly kty: 'oct';

    readonly k?: string;

    readonly crv: undefined;
    readonly d: undefined;
    readonly dp: undefined;
    readonly dq: undefined;
    readonly e: undefined;
    readonly n: undefined;
    readonly p: undefined;
    readonly q: undefined;
    readonly qi: undefined;
    readonly x: undefined;
    readonly y: undefined;

    toJWK(private?: boolean): JWKOctKey;
  }

  interface NoneKey {
    readonly type: 'unsecured';
    readonly alg: 'none';
    algorithms(operation?: keyOperation): Set<string>;
  }

  const None: NoneKey;

  interface EmbeddedJWK {
    readonly type: 'embedded';
    algorithms(operation?: keyOperation): Set<string>;
  }

  const EmbeddedJWK: EmbeddedJWK;

  interface EmbeddedX5C {
    readonly type: 'embedded';
    algorithms(operation?: keyOperation): Set<string>;
  }

  const EmbeddedX5C: EmbeddedX5C;

  function isKey(object: any): boolean;

  function asKey(key: KeyObject | KeyInput, parameters?: KeyParameters): RSAKey | ECKey | OKPKey | OctKey;
  function asKey(jwk: JWKOctKey): OctKey;
  function asKey(jwk: JWKRSAKey, options?: ImportOptions): RSAKey;
  function asKey(jwk: JWKECKey): ECKey;
  function asKey(jwk: JWKOKPKey): OKPKey;

  /*
   * @deprecated in favor of asKey
   */
  function importKey(key: KeyObject | KeyInput, parameters?: KeyParameters): RSAKey | ECKey | OKPKey | OctKey;
  function importKey(jwk: JWKOctKey): OctKey;
  function importKey(jwk: JWKRSAKey): RSAKey;
  function importKey(jwk: JWKECKey): ECKey;
  function importKey(jwk: JWKOKPKey): OKPKey;

  function generate(kty: keyType, crvOrSize?: Curves | number, parameters?: BasicParameters, private?: boolean): Promise<JWK.Key>;
  function generate(kty: 'EC', crv?: ECCurve, parameters?: BasicParameters, private?: boolean): Promise<ECKey>;
  function generate(kty: 'OKP', crv?: OKPCurve, parameters?: BasicParameters, private?: boolean): Promise<OKPKey>;
  function generate(kty: 'RSA', bitlength?: number, parameters?: BasicParameters, private?: boolean): Promise<RSAKey>;
  function generate(kty: 'oct', bitlength?: number, parameters?: BasicParameters): Promise<OctKey>;

  function generateSync(kty: keyType, crvOrSize?: Curves | number, parameters?: BasicParameters, private?: boolean): JWK.Key;
  function generateSync(kty: 'EC', crv?: ECCurve, parameters?: BasicParameters, private?: boolean): ECKey;
  function generateSync(kty: 'OKP', crv?: OKPCurve, parameters?: BasicParameters, private?: boolean): OKPKey;
  function generateSync(kty: 'RSA', bitlength?: number, parameters?: BasicParameters, private?: boolean): RSAKey;
  function generateSync(kty: 'oct', bitlength?: number, parameters?: BasicParameters): OctKey;
}

export namespace JWKS {
  interface KeyQuery extends BasicParameters {
    kty?: keyType;
    x5t?: string;
    'x5t#S256'?: string;
    crv?: string;
    thumbprint?: string;
  }

  class KeyStore {
    constructor(keys?: JWK.Key[]);

    readonly size: number;

    add(key: JWK.Key): void;
    remove(key: JWK.Key): void;
    all(parameters?: KeyQuery): JWK.Key[];
    get(parameters?: KeyQuery): JWK.Key;

    toJWKS(private?: boolean): JSONWebKeySet;

    generate(kty: keyType, crvOrSize?: Curves | number, parameters?: BasicParameters, private?: boolean): Promise<void>;
    generate(kty: 'EC', crv?: ECCurve, parameters?: BasicParameters, private?: boolean): Promise<void>;
    generate(kty: 'OKP', crv?: OKPCurve, parameters?: BasicParameters, private?: boolean): Promise<void>;
    generate(kty: 'RSA', bitlength?: number, parameters?: BasicParameters, private?: boolean): Promise<void>;
    generate(kty: 'oct', bitlength?: number, parameters?: BasicParameters): Promise<void>;

    generateSync(kty: keyType, crvOrSize?: Curves | number, parameters?: BasicParameters, private?: boolean): void;
    generateSync(kty: 'EC', crv?: ECCurve, parameters?: BasicParameters, private?: boolean): void;
    generateSync(kty: 'OKP', crv?: OKPCurve, parameters?: BasicParameters, private?: boolean): void;
    generateSync(kty: 'RSA', bitlength?: number, parameters?: BasicParameters, private?: boolean): void;
    generateSync(kty: 'oct', bitlength?: number, parameters?: BasicParameters): void;

    /*
     * @deprecated in favor of JWKS.asKeyStore
     */
    static fromJWKS(jwks: JSONWebKeySet): KeyStore;
  }

  interface JWKSImportOptions extends ImportOptions {
    ignoreErrors?: boolean;
  }

  function asKeyStore(jwks: JSONWebKeySet, options?: JWKSImportOptions): KeyStore;
}

export namespace JWS {
  interface JWSJSON {
    payload: string | Buffer;
  }

  interface JWSRecipient {
    signature: string;
    protected?: string;
    header?: object;
  }

  interface FlattenedJWS extends JWSRecipient, JWSJSON {}

  interface GeneralJWS extends JWSJSON {
    signatures: JWSRecipient[];
  }

  class Sign {
    constructor(payload: string | Buffer | object);

    recipient(key: ProduceKeyInputWithNone, protected?: object, header?: object): void;

    sign(serialization: 'compact'): string;
    sign(serialization: 'flattened'): FlattenedJWS;
    sign(serialization: 'general'): GeneralJWS;
  }

  function sign(payload: string | Buffer | object, key: ProduceKeyInputWithNone, protected?: object): string;
  namespace sign {
    function flattened(payload: string | Buffer | object, key: ProduceKeyInputWithNone, protected?: object, header?: object): FlattenedJWS;
    function general(payload: string | Buffer | object, key: ProduceKeyInputWithNone, protected?: object, header?: object): GeneralJWS;
  }

  interface VerifyOptions {
    complete?: boolean;
    crit?: string[];
    algorithms?: string[];
  }

  interface completeVerification<T = JWK.Key> {
    payload: Buffer;
    key: T;
    protected?: object;
    header?: object;
  }

  function verify(jws: string | FlattenedJWS | GeneralJWS, key: ConsumeKeyInput | EmbeddedVerifyKeys, options: VerifyOptions & { complete: true }): completeVerification<JWK.Key>;
  function verify(jws: string | FlattenedJWS | GeneralJWS, key: NoneKey, options: VerifyOptions & { complete: true }): completeVerification<NoneKey>;
  function verify(jws: string | FlattenedJWS | GeneralJWS, key: ConsumeKeyInputWithNone | EmbeddedVerifyKeys, options?: VerifyOptions): Buffer;
}

export namespace JWE {
  interface JWEJSON {
    protected?: string;
    unprotected?: object;
    ciphertext: string;
    tag: string;
    iv: string;
    aad?: string;
  }

  interface JWERecipient {
    header?: object;
    encrypted_key: string;
  }

  interface FlattenedJWE extends JWERecipient, JWEJSON {}

  interface GeneralJWE extends JWEJSON {
    recipients: JWERecipient[];
  }

  class Encrypt {
    constructor(cleartext: string | Buffer, protected?: object, aad?: string, unprotected?: object);

    recipient(key: ProduceKeyInput, header?: object): void;

    encrypt(serialization: 'compact'): string;
    encrypt(serialization: 'flattened'): FlattenedJWE;
    encrypt(serialization: 'general'): GeneralJWE;
  }

  function encrypt(payload: string | Buffer, key: ProduceKeyInput, protected?: object): string;
  namespace encrypt {
    function flattened(payload: string | Buffer, key: ProduceKeyInput, protected?: object, aad?: string, header?: object): FlattenedJWE;
    function general(payload: string | Buffer, key: ProduceKeyInput, protected?: object, aad?: string, header?: object): GeneralJWE;
  }

  interface DecryptOptions {
    complete?: boolean;
    crit?: string[];
    contentEncryptionAlgorithms?: string[];
    keyManagementAlgorithms?: string[];
    maxPBES2Count?: number;
    inflateRawSyncLimit?: number;
  }

  interface completeDecrypt {
    cleartext: Buffer;
    key: JWK.Key;
    cek: JWK.OctKey;
    aad?: string;
    header?: object;
    unprotected?: object;
    protected?: object;
  }

  function decrypt(jwe: string | FlattenedJWE | GeneralJWE, key: ConsumeKeyInput, options: DecryptOptions & { complete: true }): completeDecrypt;
  function decrypt(jwe: string | FlattenedJWE | GeneralJWE, key: ConsumeKeyInput, options?: DecryptOptions): Buffer;
}

export namespace JWT {
  interface completeResult<T = JWK.Key> {
    payload: object;
    header: object;
    signature: string;
    key: T;
  }

  interface DecodeOptions {
    complete?: boolean;
  }

  /**
   * Decodes the JWT **without verifying the token**. For JWT verification/validation use
   * `jose.JWT.verify`.
   */
  function decode(jwt: string, options: DecodeOptions & { complete: true }): completeResult<undefined>;
  function decode(jwt: string, options?: DecodeOptions): object;

  interface VerifyOptions {
    complete?: boolean;
    ignoreExp?: boolean;
    ignoreNbf?: boolean;
    ignoreIat?: boolean;
    maxTokenAge?: string;
    subject?: string;
    issuer?: string | string[];
    jti?: string;
    clockTolerance?: string;
    audience?: string | string[];
    algorithms?: string[];
    typ?: string;
    now?: Date;
    crit?: string[];
  }

  function verify(jwt: string, key: NoneKey, options: VerifyOptions & { complete: true }): completeResult<NoneKey>;
  function verify(jwt: string, key: ConsumeKeyInput | EmbeddedVerifyKeys, options: VerifyOptions & { complete: true }): completeResult;
  function verify(jwt: string, key: ConsumeKeyInputWithNone | EmbeddedVerifyKeys, options?: VerifyOptions): object;

  interface SignOptions {
    iat?: boolean;
    kid?: boolean;
    subject?: string;
    issuer?: string;
    audience?: string | string[];
    header?: object;
    algorithm?: string;
    expiresIn?: string;
    notBefore?: string;
    jti?: string;
    now?: Date;
  }

  function sign(payload: object, key: ProduceKeyInputWithNone, options?: SignOptions): string;

  interface ProfiledVerifyOptions {
    issuer: string | string[];
    audience: string | string[];
  }

  interface IdTokenVerifyOptions extends ProfiledVerifyOptions {
    nonce?: string;
    maxAuthAge?: string;
  }

  interface AccessTokenVerifyOptions extends ProfiledVerifyOptions {
    maxAuthAge?: string;
  }

  interface LogoutTokenVerifyOptions extends ProfiledVerifyOptions {}

  namespace IdToken {
    function verify(jwt: string, key: ConsumeKeyInput | EmbeddedVerifyKeys, options: VerifyOptions & { complete: true } & IdTokenVerifyOptions): completeResult;
    function verify(jwt: string, key: NoneKey, options: VerifyOptions & { complete: true } & IdTokenVerifyOptions): completeResult<NoneKey>;
    function verify(jwt: string, key: ConsumeKeyInputWithNone | EmbeddedVerifyKeys, options: VerifyOptions & IdTokenVerifyOptions): object;
  }

  namespace LogoutToken {
    function verify(jwt: string, key: ConsumeKeyInput | EmbeddedVerifyKeys, options: VerifyOptions & { complete: true } & LogoutTokenVerifyOptions): completeResult;
    function verify(jwt: string, key: NoneKey, options: VerifyOptions & { complete: true } & LogoutTokenVerifyOptions): completeResult<NoneKey>;
    function verify(jwt: string, key: ConsumeKeyInputWithNone | EmbeddedVerifyKeys, options: VerifyOptions & LogoutTokenVerifyOptions): object;
  }

  namespace AccessToken {
    function verify(jwt: string, key: ConsumeKeyInput | EmbeddedVerifyKeys, options: VerifyOptions & { complete: true } & AccessTokenVerifyOptions): completeResult;
    function verify(jwt: string, key: NoneKey, options: VerifyOptions & { complete: true } & AccessTokenVerifyOptions): completeResult<NoneKey>;
    function verify(jwt: string, key: ConsumeKeyInputWithNone | EmbeddedVerifyKeys, options: VerifyOptions & AccessTokenVerifyOptions): object;
  }
}

export namespace errors {
  class JOSEError<T = string> extends Error {
    code: T;
  }

  class JOSEInvalidEncoding extends JOSEError<'ERR_JOSE_INVALID_ENCODING'> {}
  class JOSEMultiError extends JOSEError<'ERR_JOSE_MULTIPLE_ERRORS'> {}

  class JOSEAlgNotWhitelisted extends JOSEError<'ERR_JOSE_ALG_NOT_WHITELISTED'> {}
  class JOSECritNotUnderstood extends JOSEError<'ERR_JOSE_CRIT_NOT_UNDERSTOOD'> {}
  class JOSENotSupported extends JOSEError<'ERR_JOSE_NOT_SUPPORTED'> {}

  class JWEDecryptionFailed extends JOSEError<'ERR_JWE_DECRYPTION_FAILED'> {}
  class JWEInvalid extends JOSEError<'ERR_JWE_INVALID'> {}

  class JWKImportFailed extends JOSEError<'ERR_JWK_IMPORT_FAILED'> {}
  class JWKInvalid extends JOSEError<'ERR_JWK_INVALID'> {}
  class JWKKeySupport extends JOSEError<'ERR_JWK_KEY_SUPPORT'> {}

  class JWKSNoMatchingKey extends JOSEError<'ERR_JWKS_NO_MATCHING_KEY'> {}

  class JWSInvalid extends JOSEError<'ERR_JWS_INVALID'> {}
  class JWSVerificationFailed extends JOSEError<'ERR_JWS_VERIFICATION_FAILED'> {}

  class JWTClaimInvalid<T = 'ERR_JWT_CLAIM_INVALID'> extends JOSEError<T> {
    constructor(message?: string, claim?: string, reason?: string);

    claim: string;
    reason: 'prohibited' | 'missing' | 'invalid' | 'check_failed' | 'unspecified';
  }
  class JWTExpired extends JWTClaimInvalid<'ERR_JWT_EXPIRED'> {}
  class JWTMalformed extends JOSEError<'ERR_JWT_MALFORMED'> {}
}
```
