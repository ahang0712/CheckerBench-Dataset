# CWE-125: Out-of-bounds Read
> Tier: 1 (userspace)
> Global samples: 418 | Variant samples: 368 | Source projects: 102

## 1. Typical Code Patterns

CWE-125 manifests in C/C++ userspace code when a buffer is read at an index (or through a pointer offset) that lies outside the buffer's allocated bounds. This commonly appears in the following patterns:

- **Unchecked array subscript from external input.** A buffer is indexed using a value derived from a network packet, file format field, or command-line argument without verifying that the index falls within `[0, size)`. Example: `uint8_t cmd = buf[header.cmd_index];` where `cmd_index` is attacker-controlled and no range check precedes the access.

- **Off-by-one loop boundary.** A classic pattern where a loop reads up to and including index `N` from a buffer of size `N`. Typically manifests as `for (i = 0; i <= N; i++) { ... buf[i] ... }` where the correct bound is `i < N`. This also appears in string-length comparisons where the null terminator is accidentally read as payload data.

- **Pointer arithmetic with missing bound enforcement.** A function receives a pointer and a length, then performs `ptr[offset]` or `*(ptr + offset)` after checking against the wrong limit, or after mutating the length unsafely. Integer overflow in the length computation can silently bypass a guard: `if (offset + count < size)` where `offset + count` wraps around.

- **Incorrect struct traversal / type confusion.** Code casts a byte buffer to a struct pointer and accesses fields beyond the actual allocated object size, or walks a linked list of variable-length records using a corrupted length field. This is especially common in protocol parsers (e.g., TLV parsing loops) and archive decompressors.

- **Read past string terminator.** Standard library calls such as `strcpy`, `strlen`, `strchr`, or `memchr` are used on a character buffer that is not null-terminated, causing reads beyond the allocated region until a null byte is found in adjacent memory.

- **memcpy / read with oversized length argument.** A copy operation specifies a byte count larger than the source buffer, causing the copy (and any subsequent read-side analysis) to overrun the source. Although often discussed as a buffer-overflow write concern, the *read* half of `memcpy(dst, src, n)` where `n > sizeof(src)` is itself an OOB read on `src`.

- **Readahead in stateful parsers.** A parser peeks ahead several bytes into an input buffer to decode a variable-length field, but advances the offset without confirming that at least the peek-window fits within the buffer bounds.

## 2. CSA Checker Architecture Recommendations

Base the checker on `ento::Checker<check::PreStmt<ArraySubscriptExpr>, check::PreStmt<UnaryOperator>, check::PreStmt<CallExpr>>` to intercept array reads, pointer dereference via `*`, and calls to memory/string functions. Use `check::Location` / `check::Bind` to track where pointer values originate and what extent they carry.

**SymbolRef / ProgramState granularity:**
- Attach a **buffers extent map** in `ProgramState` — a `RegionRef` → `DefinedOrUnknownVal` mapping that records the allocated size of every memory region the analyzer can name (`ElementRegion`, `SubRegion` of a `VarRegion`, heap `MemSpaceRegion`).
- Use `SValBuilder::evalBinOp` to compare access offsets against recorded extents. A symbolic offset from untrusted input should be represented as a `SymbolicExpr` node so the constraint manager can reason about it across branches.
- After a bound check branch (e.g., `if (idx < size)`) the `ProgramState` already gains a constraint on the symbolic offset. The checker should query `State->getConstraint(Sym)` before emitting a report.

**Pattern references from `csa_checker_patterns.md`:**
- *Pattern: Bounds-check-early —* implement `check::BranchCondition` to observe when bounds checks create a split in the exploded graph. Store a `bool` in `ProgramState` indicating that a particular symbolic offset has been verified against a region's extent. Suppress reports when the constraint holds in the path's state.
- *Pattern: Extent-carrying-wrapper —* for inter-procedural analysis, wrap `malloc`/`calloc`/`realloc` and stack-allocated buffers in a helper that pushes both base pointer and size into the extent map. Use `check::PostCall` on allocator functions.
- *Pattern: Symbolic-offset-triage —* when the offset is fully concrete (constant), emit immediately if out of bounds. When symbolic, defer reporting until the constraint solver can prove the offset exceeds the extent on the path, or suppress if the path includes a passing bounds check.

Use `BugType` with `CWE-125` metadata. For string functions (`strcpy`, `memcpy`, etc.), provide a secondary diagnostic pointing at the source buffer extent and the copied length.

## 3. Known High-Frequency False Positive Patterns

Path-sensitive analyzers struggle with several constructs that are safe in practice but produce spurious OOB-read reports. Suppression logic should be added in the report-emitting callback, or earlier in `evalCall` / `evalStmt`, when any of the following heuristics match:

- **Sentinel-terminated data structures.** The "read one past the last element to find the sentinel" idiom (e.g., reading the null terminator of a C string, or scanning a custom linked list terminated by a magic value) triggers an off-by-one report because the analyzer sees the read index equal to the buffer length. *Suppression:* if the value being read is only compared against a sentinel constant and never used as payload, suppress. Recognize `buf[len] == '\0'` on a region whose extent is `len` as the idiomatic terminator check.

