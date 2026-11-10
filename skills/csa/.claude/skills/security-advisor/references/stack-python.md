# Python — brief playbook

Read this for Python projects (FastAPI, Django, Flask, or standalone). This reference is deliberately terse — expand it on demand if the user runs the skill on a Python-heavy codebase and you find gaps.

---

## Deserialization / code execution

**Always CRITICAL:**
- `pickle.loads(untrusted)` — RCE by design. Never safe with untrusted input.
- `yaml.load(untrusted)` without `Loader=yaml.SafeLoader` — RCE. Use `yaml.safe_load`.
- `eval(untrusted)`, `exec(untrusted)`, `compile(untrusted)` — obvious.
- `subprocess.run(cmd, shell=True)` with user input — shell injection.
- `os.system(cmd)` with user input — shell injection.

**Safe:**
- `subprocess.run(['binary', arg1, arg2])` — args as list, no shell.
- `json.loads` — safe.
- `yaml.safe_load` — safe.

---

## SQL injection

- Any f-string / `%` format / `.format()` building SQL with user input = CRITICAL.
- Safe: parameterised queries (`cursor.execute("... WHERE id = %s", (user_id,))`) or ORM queries.
- Django ORM: `objects.raw("... WHERE id = " + id)` is bad. `objects.raw("... WHERE id = %s", [id])` is safe.
- SQLAlchemy `text("... WHERE id = " + id)` is bad. Parameterised `text("... WHERE id = :id")` is safe.

---

## Web frameworks

### FastAPI
- Dependency injection auth: verify every protected route has an auth `Depends(...)` parameter. A route without = public.
- Pydantic models handle input validation well — but check that routes actually use them, not raw `dict` bodies.
- CORS middleware — audit allowed origins / methods / credentials.

### Django
- `CSRF_COOKIE_HTTPONLY = True`, `SECURE_SSL_REDIRECT = True`, `SESSION_COOKIE_SECURE = True` in production settings.
- `ALLOWED_HOSTS` not wildcard in production.
- `DEBUG = False` in production (debug mode leaks stack traces + settings).
- Middleware order — `SecurityMiddleware` early, `CsrfViewMiddleware` before any view middleware that auths.
- `mark_safe` / `|safe` template filter with user content = XSS.
- Raw SQL paths (see SQL injection above).

### Flask
- No CSRF by default — verify `flask-wtf` CSRFProtect is registered, or JWT auth with SameSite cookies.
- `render_template_string(userInput)` — SSTI (server-side template injection) = CRITICAL RCE.
- `send_from_directory(dir, user_filename)` — path traversal risk; Flask's built-in safeguards help but verify.

---

## SSRF

Same principle as Node:
- `requests.get(url)` / `httpx.get(url)` with user-controlled URL.
- Allowlist hosts + protocols; reject private IP ranges.
- AWS IMDS (169.254.169.254) is the classic SSRF-to-RCE pivot on EC2.

---

## Weak crypto

- `hashlib.md5`, `hashlib.sha1` for anything security-relevant (passwords, signatures, tokens) = CRITICAL.
- Passwords: `passlib` with bcrypt/argon2/scrypt; `werkzeug.security.generate_password_hash` (Flask) is OK.
- JWT: `pyjwt` — verify `algorithms=[...]` is explicit on `decode()`. Missing = CRITICAL.

---

## Common checks in order

1. Framework detection (FastAPI vs Django vs Flask vs other).
2. Grep for `pickle.loads`, `yaml.load` (not `safe_load`), `eval`, `exec`, `shell=True` — each is a finding.
3. Grep for f-string SQL / `.format(` near `execute(` / `raw(` — SQL injection.
4. Framework-specific debug/production config (Django DEBUG, Flask ENV).
5. Dependency audit: `pip audit` / `safety check` / `poetry export` + feed to audit.
6. JWT misuse.
7. Check `.env`, `settings.py`, secrets.

---

## XXE — XML entity expansion

