# CWE-787: Out-of-bounds Write
> Tier: 1 — userspace variant (libraries, applications, daemons)
> Global samples: 449 | Variant samples: 365 | Source projects: 119

## 1. Typical Code Patterns

CWE-787 manifests when a program writes data beyond the bounds of an allocated memory buffer. In userspace C/C++ code, this commonly appears in the following forms:

- **Off-by-one loop writes:** A loop iterates one time too many, writing one element past the end of an array. Typically caused by using `<=` instead of `<` in the loop condition, or by zero-index confusion: `for (i = 0; i <= n; i++) buf[i] = ...`.

- **Unbounded string/memory copies into fixed buffers:** Calls to `sprintf`, `strcpy`, `strcat`, `memcpy`, or `bcopy` where the destination buffer size is unchecked and the source length is attacker-controlled or unknown. Stack buffers (e.g., `char buf[64]`) are especially common targets.

- **Incorrect pointer arithmetic:** Writing through a pointer computed as `base + offset` where `offset` was derived from untrusted data without validation, or where the arithmetic overflowed silently (e.g., `base + len` where `len` wraps around).

- **Array indexing with untrusted indices:** Using attacker-controlled or unsanitized integer values as array subscripts (e.g., `arr[user_index] = val;`) without a prior bounds check, or with a bounds check that uses a signed comparison on unsigned indices.

- **Integer truncation in size calculations:** Computing allocation or copy sizes with an expression that overflows or is truncated (e.g., multiplying two 32-bit values and passing the result to `malloc`, then writing beyond the undersized buffer).

- **Protocol parsing with malformed length fields:** Code that reads a length field from a network packet or file format and uses it directly as a write bound without verifying it against the actual buffer capacity. Common in media parsers, network daemons, and archive extractors.

- **Write via out-of-bounds pointer returned from arithmetic:** Functions that advance a pointer through a buffer (e.g., `p += consumed`) without checking that the resulting pointer still falls within the allocated region before writing.

## 2. CSA Checker Architecture Recommendations

Base the checker on **`check::PreStmt`** for array subscript expressions (`ArraySubscriptExpr`) and **`check::PreCall`** / **`check::PostCall`** for memory/string functions (`memcpy`, `memset`, `sprintf`, etc.).

**Core analysis components:**

- **Region-based buffer-size tracking:** Use `MemRegion` and `ElementRegion` to associate each allocated region with its known extent. For heap allocations, model `malloc`/`calloc`/`realloc` return values with symbolic sizes anchored to the allocation site. For stack buffers, use `VarRegion` extent information from the AST.

- **Symbolic offset evaluation:** For each write operation, compute the symbolic access offset relative to the region base. Compare against the symbolic capacity using the constraint manager (`SValBuilder` + `RangeConstraintManager`). Treat writes where the offset is *definitely* beyond capacity as bugs; where it is *possibly* beyond, emit a warning with a lower severity.

- **State splitting on conditionals:** When the checker encounters a bounds-checking branch (e.g., `if (idx < size)`), use `ProgramState` to track the feasible-range constraint on the index symbol in each post-branch state. This enables path-sensitive detection that eliminates false positives on guarded paths.

- **Multi-step propagation:** Track pointer-derived relationships: if `q = p + offset` and `p` is known to point to a region of size `N`, then any write through `q` must satisfy `offset < N` (for element-wise access) or `offset + write_size <= N` (for byte-wise access).

**Recommended cross-references in `csa_checker_patterns.md`:**
- *Pattern: "BufferSizeTracking"* — for mapping allocation sites to symbolic extents.
- *Pattern: "SymbolicOffsetComparison"* — for comparing access offsets against extents.
- *Pattern: "PostCallModeling"* — for summarizing the effects of `memcpy`/`memset` length parameters.
- *Pattern: "ConditionalStateSplit"* — for path-sensitive bounds-check recognition.

## 3. Known High-Frequency False Positive Patterns

The following constructs inherently confuse path-sensitive buffer-overflow checkers and require targeted suppression logic:

- **Flexible array members (FAM):** C99 `struct` types ending with `data[]` or `data[0]` are declared with zero static extent. A checker that relies solely on `VarRegion` extent will flag any access to `p->data[i]` as OOB. **Mitigation:** Recognize FAM declarations via `FieldDecl::hasFlexibleArrayMember()` on the containing `RecordDecl` and suppress bounds checking for the trailing member.

