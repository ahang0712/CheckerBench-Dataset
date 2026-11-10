# CWE-787: Out-of-bounds Write
> Tier: 1 — kernel variant
> Global samples: 449 | Variant samples: 84 | Source projects: 119

## 1. Typical Code Patterns

Out-of-bounds writes in kernel-space C/C++ code commonly appear when a buffer index or pointer arithmetic expression exceeds the allocated region due to an incorrect bounds check, an unsanitized attacker-controlled offset, or a mismatch between allocation size and access range. The kernel variant of CWE-787 manifests through the following recurring patterns:

- **Array index underflow/overflow in interrupt or tasklet context:** A per-CPU buffer is indexed by a value derived from hardware registers or shared state without verifying it falls within `[0, size)`. Because interrupt handlers cannot sleep, developers sometimes elide bounds checks under the assumption that "hardware always produces valid values"; this assumption is violated when a faulting device or driver state corruption produces an out-of-range index.

- **Ring buffer / circular queue writes in RCU-protected data structures:** A writer advances a head pointer past the tail without atomic comparison, or uses a `head + count` expression that wraps around a `UINT_MAX` boundary in a `kmalloc`'d ring. The write then lands at an unexpected offset inside (or beyond) the slab object. Classic example: `buf[head++ & mask] = val` where `mask` is stale after a resize, or `head` is not masked at all.

- **Manual slab allocator / object pool overruns:** Custom allocators using `kmem_cache_alloc` followed by placement-new or manual construction may compute offsets incorrectly when the object contains a flexible-array member (`struct foo { int hdr; char data[]; }`). The `data` region is written past `sizeof(struct foo)` because the allocation computed `sizeof(struct foo) + payload_len` but the write used a different size expression.

- **Offset confusion in `copy_to_user` / `copy_from_user` with compound objects:** A driver copies a fixed-size structure to user space, but a member field (e.g., a length embedded in the structure) is used as the byte count without validation. If the user-supplied length exceeds the kernel buffer, an out-of-bounds write occurs inside the kernel memory region during the copy.

- **Pointer arithmetic on `__iomem` or `__percpu` addresses:** A driver dereferences `base + reg_offset` where `reg_offset` is read from device MMIO space or a shared register without sanity checking against the resource size. The write hits adjacent MMIO regions or, on non-MMIO paths, overflows a `ioremap`'d buffer.

- **`memcpy` / `memmove` with attacker-controlled length in netlink or ioctl handler:** A `len` field from a user-provided message is used directly in `memcpy(dst, src, len)` after a partial validation that checks `len > 0` but misses an upper-bound comparison against the destination buffer capacity.

- **`snprintf` / `scnprintf` misuse in `procfs` or `sysfs` show handlers:** The return value (which represents how many bytes *would have* been written, not how many were written) is accumulated into an offset without checking against the buffer size, leading to an out-of-bounds write on subsequent `snprintf` calls into the same buffer.

## 2. CSA Checker Architecture Recommendations

- **Checker base class:** `ento::checker::CheckPointerEscape` combined with `ento::checker::CheckPreCall`/`ento::checker::CheckPostCall`. Because OOB writes span both allocation sites and access sites, a path-sensitive checker should monitor the lifetime of buffer regions across calls.

- **Core callback:** `checkPreCall` for standard library functions (`memcpy`, `memset`, `memmove`, `copy_to_user`, `copy_from_user`, `snprintf`) and array subscript operators. Register a `checkPostCall` to record allocation-size facts (`alloc_size` ∈ `ProgramState`).

- **SymbolRef / ProgramState granularity:**
  - Track each allocated region as a `SymbolicRegion` with a `RegionRawSymbol` representing its byte extent.
  - For array subscripts, bind the index expression to a `NonLoc` concrete-int or `SymbolRef` in the state. At each write access the checker should compare `(offset + write_size)` against the region extent symbol.
  - Maintain a `ProgramState` trait `RegionSizeMap` that maps `SymbolicRegion → SVal` (the byte capacity). Update this map at allocation points (`malloc`, `kmalloc`, `kcalloc`, `kmem_cache_alloc`, variable-length arrays) and invalidate on `realloc` or `krealloc`.
  - Use `checkPointerEscape` to remove regions from the size map when their address escapes to opaque callbacks (e.g., workqueue handlers, RCU callbacks), preventing stale-size false negatives.

- **Cross-reference patterns in `csa_checker_patterns.md`:**
  - **Pattern A** ("Region extent tracking via ProgramState trait") — for recording allocation byte counts.
  - **Pattern C** ("Pre/Post call instrumentation for memory functions") — for intercepting `memcpy`-style calls.
  - **Pattern E** ("Symbolic index upper-bound constraint") — for reasoning about `index < size` under path-sensitive assumptions.
  - **Pattern G** ("Pointer-arithmetic-to-region-offset conversion") — for reducing `(base + off)` to a byte-offset relative to the region base.

- **Constraint merging:** The checker should leverage the existing `SValBuilder` and `RangeConstraintManager` to test whether `offset` can exceed `extent` on the current symbolic path. If the index is fully constrained below `extent`, suppress the warning; if unconstrained or partly overlapping, emit.

## 3. Known High-Frequency False Positive Patterns

