# Diagnosing Unclassified CVEs (NVD-CWE-noinfo / NVD-CWE-Other)

> The CWE label for this CVE pair lacks a clear classification. This file does
> NOT provide vulnerability-pattern priors. Instead, it guides you to infer the
> likely vulnerability type from the patch.diff itself, so you can subsequently
> consult the relevant CWE skill card in references/cwe_cards/ (if available).

## 1. Checklist: Inferring Vulnerability Type from Diff Changes

Examine patch.diff for the following patterns. Each match suggests a likely
CWE category — check references/cwe_cards/ for the corresponding card:

- **New bounds checks** (e.g., newly added `if (idx >= len)` or `if (offset > size)`
  guards) → Likely **CWE-787/CWE-125** (out-of-bounds write/read)
- **New null-pointer checks** (e.g., newly added `if (!ptr) return -EINVAL` or
  `if (obj == NULL) goto err` guards) → Likely **CWE-476** (NULL pointer dereference)
- **Adjusted free/release position or newly added pointer invalidation** (e.g.,
  `free(ptr)` moved later, or `ptr = NULL` added after free) → Likely
  **CWE-416** (use-after-free) or **CWE-415** (double-free). Check the
  direction of the change to distinguish.
- **New overflow checks before arithmetic** (e.g., newly added
  `if (a > SIZE_MAX / b)` before multiplication) → Likely **CWE-190**
  (integer overflow/wraparound)
- **Adjusted lock acquire/release ordering or new lock guards** (e.g.,
  repositioned `mutex_lock`/`mutex_unlock` calls, or newly added `spin_lock`
  around a block) → Likely **CWE-362/CWE-667** (race condition / improper locking)
- **Modified memory allocation size calculation** (e.g., expression changed from
  `malloc(n * sizeof(T))` to `malloc(n * sizeof(T) + extra)`, or `calloc` replacing
  `malloc`) → Likely **CWE-131** (incorrect buffer size) or
  **CWE-122/CWE-121** (heap/stack buffer overflow)
- **None of the above** → The vulnerability may not belong to the typical
  memory-safety category. Analyze based on the commit message and function
  semantics independently. Do NOT force-fit a CWE pattern.

## 2. Next Steps After Inference

After completing the checklist above, if a high-confidence category is inferred,
check references/cwe_cards/ for the corresponding card(s). Even if the specific
card was not automatically copied (the `NVD-CWE-noinfo`/`NVD-CWE-Other` label
does not trigger regular CWE index lookup), you may still apply the general
pattern knowledge from this inference.

In summary.json, do NOT report the inferred type as a "confirmed CWE label."
Instead, note it as "CWE category inferred from diff patterns" and preserve
the original label's uncertainty.

## 3. Prohibited Actions

Do NOT lower analysis rigor or skip before/after validation steps simply because
CWE priors are unavailable. This file supplements missing priors — it does not
relax the 7-stage synthesis workflow's verification requirements.