- **Custom slab/arena allocators:** Many performance-critical libraries implement their own allocators (e.g., `pool_alloc(size)`) that return memory from a pre-allocated pool. The analyzer sees the raw `malloc`-like callsite but cannot derive a per-object extent. **Mitigation:** Add `check::PostCall` handling that recognizes known allocator function names via a configuration list, and manually associates a symbolic extent equal to the requested size.

- **Index wrapping in ring/circular buffers:** Code that uses modular arithmetic (e.g., `buf[(head + i) & MASK]`) can produce indices that appear unbounded to the constraint manager when `MASK` is derived symbolically. **Mitigation:** Detect bitwise-AND masking on index expressions; if the mask implies a power-of-two bound, treat it as an implicit bounds constraint.

- **Pointer arithmetic with struct offsets:** Code that computes `(char*)s + offsetof(S, field)` and writes through the resulting pointer for serialization or reflection often triggers OOB reports because the checker loses the association with the original struct region. **Mitigation:** Before emitting a warning, check whether the base pointer points into a well-typed `MemRegion` whose layout accounts for the written range.

- **Dynamic stack allocation with VLA:** Variable-length arrays (`int n = ...; char buf[n]`) allocate on the stack with symbolic extent. If the extent is unconstrained (e.g., read from input before validation), the checker may assume a small extent and flag later writes. **Mitigation:** For VLA extents, only emit warnings when the extent symbol is *explicitly* bounded by constraints; otherwise degrade to a note rather than a warning.

- **Uninitialized padding bytes:** Code that `memset`s a struct using a compile-time constant larger than the sum of its named fields can hit padding bytes inserted by the ABI. The extent is correct (the full `sizeof`), but the checker may flag it if it computes a per-field bound. **Mitigation:** When the destination of `memset`/`memcpy` is a `MemRegion` with `RecordType`, allow writes up to `sizeof(RecordType)` even if named fields end earlier.

## 4. Generic Discriminant Features

The following characteristics help distinguish a textbook CWE-787 instance from a variant or hybrid:

**Textbook CWE-787 (pure spatial overflow):**
- A single, directly-indexed write to a heap or stack buffer.
- The index is an unconstrained symbolic value derived from input or loop iteration.
- No aliasing, no type confusion, no temporal issues (use-after-free).
- The buffer extent is statically known or traceable to a single allocation site.
- There is a clear mismatch: the index range exceeds the allocated extent.

**Variant or hybrid patterns:**
- **Type-confusion-driven OOB write:** The buffer is accessed through a pointer with an incorrect type, causing the analyzer's computed element size to differ from the actual access stride. Symptom: the offset is in bounds for the declared type but out of bounds for the real object.
- **Use-after-free + OOB write:** The write target is a dangling pointer; the spatial overflow is secondary to the temporal error. The report should be classified as CWE-416 (Use After Free) if the primary flaw is lifetime, not extent.
- **Integer-overflow-to-OOB write:** The write bounds are correct at the C level, but an earlier integer overflow caused the allocation to be undersized. The root cause is CWE-190 (Integer Overflow or Wraparound); the OOB write is a downstream consequence.
- **Concurrent/race-condition OOB write:** A TOCTOU race between the bounds check and the write invalidates the check. Root cause is CWE-362 (Concurrent Execution using Shared Resource); classify accordingly.

## 5. Repository Distribution Reference

This userspace variant covers libraries, CLI tools, daemons, network servers, and application-layer code. The top source projects by sample count are:

| Repository | Pairs |
|---|---|
| torvalds/linux (kernel, userspace samples only) | 84 |
| gpac/gpac | 51 |
| radareorg/radare2 | 15 |
| jerryscript-project/jerryscript | 15 |
| FreeRDP/FreeRDP | 15 |

The presence of torvalds/linux in this list reflects userspace-facing code within the kernel tree (e.g., drivers, filesystem utilities, selftests). The remaining projects are typical userspace targets: a multimedia framework (GPAC), a reverse-engineering tool (radare2), a JavaScript engine (JerryScript), and a remote desktop client (FreeRDP). These projects share common OOB-prone patterns: complex file format parsing, network protocol handling, custom memory management, and runtime code generation.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*