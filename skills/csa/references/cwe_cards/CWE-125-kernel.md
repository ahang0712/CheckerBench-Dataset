# CWE-125: Out-of-bounds Read
> Tier: 1 | Variant: kernel / OS-level projects
> Global samples: 418 | Variant samples: 50 | Source projects: 102

## 1. Typical Code Patterns

CWE-125 manifests in kernel-space C/C++ code when a read operation accesses memory at an offset beyond the allocated or intended boundary of a buffer, array, or memory region. The vulnerability is triggered before any write — the read leaks data, crashes via an access-fault, or feeds an out-of-bounds value into a subsequent computation.

**Off-by-one array indexing.** The most common kernel pattern occurs when a loop iterates up to and including an element index equal to the allocation count (e.g., `for (i = 0; i <= num_entries; i++)` instead of `i < num_entries`). This typically appears in per-CPU arrays, interrupt-vector dispatch tables, and `nlattr`-style netlink attribute parsing where the upper bound check uses `>=` instead of `>`.

**Untrusted length from protocol headers.** In network-stack or storage-protocol parsers, a length field extracted from a packet or disk structure is used to index into a fixed-size buffer before validation (e.g., `struct iphdr` option parsing, `struct sg_io_hdr` payload access). The read overlaps into adjacent kernel heap objects or unmapped pages. This is especially dangerous in interrupt context where a fault cannot always be handled gracefully.

**Pointer arithmetic on slab-allocated objects.** Kernel slab caches (`kmem_cache`, `kmalloc`-family) return objects whose usable size may be rounded up by the allocator. Code that computes an offset into the object based on caller-supplied metadata can read past the logically valid region even if still within the physical slab — a "slim" OOB read that evades basic bounds checkers.

**RCU-protected stale pointer traversal.** Under RCU (Read-Copy-Update), a reader may dereference a pointer without locking. If a concurrent writer reallocates the pointed-to buffer to a smaller size, an RCU reader using the old size as a bound can read past the end of the new allocation. This is a concurrency-specific OOB read pattern unique to kernel code.

**Flexible-array and trailing-zero confusion.** Kernel structures often end with a flexible-array member (`struct foo { ...; char data[]; }`) or a fixed-size trailing array that is treated as larger by the driver. A read that indexes past the declared extent using an externally-supplied offset triggers CWE-125. The analyzer must distinguish "known trailing space due to `kzalloc` rounding" (intentional) from "no allocation excess" (vulnerability).

**Array decaying to pointer with lost length.** When a kernel function accepts a `char *` or `void *` parameter without a corresponding size argument, and the body reads from it using a loop bound computed from a separate global or heuristic, the read can exceed the actual allocation. This is common in legacy VFS path-walking and ioctl handlers where the buffer originates from user space via `copy_from_user` but is subsequently used by an in-kernel routine that discards the original allocation-size metadata.

## 2. CSA Checker Architecture Recommendations

**Checker base class and callbacks.** Use `ento::Checker<check::PreStmt<ArraySubscriptExpr>, check::PreStmt<UnaryOpOrTypeTraitExpr>, check::PreCall, check::EndAnalysis>`.

- `check::PreStmt<ArraySubscriptExpr>` — intercept every array-index expression. Compare the index symbol (or concrete value) against the known extent of the referenced memory region.
- `check::PreStmt<UnaryOpOrTypeTraitExpr>` — catch `sizeof` and `alignof` applied to zero-length or flexible trailing arrays; these are not OOB, but the result may be fed into an allocation that later enables an OOB read.
- `check::PreCall` — instrument known kernel allocators (`kmalloc`, `kzalloc`, `kmem_cache_alloc`, `vmalloc`, `__get_free_pages`) to record allocation size into `ProgramState`. Also instrument `copy_from_user` and `copy_to_user` to record user-buffer extents.
- `check::EndAnalysis` — flush any deferred OOB reports that depend on path-sensitive constraints resolved only at function exit (e.g., OOB in a loop whose bound is set by a `param` post-condition).

