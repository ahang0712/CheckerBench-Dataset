# CWE-401: Missing Release of Memory (Memory Leak)
> Tier: 1 — Userspace variant (libraries, applications, daemons)
> Global samples: 116 | Variant samples: 59 | Source projects: 22

## 1. Typical Code Patterns

CWE-401 commonly appears when dynamically allocated memory loses all reachable references before deallocation. The following are the key patterns found in C/C++ userspace code:

**Pattern A — Missing deallocation on some control-flow paths.** `malloc()`/`strdup()`/`new` is called but one or more exit paths skip `free()`/`delete`. Typically manifests as: early-return paths added above existing cleanup code; goto-based cleanup where a new error condition jumps to the wrong label; or C++ exception paths that unwind past the deallocation. This is the single most common pattern in patch-level fixes. The fix usually adds a `free()` call or restructures control flow to fall through to existing cleanup.

**Pattern B — Pointer overwrite before deallocation.** The pointer holding the allocation is reassigned (by a second allocation, buffer increment, or function return) without freeing the original block. Common in loop-based reallocation, string processing that advances a `char *`, and parser code that cycles through tokens. The original allocation becomes unreachable even though the program continues executing. Fixes typically save the original pointer in a temporary or restructure to use `realloc()` safely.

**Pattern C — Container contents never recursively freed.** A struct field, list node, or vector element holds allocated memory, but the container's cleanup routine does not traverse children. Frequent in C code with hand-rolled linked lists, hash tables, or growable arrays where only the outer container is freed. The leak materializes only when the container is destroyed. The fix adds traversal and per-element deallocation inside the container destructor.

**Pattern D — Returned ownership silently lost.** A function returns an allocated buffer documented as "caller frees", but callers ignore the return value, discard it, or have paths that skip `free()`. Common with POSIX functions (`strdup`, `getline`, `asprintf`, `realpath`) and in APIs with informal ownership contracts. The fix either stores the return value and frees it, or changes the API to avoid allocation entirely.

**Pattern E — C++ `new` without matching `delete`.** Using `new[]` without `delete[]` (or vice versa) or raw `new` in constructor/destructor pairs that diverge under exceptions. Pre-C++11 code without RAII idioms is especially susceptible. The fix replaces raw pointers with `std::unique_ptr`/`std::shared_ptr` or STL containers, or adds matching delete calls in destructors.

These five patterns cover the majority of memory leak fixes seen in userspace C/C++ projects and map naturally to distinct checker detection strategies.

## 2. CSA Checker Architecture Recommendations

**Base class:** `ento::Checker<check::PreCall, check::DeadSymbols, check::EndFunction>` — the standard ownership-tracking triple used by `MallocChecker` et al.

- **`check::PreCall`** — Intercept allocation functions (`malloc`, `calloc`, `realloc`, `strdup`, `new`, `new[]`) and mark the returned symbol as "allocated" in `ProgramState`. Intercept deallocation functions to transition to "freed"/"reallocated". Handle `realloc` specially: split state on success vs. failure branches. For C++ `new`, match `CXXNewExpr` to capture the allocated type and array bounds.
- **`check::DeadSymbols`** — Primary detection callback. When an "allocated" symbol loses all references (no remaining binding in the symbolic expression graph), emit a leak warning. Catches pointer-overwrite leaks (Pattern B) and return-value leaks where callers drop the pointer (Pattern D). This callback fires during garbage collection of dead symbols and provides the most reliable detection signal.
- **`check::EndFunction`** — For live pointer variables holding "allocated" symbols at function exit: walk all exit paths and report any path where memory is not freed before return. Catches Pattern A where the pointer remains live but no `free()` was called. Also handles conditional allocation.

**SymbolRef / ProgramState granularity:** Attach a `RegionState` trait (states `Allocated`, `Freed`, `Reallocated`, `Escaped`) keyed on `SymbolRef`. The pointer value is the tracked entity. Model ownership transfer through unknown callees via the escape mechanism — when a pointer is passed to an opaque function, conservatively mark it `Escaped` to suppress cascading false positives. See **pattern D in `csa_checker_patterns.md`** (escape via function calls and aliasing). Also cross-reference **pattern A** (explicit ownership tracking via `MallocChecker`-style region-state maps) and **pattern E** (C++ allocation wrappers via `CXXNewExpr` AST matching).

## 3. Known High-Frequency False Positive Patterns

The following false positives arise from inherent limitations of path-sensitive symbolic analysis rather than project-specific quirks. Each entry describes the root cause, explains why the analyzer gets confused, and gives a concrete mitigation strategy for the checker implementation. Addressing these five categories eliminates the vast majority of spurious memory-leak reports in practice.

**FP 1 — Cleanup via opaque functions.** Memory passed to an external callee (`pthread_create`, `atexit`, `register_cleanup`, callback registration, custom list-append APIs) cannot be verified by the analyzer. The pointer escapes the current translation unit and the analyzer cannot confirm eventual deallocation. **Mitigation:** Model well-known POSIX and library sinks explicitly in `check::PreCall`. For unknown callees, require a `#pragma` or function attribute annotation before emitting a warning on escaped pointers. The escape heuristics should be configurable per project — some projects pass ownership to threads, others do not.

