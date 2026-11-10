---
name: csa-search
description: Use instead of blind grep+Read cycles — scope-based search with source context embedded in results. Maps source→worktree, llvm→CSA headers, workdir→references.
---

# CSA Code Search

## Scope Mapping

| Scope | Directory | When to use |
|-------|-----------|-------------|
| `source` | `scan/tmp/wt_*/` | Searching CVE source code in worktrees |
| `llvm` | `/opt/llvm/include/clang/StaticAnalyzer/` | Searching CSA API headers |
| `workdir` | `/work/` | Searching reference files and scripts |

## Search Pattern

```bash
# Basic search
grep -rn "pattern" <directory>

# Filter by file extension
grep -rn --include="*.h" --include="*.c" "pattern" <directory>

# Find definitions (not just usage)
grep -rn "typedef.*target_type\|struct target_type" <directory>
```

## After grep, Read context

After finding matches, read 3 lines of context:
```bash
# Read 3 lines around line N
sed -n "$((N-3)),$((N+3))p" <file>
```

## LLVM header search shortcut

```bash
LLVM_HEADERS="/opt/llvm/include/clang/StaticAnalyzer"
# Find a class definition
grep -rn "class TargetClass" $LLVM_HEADERS
# Find a method signature
grep -rn "method_name" $LLVM_HEADERS
```
