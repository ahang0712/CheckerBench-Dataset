# CWE-401: Missing Release of Memory (Memory Leak)
> Tier: 1 | Variant: kernel (Linux kernel & similar OS-level projects)
> Global samples: 116 | Variant samples: 57 | Source projects: 22

## 1. Typical Code Patterns

CWE-401 in kernel-space code manifests when dynamically allocated memory is not freed on every possible execution path after its last point of use. Unlike userspace leaks which are eventually reclaimed at process exit, kernel memory leaks are permanent system-wide resource drains that degrade performance and can trigger OOM conditions.

**Pattern 1: Allocation followed by guarded failure paths.**  
A `kmalloc()`, `kzalloc()`, or `alloc_netdev()` is called early in a function; subsequent error checks, validation branches, or early-return guards skip the matching `kfree()`, `kfree_skb()`, or similar deallocation. The leak typically lies on an error-return path that was added later during maintenance without updating all deallocation sites. This is the single most common kernel memory leak pattern.

**Pattern 2: RCU-protected deferred deactivation.**  
Memory pointed to by RCU-protected pointers is removed from a data structure via `rcu_assign_pointer()` and scheduled for deferred reclamation with `call_rcu()` or `kfree_rcu()`, but the callback function either does not free the enclosing structure or frees it only under a subset of conditions. Because the reclamation callback is decoupled from the modification site, the leak is invisible during normal code review.

**Pattern 3: Reference-counted objects underflowing or never reaching zero.**  
Kernel objects (e.g., `struct kobject`, `struct file`, `struct net_device`, `struct inode`) are managed via `kref` / `refcount_t`. Every `kref_get()` must be balanced by a `kref_put()`. A common leak occurs when an asynchronous work queue, timer callback, or completion handler increases the reference count but the corresponding decrement is skipped on a completion or teardown path. The object's `release()` callback never fires.

**Pattern 4: Manual slab allocator resource tracking.**  
Memory allocated from per-subsystem slab caches (`kmem_cache_alloc()`) must be returned with `kmem_cache_free()`. This pattern appears in structures with embedded buffer pointers (e.g., `skb->data`, `bio->bi_io_vec`). When a structure is freed without first freeing its embedded allocations, or when an embedded allocation is overwritten without freeing the old buffer, a leak results.

**Pattern 5: Interrupt-context deferred work with allocation.**  
Allocations made in `tasklet`, softirq, or hardirq context that are passed to a bottom-half handler or work queue must be freed consistently in the deferred handler. If the handler is canceled, rescheduled, or reinitialized before consuming the allocation, the memory reference is lost without a matching free.

**Pattern 6: Scatter-gather DMA buffer mismanagement.**  
Memory allocated via `dma_alloc_coherent()`, `dma_map_single()`, or `skb_put()` followed by DMA mapping is leaked when the buffer is reused, the descriptor ring is reset, or a transmission error path frees the `sk_buff` but not the DMA-mapped payload region.

## 2. CSA Checker Architecture Recommendations

Base the checker on `ento::checker::CheckPointerEscape` combined with `PostStmt` / `PreStmt` callbacks on allocation and deallocation functions. Use `ento::checker::CheckEndAnalysis` as a terminal callback to report unreleased tracked symbols that are still live at function exit.

**Program state granularity:**  
Maintain a `REGISTER_TRAIT_WITH_PROGRAMSTATE` set of `SymbolRef` values representing allocated-but-not-yet-freed memory regions. Each allocation call (`kmalloc`, `kzalloc`, `kmem_cache_alloc`, etc.) adds the returned `SymbolRef` to the set; each deallocation call (`kfree`, `kfree_skb`, `kmem_cache_free`, etc.) removes it. At `EndAnalysis` (or `EndFunction` for callee-sensitivity), any surviving `SymbolRef` that is neither escaped via a global/pointer-parameter assignment nor intentionally retained (see patterns in `csa_checker_patterns.md`) is reported as a leak.

