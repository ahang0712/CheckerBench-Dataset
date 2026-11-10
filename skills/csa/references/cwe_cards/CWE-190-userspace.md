# CWE-190: Integer Overflow/Wraparound
> Tier: 1 (userspace)
> Global samples: 120 | Variant samples: 108 | Source projects: 48

## 1. Typical Code Patterns

Integer overflow and wraparound in C/C++ userspace code manifests when an arithmetic operation produces a result that exceeds the range of the target integer type. The C standard treats signed integer overflow as **undefined behavior**, while unsigned integer wraparound is defined (modulo 2^n) but often equally dangerous in security contexts.

**Pattern 1 — Unchecked arithmetic on untrusted input.** The most common pattern: two attacker-controlled values are added or multiplied with no preceding bounds check. The result is then used as a memory allocation size, copy length, or array index. Example: `size_t total = count * sizeof(T)` where both `count` and `sizeof(T)` may appear safe individually but the product overflows, producing a small `total` that passes subsequent safety checks.

**Pattern 2 — Allocation size computation overflow.** A special case of Pattern 1 that is the dominant exploitation vector. The overflowed size is passed to `malloc`, `calloc`, `realloc`, or a custom allocator. The returned buffer is too small, and subsequent writes overrun the heap. This appears in media parsers, network protocol decoders, and serialization routines where multi-dimensional data structures are reconstructed from input.

**Pattern 3 — Signed integer overflow in bounds checks.** A signed addition or multiplication produces a value that wraps into the negative range, bypassing an upstream guard such as `if (n > 0 && n + offset > limit)`. Because signed overflow is UB, compilers may also optimize the guard away entirely, creating a "time bomb" that only triggers under aggressive optimization flags.

**Pattern 4 — Truncation through narrowing conversion.** A wider integer (e.g., `uint64_t` or `size_t`) is implicitly or explicitly cast to a narrower type (`int`, `uint16_t`). High-order bits are discarded, and the truncated value is used in a security decision. This appears in legacy code where API boundaries use different width types.

**Pattern 5 — Off-by-one wraparound in string or buffer operations.** Computing `len + 1` for a null terminator when `len` equals `SIZE_MAX` (or `UINT_MAX` for `unsigned int`) produces 0. The resulting zero-length allocation is followed by a write of at least one byte, causing a heap buffer overflow.

**Pattern 6 — Loop induction variable overflow.** A loop counter of type `int` or `size_t` increments past its maximum value. For signed counters this is UB; for unsigned counters the wraparound to 0 can cause an infinite loop or out-of-bounds access when the counter is used as an index.

## 2. CSA Checker Architecture Recommendations

- **Base class:** `BuiltinBugChecker` — suitable for pattern-matching on binary operators and call arguments where the overflow site can be identified syntactically. For deeper path-sensitive tracking (e.g., taint propagation through multiple operations), prefer a checker that subclasses `Checker` and manages custom `ProgramState`.
- **Callbacks:** `checkPreStmt<BinaryOperator>` to inspect addition, subtraction, and multiplication before evaluation; `checkPostCall` for allocation functions (`malloc`, `calloc`, `realloc`, `aligned_alloc`) to validate size arguments against symbolic upper bounds; `checkEndFunction` to report unmitigated overflows on untrusted data paths.
- **SymbolRef granularity:** Represent each potentially overflowing integer as a `SymbolRef` with `RangeConstraint`. Maintain the symbolic expression tree of the computation (`$sym + $offset`) so the checker can distinguish guarded operations (e.g., `if (x < LIMIT) { x + 1; }`) from unguarded ones.
- **ProgramState:** Maintain a map from `SymbolRef` to a "trust" tag (trusted, untrusted, or sanitized). Integrate with a generic taint checker to mark symbols derived from `fread`, `recv`, `getenv`, `atoi`, or function parameters as untrusted. After a comparison-based guard is detected, transition the state to sanitized.
- **Cross-reference:** See patterns **"taint-tracking"**, **"range-propagation"**, and **"post-call-size-validation"** in `references/csa_checker_patterns.md`. For allocation-size overflow specifically, pattern **"allocation-size-computation"** describes how to hook `CallEvent` on allocation routines and walk back through the size argument's symbolic expression.

## 3. Known High-Frequency False Positive Patterns

False positives in CWE-190 detection arise when the analyzer lacks sufficient context to determine that an arithmetic operation is safe by design. The following constructs are perennial sources:

