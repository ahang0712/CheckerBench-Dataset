# Node.js / TypeScript — non-framework patterns

Read this for any JavaScript/TypeScript project (alongside `stack-nextjs.md` for Next.js apps, or standalone for Express / Fastify / Hono / Bun servers).

---

## Command injection

**Bad patterns:**
```
child_process.exec(`convert ${userInput} out.png`)
child_process.execSync(`git log --grep="${query}"`)
util.promisify(exec)(`sh -c "${userCmd}"`)
```

Any `exec` / `execSync` with user-controlled string concatenation = **CRITICAL**. No exceptions.

**Safe alternatives to verify:**
- `execFile(binary, [arg1, arg2])` — args are passed as an array, not shell-interpolated.
- `spawn(binary, [args])` — same.
- `execa(binary, args)` — same, with better DX.

If you see `exec` / `execSync` in the codebase, read every instance to confirm the input is either constant or fully validated to an allowlist.

---

## Path traversal

Any `fs.readFile(path.join(dir, userPath))` where `userPath` can contain `..` is potential LFI/arbitrary read.

**Grep:**
```
grep -rn "fs\\.\\(readFile\\|readFileSync\\|createReadStream\\)" --include="*.ts" src/
```

**Check:**
- Is the joined path then validated via `path.resolve` + `startsWith(baseDir)`?
- Does the code normalize first and reject `..`?
- For download endpoints using `[slug]` / `[id]`, is the slug validated against the DB rather than the filesystem?

**Finding severity:** CRITICAL if the path is a user route param that reaches `fs`. HIGH if it's a query param used in a less-exposed context.

---

## Insecure deserialization

- `JSON.parse(untrusted)` — safe (no code execution).
- `eval(untrusted)` — CRITICAL.
- `Function(untrusted)()` — CRITICAL.
- `vm.runInContext(untrusted)` / `vm.runInNewContext(untrusted)` — the `vm` module is **not a security boundary**; treat as eval.
- `require(userInput)` — RCE via arbitrary module load.
- `node-serialize` / legacy `serialize` libraries with `eval: true` — CRITICAL, the library warns about this itself.
- YAML: `yaml.load()` from `js-yaml` is unsafe with untrusted input (tags can execute code); use `yaml.safeLoad()` or modern `yaml.parse()`.

---

## JWT misuse

If the project uses `jsonwebtoken` / `jose` / custom JWT code:

- `jwt.verify(token, secret, { algorithms: ['HS256'] })` — explicit algorithms list. Missing = CRITICAL (algorithm confusion, including `alg: none`).
- The secret is from env, not hardcoded, not short.
- Tokens have `exp` / `iat` claims and the verify call actually checks them.
- For asymmetric: public key is used to verify, private key to sign. Swapped = CRITICAL.
- `jwt.decode()` (no verify) used anywhere auth decisions depend on the result = CRITICAL.

---

## Weak crypto

Grep:
```
crypto.createHash('md5' | 'sha1')     // weak; flag unless clearly non-security (e.g. cache key)
crypto.createCipher(...)              // deprecated, use createCipheriv
ecb                                   // mode never safe
```

For password hashing specifically: `bcrypt`, `argon2`, `scrypt` are fine. `pbkdf2` is fine with high iterations (≥100k for SHA-256). Anything else for passwords = CRITICAL.

---

## Timing-unsafe comparison for secrets

Comparing secrets with `!==` / `===` is vulnerable to timing attacks in theory. In practice, LAN timing attacks on comparison are rare but real.

**Flag** when:
- Comparing API keys, session tokens, webhook signatures with `===`. Use `crypto.timingSafeEqual(Buffer.from(a), Buffer.from(b))`.

Severity: MEDIUM usually (timing attacks are hard to exploit over the internet); HIGH if the endpoint is high-value (webhook signature, auth token).

---

## SSRF

Any `fetch(url)` / `axios.get(url)` / `http.request(url)` where `url` is built from user input:

- Is there an allowlist of hosts/protocols?
- Is the URL resolved (DNS) + checked to not be a private IP (10/8, 172.16/12, 192.168/16, 169.254.169.254 AWS IMDS, ::1, fc00::/7, etc.)?
- Can `file://` / `gopher://` / `ftp://` be passed? Schemes should be explicit.

**Classic exploit:** user supplies `http://169.254.169.254/latest/meta-data/iam/security-credentials/` — on AWS EC2, the server's IAM credentials leak.

**Severity:** CRITICAL if unfiltered user URL → server-side fetch. HIGH if host is user-controlled but scheme/port filtered.

---

## Prototype pollution

