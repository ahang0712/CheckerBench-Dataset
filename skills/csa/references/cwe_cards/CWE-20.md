# CWE-20: Improper Input Validation
> Tier: 2 | Global samples: 88

## 1. Typical Code Patterns

Software fails to verify that externally sourced data satisfies safety or correctness constraints before using it. In C/C++ the most common manifestations are:

- **Missing bounds checks on array indices or pointer arithmetic** — using an attacker-controlled integer as an offset without verifying it is within `[0, len)`.
- **Unvalidated loop bounds** — an external value controls loop iteration count, enabling resource exhaustion or out-of-bounds access when the value is negative or excessively large.
- **Trusted format strings** — passing external data as the format argument to `printf`, `snprintf`, or `syslog`, allowing format-string exploits.
- **Unvalidated file paths** — using external input to construct a path without checking for `..` components, null bytes, or symbolic-link traversals.
- **Missing range checks on numeric values** — using an unchecked external integer in arithmetic, allocation size, or type conversion without verifying it fits the expected domain.
- **Unvalidated enum or switch discriminants** — switching on an external value without a default case or range guard, leading to undefined behavior or logic errors.

The core flaw is the same: an **assumption about input well-formedness that is not enforced** before the value reaches a sensitive operation.

## 2. CSA Checker Architecture Recommendations

- **Base class:** `BuiltinBugchecker` (or `Checker` when cross-checker state is needed).
- **Core callbacks:** `checkPreCall` / `checkPostCall` for library functions (`memcpy`, `printf`, `atoi`, `read`, `fgets`), and `checkBranchCondition` / `checkLocation` for index expressions and allocation arguments.
- **Tracking granularity:** Track taint via a `TaintMap` (region → set of tags). Propagate taint through assignments, arithmetic, and casts. Stop propagation at sanitizer calls (`is_valid()`, range checks, explicit predicates).
- **Path-sensitive pruning:** Refine the exploded graph when a branch condition tests a tainted value (e.g., `if (idx < len)`). The checker should mark the tainted value as *constrained* on the true edge and *unconstrained* on the false edge, eliminating OOB paths.
- **Cross-reference:** Follow `csa_checker_patterns.md` §2.2 (taint tracking) and §3.1 (path-sensitive sinks).

## 3. Known High-Frequency False Positive Patterns

- **Guard code not recognized due to opaqueness** — the checker cannot prove that `if (x < limit)` covers all reachable paths (e.g., guard uses a function call, `assert`, or integer-overflow-prone expression). Mitigation: model common sanitizer idioms (`__builtin_expect`, `safe_math` wrappers) directly.
- **Indirect validation through enum dispatch** — a value selects a branch in a switch, and the switch body uses the value safely. The checker flags the switch discriminant itself. Mitigation: suppress when the value is only used as a selector, not as an index/size.
- **Input that is "valid by construction"** — data read from a trusted source (e.g., a fixed config file) is marked tainted because the function signature is generic. Mitigation: allow user-annotated "trusted sources" or whitelist certain APIs.
- **Signed vs. unsigned mismatch** — a negative value passes a `>= 0` check but is implicitly cast to unsigned before use in allocation, producing a huge `size_t`. The checker fires even though the program handles `SIZE_MAX` gracefully. Mitigation: add a dedicated unsigned-underflow sink rule rather than a generic taint warning.
- **Configuration/time-based validation** — input is validated at program start (e.g., a global limit set once) and later used without re-validation. The checker misses the earlier check. Mitigation: intra-procedural taint tracking with state merging at function boundaries.

## 4. Generic Discriminant Features

| Feature | Textbook (definite CWE-20) | Variant / Borderline |
|---|---|---|
| **Data origin** | Externally sourced (network, file, `stdin`, env var) | Internally generated but derivative of external data |
| **Validation site** | Missing — no check before sensitive use | Check exists but is flawed (off-by-one, TOCTOU, signedness bug) |
| **Impact mechanism** | Direct: OOB write, format-string read, infinite loop | Indirect: corrupted metadata, delayed failure |
| **Checker evidence** | Tainted value reaches a sink without a path-constraining predicate | Value passes through a sanitizing function the checker cannot model |
| **Fix pattern** | Insert `if (x < limit)` or use safe wrapper (`snprintf`, `safe_atou`) | Tighten existing check or change type domain (unsigned → checked signed) |

A report is **high-confidence CWE-20** when: (a) the data source is clearly external, (b) zero predicates on the value appear on the path from source to sink in the exploded graph, and (c) the sink is a well-known dangerous operation. Absent any of these, reclassify as variant or lower-tier weakness (CWE-754, CWE-191, CWE-1284).

---
*Tier: 2 | Generated: 2026-06-20 | Source report: 2026-06-20*