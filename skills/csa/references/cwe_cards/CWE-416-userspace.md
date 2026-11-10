# CWE-416: Use After Free
> Tier: 1 — userspace variant (libraries, applications, daemons)
> Global samples: 340 | Variant samples: 102 | Source projects: 38

## 1. Typical Code Patterns

Use After Free (UAF) in userspace C/C++ code occurs when a heap-allocated object is accessed through a pointer after the memory has been returned to the allocator via `free()` (C) or `delete`/`operator delete` (C++). The dangling pointer is dereferenced (read, written, or passed to a function that dereferences it), producing undefined behavior that commonly leads to information disclosure or code execution.

**Pattern 1 — Direct free-and-use in straight-line code.** The simplest manifestation: `free(ptr); ... ptr->field = val;` or `free(ptr); ... memset(ptr, 0, sz);`. A pointer is freed and then, within the same function (or immediately after return), a now-invalid pointer is dereferenced without being reassigned or set to NULL.

**Pattern 2 — Double-clean with stale copy.** A resource is freed through one alias but a second alias survives. Commonly appears when a container (hash table, array list, tree) stores pointers to objects; one code path frees the object via a direct pointer while another code path later iterates the container and accesses the (now dangling) stored pointer. Also manifests in refcounted objects where an early `unref` drops the last reference and deallocates the object, but a pre-existing raw pointer is later dereferenced.

**Pattern 3 — Conditional or error-path free with subsequent normal-path use.** A function frees a buffer on an error branch (`if (err) { free(buf); return; }`) but the normal-path code continues to use the buffer after a goto or shared cleanup block that inconsistently handles the pointer. This is especially common in C projects using `goto cleanup` patterns where the cleanup block frees resources but the pointer is not NULLed, and a later line in the success path still references it.

**Pattern 4 — Realloc aliasing.** `buf = realloc(buf, new_size);` fails (returns NULL), leaking the original block and leaving `buf` pointing to freed memory. Subsequent accesses through `buf` constitute a UAF. In C++ this manifests when a `std::vector` or similar resizable buffer reallocates internally and external raw pointers or iterators into the old buffer are not invalidated.

**Pattern 5 — Object lifetime mismatch in callback/closure patterns.** A struct containing a function pointer and a `void *context` is registered as a callback. The context object is freed before the callback fires, so when the callback executes and dereferences its context argument, it reads freed memory. This pattern is endemic in asynchronous APIs, event loops, and plugin interfaces.

**Pattern 6 — C++ object slicing or virtual dispatch after destruction.** In C++ specifically, a base class destructor completes and the derived portion is deallocated, but a base class pointer is still used to call a virtual method. More commonly: `delete ptr; ptr->virtual_method();` where the vtable pointer is already part of the freed object. Also manifests when an object is destroyed while still bound to a callback (e.g., `std::shared_ptr` cycle break via `weak_ptr::lock` check omitted).

## 2. CSA Checker Architecture Recommendations

Implement this checker as a **`check::DeadSymbols` / `check::PreCall` / `check::PostCall` triple** built on **ProgramState** tracking that maps `SymbolRef` → liveness status (alive / freed / escaped). The recommended base class is `ento::Checker<ento::check::PreCall, ento::check::PostCall, ento::check::DeadSymbols>`.

- **PostCall callback** — intercept calls to `free()`, `realloc()`, `delete`, `delete[]`, `operator delete`, and C++ destructor end-of-life. Mark the associated `SymbolRef`'s memory region as **freed** in the program state. For `realloc`, mark the old region freed and register the new region as alive when the return value is non-null.
- **PreCall callback** — before any call, inspect each pointer argument. If the pointer bindings in the program state point to a freed region, emit a UAF warning. This catches UAF at function call boundaries (passing a dangling pointer to `memcpy`, `printf`, etc.).
- **DeadSymbols callback** — clean up the program state by removing liveness bindings for symbols that go out of scope, preventing state explosion.
- **Check::Location** (optional) — catch direct dereferences of freed pointers in expressions like `*ptr` or `ptr->field`. This fires before the load/store occurs.

