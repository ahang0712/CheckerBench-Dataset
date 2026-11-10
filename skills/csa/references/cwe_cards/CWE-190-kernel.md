# CWE-190: Integer Overflow/Wraparound
> Tier: 1 — kernel variant
> Global samples: 120 | Variant samples: 12 | Source projects: 48

## 1. Typical Code Patterns

Integer overflow/wraparound in kernel-space C/C++ code commonly appears when arithmetic on attacker-influenced or untrusted values crosses the representable range of the target integer type, producing a wrapped result that violates subsequent size, offset, or bounds checks. In kernel and OS-level code, this manifests in several recurring patterns:

- **Buffer size arithmetic via manual slab allocators:** A `kmalloc()` or `kzalloc()` call whose size argument is computed by multiplying two values (e.g., `num_entries * sizeof(struct entry)`) without overflow checking. When the product wraps to a small value, the allocated buffer is undersized, leading to a heap write overflow on subsequent access.

- **Reference-count and index arithmetic in RCU-protected structures:** Increment or decrement operations on reference counts (`refcount_t` or raw `atomic_t`) inside RCU read-side critical sections, where an overflow causes the counter to wrap to zero and triggers premature reclamation. This is especially subtle when the counter is stored in a small-width field (e.g., `u16` or `u8`) inside a packed structure.

- **Interrupt handler / bottom-half increment logic:** Fields shared between process context and interrupt context (e.g., `softirq` counters, `tasklet` statistics) that are incremented without saturation. A burst of interrupts can cause a `u32` counter to wrap, breaking logic that depends on monotonicity (e.g., "if counter > threshold" checks).

- **Signedness confusion in size / offset computation:** A signed `int` width used to hold a byte count that legitimately exceeds `INT_MAX`. When the value is passed to an unsigned parameter (e.g., `size_t` in a copy-to-user routine), the sign extension produces a huge unsigned value, or the signed overflow during computation is undefined behavior that the optimizer may eliminate.

- **Iteration bound wrap:** A loop condition like `i < num_entries` where `num_entries` was derived from user-controlled input and a previous computation overflowed. The loop iterates far fewer or far more times than intended, corrupting kernel heap or stack objects.

- **Wraparound in manual ring buffer / FIFO indices:** Producer-consumer index arithmetic (e.g., `(head + count) & (size - 1)`) where the intermediate addition overflows before the mask is applied, causing index corruption in network driver descriptor rings or `dmesg` log buffers.

## 2. CSA Checker Architecture Recommendations

Base the checker on `ento::Checker<check::PreStmt<BinaryOperator>>` to intercept addition, subtraction, and multiplication operators. For detecting overflow in function arguments (e.g., `kmalloc(a * b)`), additionally subscribe to `check::PreCall` and inspect the arguments of known allocation and copy routines.

- **SymbolRef tracking:** Track each integral value as a nonloc::SymbolVal. For multiplication and addition results, introduce a derived symbol that represents the mathematical (non-wrapped) result, and constrain the concrete integer type's range. When the operation is outside the type's representable range, flag a bug.
- **ProgramState granularity:** Maintain a map from `SymbolRef` to an "overflow state" enum (`OverflowNone`, `OverflowSaturated`, `OverflowWrapped`) in the `ProgramState`. Update this state when a symbolic value participates in an arithmetic operation. Use `SValBuilder::evalBinOp` to model the arithmetic and compare the resulting range against the type's min/max.
- **Taint integration:** Leverage `GenericTaintChecker` to mark values derived from user-controlled or copy_from_user sources. In `check::PreStmt`, skip reporting if neither operand is tainted — this dramatically reduces false positives in kernel analysis where most arithmetic is safe.
- **Callback cross-reference:** Follow the *Stateful arithmetic overflow* and *Taint-guided narrowing* patterns described in `references/csa_checker_patterns.md`. Do **not** re-derive overflow from `APSIntType` alone — kernel code frequently uses `unsigned long` arithmetic that the analyzer's `APSInt` range cannot resolve; use constraint merging against `RangeConstraint` instead.
- **False-positive suppression in PreCall:** For calls to known saturating helpers (`size_mul()`, `check_mul_overflow()`, `array_size()`, `struct_size()`), prune the report. These functions intentionally perform overflow-agnostic arithmetic before checking — flagging them is always noise.

## 3. Known High-Frequency False Positive Patterns

Derived from static analysis first principles:

- **Loop induction variables with saturating semantics:** A `size_t` counter incremented until a fixed bound. Path-sensitive analyzers examine each back-edge and may conclude the counter could wrap if the loop runs sufficiently long. However, loops in kernel code (e.g., `for (i = 0; i < nr_pages; i++)`) are bounded by call-site invariants invisible to the analyzer (e.g., `nr_pages` was clamped earlier by a memory-allocation limit). **Suppression:** Add a heuristic that recognizes `i`-over-`<`-bound loops where the induction variable starts at zero and the bound is a symbolic value — do not flag the increment if the bound is a fresh symbol with an upper range constraint (e.g., `reg_val <= 4096`), as this implies a prior limiting check.