**State tracking granularity.** Maintain a `RegionBinding` in `ProgramState` that maps each `MemRegion` (or its `SymbolicRegion` base) to a `SymbolRef` representing the number of accessible bytes. The checker pattern that applies is **"buffer-size tracking via ProgramState"** (see pattern *BufferSizeTracking* in `csa_checker_patterns.md`). Use a `SValBuilder`-based approach to compute the access extent (`index * element_size + access_width`) and emit a `BuiltinBug` if the access range exceeds the tracked size.

**Symbolic index refinement.** For bounds checks that depend on symbolic values (e.g., `if (i < n) { read buf[i]; }`), rely on CSA's built-in path-splitting via `assumeInBoundDual` or equivalent range constraints. Do *not* attempt to reimplement a full constraint solver — delegate to `ConstraintManager`. However, add a **"loop widening heuristic"** (see pattern *LoopBoundWidening* in `csa_checker_patterns.md`) to avoid exponential state explosion: when the loop back-edge is taken and the index symbol is unconstrained (e.g., `while (i < n)` without a concrete bound), replace the symbolic index with a `ConcreteInt` at `n-1` to conservatively check the last iteration only.

**Suppression regions.** Kernel code makes heavy use of `__builtin_trap`, `BUG_ON`, and `WARN_ON` as side-effect-free guards. The checker should recognize these as `noreturn` or assertion contexts and suppress OOB reports when a prior `BUG_ON(index >= size)` constrains the path.

## 3. Known High-Frequency False Positive Patterns

**Flexible-array members at structure tail.** Any `struct` ending with `type data[];` or `type data[0];` is inherently unbounded from the analyzer's perspective because the declared size is zero. The analyzer only knows the allocation size of the enclosing region (e.g., from `kmalloc(sizeof(struct foo) + extra)`). False positives occur when the read index is valid w.r.t. the *allocation* but invalid w.r.t. the *declared type*. Mitigation: during `check::PreStmt<ArraySubscriptExpr>`, suppress the report if the subscripted region is a flexible-array member and the `MemRegion` containing it was heap-allocated with a recorded excess size.

**Sentinel-terminated vs. length-counted arrays.** Kernel code frequently uses sentinel-terminated arrays (e.g., NULL-terminated pointer tables in `struct file_operations` or `struct pci_device_id`). The analyzer cannot statically determine the array length. Reads via an index derived from a sentinel scan trigger OOB reports. Mitigation: recognize patterns where a `while (ptr[i])` or `for (; ptr[i]; i++)` loop is followed by a read of `ptr[i]` — suppress if the loop condition is a sentinel check on the same array.

**Struct padding and alignment holes.** The compiler may inject padding between struct members. A read like `*(u32 *)((u8 *)p + offsetof(struct x, member))` that targets a natural-alignment byte offset may be flagged as exceeding the struct's declared size if the checker computes the extent as the sum of member types rather than the `sizeof` the struct. Mitigation: use `ASTContext::getTypeInfo()` to get the actual `sizeof` and align of struct types when computing the accessible region extent.

