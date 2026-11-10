---
name: checker-scoping
description: Use AFTER choosing architecture, BEFORE writing checker code — determine whether your checker will be patch-specific, project-general, or cross-project. Write scope_statement.md documenting the vulnerability pattern, applicable APIs, sources/sinks/sanitizers, limitations, and transferability.
---

# Checker Scope Analysis

CSA checkers are NOT万能. Before writing code, determine what **level** of checker you are building and document its expected capabilities and limitations. This prevents over-engineering a pattern-specific checker and avoids claiming cross-project generality for a project-specific heuristic.

## Step 1: Understand CSA Capability Boundaries

### What CSA CAN Detect (static, symbolically visible)

| Category | Examples |
|----------|----------|
| **Static data flow** | taint from source→sink, uninitialized values, use-after-free via path tracking |
| **Static control flow** | missing branch conditions, unreachable code, inverted guards |
| **API usage patterns** | mismatched alloc/free pairs, missing error-check after a known call, incorrect argument order |
| **Missing checks** | missing NULL/bounds/size/range guard before a sensitive operation |
| **Generic dangerous patterns** | divide-by-zero, null dereference, buffer overflow via fixed-size access, double-close |

### What CSA CANNOT Detect (dynamic, semantic, runtime)

| Category | Why it fails |
|----------|-------------|
| **Dynamic state** | heap values only known at runtime; CSA models symbols, not concrete values |
| **Business semantics** | "this user should not access that record" — requires domain knowledge CSA doesn't have |
| **Runtime configuration** | feature flags, config files, environment variables that change behavior |
| **Concurrency timing** | CSA is single-threaded; no interleaving model for data races or TOCTOU |
| **Project-custom security semantics** | internal reference-count invariants, custom capability models, proprietary protocols |

### Cross-Project Transferability

**Can transfer across projects:** generic vulnerability patterns (buffer overflow, use-after-free, double-free, null-deref), generic framework/API patterns (glib `g_*` functions, Linux kernel `copy_from_user`, OpenSSL `BIGNUM` operations).

**Cannot transfer across projects:** rules depending on project-custom helper functions, project-specific state machines, internal ACL/permission models, proprietary protocol parsers.

**Rule of thumb:** If the vulnerability can be described entirely in terms of the C standard library, POSIX API, or a widely-used framework (glib, APR, etc.), it can be a cross-project checker. If the description requires naming project-specific functions or data structures, it is at most project-general.

## Step 2: Classify the Fix Pattern into One of Three Levels

Read `patch.diff` and `pre_functions.txt` / `post_functions.txt`. Based on the **nature of the fix**, classify into:

### Level 1: Patch-Specific Checker

**Definition:** The checker can identify the exact vulnerability point in the original code but has weak generalization beyond this specific code pattern.

**Characteristics:**
- The fix is highly specific to this function's logic (e.g., a particular `if` condition order, a specific variable assignment)
- The vulnerability depends on a unique code structure unlikely to repeat even in the same project
- The checker essentially "matches the buggy pattern" rather than detecting the vulnerability class

**Examples:**
- A fix that swaps the order of two specific operations in one function
- A fix that changes a specific constant threshold value
- A fix that adds a check for a project-specific enum value

**Expected behavior:**
- `N_buggy`: fires on the original buggy code (at least 1 warning)
- `N_patched`: 0 on the patched code (the fix site is silent)
- May fire on near-identical copies within the same file; unlikely to generalize to other files

### Level 2: Project-General Checker

**Definition:** The checker can detect the same vulnerability pattern across the same project, using project-specific APIs or conventions.

**Characteristics:**
- The vulnerability involves a project-internal API that appears in multiple places
- The fix enforces an invariant that the project's own functions should always satisfy
- The checker needs to reference project-specific types, function names, or macros
- Project-specific helper functions serve as sources, sinks, or sanitizers

**Examples:**
- A project uses `my_alloc()` / `my_free()` wrappers and the checker tracks those pairs
- A project has a convention that `get_handle()` must be followed by `validate_handle()`
- A project-specific struct field must be checked before use (e.g., `obj->refcnt > 0`)

**Expected behavior:**
- `N_buggy`: fires on multiple sites within the project that violate the invariant
- `N_patched`: may still fire on sites that are not yet patched (potential new bugs found!)
- The checker is reusable across different versions/forks of the same project

### Level 3: Cross-Project Checker

**Definition:** The checker can detect the vulnerability class across different projects, libraries, or codebases using only standard language features or widely-used framework APIs.