**Granularity notes:** Track at the `SymbolRef` level, not `Stmt*` level. A single allocation site produces one symbol; all aliases derived from it (via pointer arithmetic, struct field access, casts) should resolve to the same base region symbol. Use `SymbolManager::getSymbolForRegion()` to canonicalize.

Cross-reference patterns in `references/csa_checker_patterns.md`:

- **Pattern A (PostCall region-state tracking):** The freed-state marking precisely matches the "track resource state through `ProgramState`" pattern.
- **Pattern D (PreCall argument inspection):** Checking pointer arguments before function calls follows this generic callback pattern.
- **Pattern C (DeadSymbols cleanup):** Required to prevent state bifurcation explosion from stale liveness bindings.
- **Pattern E (pointer-arithmetic alias analysis):** Needed when the checker must distinguish `ptr + offset` vs. `ptr` itself as the freed/base region.
- **Pattern H (false-positive suppression heuristics):** Apply for the known FP categories listed below.

Do **not** attempt to sound the warning in `PostCall` immediately after the free — this misses temporal gaps where the free and use are in different basic blocks or different functions. Always defer emission to the PreCall or Location callback when the dangling pointer is *used*.

## 3. Known High-Frequency False Positive Patterns

These FPs arise from inherent limitations of path-sensitive static analysis, not from bugs in the checker implementation.

**FP 1 — Pointer reassignment after free missed due to path insensitivity.** The analyzer sees `free(p);` and marks the region as freed. Later, `p = malloc(...);` reassigns the pointer variable. If the analysis splits paths at the reassignment and one path fails to kill the old binding, the freed-state symbol may survive into a join point, causing a spurious report when `p` is dereferenced on the merged path. *Mitigation:* In the `PostCall` for allocation functions, explicitly overwrite the `SymbolRef` binding for `p` in the store, unlinking the old freed symbol.

**FP 2 — Custom allocators / region pools.** Projects that manage memory through slab allocators, object pools, or arena allocators call `free()` on a pool block while individual objects within the block are still "logically" alive. The analyzer sees the `free()` and marks the entire region as freed, but subsequent accesses to pool-allocated objects are legitimate. *Mitigation:* Add a user-configurable allowlist of allocation functions that do *not* truly free memory (e.g., `pool_free`, `slab_free`). Suppress when the freed pointer's base region is known to be a pool/arena.

**FP 3 — C-style discretionary casts through `void *`.** A `void *` is freed in one context and later cast back to a different pointer type. The analyzer may lose the memory region identity through the opaque cast, failing to match the freed symbol to the use symbol. Alternatively, the analyzer may *incorrectly* match them, producing a false alarm when the programmer deliberately reused the storage for a different type (union-like patterns). *Mitigation:* Implement a conservative "escape" model — when a `void *` is passed through an opaque function that *may* rebind it, suppress the warning for that pointer on the return path (see Pattern H in `csa_checker_patterns.md`).

**FP 4 — `realloc` success/failure path splitting.** As described in Pattern 4 of Section 1, the standard code `p = realloc(p, n);` creates two post-call states: one where realloc succeeds (old `p` freed, new `p` allocated) and one where it fails (old `p` unchanged). Both branches converge and the analyzer's reduced/increased program state may mix freed and alive bindings. *Mitigation:* In `PostCall` for `realloc`, explicitly bifurcate the state to match the C standard semantics, then immediately merge by marking the *failed* path where `p` remains alive but NULL is returned.

**FP 5 — Destructor-invoked cleanup with shared pointers.** In C++, a `std::shared_ptr`'s managed object may be destroyed in the destructor of the last owning copy. If the analyzer does not fully model the reference count, it may believe the object is freed while a `shared_ptr` copy still holds a reference. *Mitigation:* Do not emit UAF on `shared_ptr::operator->()` or `operator*()` unless the pointer originates from `.get()` and the analyzer can prove the reference count reached zero.

**FP 6 — Incomplete modeling of `exit()` / `longjmp()` / `exec()`.** Calls to `exit()` or `longjmp()` terminate or rewind execution non-locally. The analyzer may continue analyzing a path past such a call, freeing resources and then using them on an unrealizable path. *Mitigation:* In the PreCall callback, if the called function is a `[[noreturn]]` or known process-terminating function, stop exploring that path (add sink in `ProgramState`).