**FP 2 — Conditional realloc idiom.** `p = realloc(p, n)` overwrites the pointer on failure, leaking the original block. Many codebases intentionally skip the NULL check in non-critical paths or rely on process-exit cleanup. **Mitigation:** In `check::PreCall` for `realloc`, branch the state: on success (non-NULL), original pointer is freed; on failure (NULL), original pointer remains allocated but has been overwritten — treat the region as escaped rather than leaked and emit only a low-severity note. Provide an optional suppression flag for projects that accept this pattern.

**FP 3 — Static/global pool structures.** Code that allocates a pool, cache, or arena once at startup and never explicitly frees it (relying on OS reclamation at process exit). The analyzer does not understand whole-program lifetime scopes. **Mitigation:** Suppress warnings when the only reference to an allocation is via a `static` or `global` pointer that remains live until program termination. Implement this check at `check::EndAnalysis` by inspecting symbol bindings in the global store. Allow project-specific annotations to tag intentional persistent allocations.

**FP 4 — Intentional leak on fatal error.** `abort()`/`exit()` called immediately after allocation without cleanup. The analyzer treats the allocation as live because the terminating call is not recognized as final. **Mitigation:** In `check::PreCall`, maintain a set of recognized `[[noreturn]]` functions (`abort`, `exit`, `_Exit`, `longjmp`, `quick_exit`) and flag the analysis state as "terminated" on those paths — do not emit leak reports for symbols that die only via termination. Also handle `__builtin_trap()` and compiler-specific noreturn intrinsics.

**FP 5 — Goto cleanup with CFG anomalies.** Old C codebases use a single `cleanup:` label at function exit. If a `goto cleanup` is placed after the cleanup block, or if the CFG merges paths in ways that obscure reachability, the analyzer may report a leak on a path that actually calls `free()`. **Mitigation:** Before reporting, validate concrete reachability of the `free()` call from each predecessor of the cleanup block. Use dominance analysis on the CFG to confirm that `free()` post-dominates the allocation point on all reported paths. If the CFG shows ambiguous predecessor ordering, suppress the warning.

## 4. Generic Discriminant Features

When triaging a patch flagged as CWE-401, the following features distinguish a textbook memory leak from a variant or hybrid bug:

**Textbook CWE-401 (pure memory leak) — all of:**
1. Single allocation point with clear ownership scope (no aliasing).
2. All return paths after allocation are reachable in the CFG.
3. At least one path exists where `free()` is not called between allocation and return.
4. No aliasing complexity — pointer held by a local variable, not stored in a struct field or global.
5. No callbacks, function pointers, or opaque callees that could plausibly take ownership.
6. The allocation is unconditional — not inside a conditional branch that might not execute.

**Variant / hybrid features (classify as CWE-401 plus secondary CWE):**
- **Double-free / use-after-free hybrid:** The patch moves `free()` to an earlier point and also adds a NULL check or guard after the free → CWE-401 + CWE-415 (double free) or CWE-416 (use after free). The secondary CWE dominates in severity and should be the primary classification.
- **Realloc wrap hybrid:** Fix changes `p = realloc(p, sz)` to the safe idiom `tmp = realloc(p, sz); if (!tmp) { free(p); return; } p = tmp;` → CWE-401 + CWE-252 (unchecked return value). The primary aspect is the leak-on-failure fix.
- **Ownership transfer leak:** Function switches from caller-owned buffers to returning allocated memory, but call sites are not updated → architectural leak, not a per-path analyzer finding. CWE-401 with design-scope note. The fix typically involves updating multiple call sites.
- **Destructor incomplete:** Container `->free()` method or destructor fails to traverse all child allocations → CWE-401 + CWE-762 (mismatched memory management) if mixed `malloc`/`new` sources are used within the same container. The fix adds a traversal loop or recursive deallocation.

## 5. Repository Distribution Reference

This userspace variant covers **22 source projects** with **59 sample pairs**. The distribution is heavily concentrated in a few large codebases, with the top two projects accounting for the majority of samples:

| Repository | Pairs | Project Type |
|---|---|---|
| torvalds_linux (userspace portions) | 57 | Kernel lib/, tools/, samples/ |
| gpac_gpac | 25 | Multimedia framework |
| tbeu_matio | 3 | MATLAB MAT-file I/O library |
| muttmua_mutt | 3 | Email client (Mutt) |
| isc-projects_bind9 | 3 | DNS server (BIND 9) |

The remaining 17 projects span CLI utilities, system daemons, database libraries, compression tools, and network servers — all typical userspace C/C++ with manual memory management via `malloc`/`free` or raw `new`/`delete` without widespread RAII adoption. This variant specifically targets userspace contexts; the kernel-space variant (inode caches, slab allocators, device drivers) is tracked separately.

When assessing triage priority, projects with higher pair counts (torvalds_linux, gpac_gpac) are more likely to produce textbook CWE-401 patterns, while projects with 1–3 pairs often involve ownership-transfer or destructor-incomplete hybrids.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*