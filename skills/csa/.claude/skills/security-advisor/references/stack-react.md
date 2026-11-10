# React (the UI layer) — security playbook

Read this whenever `react` appears in `package.json`, regardless of framework. If the app is Next.js, read this *alongside* `stack-nextjs.md`. If it's Vite / Remix / Expo (web) / Astro+React / CRA / React Native with webviews, this file is the primary React-specific reference.

The Next.js file is about server-side routing, server actions, middleware CVEs. This file is about the React UI layer — client-side XSS escape hatches, token storage, URL injection, open redirects, client-side route guards.

---

## 1. The four XSS escape hatches

React auto-escapes by default. It stops escaping in four places. Every finding in this section pivots on one of them.

### `dangerouslySetInnerHTML`

Grep:
```
grep -rn "dangerouslySetInnerHTML" --include="*.tsx" --include="*.jsx" src/
```

For each match:
- **Is the input user-controlled?** (a prop that eventually traces to `fetch` response, URL param, form input, LLM output) → **CRITICAL** XSS if so.
- **Is it sanitised?** Look for `DOMPurify.sanitize(...)` or `sanitize-html` wrapping the value. Unsanitised = finding.
- **`DOMPurify.sanitize(dirty, { FORBID_TAGS: [...] })` with overrides** — verify the allow-list doesn't re-enable `<script>` / `<iframe>` / `on*` event handlers. Bad config = same as no sanitisation.
- **Markdown libraries feeding HTML**: `marked` with `sanitize: false`, `markdown-it` with `html: true` and no post-sanitisation, `react-markdown` with `rehype-raw` plugin and no `rehype-sanitize` after it.

### Href / src with user URLs → `javascript:` and `data:` schemes

```jsx
<a href={userUrl}>click</a>      // if userUrl = "javascript:alert(1)" → XSS on click
<img src={userUrl} />            // data:text/html,<script>... on some contexts
<iframe src={userUrl} />         // same
```

Grep:
```
grep -rnE "href=\{[^\}]+\}|src=\{[^\}]+\}" --include="*.tsx" --include="*.jsx" src/
```

**Check:** the value is validated against an allowlist of schemes (`http`, `https`, `mailto`, optionally `tel`) OR passed through a URL parser that rejects weird schemes. A common safe pattern:

```ts
function safeUrl(url: string): string {
  try {
    const parsed = new URL(url, window.location.origin);
    return ['http:', 'https:', 'mailto:'].includes(parsed.protocol) ? parsed.href : '#';
  } catch { return '#'; }
}
```

Raw `href={someUser.website}` with no filter = **HIGH XSS** (or at minimum a phishing vector via `javascript:`).

### Direct DOM manipulation

`useRef` followed by `ref.current.innerHTML = ...` — same class as `dangerouslySetInnerHTML`. Grep for `.innerHTML = ` and `.outerHTML = `.

### Third-party component APIs

Some libraries accept HTML strings (charting libs, rich-text editors, tooltip libs). Audit any prop named `html`, `content`, `html`, `markdown`, `raw` for user-controlled data.

---

## 2. Auth token storage

This is the single most common React security finding in 2024-2025.

**The question:** where does the app store the session token / JWT / refresh token?

| Storage | XSS steals it? | CSRF risk? | Verdict |
|---------|----------------|-----------|---------|
| `localStorage` / `sessionStorage` | **yes** — any XSS reads it | no | Bad for session tokens. OK for non-sensitive UI state. |
| Cookie, not httpOnly | **yes** — `document.cookie` reads it | yes | Worst of both. Avoid. |
| Cookie, httpOnly + Secure + SameSite=Lax/Strict | **no** — JS can't read | mitigated by SameSite | Good default. |
| In-memory only (React context, Zustand, etc.) | no (until the XSS can also trigger a refetch) | n/a | Good, but re-auth needed on refresh. |

**Grep for:**
```
localStorage.setItem('token' | 'jwt' | 'accessToken' | 'auth' | 'session', ...)
sessionStorage.setItem(...)
Cookies.set('token', ...)  // js-cookie without httpOnly
```

**Finding severity:** storing a session token or refresh token in `localStorage` is **HIGH**. Combined with any suspected XSS vector, **CRITICAL**.

**OAuth-specific:** SPA flow should use PKCE, and the access token should be short-lived + not persisted across tabs. Long-lived refresh tokens in `localStorage` is a HIGH finding.

---

## 3. Open redirect

A very common pattern in login flows:

```tsx
// after login
const next = searchParams.get('next') ?? '/dashboard';
router.push(next);
```

If `next` can be `https://attacker.com/phish`, the attacker now has a login-success redirect to their own page — great phishing chain.

**Check:**
- The `next` / `redirect` / `returnTo` / `callbackUrl` param is validated to start with `/` AND not `//` (protocol-relative URL trick).
- Or validated against an allowlist of internal paths.
- Or parsed via `new URL(...)` and host-checked against the app's own origin.

Grep:
```
grep -rnE "searchParams\.get\('(next|redirect|returnTo|callbackUrl|from)'\)|useSearchParams" --include="*.tsx" src/
```