**FP 7 — Volatile / atomic pointer loads.** If a freed pointer is stored in a `volatile` or `_Atomic` location, another thread may legitimately reassign it before the load. The single-threaded analyzer sees free then load and reports a UAF that is a data-race in practice but not a deterministic UAF. *Mitigation:* Suppress when the pointer load originates from a `volatile` or `_Atomic` lvalue (conservative: skip the check entirely for such pointers, or gate behind an opt-in flag).

## 4. Generic Discriminant Features

The following axes help a code reviewer (CC) distinguish textbook CWE-416 from borderline cases, hybrid bugs, or non-UAF issues:

| Axis | Textbook CWE-416 | Variant / Hybrid | Not CWE-416 |
|------|-------------------|-------------------|-------------|
| **Temporal distance** | Free and use are in distinct operations, possibly separated by other code; the dangling pointer is clearly identified. | Free and use are in the same expression (e.g., `free(p), *p;` using the comma operator); the "use" is an argument evaluation order issue. | The pointer is used *before* the free (use-then-free) — this is a different bug (CWE-366: Race Window or correct ordering but wasteful). |
| **Alias count** | Exactly one pointer variable is freed and later dereferenced, or two aliases where one is freed and the other used. | More than two aliases; some are passed through opaque functions that may or may not have freed the object. | Every use of the pointer is guarded by a NULL check after free, making the dereference unreachable in practice. |
| **Free mechanism** | `free()`, `delete`, `realloc()`, or explicit `operator delete`. | Custom deallocation via `munmap()`, `VirtualFree`, or `mprotect+unmap`. | Reference counting decrement that does *not* reach zero; the object is not actually freed. |
| **Use kind** | Read or write through the dangling pointer, or passing it to a function that dereferences it (type-safe). | Pointer is only compared to another pointer, used as a key in a hash computation, or passed to `free()` a second time (double-free, CWE-415). | The pointer is cast to `uintptr_t` and used only as an integer; no memory access occurs. |
| **Scope** | Single-threaded, sequential lifetime mismatch. | Multi-threaded: one thread frees while another reads; this is a race condition (CWE-362 / CWE-366), not a pure UAF. | The "freed" memory is still mapped (e.g., `mmap` with `MAP_FIXED` reused); the address is valid even though the original allocation is gone. |
| **Pointer provenance** | Pointer originates from `malloc`, `new`, `calloc`, `realloc`. | Pointer originates from stack (address-of local), `mmap`, shared memory, or custom allocator. | Pointer is NULL or was never initialized; the error is a null-pointer dereference (CWE-476), not UAF. |

**CC heuristic:** If the bug report involves a single sequential function (or a short call chain) with exactly one `free` call and exactly one subsequent use, it is almost certainly a textbook CWE-416. If the report involves cross-thread communication, opaque function calls, or exotic allocators, treat it as a variant that requires deeper manual inspection.

## 5. Repository Distribution Reference

This userspace variant card covers CWE-416 as it appears in **application, library, daemon, and CLI tool** codebases. The kernel-space variant (drivers, core kernel, filesystems) is generated under a separate card. The project distribution for this variant is:

| Project | CWE-416 sample pairs | Category |
|---------|----------------------|----------|
| torvalds/linux (userspace-facing samples) | 238 | Kernel (kernel variant; only userspace-relevant patterns counted here) |
| gpac/gpac | 25 | Multimedia framework |
| LibVNC/libvncserver | 7 | Remote desktop library |
| GNOME/libxml2 | 5 | XML parser library |
| vcftools/vcftools | 4 | Bioinformatics VCF tool |

**Note:** The `torvalds/linux` figure (238 pairs) is dominated by kernel-space samples. For the userspace variant, the representative distribution centers on **multimedia frameworks** (gpac), **network libraries** (libvncserver, libxml2), and **command-line data processing tools** (vcftools). These projects typically share a C-with-subset-of-C++ codebase, manual memory management, complex lifetime reasoning across library boundaries, and limited use of smart pointers — all factors that elevate UAF density.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*