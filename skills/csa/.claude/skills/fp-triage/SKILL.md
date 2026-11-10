---
name: fp-triage
description: Use when total_reports >= 20 and you need to sample and triage warnings — classify each as TP or FP, identify common FP patterns (field heuristic too broad, struct-type mismatch, pre-existing guards), and decide whether to accept or refine.
---

# False Positive Triage

## Sampling

When total_reports >= 20, sample at least 5 warnings (see config.json `triage_sample_size`).

## Triage Process

For each sampled warning:
1. **Read source at warning location**: The warning includes `file:line:column`.
2. **Classify**: True Positive (TP) or False Positive (FP).
3. **For FPs**: Identify WHY the checker matched incorrectly.

## Common CSA FP Patterns

| Pattern | Cause | Fix |
|---------|-------|-----|
| Internal/private fields matched | Field name heuristic too broad | Add struct-type or naming filter |
| Intermediate chain dereference | Checker fires on every `->` in a chain | Filter to only the final dereference |
| Debug/assert code matched | Checker doesn't exclude test code | Add file path or function name filter |
| Different struct, same field name | Lacks struct type discrimination | Match enclosing struct type |
| Pre-existing checks not recognised | Checker doesn't see the guard | Check for `if (ptr)` or `if (ptr->field)` before deref |

## Decision

- FP rate ≤ 1/5 → status = "plausible", done
- FP rate > 1/5 → revise checker (narrow matching, add constraints), re-scan. Max 3 refinement iterations.
- After 3 iterations still high FP → status = "valid_only"