Bad patterns:
```
Object.assign(target, userInput)
_.merge(target, userInput)   // lodash merge is vulnerable in older versions
JSON.parse + recursive merge without prototype check
```

An attacker sending `{"__proto__": {"isAdmin": true}}` can modify `Object.prototype`, affecting every object in the process. Real attacks from 2020-2023 on express apps.

Severity: HIGH if confirmed path with user input. MEDIUM if the lib is potentially vulnerable but input is validated.

---

## Unsafe regex (ReDoS)

LLMs are not great at detecting this — flag as "worth a second look with a ReDoS tool" if you see regex with nested quantifiers (`(a+)+`, `(a|a)*`) applied to user input. Don't try to confirm exploitability — recommend the user run a tool like `safe-regex` or `rxxr2`.

---

## Useful `npm audit` / `bun outdated` integration

- Run the audit if available. Read its output. Do NOT blindly report everything — `npm audit` is famously noisy.
- devDependency CVEs are MEDIUM max unless the dev dep runs in a published artefact or CI with secrets.
- Transitive-only CVEs with no known exploitation path: INFORMATIONAL unless the affected function is likely called via the direct dep.

---

## Environment variable hygiene

- Validate env at boot (zod, envalid, @t3-oss/env) — unvalidated env access in handlers is MEDIUM (production runs can silently run with missing config).
- Grep for `process.env.SECRET_NAME` appearing in client-bundled code paths (in Next.js: anything imported from `app/`, `pages/`, `components/` that doesn't have `"use server"` at top). Server-only secrets leaking to client bundle = CRITICAL.
- `.env.local`, `.env.development`, `.env.production` — all should be in `.gitignore`. Only `.env.example` / `.env.template` should be tracked.

---

## Express-specific (when not using Next.js)

- `app.use(bodyParser.json({ limit: '...' }))` — if no limit, any huge JSON body DoSes the server. MEDIUM.
- `cors()` without config = open CORS. For APIs that deal in credentials, HIGH.
- Missing `helmet()` or equivalent security headers middleware. MEDIUM.
- Route ordering — auth middleware must come BEFORE the protected route, not after. Flag any route registered before its auth middleware.
- `req.params`, `req.query`, `req.body` trusted without schema validation. MEDIUM unless reaching DB / fs / shell.

---

## Hono / Fastify / Bun server

Same patterns as Express largely. Key differences:
- Fastify has a schema validation system baked in — flag routes without a `schema:` entry as MEDIUM.
- Hono's `c.req.header('...')` — headers are trivially spoofed; never use `x-forwarded-for` or `x-real-ip` for auth decisions.
- Bun's `Bun.serve` — similar to Node HTTP; audit as such.

---

## NoSQL injection (Mongo, Redis, Elasticsearch)

If the project uses `mongodb` / `mongoose`: user bodies typed as `any` / spread directly into queries let attackers inject operators (`$ne`, `$gt`, `$regex`). See `stack-api-surface.md §7` for the full pattern and fix. Quick grep:

```
findOne\(\s*req\.body
find\(\s*req\.body
```

Any typeless spread of a request body into a `find` / `update` is **HIGH → CRITICAL** if authentication is downstream.

---

## ZIP Slip / archive extraction

If the project extracts archives (`adm-zip`, `yauzl`, `unzipper`, `tar`, `node-stream-zip`, `7zip-bin`), every loop that uses the archive entry name in a filesystem path is ZIP Slip. See `stack-api-surface.md §11`. Grep:

```
adm-zip | unzipper | yauzl | node-stream-zip
path\.join\([^,]+,\s*entry\.
```

---

## SSRF — DNS rebinding & redirect bypass

Basic SSRF is above. Additional variants to check (detail in `stack-api-surface.md §12`):
- **DNS rebinding:** allowlist checks host; `fetch` separately resolves DNS. Defence: resolve once, dial by IP.
- **Redirect-based bypass:** `http://allowed.com/r` 302s to `http://169.254.169.254/`. Defence: `redirect: 'manual'` or re-check on each hop.
- **IMDSv1 on EC2:** any SSRF leaks AWS creds unless `http_tokens = required` is set in Terraform.

---

## Supply-chain install-time risk

- `npm install` runs every dep's `preinstall`/`postinstall`/`prepare` script. See `stack-cicd-supply-chain.md §2`.
- Verify `.npmrc` with `ignore-scripts=true` or explicit allowlist for projects with large dep trees.
- Lockfile committed + CI uses `npm ci` / `pnpm install --frozen-lockfile`.
- Typosquats / dependency confusion — eyeball `@yourco/*` scoped deps against a private registry.
