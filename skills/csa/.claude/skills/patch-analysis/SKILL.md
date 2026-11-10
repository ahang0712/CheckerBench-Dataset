---
name: patch-analysis
description: Use when the patch touches multiple functions or the bug pattern is not obvious from the diff — classify changes, trace data flow across function boundaries, infer the invariant being enforced by the fix, and map fix patterns to CSA detection strategies.
---

# Advanced Patch Diff Analysis

## Step 1: Classify the Change

Read the full diff. For each changed function, identify:

- **Added lines (+)** → what the fix introduces (check, cleanup, logic change)
- **Removed lines (-)** → what was wrong (missing check, missing cleanup, wrong logic)
- **Context lines (no prefix)** → the surrounding code that helps understand intent

## Step 2: Multi-Function Tracing

When the patch spans multiple functions:

```
git show <after_commit>     → the fixed version
git show <before_commit>    → the buggy version
```

1. Identify the **primary** function (where the fix is applied)
2. Identify **caller** functions (who calls the primary function)
3. Identify **callee** functions (what the primary function calls)
4. Trace data flow across function boundaries

## Step 3: Implied Invariants

From the fix, reverse-engineer the invariant being enforced:

| Fix pattern | Implied invariant |
|-------------|-------------------|
| Added `if (x < 0) return -1` | x must be non-negative |
| Added `free(ptr)` before return | all allocated resources must be freed |
| Added `if (!ptr) goto err` | ptr must not be NULL before use |
| Changed `a+b` to `checked_add(a,b)` | arithmetic must not overflow |

## Step 4: Map to Detection Strategy

| Invariant type | Detection approach |
|---------------|-------------------|
| Value range constraint | Track symbolic value, check at use point |
| Resource cleanup | Track allocation state, check at function exit |
| Null check requirement | Track pointer source, check before dereference |
| Overflow prevention | Track arithmetic operands, check range before operation |
