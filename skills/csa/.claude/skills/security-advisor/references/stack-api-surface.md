# API surface — cross-framework web-layer checks

Read this for ANY web app that exposes HTTP endpoints — Next.js, Express, Fastify, Hono, FastAPI, Django, Flask, Rails, Spring. These are the classes that don't slot neatly into a single framework file but bite every language.

Order below is roughly by how often the class shows up as a real finding in 2024-2025 app reviews.

---

## 1. CSRF — still shipped broken in 2025

CSRF is "someone else's website makes the victim's browser fire a state-changing request at your site". It matters when the server uses cookie-based auth (the browser attaches the cookie automatically).

**Does this app need CSRF defences?**
- Cookie-based sessions → YES.
- Bearer-token auth (Authorization header, read from `localStorage` / in-memory) → generally NO (browser doesn't attach it cross-site), but localStorage carries its own XSS problem (see `stack-react.md`).
- Mixed (some cookie, some bearer) → YES for the cookie paths.

**Verify in this order:**

1. **SameSite on the session cookie.** Look at where the cookie is set (`Set-Cookie`, framework session config, better-auth config, `express-session` options). `SameSite=Lax` or `Strict` stops most CSRF. `SameSite=None` (without a concrete cross-origin reason) re-opens it — **HIGH**.
2. **Origin / Referer check on state-changing requests.** For SameSite=Lax, the browser still sends the cookie on top-level GETs and same-site POSTs. Any POST/PUT/PATCH/DELETE endpoint should additionally verify `Origin` (and fall back to `Referer`) against the app's own origin. Missing check = MEDIUM on its own, HIGH if SameSite is not Strict.
3. **CSRF token** where supported by the framework (Django `{% csrf_token %}`, `flask-wtf`, `csurf` on Express, better-auth built-in). Disabled or not registered = HIGH.
4. **Server Actions (Next.js App Router)**: since Next 14, server actions enforce an Origin check against the current host by default via `allowedOrigins` / the `x-forwarded-host` check. Verify this hasn't been disabled via `experimental.serverActions.allowedOrigins = ['*']` — **HIGH**.
5. **GraphQL endpoints with cookie auth** must still CSRF-protect. A `POST /graphql` with `Content-Type: application/json` used to be CSRF-safe due to preflight; browsers now allow `application/json` simple requests in some configurations. Require a custom header (`x-apollo-operation-name`, `x-graphql`) and verify the server rejects requests without it.

Grep patterns:

```
SameSite                              # check value on session cookie
originCheck: false                    # better-auth
csrfProtection: false | csrf: false   # any framework
serverActions.*allowedOrigins.*\*     # Next.js config
```

**Exploit path (template):**
```
1. Victim is logged in to target.com (cookie present).
2. Victim visits evil.com which hosts: <form method=POST action="https://target.com/account/delete"><input name="confirm" value="yes"></form><script>document.forms[0].submit()</script>
3. Browser attaches session cookie. Target's server processes the request. Victim's account deleted.
```

---

## 2. CORS — most config is wrong the first time

CORS is *not* a security feature in the "protect my API" sense — it's a browser policy that decides whether *JS on other sites* can read responses from your API. But a bad CORS config turns a browser into a credentialed cross-origin exfiltrator.

**The always-CRITICAL combinations:**

- `Access-Control-Allow-Origin: *` + `Access-Control-Allow-Credentials: true` → the browser will reject this, but libraries often implement it wrong (reflect the origin instead of `*`).
- `Access-Control-Allow-Origin: <reflected Origin header>` + `Allow-Credentials: true` → attacker origin is reflected, credentialed cross-origin reads succeed. **CRITICAL** on any authed endpoint.
- `Access-Control-Allow-Origin: null` accepted (happens with sandboxed iframes, some redirect flows) + `Allow-Credentials: true` → attacker can force `null` origin. **HIGH**.

**Grep:**
```
Access-Control-Allow-Origin
cors\(\)                                           // Express default-open
cors\({[^}]*origin:\s*true                         // reflects any origin
cors\({[^}]*origin:\s*\(origin, cb\) => cb\(null, true\)   // same
CORSMiddleware.*allow_origins=\["\*"\].*allow_credentials=True   // FastAPI
```

**Also check:** `Access-Control-Allow-Methods` / `-Headers` that permit privileged headers (`Authorization`, custom auth headers) cross-origin without scrutiny. A `PUT`/`DELETE` method allowed from `*` against an authed endpoint = HIGH.

**Calibration:** `cors()` on a *public read-only* API is fine. `cors()` on a cookie-authenticated API is a breach waiting to happen.

---

## 3. Host header injection → password reset poisoning

The classic: the app builds a password-reset link from the `Host` request header.

```js
const resetLink = `https://${req.headers.host}/reset?token=${token}`;
await sendEmail(user.email, `Reset your password: ${resetLink}`);
```

Attacker POSTs to `/forgot-password` with `Host: attacker.com`. The victim receives an email with a link to `https://attacker.com/reset?token=<victim's valid token>`. They click, their token is leaked to the attacker's server. **CRITICAL** account takeover.

**Hunt for:**
```
req.headers.host                                   # Node
request.get_host() / request.META['HTTP_HOST']     # Django/Flask-ish
request.url.host                                   # URL-based
$_SERVER['HTTP_HOST']                              # PHP
```

…used in any context that ends up in an email, webhook callback URL, OAuth redirect, or cache key.

**Defence to verify is present:**
- App is configured with a canonical base URL (`APP_URL`, `PUBLIC_URL`, `NEXTAUTH_URL`) and uses *that*, not the request header.
- Django: `ALLOWED_HOSTS` configured and enforced (without it, `request.get_host()` trusts `Host`).
- Reverse proxy rewrites `Host` to the canonical value.
- Framework-level default: check `X-Forwarded-Host` isn't silently trusted unless a `trust proxy` setting is explicit.

**Also flag:** `X-Forwarded-Host` / `X-Forwarded-Proto` / `X-Forwarded-For` used for auth decisions or log attribution when the proxy layer isn't strictly enforcing them.

---

## 4. HTTP Request Smuggling

Rare in LLM-reviewed code because it's a protocol-level attack between a front-end proxy and a back-end, but when you see hand-rolled HTTP parsing or mismatched `Content-Length` / `Transfer-Encoding` handling, flag it.

**Signals in code:**
- Custom HTTP servers (not just Express/Fastify — raw `http.createServer` with custom body handling).
- Code that inspects `Transfer-Encoding` or `Content-Length` in middleware.
- Known-bad proxy configs (HAProxy older versions with `http-legacy`, older nginx).

Severity: **HIGH** if the stack shows the pattern and the product sits behind a CDN. Mostly this is a "note for the user and recommend Burp / desync tool" finding.

---

## 5. CRLF injection / header injection

Any response header built from user input without `\r\n` stripping is a header-injection → response-splitting → potentially XSS.

```js
res.setHeader('Location', req.query.next);        // if next contains \r\n<header injection>
res.setHeader('Set-Cookie', `role=${req.body.r}`); // attacker sets additional headers
```

Node's HTTP stack now throws on illegal header values, which mitigates a lot of historical CRLF injection. But:
- User input used in `Location` headers is still open-redirect + sometimes cache deception.
- Reflected headers (`X-Response-From: <user input>`) passed to less-strict downstream tools can still smuggle.

Grep:
```
setHeader\([^)]+,\s*req\.
Response\(.*{.*headers.*:.*req\.
redirect\(req\.query
```

---

## 6. HTTP Parameter Pollution + verb tampering

- **HPP:** same param submitted twice (`?user=admin&user=victim`). Different layers pick different values → auth bypass. Example: Node's `qs` gives an array, the validator reads the first, the handler reads the last.
- **Verb tampering:** framework routes `POST /admin` to an admin handler, but `GET /admin` falls through to a read-only one that leaks data. Always check admin-only routes for explicit method guards.
- **Method override header:** `X-HTTP-Method-Override: DELETE` processed by middleware (Express `method-override`, Django REST Framework option) can turn a benign POST into a DELETE. Disable in production unless actively needed.

Grep:
```
method-override | methodOverride
X-HTTP-Method-Override
```

---

## 7. NoSQL injection — MongoDB especially

JSON bodies + MongoDB queries = attackers can inject operators:

```js
// vulnerable: auth check
const user = await db.users.findOne({ email: req.body.email, password: req.body.password });
// attacker POSTs { email: 'admin@x.com', password: { '$ne': null } }
// → matches any non-null password → auth bypass
```

**Grep:**
```
findOne\(.*req\.body
find\(.*req\.body | .*req\.query
updateOne\(.*\$set.*req\.body
```

**Fix patterns to verify:**
- Schema validation (Mongoose, Zod, Joi) that coerces `password` to a string, rejecting objects.
- Explicit `{ email: String(body.email), password: String(body.password) }` construction.
- `mongo-sanitize` / `express-mongo-sanitize` middleware stripping `$` keys.

Also applies to:
- **Redis** — `SET user:${input}` with `\r\n` injection lets attackers inject additional commands (less common post-RESP3; still possible in raw-socket code).
- **Elasticsearch / OpenSearch** — JSON query DSL injection if the user controls JSON fields.

**Severity:** CRITICAL if auth / tenancy / money is downstream. HIGH for data read-only paths.

---

## 8. SSTI — Server-Side Template Injection

User input compiled into a template = full RCE in most template engines.

| Engine | Typical sink | Verdict |
|--------|--------------|---------|
| Handlebars | `Handlebars.compile(userInput)` | CRITICAL — `constructor.constructor` escape |
| EJS | `ejs.render(userInput)` | CRITICAL — `include` + JS code |
| Pug | `pug.compile(userInput)` | CRITICAL — full JS eval |
| Nunjucks | `nunjucks.renderString(userInput)` | CRITICAL — access to `range`, constructor |
| Mustache / Hogan | safe (logic-less) | Lower — still avoid |
| Jinja2 (Python) | `Environment().from_string(userInput).render()` | CRITICAL — `{{ ''.__class__.__mro__[1].__subclasses__() }}` |
| Twig / Jinja (PHP) | same pattern | CRITICAL |
| Liquid | safer by design, but custom filters leak | MEDIUM — audit filters |
| Razor / ASP.NET | `Html.Raw` + user templates | CRITICAL |
| Go `html/template` vs `text/template` | `text/template` over HTML = XSS; user-controlled template body = RCE-equivalent via reflection | CRITICAL |

Safe pattern: templates are **files on disk at build time**, and user input goes into *variables* the template interpolates. User input never reaches `compile`/`render_string`.

Grep:
```
compile\(  | renderString\(  | render_template_string\(  | from_string\(
```

---

## 9. XXE — XML External Entities

Still shipped by Java, Python, Node libs that parse XML.

**Bad defaults:**
- Python `xml.etree.ElementTree` pre-3.7.1 — resolves entities.
- Python `lxml` — resolve_entities=True by default.
- Node `xml2js`, `libxmljs` with `noent: true`.
- Java `DocumentBuilderFactory` without `setFeature("...disallow-doctype-decl", true)`.

**Exploit:**
```xml
<!DOCTYPE r [<!ENTITY e SYSTEM "file:///etc/passwd">]>
<r>&e;</r>
```
…or `file:///proc/self/environ`, or `http://169.254.169.254/latest/meta-data/` (SSRF via XXE).

Grep:
```
parseString | parseXml | ElementTree | lxml.etree | DocumentBuilder
```

Severity: **CRITICAL** on endpoints that accept XML from users (webhook receivers, SAML, SOAP legacy, uploaded configs).

---

## 10. File upload — the classic RCE vector

For every upload endpoint, verify all of these:

- **MIME type is not trusted from the client.** Content-Type in the request is attacker-controlled. Server must re-detect via magic bytes (e.g. `file-type` npm, `python-magic`).
- **Extension allowlist, not blocklist.** Blocklisting `.php` misses `.pht`, `.phtml`, `.php5`, `.phar`. Allowlist `['.jpg', '.png', '.pdf']` explicitly.
- **Double extensions.** `shell.php.jpg` — some servers (Apache with wrong `AddHandler`) execute on internal extension. Rename to a server-generated name.
- **Stored outside webroot** or served via a handler that forces `Content-Disposition: attachment` and a safe content-type.
- **Size cap** (MB, not unbounded). Prevents memory exhaustion + storage DoS.
- **SVG uploads** = XSS. SVGs contain `<script>`. Either disallow SVG, sanitise on upload, or serve with `Content-Security-Policy: script-src 'none'` response header.
- **PDF uploads** = phishing + JS. Serve with `Content-Disposition: attachment` if not rendered inline.
- **Polyglot files** (GIF that's also a valid JS / HTML) — validate via magic bytes AND refuse ambiguous types.
- **Image processing libraries** (ImageMagick — ImageTragick CVE-2016-3714 still bites copycat libs; older `sharp`, `libvips` with custom loaders). Pin versions and keep current.
- **Pixel / decompression bombs** — a 10MB PNG that decompresses to 100GB. Set max dimensions before decoding (not after).

Severity: **CRITICAL** if the server executes uploaded files (upload dir in PHP path, or `node` interpreter reaches uploaded JS, or WASM sandbox escape).

---

## 11. ZIP Slip / archive extraction

Every archive extraction (`.zip`, `.tar`, `.gz`, `.7z`, `.rar`) that uses entry names from the archive as filesystem paths is a ZIP Slip candidate:

```js
const entries = await archive.readEntries();
for (const entry of entries) {
  await fs.writeFile(path.join(outDir, entry.name), entry.data); // entry.name can be ../../etc/passwd
}
```

A malicious archive contains an entry named `../../../etc/cron.d/backdoor` — extraction writes outside `outDir`. CVE-2018-1002200 class; still ships regularly.

**Fix to verify:**
```js
const resolved = path.resolve(outDir, entry.name);
if (!resolved.startsWith(outDir + path.sep)) throw new Error('zip slip');
```

**Also:**
- **Decompression bomb (zip bomb)** — 42KB file that extracts to 4.5GB. Set per-entry and total size caps during extraction.
- **Symlink entries** — some archive libs create symlinks from the archive; a symlink to `/etc/passwd` + a subsequent write through it = arbitrary write. Disable symlink expansion unless needed.

---

## 12. SSRF deep — beyond "check the allowlist"

`stack-nodejs-general.md` covers the basic allowlist. Additional classes:

- **DNS rebinding.** Allowlist checks `host` → resolves to `1.2.3.4` (public, allowed) → a second later DNS re-resolves to `169.254.169.254` (IMDS). The `fetch()` call resolves separately. Defence: resolve DNS once, connect by IP, verify IP is public.
- **IMDSv1 on AWS EC2.** If the target runs on EC2 with IMDSv1 enabled, *any* SSRF to `http://169.254.169.254/latest/meta-data/iam/security-credentials/<role>` leaks AWS creds. Fix: IMDSv2 with required token. Check `aws_instance.metadata_options.http_tokens = "required"` in Terraform.
- **Redirect-based SSRF.** App fetches `http://allowed.example.com/redirect` which 302s to `http://169.254.169.254/`. If the HTTP client follows redirects automatically, the allowlist is bypassed. Fix: either `maxRedirects: 0` and re-check after each hop, or disable cross-host redirects.
- **Blind SSRF via webhooks.** Even if the response isn't returned to the attacker, the side-effect (POST to an internal endpoint) can cause state change. Use allowlisted outbound egress at network layer.
- **Gopher / file / dict schemes.** Explicit scheme allowlist.

---

## 13. GraphQL security

GraphQL is common and underscrutinised. Checklist:

1. **Introspection disabled in production.** `introspection: false` or Apollo's `NODE_ENV === 'production'` check. Otherwise attackers get the full schema. MEDIUM on its own; HIGH if the schema reveals sensitive fields (`User.passwordHash`, `internalNote`).
2. **Query depth limit.** `graphql-depth-limit` or equivalent. A malicious query `{ user { friends { friends { friends { ... 10k deep } } } } }` DoSes the server. Missing = HIGH (concrete amplification).
3. **Query complexity analysis.** `graphql-cost-analysis` or `graphql-validation-complexity`. Without it, a 10-line query can trigger 1M DB rows. HIGH.
4. **Alias-based amplification.** Attackers can request the same expensive field 100× via aliases in one query. Complexity analysis catches this; depth limit alone doesn't.
5. **Batching.** `allowBatchedHttpRequests: true` lets attackers submit 100 queries in one HTTP request → can amplify rate-limit-bypass. Verify cost caps apply to the whole batch.
6. **Field-level authorization.** Resolvers must re-check auth per field, not rely on query-level middleware. A `Query.user.secretField` resolver without a check leaks even if `Query.user` requires login. Use `shield` / `graphql-shield` / directives — verify every sensitive field has a guard.
7. **Persisted queries / allowlist.** Best practice: server accepts only known query hashes in production. Without this, attackers can send arbitrary queries.
8. **CSRF.** See §1. Apollo Studio-era servers often accept cross-site `application/json` POSTs.
9. **Error verbosity.** Verify production errors don't echo DB internals / stack traces. Apollo's `formatError` should strip `extensions.exception.stacktrace` in prod.

---

## 14. WebSockets

WebSocket auth is commonly done **once at connection** and trusted for the session. Check:

- **Origin validation** on the HTTP upgrade handshake. Without it, any site can open a WS from the victim's browser (CSWSH — Cross-Site WebSocket Hijacking). For cookie-authed apps, = CRITICAL.
- **Per-message auth** — the handshake validates who you are; subsequent messages may carry `userId` that the server trusts. Treat every incoming message as untrusted JSON body.
- **Rate limit per connection** — a single WS can send millions of messages / sec. DoS + LLM10 cost amplification.
- **Heartbeats + disconnect on idle** — prevents resource exhaustion via many idle connections.
- **Sticky sessions / tenant isolation** — if WS routing uses a shared Redis pub/sub, verify topic filters by tenant.

---

## 15. OAuth / OIDC — the mistakes that keep shipping

Every OAuth client implementation must verify:

1. **`state` parameter.** Generated server-side, opaque, bound to session. Verified on callback. Missing = CSRF on the OAuth callback → account hijack. **CRITICAL**.
2. **PKCE (`code_challenge` / `code_verifier`) for public clients** (SPAs, mobile). Missing = auth-code-interception. HIGH.
3. **`redirect_uri` on the client side matches exactly one value sent to the IdP.** IdP-side allowlists alone aren't enough; if the client accepts a `redirect` query param and follows it, see open redirect (`stack-react.md §3`).
4. **`nonce` for OIDC.** Generated per request, verified in the returned `id_token`. Missing = token replay.
5. **`id_token` signature verified** using the IdP's JWKS. Verify `alg` allowlist, `iss`, `aud`, `exp`, `nbf`. `alg: none` acceptance = CRITICAL.
6. **`kid` handling.** Some libs trust `kid` to select a key, then follow it to arbitrary URLs (`jku`, `x5u`). Disable or strictly allowlist. See JWT section in `stack-nodejs-general.md`.
7. **Email-as-identity.** `id_token` with `email_verified: true` from provider A ≠ same user at provider B. Key accounts on `(iss, sub)`, not `email`.
8. **Implicit flow (response_type=token)** — deprecated. If the client uses it, MEDIUM (migrate to code + PKCE).
9. **Refresh token rotation + reuse detection** — when a refresh token is used, issue a new one AND invalidate the old. If the old is presented again → break the entire chain (theft detection).
10. **Token audience check.** An access token for service A used against service B (if they share a signing key or JWKS) — reject via `aud` check.

---

## 16. Session management

- **Rotate session ID on login.** Prevents session fixation. Grep for `req.session.regenerate()` / equivalent after successful login. Missing = HIGH.
- **Invalidate all sessions on password change.** Real bugs in 2024. Missing = HIGH (attacker keeps stolen session after victim "recovers" by changing password).
- **Session timeout** — absolute + idle. A 30-day sliding session with no absolute cap is MEDIUM.
- **Concurrent session control** — for high-sensitivity apps (banking), limit concurrent sessions. Product decision, not always security.
- **Session token entropy** — framework-managed is fine (`next-auth`, `better-auth`, `express-session`). Custom `Math.random()` session IDs = CRITICAL.

---

## 17. Password reset flow — repeatedly broken

- **Reset token generation** — must be cryptographically random, ≥128 bits. `Math.random()` = CRITICAL. `crypto.randomUUID()` / `crypto.randomBytes(32)` = OK.
- **Token expiry** — ≤ 1 hour typically. Unexpiring tokens = HIGH.
- **One-time use** — invalidated after successful reset. Reusable tokens = HIGH.
- **Token stored as hash** in the DB (like a password) so DB leak doesn't hand out active resets. Plaintext storage = MEDIUM.
- **Email uses canonical app URL, not `Host` header.** See §3.
- **Race condition on reset** — two concurrent `/reset` requests with the same token should not both succeed. Use atomic token consumption (`UPDATE … WHERE token = ? AND used = false RETURNING id`).
- **Don't reveal user existence** — `/forgot` with an unknown email should respond identically to one with a known email (same status, same timing). User-enumeration via different response = MEDIUM.
- **Don't send reset tokens via SMS alone** — SIM swap attacks are cheap.

---

## 18. MFA / 2FA bypass patterns

- **Backup code entropy** — ≥ 10 chars random; one-time use; stored hashed.
- **SMS fallback** — many breaches started with "forgot TOTP → fall back to SMS → SIM swap". Audit whether SMS-only fallback exists.
- **Admin impersonation / support bypass** — a support tool that logs in as a user without MFA is an insider-risk CRITICAL.
- **Reset email as MFA bypass** — if "forgot password" skips MFA after reset, the full MFA layer is bypassable via email access alone. Fix: require MFA after reset before sensitive action.
- **Trusted device / "remember me"** — cookie that skips MFA. Verify device token is short-lived + revocable + bound to device fingerprint.
- **Enrollment weakness** — MFA *only* required for login. First-time enrollment must use step-up auth. A stolen session without MFA should not be able to enrol a new authenticator.

---

## 19. Timing-based user enumeration

Login endpoint:
```js
const user = await db.users.findOne({ email });
if (!user) return res.status(401).send('invalid creds');       // fast
const ok = await bcrypt.compare(password, user.password_hash); // slow
if (!ok) return res.status(401).send('invalid creds');
```

Known emails take ~100ms (bcrypt); unknown emails take <10ms. Attacker diffs response time to enumerate users. Fix: always compute a bcrypt round, or use constant-time auth.

Also:
- Different error messages for "unknown user" vs "wrong password" — MEDIUM user-enumeration (mentioned in §17).
- Password reset confirming email existence via UI flash messages.
- Sign-up forms that say "email already in use" (bad) vs generic "we'll send a code".

Severity: MEDIUM. Chain with credential-stuffing = HIGH.

---

## 20. Business logic flaws — money paths especially

Not a single pattern; a mindset. For any endpoint that moves money, items, or permissions:

- **Negative quantity / negative price.** Cart with `-1` quantity → negative total → refund as order. Classic.
- **Coupon reuse.** One-time coupons applied N times concurrently before the "used" flag commits. Needs atomic DB update.
- **Race conditions in balance transfer.** `SELECT balance; UPDATE balance = balance - X` without row lock → double-spend. Use atomic `UPDATE … SET balance = balance - X WHERE balance >= X RETURNING *`.
- **Order state machine skips.** Attacker POSTs to `/orders/:id/ship` on an unpaid order. Check state in handler, not just in UI.
- **Currency confusion.** Amount in cents vs dollars vs paise. Fixed-point arithmetic; never `parseFloat` for money.
- **Webhook re-ordering.** Stripe `payment_intent.succeeded` arrives before `invoice.created` (or vice versa). Idempotent + state-machine-aware handlers; don't assume order.
- **Refund-after-access.** User pays → gets access → files chargeback. Does access revoke on `charge.dispute.created`?

These won't be caught by grep — they're read-the-code, ask-"what if" findings. Flag as HIGH when the exploit path is clear.

---

## 21. Clickjacking, tabnabbing, CSP bypass

- **Clickjacking.** Covered via `X-Frame-Options: DENY` or CSP `frame-ancestors 'none'`. Don't flag absence on pages with no sensitive state-change actions; DO flag on admin / payment / settings UIs.
- **Tabnabbing.** Any `<a target="_blank">` to a user-supplied URL without `rel="noopener noreferrer"` lets the opened page rewrite `window.opener.location` → phishing. Modern browsers default-safe since ~2021, but older bundles, older React, React Native webviews — still real. Grep: `target="_blank"` + grep the same lines for `rel=`. Missing = MEDIUM.
- **CSP bypass patterns.** If a CSP exists but allows:
  - `'unsafe-inline'` in `script-src` → CSP is mostly cosmetic. MEDIUM.
  - `'unsafe-eval'` → same. MEDIUM.
  - `https:` or `*.googleapis.com` blanket → attackers find a hosted JSONP / AngularJS gadget → bypass. MEDIUM.
  - Nonces reused across requests / predictable → MEDIUM.
  - `script-src 'self'` + endpoint that reflects JS → bypass via your own domain.

---

## 22. Subdomain takeover

Dangling DNS → cloud provider → attacker claims.

Pattern: `blog.company.com` CNAMEs to `company.github.io` or `company.netlify.app`. The company later deletes that site. DNS still points there. Attacker creates a new GH Pages site / Netlify app at the same name → serves content from `blog.company.com`.

In repos, the hint is often in `README.md`, deploy configs, or `dns/*.tf`. If you spot a CNAME to an external provider + commit history showing the target was deleted / migrated, flag as HIGH ("verify DNS points to a live resource").

Tools: `dnsrecon`, `subjack`. Recommend the user run them rather than confirming by probing.

---

## 23. Insecure defaults for data stores

If the repo has docker-compose / k8s / Terraform for infra:

- **MongoDB without auth** (`MONGO_INITDB_ROOT_USERNAME` unset). Historically the Mongo Apocalypse (2017). Still happens.
- **Redis without `requirepass`** exposed outside `127.0.0.1`. RCE via `CONFIG SET dir` + Lua.
- **Elasticsearch / OpenSearch without xpack security**. Open clusters leak data.
- **Postgres `trust` auth in `pg_hba.conf`** outside dev.
- **RabbitMQ `guest/guest`** default.

These are MEDIUM if only in dev configs (`docker-compose.dev.yml`), CRITICAL if in production configs (Helm values, prod Terraform).

---

## 24. Error verbosity in production

Framework defaults leak stack traces in production if `NODE_ENV` / `DJANGO_DEBUG` / equivalent is mis-set. A stack trace reveals:
- File paths → confirms internal structure.
- SQL error messages → confirms / denies injection.
- Library versions → CVE targeting.
- Sometimes env var values or request bodies.

Flag any `process.env.NODE_ENV !== 'production'` default in runtime code paths where the *production* behaviour is less safe than the dev one. Severity: MEDIUM.

---

## 25. API-specific OWASP 2023 additions worth a pass

OWASP API Security Top 10 (2023) complements OWASP Web Top 10. Most useful additions:
- **API#1 BOLA / IDOR** — covered in `stack-nextjs.md §3` (tenant scoping).
- **API#3 BOPLA** — Broken Object *Property* Level Auth. E.g. `PATCH /users/:id { role: "admin" }` — auth passes (you can edit yourself) but field-level check missing. Mass-assignment again (see Prisma §3).
- **API#4 Unrestricted Resource Consumption** — rate limits + cost caps (LLM10 for AI apps).
- **API#6 Server-Side Request Forgery** — this file §12.
- **API#8 Security Misconfiguration** — CORS, headers, defaults.
- **API#10 Unsafe Consumption of APIs** — trusting a third-party API response blindly (especially SSRF re-emitting third-party redirects).

---

## Quick checklist — run on any web app

1. Cookie-auth + CSRF protection (SameSite, Origin, tokens).
2. CORS reflect-Origin-with-credentials pattern.
3. Password reset: random token, expiry, one-time, canonical URL, hashed storage.
4. Session rotation on login + invalidate on password change.
5. Open-redirect patterns in `next=`, `redirect=`.
6. OAuth: state, PKCE, nonce, id_token verify, redirect_uri exact match.
7. File upload: magic bytes, extension allowlist, size cap, stored outside webroot.
8. Archive extraction: ZIP Slip guard, size cap, no symlink expansion.
9. NoSQL queries: typed inputs (`String(body.x)`), `$` operator stripping.
10. SSTI: no user input reaching `compile` / `render_string`.
11. XXE: XML parsers disable entity expansion.
12. GraphQL: introspection off in prod, depth + complexity limits, field-level auth.
13. WebSocket: Origin check on upgrade, per-message validation, rate limits.
14. Timing-safe auth path; no user-enumeration via error text.
15. Clickjacking headers on sensitive pages; tabnabbing on external links.
16. Data stores: auth enabled, not exposed beyond trusted network.