**Characteristics:**
- The vulnerability is a well-known CWE class (CWE-120 buffer overflow, CWE-416 use-after-free, CWE-476 null-deref)
- Detection relies entirely on C standard library, POSIX, or a widely-adopted framework API
- No project-specific function names or type names appear in the detector logic
- The checker models generic resources: memory, file descriptors, locks, reference counts via standard APIs

**Examples:**
- Buffer overflow: `malloc(n)` followed by access beyond `n` bytes → cross-project
- Use-after-free: `free(p)` then `*p` on any path → cross-project
- Double-close: `close(fd)` on an already-closed descriptor → cross-project
- `malloc()` return value not checked before dereference → cross-project
- `strcpy()` with destination size not validated against source length → cross-project

**Expected behavior:**
- `N_buggy`: fires on the known buggy pattern in this project
- `N_patched`: 0 on the fixed code
- Can be applied to entirely different codebases and still find real bugs
- False positive rate is the main concern — cross-project checkers need careful sanitizer/guard modeling

## Step 3: Decision Flow

```
Read the patch fix pattern
    │
    ▼
Does the fix reference project-custom types, functions, or macros
as essential elements of the vulnerability?
    │
    ├── YES ──▶ Does the custom API appear in ≥3 files across the project?
    │               │
    │               ├── YES ──▶ Level 2: Project-General
    │               └── NO  ──▶ Level 1: Patch-Specific
    │
    └── NO  ──▶ Can the vulnerability be described purely in terms of
                 standard C / POSIX / widely-used framework APIs?
                    │
                    ├── YES ──▶ Level 3: Cross-Project
                    └── NO  ──▶ Level 2: Project-General (re-evaluate —
                                you may have missed an implicit dependency)
```

**Tie-breaking rules:**
- When in doubt, round **down** (3→2, 2→1).  It is better to under-promise and deliver a useful checker than to claim cross-project generality for a heuristic that only works in one codebase.
- If the fix involves both a generic pattern AND project-specific types, it is Level 2.  The project-specific dependency prevents Level 3.
- If the fix is for a well-known CWE but implemented via project-internal APIs, it is Level 2, not Level 3.

## Step 4: Write the Scope Statement

After classifying the level, write a scope statement into the workdir.  This helps you design the checker realistically and documents its limits for future users.

Write to `scope_statement.md`:

```markdown
# Checker Scope Statement

## Vulnerability Pattern
[One sentence: what vulnerability class and detection pattern this checker implements]

## Scope Level: [Level 1 / Level 2 / Level 3]
[Brief justification referencing the fix pattern]

## Applicability

| Dimension | Detail |
|-----------|--------|
| **Languages** | C, C++ (if applicable) |
| **Frameworks/APIs** | [Standard C, POSIX, or specific library/framework] |
| **Source (where bad values come from)** | [e.g., function return values, user input, network reads] |
| **Sink (where the bug manifests)** | [e.g., dereference, memory access, function call with bad arg] |
| **Sanitizer/Guard (what prevents the bug)** | [e.g., NULL check, bounds check, error-handling branch] |

## Limitations

| Dimension | Detail |
|-----------|--------|
| **Not applicable to** | [Scenarios where this checker is expected to miss or be wrong] |
| **Known false positive patterns** | [Code patterns that trigger the checker but are safe] |
| **Known false negative patterns** | [Bug variants the checker cannot catch] |

## Transferability Assessment (for Level 2/3 only)

[Level 2: Which other modules/components within this project would benefit?]
[Level 3: Which other projects/libraries/codebases could this checker be applied to?]
```

## Step 5: Adjust Architecture Based on Level

Your scope level affects architecture decisions:

| Concern | Level 1 | Level 2 | Level 3 |
|---------|---------|---------|---------|
| **Hardcoded function names** | Acceptable — the bug IS in that function | Use configurable function sets or pattern-based matching | MUST be generic — no hardcoded project names |
| **Hardcoded type names** | Acceptable | Use the project's public header types | MUST use standard types or trait-based matching |
| **FP tolerance** | Low — matching exactly one pattern should produce few FPs | Medium — project-wide patterns may have edge cases | Higher — cross-project scanning hits diverse code patterns |
| **Whitelist/allowlist** | Not needed | Optional — can list project-specific safe functions | Recommended — model standard sanitizer patterns |
| **Documentation** | Minimal — it's a one-off detector | Good — other project devs may use it | Excellent — users in other projects need clear docs |

If you classified as Level 3 but find yourself hardcoding a project-specific function name, you are really building a Level 2 checker — reclassify.