**Severity:** MEDIUM standalone (phishing enabler). HIGH if combined with cookie-based auth (attackers can exfiltrate if they also control a page on an allowed origin).

---

## 4. postMessage

If the app uses iframes or window.postMessage:

```tsx
window.addEventListener('message', (e) => {
  if (e.data.type === 'auth') setToken(e.data.token);  // no origin check!
});
```

**Check:** every `message` handler verifies `e.origin` against a known-safe list. Missing origin check = CRITICAL (any iframe, extension, or embedded content can inject data).

Grep:
```
grep -rn "addEventListener\('message'\|window\.onmessage" --include="*.tsx" --include="*.ts" src/
```

---

## 5. Client-side route guards vs server enforcement

React Router / Remix / Tanstack Router route guards look like security, but they aren't.

```tsx
<Route path="/admin" element={user?.isAdmin ? <Admin /> : <Navigate to="/" />} />
```

This controls *rendering* only. An attacker who doesn't care about the UI just calls the API directly. The API routes / server actions / mutations MUST re-check auth.

**Do not flag the client-side guard itself.** That's fine. Flag the API endpoint if it doesn't re-auth. (This is the P14 case from `false-positive-rules.md`.)

**What to hunt for instead:** client-side route guard that reads `user.isAdmin` or a role field from **localStorage** or a client-only source that's user-modifiable. If the UI gates on `localStorage.getItem('role') === 'admin'` and there's no server check, the server is definitely misconfigured too.

---

## 6. Environment variables exposed to the client

Every React bundler prefixes client-exposed env vars. Anything with these prefixes ends up in the JS bundle and is visible to every user:

| Framework | Client prefix |
|-----------|---------------|
| Next.js | `NEXT_PUBLIC_` |
| Vite | `VITE_` |
| CRA | `REACT_APP_` |
| Remix | (all env accessible server-side; `window.ENV` pattern for client) |
| Expo | `EXPO_PUBLIC_` |
| Astro | `PUBLIC_` |

**Check:** any var matching those prefixes that contains a secret, signing key, webhook secret, database credential, admin token, or OpenAI-style server API key is **CRITICAL** — it's in every user's browser.

Grep:
```
grep -rnE "(NEXT_PUBLIC|VITE|REACT_APP|EXPO_PUBLIC|PUBLIC)_[A-Z_]*(SECRET|KEY|TOKEN|PASSWORD|PRIVATE)" --include=".env*" --include="*.ts" --include="*.tsx"
```

