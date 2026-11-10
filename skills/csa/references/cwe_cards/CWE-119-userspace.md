# CWE-119: Improper Restriction of Operations within the Bounds of a Memory Buffer
> Tier: 1 — userspace variant
> Global samples: 248 | Variant samples: 219 | Source projects: 59

## 1. Typical Code Patterns

CWE-119 encompasses any scenario where software performs operations on a memory buffer but fails to correctly restrict the operation to within the bounds of that buffer. In C/C++ userspace code, this commonly manifests in the following forms:

**Direct array subscript overflow.** The most straightforward pattern: an array is accessed via a subscript that is derived from untrusted or insufficiently validated input. Typical examples include index variables read from network packets, file headers, or command-line arguments without verifying `0 <= index < array_length`. This pattern appears in parsing loops where a field index or offset is taken directly from external data.

**Pointer arithmetic with missing bounds checks.** A base pointer is advanced by an unchecked offset, then dereferenced. This is especially common in serialization/deserialization code, image parsers, and protocol implementors. The arithmetic may overflow a `size_t` intermediate value (wrapping around), or the offset may simply exceed the remaining buffer capacity.

**Off-by-one errors in comparison logic.** Loop conditions that use `<=` instead of `<`, or fencepost errors in length calculations, lead to a single-element overrun. These are notoriously frequent in string processing (null-terminator placement) and in `memset`/`memcpy` calls where the byte count is derived from a `strlen` result or a `sizeof` that includes space for the terminator when it should not.

**Unsafe standard library calls.** Functions like `sprintf`, `strcpy`, `strcat`, `gets`, and `vsprintf` implicitly assume that the destination buffer is large enough. While modern compilers produce warnings for some of these, they persist in legacy code, conditional compilation paths, and in wrappers that suppress diagnostics. Even `memcpy`/`memmove` with a computed size that exceeds the dest or src capacity is a frequent variant.

**Truncation and sign issues.** A size or length stored in a signed integer is treated as an unsigned value, or vice versa. Negative values pass a bounds check (e.g., `len < bufsize`) but then cause out-of-bounds access when cast to `size_t` in a subsequent allocation or copy operation. This pattern is especially common in codebases that mix `int`, `size_t`, and `uint32_t` length fields interchangeably.

**Tainted length propagation.** A length value enters the function through an external source (network, file, environment variable), undergoes insufficient validation (or none), and is then passed to `malloc`, `read`, `recv`, or `memcpy`. Even if the immediate call site is correct, the allocated buffer may be undersized relative to the copied data, or the length parameter to a read/copy operation may exceed the buffer capacity.

**Iterator invalidation and reallocation confusion.** A pointer or iterator into a container (e.g., a `std::vector`, a dynamically grown buffer) is cached, then the container is resized or reallocated, leaving the cached pointer dangling. Subsequent writes through that pointer produce an out-of-bounds write on the new allocation. This pattern crosses the line into use-after-free territory but is fundamentally a bounds-restriction failure.

## 2. CSA Checker Architecture Recommendations

**Base class.** Implement the checker as a `BuiltinFunctionChecker` or a `CallDescription`-based checker when tracking operations on standard library functions (`memcpy`, `memset`, `realloc`, `fread`, etc.). Use a `CheckLocation`-based visitor for direct array-subscript and pointer-dereference checks — the `checkLocation` callback on `MemRegion` accesses is the natural hook for out-of-bounds detection.

**Core tracking granularity.** Maintain `SymbolRef`-based metadata in `ProgramState` for:
- The **allocated extent** of every `MemRegion` that was produced by `malloc`, `calloc`, `realloc`, `alloca`, or a stack-allocated array (tracked via `ElementRegion` with known `Extent`).
- The **offset from the base region** at every pointer-arithmetic operation. Record this as a `nonloc::SymbolVal` constrained by a `SymExpr` (e.g., `(offset sym) + (base reg)`).  
- **Taintedness** of index and size values. Use the `TaintManager` to tag values that originate from `fread`, `recv`, `read`, `getenv`, or user-controlled function arguments. The checker should suppress warnings only when a tainted index has been demonstrably constrained (e.g., `assumeInBoundRange` on the symbolic value).

