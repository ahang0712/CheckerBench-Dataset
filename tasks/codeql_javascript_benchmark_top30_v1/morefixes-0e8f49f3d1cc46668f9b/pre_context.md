# Patch-overlapping JavaScript context before the fix

## `src/app/lib/enc.js` (changed lines (3, 5, 6, 22, 23, 24, 26, 27, 28, 31, 32, 37, 38, 39, 41, 42, 43, 44, 45, 53, 54, 55, 57, 58, 59, 62, 63, 68, 69, 70, 72, 73, 74, 75, 76))

```javascript
/**
 * data encrypt/decrypt
 */

const algorithmDefault = 'aes-192-cbc'

function scryptAsync (...args) {
  const crypto = require('crypto')
  return new Promise((resolve, reject) =>
    crypto.scrypt(...args, (err, result) => {
      if (err) {
        reject(err)
      }
      resolve(result)
    })
  )
}

exports.encrypt = function (
  str = '',
  password,
  algorithm = algorithmDefault,
  iv = Buffer.alloc(16, 0)
) {
  const crypto = require('crypto')
  const key = crypto.scryptSync(password, 'salt', 24)
  // Use `crypto.randomBytes` to generate a random iv instead of the static iv
  const cipher = crypto.createCipheriv(algorithm, key, iv)
  let encrypted = cipher.update(str, 'utf8', 'hex')
  encrypted += cipher.final('hex')
  return encrypted
}

exports.decrypt = function (
  encrypted = '',
  password,
  algorithm = algorithmDefault,
  iv = Buffer.alloc(16, 0)
) {
  const crypto = require('crypto')
  // Use the async `crypto.scrypt()` instead.
  const key = crypto.scryptSync(password, 'salt', 24)
  const decipher = crypto.createDecipheriv(algorithm, key, iv)
  // Encrypted using same algorithm, key and iv.
  let decrypted = decipher.update(encrypted, 'hex', 'utf8')
  decrypted += decipher.final('utf8')
  return decrypted
}

exports.encryptAsync = async function (
  str = '',
  password,
  algorithm = algorithmDefault,
  iv = Buffer.alloc(16, 0)
) {
  const crypto = require('crypto')
  const key = await scryptAsync(password, 'salt', 24)
  // Use `crypto.randomBytes` to generate a random iv instead of the static iv
  const cipher = crypto.createCipheriv(algorithm, key, iv)
  let encrypted = cipher.update(str, 'utf8', 'hex')
  encrypted += cipher.final('hex')
  return encrypted
}

exports.decryptAsync = async function (
  encrypted = '',
  password,
  algorithm = algorithmDefault,
  iv = Buffer.alloc(16, 0)
) {
  const crypto = require('crypto')
  // Use the async `crypto.scrypt()` instead.
  const key = await scryptAsync(password, 'salt', 24)
  const decipher = crypto.createDecipheriv(algorithm, key, iv)
  // Encrypted using same algorithm, key and iv.
  let decrypted = decipher.update(encrypted, 'hex', 'utf8')
  decrypted += decipher.final('utf8')
  return decrypted
}
```