- **Flexible array members (`struct s { int h; char data[]; }`).** The analyzer treats the trailing flexible array as having zero size or a fixed size computed from the allocation. If allocation size is retrieved via `malloc(sizeof(struct s) + extra)`, the symbolic extent may not account for `extra`. *Suppression:* if the access targets a flexible-array-member region (`FieldRegion` with `isFlexible()` true) and the overall region's extent is wider than the struct base type, permit reads up to `extent - sizeof(struct s)`.

- **Address-of-end-of-buffer idiom.** Code computing `&buf[N]` (one past the end) for reverse iteration — `for (p = &buf[N]; p != buf; p--)` — is legal per the C standard, but a checker may fire on the initial address calculation because it treats `&buf[N]` as an OOB read. *Suppression:* when the result of the address-of expression is used only in pointer comparison, not dereferenced, skip the report. Track the lvalue vs rvalue use of the pointer.

- **Custom allocators with internal metadata.** Projects that use slab allocators, arena allocators, or embedded reference counts stored *before* the returned pointer (e.g., `return ptr + sizeof(header)`) confuse size tracking because the analyzer sees the returned pointer but not the header region. *Suppression:* add a `check::PreCall` to known allocator wrapper functions registered via `CheckerContext::registerForReceiver` so that the returned region's extent matches the actually allocated size, not the visible pointer's type.

- **Union type-punning.** Accessing a union member larger than the currently active member reads padding bytes, which some analyzers flag as an OOB read. In practice this is implementation-defined but widely used (e.g., `struct sockaddr` casts). *Suppression:* when the dereferenced region is a `FieldRegion` rooted in a `UnionRegion`, and the union's total size is larger than the member being read, treat the read as covering the full union extent.

- **Branches the analyzer cannot explore.** When a bounds check depends on a value the analyzer treats as `unknown` (e.g., a volatile read, a value set by an external library call), the checker conservatively assumes the worst. *Suppression:* if the offset is fully symbolic and the `ProgramState` has *no* constraint on it (not `inRange`, not `outOfRange`), suppress with a soft note. Only report when the constraint manager can prove the offset exceeds the extent.

## 4. Generic Discriminant Features

The following criteria help distinguish textbook CWE-125 instances from variant or hybrid cases:

- **Textbook CWE-125 (pure OOB read):**
  - The access offset is directly derived from untrusted input (network, file, environment).
  - There is *no* bounds check on the execution path before the read.
  - The read value is used as payload (data comparison, arithmetic, control flow).
  - The buffer is a fixed-size stack or heap array with a known constant extent.
  - No concurrent write occurs to the same region; the bug is purely a leak or crash on read.

- **Variant / borderline CWE-125:**
  - Off-by-one: a bound check exists but uses `<=` instead of `<`, or the length is computed with an off-by-one rounding error (`(len + 7) / 8` style miscalculation).
  - Pointer-bounce: a multi-level pointer indirection (`p->ptr[idx]`) where the inner pointer is itself invalid but the outer object bounds check passes.
  - Integer-truncation-induced: the bound is checked against a `size_t` value that was truncated from a larger type, so the check passes but the index is still beyond the true size.
  - Time-of-check-time-of-use: a bounds check is performed, then the index is modified between the check and the access.

- **Hybrid (CWE-125 + another weakness):**
  - OOB read that simultaneously enables a buffer-overflow write (CWE-787) — for example, reading a length field OOB produces a large value that feeds a subsequent write.
  - OOB read that leaks sensitive memory (combining CWE-125 with CWE-200: Information Exposure).
  - OOB read that reaches attacker-controlled memory (union type confusion, CWE-843: Type Confusion), where the read accesses reinterpreted data.
  - **The presence of a *write* to the same out-of-bounds location in the same patch strongly suggests hybrid classification.** A pure OOB read has no corresponding OOB write.

## 5. Repository Distribution Reference

This **userspace variant** card covers the 368 samples drawn from 102 userspace projects. The top five source projects by frequency are:

| Project | Sample Pairs |
|---|---|
| FreeRDP/FreeRDP | 37 |
| gpac/gpac | 29 |
| FFmpeg/FFmpeg | 27 |
| libarchive/libarchive | 19 |
| (remaining 98 projects) | 256 |

The **torvalds/linux** (50 pairs) samples are covered by the separate kernel variant card and are excluded from this userspace variant's counts.

Typical project categories covered by this variant: remote-desktop clients (FreeRDP), multimedia containers/codecs (gpac, FFmpeg), archive tools (libarchive), system daemons, CLI utilities, network protocol libraries, and firmware/bootloader user-space components. The common thread is that all operate on untrusted input with complex, length-prefixed data structures where parser boundary checks are easily omitted or computed incorrectly.

---
*Tier: 1 | Generated: 2026-06-20 | Source report: 2026-06-20*