**Callbacks to implement.**
- `checkPreCall` / `checkPostCall`: Intercept calls to memory-management functions and standard copy routines. Record buffer extents and propagate taint tags on return.
- `checkLocation`: Before the analyzer evaluates a load or store via `checkLocation`, inspect the `ElementRegion` or `FieldRegion`. Compare the symbolic offset against the recorded extent. Emit a bug report if the symbolic offset is provably at or past the end, or if it is unbounded but tainted.
- `checkEndOfFunction` (optional): Detect missed-bounds patterns where a local buffer is returned (producing a stack-pointer leak — though this crosses into CWE-466, it is a close cousin).
- `evalAssume` for refining offset constraints: When the analyzer assumes a condition like `offset < size`, constrain the symbolic offset in the state so that subsequent `checkLocation` hits are more precise.

**Region classification.** Inside `checkLocation`, classify the target region:
- `ElementRegion` with a concrete or symbolic index → candidate for out-of-bounds.
- `ElementRegion` where the index is a tainted symbol → high-priority candidate.
- `FieldRegion` at offset beyond the containing struct *(struct tail overread)* → potential CWE-119 or CWE-126 (buffer overread).

**Cross-reference.** See **pattern A ("Tainted array index")** and **pattern C ("Pointer arithmetic with unvalidated offset")** in `csa_checker_patterns.md` for the concrete `ProgramState` update logic and assume-constraint wiring. See also **pattern E ("Copy length exceeds allocated size")** for the `checkPreCall` instrumentation of `memcpy`/`memmove`.

## 3. Known High-Frequency False Positive Patterns

**Symbolic extent on dynamic allocations.** When a buffer is allocated with `malloc(n)` where `n` is a symbolic value, the analyzer cannot statically determine the exact extent. Any access with a symbolic offset will appear potentially out-of-bounds even when the programmer intended `offset < n`. Mitigate by requiring that either (a) the offset is tainted and unconstrained, or (b) the analyzer can prove `offset >= extent` — not simply that it's uncertain. Suppress reports when neither condition holds.

**Flexible array members (FAM).** C99/C11 structs with a trailing flexible array member (`char buf[];`) are accessed at arbitrary offsets beyond the struct's `sizeof`. The analyzer's `MemRegion` for the struct has a fixed extent equal to `sizeof(struct)` ignoring the flexible member. Every access to the FAM triggers a false positive. Suppress by special-casing `FieldRegion` nodes whose field decl is a flexible array member (check `FieldDecl::isFlexibleArrayMember()`).

**Array parameters decayed to pointers.** Function parameters declared as `int arr[]` are semantically pointers; no extent information is available from the type. CSA will not know the array size. False positives occur whenever the function accesses `arr[i]` without a separate size parameter. The checker should only warn when the index is tainted **and** the parameter lacks an associated size-tracking invariant (e.g., via an `arg_size` attribute or explicit `ProgramState` constraint from the caller).

**Guard conditions too complex for the solver.** Access guarded by a compound condition such as `if (offset >= 0 && offset < size && offset + count <= size)`. The Z3 or range-based constraint solver may not fully resolve this conjunction, producing a spurious warning. Suppress when all involved symbols are untainted, or when the condition involves the same symbols that index the buffer — it suggests deliberate validation logic.

**Loop idioms with memcpy.** A loop that copies data in chunks (e.g., frame-by-frame) using `memcpy(dst + offset, src + offset, framesize)` where `offset` increments by `framesize` each iteration. The analyzer's symbolic offset quickly grows into a large expression that the constraint solver cannot compare against the allocated extent. Suppress warnings inside loops where the offset is loop-carried and incremented by a constant value.

**Aliasing through external libraries.** When a buffer is allocated in one translation unit and populated by a library function (e.g., `libavcodec` decode callbacks), the analyzer has no visibility into the library's write pattern. Access after the call will appear to use an uninitialized or undersized buffer. Suppress by marking buffers passed to opaque function calls as *unknown extent* rather than zero extent.