**Callback design:**  
- **`checkPostStmt`** on calls to known allocators — add the return-value `SymbolRef` to the tracked set.  
- **`checkPreStmt`** on calls to known deallocators — erase the operand `SymbolRef` from the tracked set.  
- **`checkPointerEscape`** (the `CheckPointerEscape` callback) — remove escaped `SymbolRef`s from the tracked set, because once a pointer is stored to global memory or passed to a foreign function, the analyzer cannot prove it will not be freed externally.  
- **`checkEndAnalysis`** — iterate remaining tracked `SymbolRef`s; filter out those still reachable from live stack variables or that were intentionally leaked (e.g., pool-allocated memory for the lifetime of a driver), then emit a `BuiltinBug` of type `MemoryLeak`.

**Cross-reference:** See patterns "LeakOnEarlyReturn", "LeakOnReassignment", and "EscapeViaCallback" in `csa_checker_patterns.md` for the canonical symbol-tracking approach. The heuristic for distinguishing "leaked" from "intentionally retained" memory is described under pattern "RetainedMemoryFilter" in the same reference.

**Kernel-specific allocator tables:**  
The checker must recognize the kernel allocator family (`kmalloc`, `kzalloc`, `kcalloc`, `kmem_cache_alloc`, `alloc_pages`, `__get_free_pages`, `dma_alloc_coherent`, `kobject_init`+`kobject_add`, `skb_alloc`, `netdev_alloc`) and their matching deallocators. These should be configurable via a `CheckerContext` lookup table rather than hardcoded, because kernel allocator names evolve across versions.

## 3. Known High-Frequency False Positive Patterns

**FP Pattern 1 — Pointer escape through struct member store.**  
Many kernel functions store an allocation into a field of a caller-provided structure (e.g., `dev->priv = alloc_netdev(...)`) but do not free it within the same function. The analyzer cannot see that the caller will free it later. **Mitigation:** In `checkPointerEscape`, when a pointer is stored into a heap-allocated struct that itself remains reachable, consider it escaped and remove from tracking. This is a first-class escape; no further heuristics needed.

**FP Pattern 2 — Allocators that never fail in practice.**  
`kmalloc(..., GFP_KERNEL)` is sometimes used with `__GFP_NOFAIL`, which guarantees success. The analyzer models a symbolic failure branch; if the user writes `ptr = kmalloc(...); if (!ptr) return -ENOMEM;` the leak is correctly caught only on the success path. However, if the user omits the NULL check (relying on nofail), the analyzer may still explore a NULL-return path and report a leak of a null dereference false positive on the dangling pointer. **Mitigation:** Recognize `__GFP_NOFAIL` flag via a `CallDescription` constraint; when present, treat the allocator as never returning NULL (skip the failure branch in the exploded graph).

**FP Pattern 3 — Pool- / slab-cache pre-allocated memory.**  
Drivers sometimes allocate a pool of descriptors or buffers at probe time and free the entire pool at remove time, never freeing individual elements. The analyzer sees each `kmem_cache_alloc` as unpaired at function exit. **Mitigation:** Add a suppression flag for "pool-allocated" memory: if a `SymbolRef` is stored into a driver-private pool (a fixed-size array or list head that persists until driver teardown), mark it as intentionally retained. This can be detected by checking whether the pointer is stored into a variable with global or file-static storage duration.

**FP Pattern 4 — RCU-protected deferred free patterns.**  
A common RCU idiom: `old = rcu_replace_pointer(ptr, new); kfree_rcu(old);`. The analyzer does not model `call_rcu` / `kfree_rcu` as a deallocation by default because the free is asynchronous. **Mitigation:** Register `kfree_rcu`, `call_rcu` with a `deallocator` callback that immediately removes the pointer from tracked memory, even though the actual reclamation is deferred. This is sound for leak-checking purposes: once passed to `call_rcu`, the caller no longer owns the memory.

