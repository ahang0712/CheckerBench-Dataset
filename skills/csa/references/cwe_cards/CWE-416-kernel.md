# CWE-416: Use After Free
> Tier: 1 — kernel variant
> Global samples: 340 | Variant samples: 238 | Source projects: 38

## 1. Typical Code Patterns

Use After Free in kernel-space code commonly appears when a heap-allocated object (kmalloc, slab cache, vmalloc) is freed via kfree(), kmem_cache_free(), or a refcount-dropping function, and a subsequent code path dereferences a pointer to the same memory before the pointer is either NULLed or falls out of scope. Three kernel-specific patterns dominate:

- **RCU-protected object reuse:** A reader obtains an RCU-protected pointer via rcu_dereference(), the updater calls synchronize_rcu() / call_rcu() and frees the object, but the reader continues accessing through the stale pointer without re-checking RCU validity. The gap between rcu_read_lock() / rcu_read_unlock() does not guarantee the object remains allocated if the updater does not wait for all readers.

- **Slab cache UAF with use-after-free in interrupt context:** An object is freed from a slab cache (kmem_cache_alloc / kmem_cache_free) while a deferred interrupt handler or tasklet still holds a reference to it. Because softirq / hardirq can preempt the freeing path, the handler may dereference the object after the slab has been recycled.

- **Refcount underflow / put-before-use:** A reference-counted object (kref, struct kobject) is released one too many times. One caller drops the count to zero (triggering the destructor / kfree), while another caller still holds a pointer and accesses fields after the refcount dropped. Typically manifests as "put" racing with a "get" that was never taken, or a double-put in an error path.

In all three cases the freed memory may be reallocated to a different object (same slab cache or a different one), so the dereference does not necessarily crash — it silently operates on corrupted data, making the bug hard to reproduce.

## 2. CSA Checker Architecture Recommendations

Use `ento::Checker<check::PreCall, check::PostCall, check::DeadSymbols, check::PointerEscape>` as the base class. This gives callbacks before and after every function call, plus visibility when symbols escape or become dead.

- **Tracking granularity:** Attach to `SymbolRef` (the allocated region) as the identity of a heap object. Track two orthogonal pieces of state in `ProgramState`: (a) the allocation site (a `SymbolRef` representing the return value of kmalloc / kmem_cache_alloc), and (b) a "freed" flag stored in a `RegionState` trait. When `kfree()` is called in the post-call hook, mark the region as `Freed`.

- **Refcount awareness:** For kref / kobject patterns, track the refcount value as an `SVal` bound to a field region. When a put-function decrements it to zero, mark the containing region as `Freed` — but only if the analyzer can prove the decrement reaches zero. Concretize this via a `SymbolicRef` constraint on the counter field.

- **RCU region modeling:** Use a separate `RCUState` trait on the `ProgramState` to track whether the analysis is inside `rcu_read_lock() .. rcu_read_unlock()`. If a freed region is dereferenced inside such a critical section and the free occurred before the lock (or in a different thread), emit a UAF warning. See pattern **RCU-use-after-free** in `csa_checker_patterns.md` for state merging across the unlock barrier.

- **Interrupt / softirq context:** Model `in_interrupt()` as a path-sensitive constraint. If a free happens in a context where `in_interrupt()` is false, but a dereference of the same symbol follows in a path where `in_interrupt()` is true (or after a `local_irq_restore` that enables the handler), flag the issue. See pattern **context-switch-dangling** in `csa_checker_patterns.md`.

- **DeadSymbols callback:** Clean up stale freed-region mappings to avoid state explosion. The `check::DeadSymbols` callback should remove `Freed` markings for symbols that are no longer live in the exploded graph.

## 3. Known High-Frequency False Positive Patterns

Derived from static-analysis first principles for pointer tracking in C:

- **Placement re-allocation after free (slab recycling):** The analyzer sees `kfree(obj); obj = kmem_cache_alloc(...); obj->field = val;` and assumes the second allocation is unrelated because it has a new `SymbolRef`. However, if the code uses `obj` (the same pointer variable) to store the new allocation, the analyzer must invalidate the old `Freed`-region binding for that `SymbolRef` when the new allocation occurs. Failure to do so produces false positive "use after free" on the second assignment. **Suppression:** In `check::PreCall` for `kmem_cache_alloc`, remove the `Freed` trait for any region that was associated with the pointer expression being reassigned.