The following patterns inherently confuse path-sensitive static analysis and are frequent sources of false positives for OOB-write checkers:

- **Guard variable idiom:** Code that uses a runtime variable (`buf_initialized`) or `static_branch` to protect access after lazy allocation. The analyzer cannot always prove that the guard implies the buffer size, so it reports every write as potentially OOB. Suppression: when the region's `SymbolicRegion` is associated with a guard `SymbolRef` that implies non-null and adequate size, the checker can mark the region as "verified" on paths where the guard is true.

- **Sentinel-terminated writes in fixed-size arrays:** A loop writes into `buf[i]` until a sentinel value (e.g., `'\0'` or `-1`) is seen, but the loop bound is not an explicit `i < size` comparison. The analyzer conservatively assumes the loop may run past the buffer. Suppression: check whether the loop exit condition depends on a sentinel that is guaranteed to appear within the allocated region (requires loop-invariant analysis; if not available, suppress only when the sentinel is at a known-offset symbol).

- **Flexible-array-member access through the containing struct pointer:** Code accesses `p->data[i]` where `p` comes from `kmalloc(sizeof(struct foo) + extra)` and `extra` is symbolic. The analyzer sees `p` as a `SymbolicRegion` of size `sizeof(struct foo) + extra` and treats `data[i]` as offset `offsetof(struct foo, data) + i`. If `i` is symbolic (not concretized), the checker may report OOB even when `i < extra` holds. Suppression: teach the checker to recognize that flexible array members may be accessed with a symbolic offset that is bounded by the allocation-size symbol minus the fixed-struct size.

- **`__int128` / vector extension access on under-aligned buffers:** The analyzer's memory model may widen the byte extent of a write due to type-based alignment assumptions, causing the core to think the write extends beyond `extent`. Suppression: when the destination region's element type matches the access type and the offset is element-aligned, recompute the byte extent from the element count rather than the raw allocation size.

- **`union`-based type punning with overlapping fields:** A write to one union member may appear to overflow into adjacent memory when the analyzer models the region layout by the largest member, but the actual code writes only to the smaller member. Suppression: if all symbolic paths through the union access only the member type whose size is ≤ region extent, the warning can be downgraded.

- **Dead code in `switch` / `ifdef`-guarded paths:** The analyzer sees all branches of `#ifdef CONFIG_FOO` as live (it processes one TU at a time). A write that is impossible in the current configuration may still be reported. Suppression: rely on macro-definition tracking via `Preprocessor` and suppress when the branch condition depends on a macro not defined in the translation unit.

## 4. Generic Discriminant Features

The following features help a code reviewer (or an automated classifier) distinguish a textbook CWE-787 kernel OOB write from a variant or hybrid weakness:

- **Boundary-crossing mechanism:** Textbook OOB write = the index or pointer exceeds the *upper* bound of the allocated region by a positive delta. Variants include underflow (negative index, wrapping below zero), intra-object overflow (writing past one member into another field of the same struct — often closer to CWE-822 or CWE-468), and out-of-bounds *read* (CWE-125).

- **Allocation-to-access distance:** In textbook cases the allocation and the OOB access site are in the same function or in a clear caller/callee pair with explicit size arguments. Hybrid cases involve:
  - The buffer was allocated in a different subsystem and passed through multiple opaque layers (closer to CWE-119 or CWE-20).
  - The size was correct at allocation but the buffer was subsequently shrunk via `krealloc` or mremap (CWE-131 + CWE-787 hybrid).

- **Control of the offending value:** Textbook kernel OOB = the index/length is derived from a user-space ioctl argument, a netlink attribute, or an MMIO register read (i.e., attacker-influenced). If the offending value is always a compile-time constant (e.g., `buf[100]` with a 50-byte allocation), the bug is more likely an integer truncation or an off-by-one (CWE-193) rather than an OOB write in the traditional sense.

- **Operation type:** The write path is typically a byte-level copy (`memcpy`, `copy_from_user`, `memset`) or a type-level assignment (`array[i] = val`). If the operation is a `kmem_cache_free` or `kfree` with an offset pointer, it is CWE-590 (free of memory not on the heap) or CWE-761 (free of pointer different from allocation), not CWE-787.

- **Concurrency context:** In the kernel variant, the write often occurs inside a spinlock-held critical section, an RCU read-side critical section, or an interrupt handler. If the same write is reachable only from process context without concurrency, it is more likely a userspace-style CWE-787 that happens to be in kernel code — the discriminant is whether the root cause involves shared-state race conditions (CWE-362 + CWE-787 hybrid).

## 5. Repository Distribution Reference

This kernel variant describes CWE-787 as it appears in operating system kernels, hypervisors, and low-level OS-level projects. The top contributing repositories are:

| Repository | Samples |
|---|---|
| torvalds/linux | 84 |
| gpac/gpac | 51 |
| radareorg/radare2 | 15 |
| jerryscript-project/jerryscript | 15 |
| FreeRDP/FreeRDP | 15 |

Projects in this variant are characterized by manual memory management (`kmalloc`/`kfree`), MMIO access, DMA buffer handling, RCU-protected shared data structures, and use of architecture-specific primitives such as `__iomem`, `__percpu`, `copy_to_user`, and `put_user`. The userspace variant (covering general application code with heap/stack buffers) is generated separately.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*