**FP Pattern 5 — Allocations consumed by a different subsystem.**  
Functions like `skb_alloc()` followed by `netif_rx(skb)` pass ownership to the networking stack. The caller does not free the socket buffer. **Mitigation:** Recognize ownership-transfer functions (`netif_rx`, `dev_queue_xmit`, `__skb_queue_tail`, etc.) and consider the pointer escaped upon the call. This is a variant of the general "pass-to-subheap" escape pattern and should be configured via the same `CheckPointerEscape` mechanism.

**FP Pattern 6 — errno / label-based cleanup with a single deallocator.**  
Kernel code often uses a single `goto err_free;` label that calls `kfree()`. The analyzer may explore paths where control flow jumps to the label but the pointer has been reassigned in the meantime, creating a mismatch between what the symbol set tracks and what the code actually frees. **Mitigation:** Use `checkPreStmt` on `goto` targets to inspect the state at the merge point; if the pointer tracked by the symbol set differs from the pointer passed to `kfree()` at the label, invalidate the tracking for that symbol—this is a false path due to aliasing.

## 4. Generic Discriminant Features

The following features distinguish a textbook CWE-401 memory leak from a variant or hybrid CWE:

**Textbook CWE-401 features:**
- The allocation and deallocation are in the **same translation unit** (often the same function), connected by control flow that omits the free on one or more paths.
- The freed pointer is the **exact same address** as the allocated pointer (no reference-counting, no RCU indirection).
- The allocation is unconditional (e.g., `ptr = kmalloc(...)`) and the missing free is on an early-return error path.
- No aliasing: the pointer is stored to a local variable and never copied to a structure, global, or passed to another thread before the leak site.
- The fix is a single-line insertion of `kfree(ptr)` or `goto existing_free_label`.

**Variant / hybrid features (not pure CWE-401):**
- Reference-counting leaks (`kref_get` without matching `kref_put`) involve a counting discipline, not a missing deallocator call. These border on CWE-911 (Improper Update of Reference Count) or CWE-665 (Improper Initialization), but manifest as a memory leak. A pure CWE-401 card should flag these as border variants.
- Re-use-after-free leaks (e.g., overwriting a pointer without freeing the previous allocation) involve a race or ordering problem and often overlap with CWE-415 (Double Free) or CWE-416 (Use After Free).
- Pool lifetime mismatches (e.g., allocating from a slab cache at module init and freeing at module exit) are intentional by design and should not be reported as individual leaks; they are "resource management" choices, not bugs.
- DMA buffer leaks where the buffer is unmapped but not freed (or freed but not unmapped) often hybridize CWE-401 with CWE-244 (Improper Clearing of Heap Memory) or CWE-459 (Incomplete Cleanup).

**Quick judgment heuristic:** If the patch inserts `kfree()` or a `goto` to a cleanup label on a single error path where no such call existed before, it is a textbook CWE-401. If the patch adjusts reference counts, adds RCU callbacks, or restructures lifetime management across multiple functions, it is a variant or hybrid.

## 5. Repository Distribution Reference

This kernel variant card targets projects where CWE-401 manifests in OS-level or system-level C code. The repository distribution shows strong concentration in a few large C projects:

| Project | Samples | Profile |
|---------|---------|---------|
| `torvalds/linux` | 57 | Linux kernel — the dominant source of kernel-variant leaks. Patterns involve `kmalloc`, `kzalloc`, RCU, slab caches, network buffer allocators. |
| `gpac/gpac` | 25 | Multimedia framework. Leaks in filter graph allocation, buffer management, and parsing error paths. |
| `tbeu/matio` | 3 | MAT-File I/O library. Leaks in error-handling paths around `malloc`-family allocation in C utility code. |
| `muttmua/mutt` | 3 | Mail user agent. Leaks in configuration parsing and IMAP connection setup. |
| `isc-projects/bind9` | 3 | DNS server. Leaks in memory context management and task event allocation. |

The card is optimized for kernel-space projects (Linux, RTOS, firmware, hypervisors, device drivers). Userspace projects with similar heap management semantics may benefit from the companion userspace variant card, which focuses on `malloc`/`free` patterns, C++ `new`/`delete` mismatches, and STL container ownership issues.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: CWE-401 kernel variant (57 samples, 22 projects)*