**Expected-and-fine** values (don't flag):
- Publishable Stripe keys (`pk_*` — Stripe explicitly designed for client exposure)
- PostHog / Sentry / analytics DSN keys (those are designed for client)
- OAuth client IDs (not secrets — the secret is the companion value)
- Public app URL

**Always-a-finding** values:
- Any `*_SECRET` / `*_PRIVATE_KEY`
- AI API keys (OpenAI, Anthropic, Claude, OpenRouter — all server-only)
- Webhook signing secrets
- Database URLs with credentials

---

## 7. Hydration & server-state leakage

Server-rendered React (Next.js RSC, Remix loaders, Astro islands) serialises server data into the HTML so the client can hydrate. If the serialised data contains server-only fields, they leak to every user.

**Grep for:** loaders / RSC components / `getServerSideProps` that return user objects, org objects, or config without `.pick()`ing to a whitelist. Mass-return of `await prisma.user.findUnique({...})` with no field filter sends `password`, `emailVerified`, `stripeCustomerId`, everything to the client.

**Severity:** HIGH if password hash or other credentials leak. MEDIUM for other PII (email, billing info, internal IDs).

---

## 8. Markdown / rich-text rendering

If the app renders user-submitted markdown or rich text:

- `react-markdown` — safe by default (no raw HTML). **Flag if** `rehype-raw` plugin is loaded and no subsequent `rehype-sanitize`.
- `marked` — set `sanitize: true` (now deprecated) OR pipe output through `DOMPurify`. `marked.parse(userContent)` without sanitisation + `dangerouslySetInnerHTML` = CRITICAL.
- `markdown-it` — safe by default (`html: false`). Flag if `html: true`.
- `remark-html` — outputs HTML; must be sanitised downstream.
- `quill`, `tiptap`, `lexical`, `slate` — rich-text editors that store content as JSON; ensure the render path sanitises on output, not just on input (defence in depth).

---

## 9. Third-party React libraries with XSS history

Keep a flag eye out for (check installed version):

| Package | Pattern |
|---------|---------|
| `react-html-parser` | Legacy, prone to XSS. Prefer `html-react-parser` with sanitisation. |
| `react-dom-confetti` | Historically fine, just noting. |
| `html-react-parser` | Need explicit sanitisation of the input HTML. |
| `react-quill` older versions | XSS in certain formats. Check version against advisories. |
| Old `react-intl` | Format strings with HTML interpolation. |

Use `npm audit` / `bun outdated` to confirm.

---

## 10. React Native / Expo specific add-ons

If it's a React Native or Expo project:

- **`WebView`** with `source={{ html: userContent }}` or `source={{ uri: userUrl }}` — classic XSS / navigation hijack vector. Verify `originWhitelist` is set, `javaScriptEnabled` is deliberate, `allowFileAccess` is `false` by default.
- **Deep links** — any `Linking.getInitialURL()` / `useURL()` handler must validate the incoming URL. Deep link phishing (another app opens yours with a hostile URL) is the mobile analogue of open redirect.
- **`AsyncStorage`** — not encrypted. Don't put auth tokens there; use `expo-secure-store` / `react-native-keychain`.
- **`Clipboard`** — avoid auto-reading on mount (privacy). Avoid writing sensitive data (tokens).
- **Metro bundler env vars** — `EXPO_PUBLIC_*` are in the bundle; see section 6.

---

## 11. Content Security Policy in SPAs

CSP is a framework concern (covered in `stack-nextjs.md` section 7 for Next.js). For other React app hosts (Vite dev, Netlify, Cloudflare Pages), the platform / reverse proxy sets CSP. Check the deploy config:

- `vercel.json` / `netlify.toml` / `_headers` / `meta http-equiv="Content-Security-Policy"` — at least `default-src 'self'; script-src 'self' 'unsafe-inline'`. (Note: SPAs often need `'unsafe-inline'` for hydration; nonces are better but rare in pure SPAs.)
- Missing CSP: MEDIUM.
- `script-src *` or `'unsafe-eval'`: MEDIUM (breaks the main benefit of CSP).

---

## 12. Tabnabbing — `target="_blank"` without `rel="noopener noreferrer"`

```jsx
<a href={externalUrl} target="_blank">See more</a>
```

When a user clicks this, the opened page receives `window.opener` — it can then do `window.opener.location = 'https://phishing.example.com/fake-login'`, silently replacing the original tab with a phishing clone while the user is distracted by the new tab.

Modern browsers default `noopener` for `target="_blank"` on anchor elements since ~2021. But:
- Older bundles, older React versions, React Native `WebView`s with custom link handling — still affected.
- `window.open(url, '_blank')` in JS does NOT get the browser default — explicit `'noopener,noreferrer'` required.
- `<form target="_blank">` and `<area target="_blank">` likewise not defaulted.

Grep:
```
target="_blank"
window\.open\(
```

For each match, verify `rel="noopener noreferrer"` is present. Severity: MEDIUM (phishing enabler). HIGH when combined with any path where an attacker can inject a target URL into trusted-looking UI (comments, user profiles).

---

## 13. Client-side path traversal / bundler exposure

- **Source maps in production.** `.map` files shipped to prod give attackers the original source, including comments and sometimes commented-out secrets. Verify the production build strips source maps or restricts them to authenticated users. MEDIUM if sensitive app logic is revealed; LOW for open-source.
- **Client-side path operations** — `fetch('/api/' + file)` where `file` comes from a route param and the server uses it directly in a filesystem op is an open-redirect-like path-traversal. The bug is on the server, but the client-side code is where you notice it.
- **Service Worker cache poisoning** — an XSS that runs before the SW is registered can install a malicious SW that intercepts every subsequent fetch. Always-a-finding = XSS. The SW itself amplifies impact.
- **Trusted Types** (CSP `require-trusted-types-for 'script'`) — when present, verify the app defines policies and doesn't escape them via `trustedTypes.createPolicy('default', {...})` that just passes input through. INFORMATIONAL.

---

## 14. React-specific quick wins checklist

Run these whenever React is detected:

1. Grep for `dangerouslySetInnerHTML` — each one is a potential XSS. Validate sanitisation.
2. Grep for `href={`/`src={` — check scheme validation for user-controlled URLs.
3. Grep for `localStorage.setItem.*(token|jwt|auth|session)` — flag if present.
4. Find all `<Route>` / `<Navigate>` with a `next` / `redirect` / `returnTo` — validate destination.
5. Grep for `'message'` event listeners — each needs an `e.origin` check.
6. Find `NEXT_PUBLIC_` / `VITE_` / `REACT_APP_` / `EXPO_PUBLIC_` / `PUBLIC_` env vars — any secret in there is critical.
7. For SSR apps: find loaders / RSC boundaries — are full database objects serialised to the client?
8. If markdown / rich text rendering exists, verify sanitisation path.
9. For React Native: audit `WebView`, deep links, `AsyncStorage` usage.
10. Check the deploy config for CSP headers.
11. Grep `target="_blank"` and `window.open(` — each needs `rel="noopener noreferrer"` / `'noopener,noreferrer'`.
12. Check prod build output for `.map` files / source-map exposure.

---

## Precedents worth restating

- **Client-side auth gating is a UI convenience, not a security control.** Don't flag the absence of client-side guards — flag the server endpoint that doesn't re-auth.
- **React's auto-escaping handles text content only.** Attributes, URLs, and HTML-string props are still manual.
- **`useState` / `useContext` / Zustand stores do NOT persist to disk.** In-memory secrets survive only until page reload, which is usually good.
- **Publishable keys (Stripe `pk_*`, PostHog project keys) in `NEXT_PUBLIC_*` are safe by design** — don't flag as "secret exposed". The `sk_*` / secret variant must never be there.