**Unions with heterogeneously-sized members.** A `union { char small[4]; char large[64]; }` has type-based size 4 (max of members from the checker's perspective if using the declared type of the accessed member) but the actual allocated object is 64 bytes. When `large` is accessed via a union pointer, false positives can arise. Mitigation: when the accessed `MemRegion` is a union member, compute the extent as `sizeof` the entire union, not the individual member.

**Copied-from-user buffers with unknown upper bound.** After `copy_from_user(kbuf, user_ptr, count)`, the size of `kbuf` is known (the allocation size), but the kernel may only validate a subset of bytes (e.g., first 4 bytes for a type tag) and then read an offset that depends on user-supplied data. The analyzer cannot prove the OOB because the symbolic offset may be constrained by a prior user-data-dependent check — but often that check is insufficient. False negatives, not false positives, are the risk here. For false-positive suppression, if the read offset is guarded by a comparison against a value derived from the same user-data source, the checker should conservatively suppress the report and let a taint-analysis pass re-evaluate it (see pattern *DeferToTaintChecker* in `csa_checker_patterns.md`).

**VLA and alloca with computed size.** When a variable-length array or `__builtin_alloca` appears inside a loop and the allocated size decreases on each iteration, a later read using a stale bound from a prior iteration produces a false positive. The checker can suppress by tracking the allocation's `SymbolRef` and verifying it equals the bound symbol used for the index check.

## 4. Generic Discriminant Features

To quickly classify a patch or report as a "textbook" CWE-125 vs. a variant or hybrid, evaluate the following axes:

**Direct index vs. computed offset.** Textbook: an integer index `i` used directly in `buf[i]` where `i` is compared against a named constant or a clearly named bound variable (`size`, `len`, `count`). Variant/hybrid: the offset is the result of pointer arithmetic (`buf + a + b`) or comes from a chain of arithmetic operations on struct-member offsets; this often overlaps with CWE-468 (Incorrect Pointer Scaling) or CWE-823 (Use of Out-of-range Pointer Offset).

**Single access vs. loop access.** Textbook: a single array read or a straightforward loop `for (i = 0; i < n; i++)`. Hybrid: a nested loop where the inner loop index depends on the outer loop variable, or a loop that modifies the buffer pointer and the count separately — this overlaps with CWE-467 (Use of `sizeof` on a Pointer Type) when the count is computed as `sizeof(ptr)` instead of `sizeof(*ptr) * n`.

**Contiguous buffer vs. linked data structure.** Textbook: a flat `kmalloc` buffer, `char array[]`, or `u32 array[]`. Variant: a read that traverses a linked list, an `iovec`/`kvec` scatterlist, or an `xarray`-style radix tree. These require reasoning about multi-step indirection and are more accurately classified as CWE-822 (Untrusted Pointer Dereference) or CWE-476 (NULL Pointer Dereference) with secondary OOB read.

**Bound variable origin.** Textbook: the bound is a constant, a function parameter, or a local variable set at function entry. Hybrid: the bound is a global variable, an RCU-protected value, or a value written by a hardware device (MMIO read). These cases often involve memory ordering or concurrency issues (CWE-362 / CWE-667) that cause the bound to change between the check and the read.

**Read then use vs. read then discard.** Textbook: the OOB value is used in a subsequent computation (e.g., copied back to user space, used to compute another index). If the OOB read's result is only compared or used in a branch that does not affect externally observable state, the report may still be valid for CWE-125, but it is less likely to be exploitable (information leak via side-channel). A patch that eliminates the OOB read but keeps the same control flow is a weaker fix.

## 5. Repository Distribution Reference

This kernel / OS-level variant covers projects where CWE-125 occurs in memory-unsafe system software with manual memory management, interrupt context, and limited fault recovery. The top source projects by paired fix-and-bug commit count are:

| Repository | Pairs |
|---|---|
| torvalds/linux | 50 |
| FreeRDP/FreeRDP | 37 |
| gpac/gpac | 29 |
| FFmpeg/FFmpeg | 27 |
| libarchive/libarchive | 19 |
| virgl/virglrenderer | 17 |
| qemu/qemu | 16 |
| openssl/openssl | 15 |
| android/kernel_common | 13 |
| zephyrproject-rtos/zephyr | 12 |

The **torvalds/linux** corpus anchors the kernel variant; its OOB reads span networking (netfilter, tipc, nl80211), filesystem drivers (ext4, btrfs, f2fs), sound subsystem (ALSA), and GPU/DRM drivers. The remaining projects in the top 10 are OS-level or embedded userspace daemons that operate on raw binary protocols, compressed archive formats, or virtualized hardware interfaces — code that shares the same memory-model hazards as kernel code.