- **Error-path double-free with goto cleanup:** Many kernel functions follow the pattern `goto err_free;` where multiple error labels each free the same partially-initialized object. The path-sensitive analyzer may explore one path that frees at `err_free_1` and then merges with another path that frees at `err_free_2`, hitting a "double free" (a subtype of UAF) even though at runtime only one label executes. **Suppression:** Implement a deferred-free set — track which regions have been freed along the *current* exploded path only, and suppress warnings when the free symbol is a `SymbolicRef` constrained by the same error-path condition.

- **List-head / embedded-object aliasing:** A `struct` containing a `list_head` is freed, but another path walks the list and calls `list_for_each_entry_safe` on the (now freed) list head. Because the list head is embedded in the freed object, the analyzer may report a UAF on `pos->member` inside the loop body. However, `list_for_each_entry_safe` is designed to handle removal during traversal — it loads `next` before the object is freed. **Suppression:** Model `list_for_each_entry_safe` semantics explicitly in a `check::PreCall` / `check::PostCall` pair: copy the `next` pointer to a temporary before the body executes, so the freed region is not dereferenced by the macro expansion.

- **RCU grace-period not modeled:** The analyzer cannot prove that `call_rcu` delays the free until all readers have exited, so every access to a pointer passed to `call_rcu` triggers a false UAF. **Suppression:** In `check::PreCall`, when the callee is `call_rcu` or `kfree_rcu`, mark the argument as "RCU-pending" rather than immediately `Freed`. Only transition to `Freed` after a simulated `synchronize_rcu()` or after the RCU callback is symbolically invoked. See pattern **rcu-delay-model** in `csa_checker_patterns.md`.

## 4. Generic Discriminant Features

A patch is a **textbook example of CWE-416 (kernel variant)** if it meets all of:

1. **Pointer obtained from a slab/heap allocator** — the allocation site is `kmalloc`, `kzalloc`, `kmem_cache_alloc`, `vmalloc`, or a `kref`-based constructor. The free site is `kfree`, `kmem_cache_free`, `kfree_skb`, or a `kref_put` / `kobject_put` call that triggers the release function.
2. **No intervening NULL-store** — the pointer variable is not set to NULL between the free and the use. If `ptr = NULL` appears, the bug is a "use after free with NULL check" variant (discriminant: still a UAF if the NULL store is conditional).
3. **Use is a read or write through the pointer** — the access is `ptr->field`, `ptr[i]`, `*ptr`, or passing `ptr` to a function that dereferences it. Simple pointer equality comparisons after free (e.g., `if (ptr == other)`) are not UAF unless they lead to dereference.

A patch is a **variant or hybrid** if:

- The freed object is a stack variable (CWE-466: Return of Stack Variable Address) — discriminant: allocation site is `alloca` or local array, not slab.
- The freed object is a vmalloc'd buffer and the use is a DMA/IOMMU operation (hybrid with CWE-119: Buffer Overflow).
- The free is via `kfree_rcu` or `call_rcu` and the use is outside any RCU critical section (hybrid with CWE-362: Race Condition — the bug depends on atomicity of the RCU grace period, not merely ordering).
- The pointer is a `struct file *` / `struct inode *` with reference-count semantics (hybrid with CWE-476: NULL Pointer Dereference if `fput` is called without checking the pointer first).

## 5. Repository Distribution Reference

This kernel variant of CWE-416 is drawn primarily from operating-system kernel code, bootloaders, and system-level libraries. The top repositories are:

| Repository | Sample count |
|---|---|
| torvalds/linux | 238 |
| gpac/gpac | 25 |
| LibVNC/libvncserver | 7 |
| GNOME/libxml2 | 5 |
| vcftools/vcftools | 4 |

The vast majority (238 of 238 kernel-variant samples) come from the Linux kernel, reflecting the complexity of manual memory management, RCU protection, and interrupt-context safety in OS code. The remaining projects are C/C++ userspace libraries that happen to use kernel-style manual allocation patterns (e.g., custom slab allocators, reference-counted objects). For pure userspace CWE-416 samples (not in this variant), refer to the separate userspace knowledge card.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*