Python XML libs often default-unsafe:
- `xml.etree.ElementTree.parse(untrusted)` — historically resolved entities; safe since 3.7.1 but pinned-lower projects are exposed.
- `lxml.etree.parse(untrusted)` — resolves entities by default. Must pass `parser=etree.XMLParser(resolve_entities=False, no_network=True, load_dtd=False)`.
- `xml.dom.minidom.parseString` — same class.
- `xmltodict`, `xmlsec`, `pysaml2` — audit per-lib; SAML responses are attacker-controlled by definition.

Exploit payload: see `stack-api-surface.md §9`. CRITICAL on any endpoint accepting XML (webhook receivers, SAML callbacks, SOAP).

---

## SSTI — Jinja2 / Mako / Django templates

- `Environment().from_string(user_input).render()` = user-controlled template body → RCE via `{{ ''.__class__.__mro__[1].__subclasses__() }}` chain to `os.system`.
- `render_template_string(user_input)` — Flask convenience for the same thing.
- Django `Template(user_input)` + `.render(context)` — same.
- `mark_safe(user_input)` — XSS, not RCE, but same family.

Grep:
```
from_string\(   |  render_template_string\(   |  Template\(  .*user
```

CRITICAL when user input is the template body. MEDIUM when user input is a template *variable* (unless the template itself uses `|safe` / autoescape disabled).

---

## NoSQL injection (MongoDB / pymongo)

```python
users.find_one({"email": request.json["email"], "password": request.json["password"]})
# attacker POSTs {"email": "admin@x.com", "password": {"$ne": null}} → auth bypass
```

Fix: coerce to str (`str(request.json["email"])`) or use Pydantic models that enforce `str` type. Verify the `BaseModel` has explicit string fields, not `Any` / `dict` for auth-relevant inputs.

---

## FastAPI — response_model field leakage

```python
@app.get("/me")
async def me() -> dict:
    return await db.users.find_one({...})   # returns everything, incl. password_hash
```

Without `response_model=UserPublicDTO`, FastAPI serialises whatever you return. Mass-return of ORM objects leaks `password_hash`, `stripe_customer_id`, internal fields.

Flag endpoints that return ORM/DB objects directly without a `response_model` or a Pydantic DTO. Severity: HIGH if credentials leak; MEDIUM for PII.

---

## Django — additional checks

- **`raw_query` / `extra(where=[user_input])`** — SQL injection under Django's ORM.
- **`mark_safe(user_html)`** — XSS on render.
- **`assert` in production code** — Python `-O` strips asserts; don't use for auth checks.
- **`@csrf_exempt` decorator** — every use must have a reason.
- **`django-admin` accessible in prod** — always behind VPN / staff auth.
- **Pickle session backend** — `SESSION_SERIALIZER = 'django.contrib.sessions.serializers.PickleSerializer'` is the default prior to Django 1.6 upgrade guide; verify JSON serialiser.

---

## Flask — additional checks

- **`debug=True`** anywhere reachable in production = Werkzeug debugger → authenticated RCE console at `/console`. CRITICAL.
- **`render_template_string`** — see SSTI above.
- **`send_file(user_filename)`** without `send_from_directory` — path traversal.
- **`session` uses `SECRET_KEY` as signer** — if `SECRET_KEY` leaks or is a default, session forgery = full account takeover.

---

## FastAPI — additional checks

- **Dependency injection auth bypass** — a route missing the auth `Depends(...)` is public. Easy to miss during refactors. Grep every route; every non-public route must have an auth dependency.
- **CORS `allow_origins=["*"]` + `allow_credentials=True`** — starlette CORSMiddleware will happily accept this (despite the spec); browsers usually reject, but bugs happen. Flag anyway (see `stack-api-surface.md §2`).
- **Background tasks** — tasks run with the auth of the caller but may write beyond the caller's scope. Audit any `background_tasks.add_task(...)` that touches sensitive state.
- **WebSocket auth** — FastAPI WS handshake auth must be explicit; no default. See `stack-api-surface.md §14`.

---

## If this file isn't enough

If you find yourself hitting patterns this file doesn't cover (async web frameworks like Starlette, Tornado; data frameworks like FastAPI+SQLAlchemy+Celery combos; ML serving), tell the user: "Python coverage in this skill is brief — I'll apply general OWASP principles and flag anything that looks off, but a Python-specific security linter like `bandit` would complement this review well."
