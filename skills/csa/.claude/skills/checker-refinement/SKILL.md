---
name: checker-refinement
description: Use when total_reports >= 20 or FP rate is too high — systematically reduce false positives by narrowing match conditions, adding path constraints, and suppressing known-safe patterns. Max 3 refinement iterations.
---

# Checker Refinement

## Narrowing Match Conditions

### 1. Add struct type discrimination
Instead of matching any struct field with name X, match only fields of struct Y:
```cpp
if (auto *RT = QualType->getAsStructureType()) {
    if (RT->getDecl()->getName() != "TargetStruct") return;
}
```

### 2. Filter by function context
Only fire within specific functions:
```cpp
const Decl *DC = CallerCtx->getCurrentAnalysisDeclContext()->getDecl();
if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
    if (!FD->getName().startswith("target_prefix")) return;
}
```

### 3. Add path constraints
Require that a guard condition does NOT exist:
```cpp
// Check if pointer was already checked for NULL
if (State->isNull(Val).isConstrainedTrue()) return;
```

## Common Refinement Patterns

| Issue | Fix |
|-------|-----|
| Fires on internal fields | Filter field name + struct type |
| Fires in test code | Filter file path (exclude "test/", "tests/") |
| Fires on already-guarded code | Check for preceding `if (ptr)` in same function |
| Fires across different call depths | Move from PreCall to ASTCodeBody for scope control |

## Iteration Cycle

1. Run scan → get N_buggy, N_patched
2. Sample warnings → classify TP/FP
3. If FP rate too high: apply refinement → re-compile → re-scan
4. Max 3 refinement iterations
