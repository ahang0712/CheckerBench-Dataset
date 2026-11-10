---
name: checker-architecture
description: Use AFTER understanding the bug, BEFORE writing checker code — match your bug type to the right CSA architecture (AST-based vs path-sensitive), select callbacks, and pick the closest official example to start from.
---

# CSA Checker Architecture Selection

Choose the checker architecture that matches your bug type.  Picking the wrong architecture is the #1 cause of wasted rounds — get this right first.

## Architecture Decision Table

| Bug type | Architecture | Callback(s) | Start from |
|----------|-------------|-------------|------------|
| Missing validation / bounds / sanitizer check | **AST-based** | `check::ASTCodeBody` or `check::PreStmt<BinaryOperator>` | `01_div_zero.cpp` |
| Resource leak (alloc/free, open/close, lock/unlock) | **Path-sensitive state tracking** | `check::PreCall` + `check::DeadSymbols` | `02_simple_stream.cpp`, `05_malloc.cpp` |
| Null pointer dereference | **Path-sensitive constraint** | `check::PreStmt` or `check::Location` | `04_nullability.cpp` |
| Use of undefined / uninitialized value | **Path-sensitive taint** | `check::PostStmt` | `03_undef_result.cpp` |
| Type confusion / incorrect cast | **AST-based** | `check::ASTCodeBody` | `01_div_zero.cpp` (same visitor pattern) |

If the CWE card you read recommends a different architecture, **follow the CWE card's recommendation** — it encodes domain expertise for that vulnerability class.

## How to Use the Example

Once you pick the closest example from `references/examples/llvm-official/`:

1. Read **only** that example — not all 5.  Each example is 150-1500 lines.
2. Copy its **structure** (includes, namespace, class layout, registration).
3. Adapt its **callbacks** — swap the example's callback types for yours.
4. Adapt its **logic** — the bug-detection logic is unique to your CVE.
5. Register your checker name with `addChecker<>("your.CheckerName", ...)`.

## AST-based vs Path-sensitive: Key Differences

**AST-based** (`check::ASTCodeBody`):
- Traverses the AST of each function body once.
- Good for: pattern matching on code structure (missing if-checks, wrong operators).
- Cannot track state across functions.
- Simpler, faster to implement, fewer surprises.

**Path-sensitive** (`check::PreCall`, `check::PreStmt`, `check::DeadSymbols`):
- Walks all execution paths through the program.
- Good for: tracking resource state (alloc/free pairs), null constraints.
- Can track state across function calls via `ProgramStateRef`.
- More complex, requires understanding of CSA's symbolic execution engine.