**Induction variables with statically bounded loops.** A `for (int i = 0; i < N; i++)` loop where `N` is a small constant produces signed additions (`i++`) that the analyzer may flag unconditionally. These additions cannot overflow because the loop bound guarantees `i < N`. **Mitigation at the engine level:** prune diagnostics on induction variables where the loop's exit condition can be proven to limit the counter's range.

**Unsigned decrement idioms.** `while (n--)` and `for (size_t i = len; i > 0; i--)` intentionally use wraparound when `n` or `i` is zero — the decrement from 0 wraps to `SIZE_MAX`, which fails the loop condition on the next iteration. This is defined behavior and semantically correct. **Mitigation:** suppress reports where the overflowed value is used only as a loop condition and is never dereferenced or used in an allocation.

**Saturated arithmetic as a defense.** Code that performs an addition and then checks `if (result < a || result < b)` to detect overflow is itself performing an overflowing addition. Treating the addition as a vulnerability ignores that the result is validated before use. **Mitigation:** track the data-flow of the result; if it feeds directly into a comparison that tests for overflow, suppress the diagnostic.

**Cryptographic and PRNG modular arithmetic.** `r = (a + b) & 0xFFFFFFFF` or `state = (state * 1103515245 + 12345) & 0x7FFFFFFF` deliberately relies on wraparound. These are design requirements, not bugs. **Mitigation:** allow a project-supplied allowlist of known-safe arithmetic contexts (e.g., tagged with a macro or recognized by function name patterns).

**Constant-foldable operations.** Arithmetic on compile-time constants or `sizeof` expressions appears in the AST but is known-safe. **Mitigation:** evaluate the expression at analysis time; emit a diagnostic only when at least one operand is a runtime symbol whose range could plausibly lead to overflow.

**Pointer-offset arithmetic disguised as integer arithmetic.** Adding a small index to a pointer that has already been bounds-checked via `__builtin_object_size` or similar intrinsic may produce a large intermediate `size_t` value that the analyzer does not recognize as safe. **Mitigation:** integrate with the analyzer's existing memory-region and extent information to correlate the offset with the allocation size.

## 4. Generic Discriminant Features

**Textbook CWE-190** (high confidence, pure instance):

1. A short, linear data-flow path: **untrusted source → single arithmetic operation → security-sensitive sink** (allocation, memcpy length, array index, loop bound).
2. No intervening bounds check on the arithmetic operands — or a check that uses an incorrect type (e.g., comparing a `size_t` against a signed `int` maximum).
3. The overflow result is used directly in the sink without reshaping or clamping (no `min`, `max`, or modulo operation).
4. The arithmetic is a single addition or multiplication — the simplest cases are also the most reliably exploitable.

**Variant or hybrid** (lower confidence, mixed CWE):

1. The overflow occurs after multiple operations and type conversions, requiring the analyzer to track a deep symbolic expression tree. The root cause may be an implicit cast earlier in the chain.
2. Partial sanitization exists — the code checks one operand but not the other, or checks a related but different value (e.g., checks `width` but not `height` before computing `width * height`).
3. The overflow is an enabling mechanism for a second CWE (CWE-122 heap overflow, CWE-787 out-of-bounds write, CWE-823 use of out-of-range pointer offset). In these cases the integer overflow itself may be incidental; the primary bug class is the downstream memory safety violation.
4. The arithmetic is part of a multi-step protocol state machine or format parser where the overflow only manifests for crafted sequences of inputs, not a single malicious value.
5. Use of intentional wraparound idioms (saturated arithmetic, PRNG, checksum computation, hash functions) — these should be classified as non-vulnerable upon recognition, not as hybrid CWEs.

## 5. Repository Distribution Reference

This userspace variant of CWE-190 comprises **108 sample pairs** drawn from **48 source projects**. The top five repositories by sample count:

| Repository | Sample pairs |
|---|---|
| torvalds_linux (userspace components: libraries, tools, userspace-facing APIs) | 12 |
| libsndfile_libsndfile | 10 |
| FFmpeg_FFmpeg | 10 |
| AcademySoftwareFoundation_openexr | 10 |
| uclouvain_openjpeg | 7 |

The remaining 37 projects account for 59 sample pairs and span a broad range of application domains: multimedia codecs and containers (libav, libvpx), image processing libraries (libpng, libtiff), audio file parsers, scripting language runtimes (PHP, Lua), database engines (SQLite, LMDB), compression libraries (zlib, bzip2), cryptographic libraries (OpenSSL, libtomcrypt), and network protocol implementations. The common thread across all 48 projects is C/C++ code that accepts externally supplied data and performs arithmetic on it without sufficient validation — the classic userspace integer overflow profile.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*