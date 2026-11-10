# CWE-119: Improper Restriction of Operations within the Bounds of a Memory Buffer
> Tier: 1 — kernel variant
> Global samples: 248 | Variant samples: 29 | Source projects: 59

## 1. Typical Code Patterns

CWE-119 in kernel-space code manifests as a failure to restrict operations within the bounds of a memory buffer, leading to out-of-bounds reads or writes. Unlike userspace, kernel variants involve special memory regions (DMA rings, per-CPU buffers, slab-allocated objects, ioremap'd regions) where the consequences are memory corruption of shared kernel data structures.

**Pattern 1 — Pointer-arithmetic overflow in manual slab/slob/slub allocators.**  
Kernel memory allocators frequently compute offsets into slab caches manually. CWE-119 arises when an attacker-controlled allocation size or index is added to a base pointer without unsigned-integer overflow checks, causing the effective access range to wrap around and escape the allocated object. Typically appears in `kmem_cache_alloc` wrappers or custom object pools where `offset + length` overflows a `size_t`.

**Pattern 2 — Off-by-one in ring buffer / DMA descriptor handling.**  
Network drivers and block-layer code maintain producer/consumer indices into fixed-size descriptor rings. CWE-119 occurs when the producer index is incremented past the ring capacity before the modulo operation, or when a `head + count` comparison is written as `<=` instead of `<`, allowing one-element overflow. Commonly seen in `sk_buff` fragment handling, `dma_ring` push/pop helpers, and `io_uring` submission queue manipulation.

**Pattern 3 — Fixed-stack buffer under interrupt context.**  
Interrupt handlers (`top halves`, threaded IRQs) often declare small on-stack buffers for temporary data (e.g., copying register values, constructing short strings for `printk`). CWE-119 appears when `copy_from_user` or `memcpy_fromio` is called with a length argument derived from an untrusted hardware register or ioctl argument, while the stack buffer is dimensioned with a `#define` constant that is not enforced. The analyzer must track that the source length can exceed `sizeof(buf)` and that no implicit guard exists.

**Pattern 4 — RCU-protected read-side buffer reuse.**  
Under Read-Copy-Update (RCU), a reader may obtain a pointer to a buffer that is concurrently freed and reallocated by a writer. Although the race condition is a concurrency issue, the *memory safety* violation is CWE-119: the reader accesses memory beyond the original buffer's logical bounds because the reallocated object has a different (often smaller) size. This pattern is especially tricky because the compiler may hoist the size load across the RCU read lock boundary, making path-sensitive analysis challenging.

**Pattern 5 — `memcpy` / `copy_to_user` with under-validated `count` argument.**  
A ubiquitous kernel pattern: a function receives a user-supplied length, performs a partial bounds check (e.g., checking `len <= 4096` but forgetting `len > buf_remaining`), and passes the length to `memcpy`, `memmove`, `copy_{from,to}_user`, or `__put_user` with a destination buffer that is smaller than `len`. The check exists but is incomplete — the classic "validated but wrong" pattern.

## 2. CSA Checker Architecture Recommendations

Use **`ento::Checker<check::PreStmt<CallExpr>>`** for intercepting calls to memory-transfer primitives (`memcpy`, `copy_from_user`, `__put_user`, `dma_sync_single_for_cpu`) and **`check::PostStmt<CallExpr>`** to capture the return value of allocators (`kmalloc`, `kzalloc`, `kmem_cache_alloc`, `__get_free_pages`).

**ProgramState tracking:**

- Track **allocated region extent** via `SValBuilder::getConjuredSymbolVal` with symbolic size metadata attached to `RegionState`. Use `MemRegion::getExtent` as the baseline but augment it with a symbolic upper bound for custom slab caches where `kmem_cache` object sizes are not visible to the analyzer as constant expressions.
- Track **in-flight DMA descriptor ring positions** as a pair of `SymbolRef` integers (`head`, `tail`) plus a concrete ring-size constant. Model ring-buffer wraparound with a `SVal` constraint that `head - tail <= ring_size`.
- For RCU read-side critical sections, maintain a `ProgramState` tag that marks regions as "may-be-resized-after-grace-period". Any memory operation on such a region triggers a conservative out-of-bounds warning if the access offset cannot be proven within the *minimum* possible object size across grace periods.

**Cross-reference patterns in `csa_checker_patterns.md`:**

- **Pattern: Bounds-check-on-call** — Use `check::PreStmt<CallExpr>` to inspect the `count` argument against the destination `MemRegion` extent before the call is evaluated.
- **Pattern: Symbolic-range-enforcement** — Model `len <= max_sz` checks in terms of `SymIntExpr` constraints; false-positive suppression is achieved by checking that a `SymIntExpr` already has an `AssumeState` constraint of `<=` on the relevant bound.
- **Pattern: Cross-function-taint-propagation** — Use `check::EndAnalysisUnit` or inline-call summary to propagate taint from `copy_from_user` return (or unchecked `__user` pointer dereference) to the length argument of a subsequent `memcpy`.

## 3. Known High-Frequency False Positive Patterns

**FP1 — Statically-sized sub-object access.**  
The analyzer sees `obj->field` where `field` is a trailing flexible-array member (C99 `[]`) or a fixed-size array accessed at a constant offset that is obviously within `sizeof(struct T)`. Without flexible-array-union modeling, the analyzer reports an OOB. **Suppression:** Model flexible-array members as `MemRegion` with symbolic extent that is unbounded; treat the `sizeof` of the enclosing `struct` as a *minimum* extent, not the authoritative extent.

**FP2 — `sizeof`-mismatch due to cast to base type.**  
Functions that cast `void*` slab allocations to a struct type whose size differs from the allocated `kmem_cache` object size produce false positives because the cast region may appear smaller than the allocated region. **Suppression:** Check whether the `MemRegion` is a `TypedValueRegion` whose `castType` differs from the `AllocaRegion`'s `elementType`. If the allocation region's extent is larger, suppress and note the cast widening.

**FP3 — Inductive or loop-indexed writes.**  
The analyzer's default reasoning about loop trip counts is imprecise. A `for (i = 0; i < N; i++) buf[i] = ...` where `N` equals `sizeof(buf)` produces a false OOB because the path-sensitive analysis interprets each iteration as a separate branch without proving the loop invariant `i <= sizeof(buf)-1`. **Suppression:** Use `check::EndAnalysisUnit` to detect that an OOB symbol is the loop-induction variable constrained by `< sizeof(buf)`. Implement a loop-invariant summary that over-approximates total bytes written.

**FP4 — Pre-validated length via `min()` / `clamp()`.**  
Kernel code frequently writes `memcpy(dst, src, min(len, dst_sz))`. The analyzer may not inline `min()` definitions (especially if they are `__builtin_constant_p` dispatchers) and flags the raw `len` before the call, ignoring the truncation. **Suppression:** In `check::PreStmt<CallExpr>`, perform a forward data-flow scan for `min`/`clamp`/`min_t` results on the argument's definition chain. If the argument is produced by a `min`-like expression (`a < b ? a : b`), suppress.

**FP5 — IO memory-mapped register access.**  
Accesses to `ioremap`'d regions (`readb`, `writel`, etc.) with fixed offsets that exceed the virtual address range but are legal for the hardware device. Without a model of the BAR size, the analyzer flags all device-register offsets > 4 bytes. **Suppression:** Tag `MemoryRegion` that originates from `ioremap` or `of_iomap` as "MMIO device region" with symbolic extent that is *unknown* unless a stored `resource_size` symbol is constrained.

## 4. Generic Discriminant Features

**Textbook CWE-119 (pure boundary violation):**

The patch fixes a single missing or incorrect bounds check before a memory copy/index operation. Discriminant traits:

1. The vulnerable operation is a direct memory-access primitive (`memcpy`, `memmove`, `copy_to_user`, `[]` subscript on a stack or heap buffer).
2. The fix adds **one** bounds comparison (`if (offset + len > sz) return -EINVAL;`) or changes a comparison operator (`<=` → `<`).
3. No complex state machine, locking, or reference-counting logic is involved.
4. The overflowed buffer has a statically-known extent at the source site (stack array, slab object of known `kmem_cache` size, fixed DMA ring).

**Variant / hybrid CWE-119:**

The out-of-bounds access depends on a multi-step sequence:

- The length is attacker-controlled but the extent is computed dynamically (e.g., `skb->len` vs. `skb->data_len`), requiring the analyzer to track two symbolic values.
- The overflow happens through a use-after-free or RCU race (concurrency + OOB), which is CWE-119 *and* CWE-416 / CWE-1268 simultaneously.
- The fix touches synchronization primitives (RCU lock expansion, reference count adjustments) in addition to a bounds check.

**Non-CWE-119 (lookalike):**

- The patch moves `memcpy` arguments around without changing any bounds predicate — likely a code-cleanup or refactor.
- The patch only adds `WARN_ON` or `BUG_ON` assertions without preventing the overflow — diagnostic-only, not a safety fix.
- The overflow target is a `printf`-family format string where the "bounds" are semantic (string termination), not a numeric extent.

## 5. Repository Distribution Reference

This kernel variant of CWE-119 is derived from 29 samples across multiple operating-system-level projects. The top five source repositories are:

| Repository | Sample Count |
|---|---|
| FFmpeg_FFmpeg | 55 |
| torvalds_linux | 29 |
| gpac_gpac | 15 |
| file_file | 11 |
| ArtifexSoftware_mupdf | 11 |

These represent 59 total source projects. The **kernel variant** specifically applies to operating-system kernels, kernel modules, bootloaders, RTOS codebases, and any project that interacts with hardware memory-mapped regions, DMA rings, slab allocators, or interrupt-context stack buffers. Userspace multimedia libraries (FFmpeg, gpac, mupdf) are captured under the generic userspace CWE-119 card; their presence in this variant's distribution is a statistical artifact of the global sampling method and does not imply they exhibit kernel-specific patterns.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: CWE-119 kernel variant analysis (248 global samples, 29 variant samples)*