**Realloc usage.** A buffer is `realloc`'d to a smaller size, then the old cached size variable is used in a subsequent bounds check. The analyzer may not model the size update via `realloc` unless the return value is explicitly rebound. False positives here are reduced by modeling `realloc` as producing a new `MemRegion` with the new extent, and invalidating old `SymbolRef` relationships.

## 4. Generic Discriminant Features

**Direct index vs. pointer arithmetic.** A textbook CWE-119 patch changes an unchecked array subscript to one guarded by a length comparison, or replaces a pointer-arithmetic dereference with a range-checked access. If the patch involves a `+` on a pointer whose result is immediately dereferenced without checking remaining capacity, it is a textbook example. If the patch involves a complex mix of `realloc` and cached pointers, it is a hybrid.

**Single vs. multiple bounds checks.** A pure CWE-119 fix adds a single bounds guard (`if (idx < len)`). A variant fix adds the same guard but also corrects a sign mismatch, a type width issue (`int` vs `size_t`), or a missing `break` after a bounds failure. The more orthogonal concerns in the patch, the more it leans toward a hybrid CWE (e.g., CWE-704 / CWE-190 combined with 119).

**Source of the unchecked value.** When the unchecked index/size is:  
- a direct function parameter with no validation → textbook CWE-119.  
- derived from a `strlen`, `sizeof`, or `offsetof` → off-by-one or truncation hybrid.  
- derived from a macro or enum constant → low-severity / coincidental.  
- derived from a network parse loop → high-severity but may overlap with CWE-20 (Input Validation).

**Buffer type.** Stack arrays (`char buf[64]`) that are overrun are clearer CWE-119 examples than heap allocations, because the extent is trivially known. Heap overruns where the allocation size is a function of untrusted input are closer to CWE-789 (Uncontrolled Memory Allocation) or CWE-190 hybrid.

**Operation type.** A write that exceeds the destination buffer is more severe than a read that exceeds the source (CWE-126). Patches that fix a read overrun should be distinguished from those fixing a write overrun — both are valid CWE-119 but write overruns are more likely to be exploitable and thus higher tier.

**Single-element vs. range overrun.** Accessing `buf[n]` where `n == len` (off-by-one) versus `buf[n]` where `n >> len` (wild overrun). Off-by-ones often have separate underlying causes (logic error in comparison) and may be classified under CWE-193 (Off-by-one Error), which is a child of CWE-119. Use the presence of a `<` vs `<=` correction as a discriminant.

## 5. Repository Distribution Reference

**Variant scope.** This userspace variant targets libraries, CLI tools, daemons, and application code. The kernel variant is tracked separately. The repositories listed below represent userspace-only occurrences — driver and kernel module fixes are excluded from these counts.

**Top source projects for this variant:**

| Project | Sample pairs | Profile |
|---|---|---|
| FFmpeg_FFmpeg | 55 | Multimedia framework; CWE-119 arises in demuxer/muxer parsing, codec bitstream handling, and filter graph processing where untrusted media files drive buffer operations. |
| torvalds_linux | 29 | Userspace-facing components (lib/, crypto/, security/, fs/) — notably string operations, bignum arithmetic, and netlink message handling. |
| gpac_gpac | 15 | MP4/ISOBMFF parser; classic tainted-index and copy-length overruns in box parsing loops. |
| file_file | 11 | `libmagic` file-type detection; overreads in magic byte sequence matching. |
| ArtifexSoftware_mupdf | 11 | PDF/EPUB document parser; out-of-bounds access in font encoding tables, image decode paths, and stream filters. |

**Distribution notes.** The FFmpeg dominance reflects the project's heavy exposure to untrusted file formats and its aggressive fuzzing-driven patch cadence. The gap between #1 (55) and #3 (15) indicates that CWE-119 fixes are concentrated in a few high-exposure codebases. Projects with formal parser models (e.g., mupdf, gpac) produce concentrated clusters rather than scattered fixes.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*