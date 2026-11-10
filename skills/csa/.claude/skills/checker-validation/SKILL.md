---
name: checker-validation
description: Use AFTER writing checker.cpp, BEFORE writing summary.json — compile the checker, run before/after scans, interpret results (COMPILE_ERROR, N_buggy, N_patched), triage false positives, and decide when to accept vs refine.
---

# CSA Checker Validation & Refinement

Compile the checker, scan before/after commits, and refine based on results.

## Compile

Run `bash compile.sh` (or use the MCP `compile` tool).  Max **3 compile-fix cycles**:

1. Read the error — the MCP `compile` result includes checker.cpp context.
2. Fix the issue in checker.cpp.
3. Re-compile.
4. If still failing after 3 cycles, simplify: comment out the problematic logic, get a minimal checker compiling, then add complexity back.

Common compile errors and their fixes:
- `unknown type name 'XXX'` → missing `#include` or wrong namespace.  Check `references/csa_headers_api.md` for the correct header.
- `no member named 'XXX'` → the API does not exist in LLVM 18.  Check the API reference.
- `undefined reference to 'XXX'` → missing library linkage.  Verify `compile.sh` flags.

## Scan

Run the checker on before_commit (→ N_buggy) and after_commit (→ N_patched):

```bash
CHECKER_NAME=<name from addChecker<>> bash scan/scan_before.sh
CHECKER_NAME=<name> bash scan/scan_after.sh
```

Or use the MCP `scan` tool: `scan(checker_name="<name>")`.

**Interpreting results:**

| Result | Meaning |
|--------|---------|
| `COMPILE_ERROR` | clang could not parse the file — missing headers or invalid flags.  **Warning counts are unreliable.**  Check stderr for the specific error. |
| N_buggy > N_patched AND N_patched < 50 | ✅ VALID — the checker distinguishes buggy from patched code. |
| N_buggy == 0 AND N_patched == 0 | Checker did not fire at all — the callback may not match the target function.  Re-examine the code path. |
| N_buggy == N_patched > 0 | Checker fires on both sides — too generic, not specific to this bug. |

If scan scripts are missing, follow the fallback instructions in `references/` (locate source → borrow flags → manual clang).

## Refine (if needed)

If `total_reports < 20`: status = **plausible** — done, write summary.json.

If `total_reports >= 20`: sample 5 reports (`TRIAGE_SAMPLE_SIZE`).

For each sampled report:
1. Read the source at the warning location.
2. Determine: **True Positive** (the checker found the bug pattern) or **False Positive** (unrelated code matched).
3. If FPs found: add constraints to checker.cpp (narrow the matching), re-compile, re-scan.  Max 3 refinement iterations.

If FP rate ≤ 1/5 after refinement: status = **plausible**.  Otherwise: status = **valid_only**.