- **Bitmask-and-add patterns:** `x = (a + b) & mask` where the addition is intentionally allowed to wrap, then the result is masked to a bit-width. The analyzer sees `a + b` as an overflow of the full type, but the programmer intended modulo-`2^k` arithmetic. **Suppression:** In `check::PreStmt<BinaryOperator>`, if the result of an addition or subtraction is immediately consumed by a bitwise-AND with a mask that constrains the value to fewer bits than the type width, skip the report. Require the mask to be a concrete integer literal.

- **sizeof multiplication in typed allocators:** `kmalloc(n * sizeof(type))` where `sizeof(type)` is a power-of-two constant and `n` is bounded by a prior `min()` or `clamp()` call. The analyzer symbolically multiplies and sees a possible overflow only when `n` can exceed `SIZE_MAX / sizeof(type)`. If `n`'s range constraint already implies a smaller value (e.g., `n <= 1024` and `sizeof(type) == 64` → maximum product 65536), the analyzer should automatically prune via range reasoning. **Diagnose missing range constraints** rather than suppressing; fix the upstream taint or constraint propagation.

- **Undefined behavior of signed overflow used as sentinel:** Code that deliberately performs `int` overflow to produce a negative sentinel (e.g., `(INT_MAX + 1)` to check `if (result < 0)`). In C/C++ this is UB and a real CWE-190 candidate, but kernel code sometimes relies on GCC's two's-complement behavior. **Suppression:** Only flag signed overflow when the result is used as a pointer offset, an array index, or a size argument. If the overflow result is immediately compared against a constant and not dereferenced, defer the warning (it may be a sentinel idiom).

- **Compile-time constant folding:** The analyzer's constant folder may evaluate `sizeof(struct X) * n` to a concrete `APSInt` at the call site, precluding the overflow check because the product is computed in the analyzer's arbitrary-precision domain. **Handle:** Perform overflow checking on the *source-level* integer types, not the folded value. If both operands are concrete, compute product in the wider of the two types and compare against the narrower type's range; emit a warning only if the concrete product exceeds the narrower range.

## 4. Generic Discriminant Features

A textbook CWE-190 kernel sample has most of the following traits:

1. **The overflowing operation directly produces a size or index argument** to `kmalloc`, `kcalloc`, `copy_from_user`, `copy_to_user`, `memcpy`, `array subscript`, or a ring-buffer write position. No intervening saturating check.
2. **Both operands are derived from user-controlled input** (ioctl argument, syscall parameter, network packet field, mmap offset) — at least one operand is tainted.
3. **The result is used in a way that creates a write-what-where primitive:** wrapped allocation size → heap buffer overflow; wrapped index → out-of-bounds array access; wrapped offset → wrong memory region corrupted.
4. **No overflow guard before the operation** — no `if (a > SIZE_MAX / b)`, no call to `check_mul_overflow()`, no use of `size_mul()` or `struct_size()` macros.
5. **The target integer type is unsigned** (`size_t`, `u32`, `unsigned long`) — overflow is well-defined wraparound, not undefined behavior, which means the compiler preserves the wrap and the exploit is deterministic.

A variant or hybrid sample typically deviates in one or more of these ways:

- **Signed overflow (UB):** Often a compiler-eliminated check that never actually wraps at runtime, or the overflow is in a non-security-critical debug counter. Requires confirmation that the UB was observed to produce a miscompilation.
- **Overflow in intermediate accumulator** that is later divided or scaled down before use — true but unlikely to be exploitable.
- **Wraparound in cryptographic or hash context** (e.g., additive hash accumulation) where the wrap is mathematically part of the algorithm, not a security flaw.
- **Single operand is untrusted, the other is a large constant** — the overflow opportunity is limited unless the constant is huge relative to the type width.

## 5. Repository Distribution Reference

This kernel variant is most frequently drawn from the following project categories:

- **OS kernels and low-level system software:** `torvalds/linux` (12 paired samples) is the dominant source, reflecting integer overflow in device drivers, file systems, network stacks, and memory management code — especially in `kmalloc` wrappers, `refcount_t` usage, and `copy_from_user` / `copy_to_user` paths.
- **Multimedia and codec libraries:** `FFmpeg/FFmpeg` (10), `libsndfile/libsndfile` (10) — these projects operate heavily on attacker-controlled media files, where integer overflow in sample-count, frame-size, or channel-multiplex arithmetic leads to heap corruption.
- **Image and file format parsers:** `AcademySoftwareFoundation/openexr` (10), `uclouvain/openjpeg` (7) — parsing of maliciously crafted image headers with arithmetic on width, height, and channel count.
- **Other notable categories:** `systemd/systemd`, `qemu/qemu`, `openssl/openssl` — OS infrastructure components that perform resource accounting or buffer management on untrusted input.

The kernel variant specifically covers samples where the overflow occurs in code paths that interact with kernel memory allocators, interrupt handlers, RCU-protected data, or other OS-level primitives. Userspace-only arithmetic overflow in the same source projects is handled under a separate userspace variant card.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*