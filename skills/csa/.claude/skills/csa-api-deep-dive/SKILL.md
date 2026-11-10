---
name: csa-api-deep-dive
description: Use when you need specific CSA API details beyond the quick reference — ProgramState operations, SVal type hierarchy, MemRegion navigation, CallDescription matching, BugReporter patterns, and all callback type signatures for LLVM 18.
---

# CSA API Deep Dive (LLVM 18)

## ProgramState Operations

```cpp
ProgramStateRef State = C.getState();

// Get a value from program state
SVal Val = State->getSVal(MemRegion);
// Or with type info
SVal Val = State->getSVal(MemRegion, QualType);

// Constrain a symbolic value (adds assumption)
// Assume Val is non-null
State = State->assume(*Val.getAs<DefinedOrUnknownSVal>(), true);
```

## SVal Type System

| SVal subclass | What it represents |
|---------------|-------------------|
| `DefinedSVal` | A concrete or symbolic value |
| `UnknownVal` | Unknown/unknowable value |
| `UndefinedVal` | Use of undefined value |
| `Loc` | Memory location |
| `NonLoc` | Non-location value (integer, etc.) |
| `SymbolVal` | Symbolic (unknown but tracked) value |

## Key Callback Types

| Callback | Fires |
|----------|-------|
| `check::PreCall` | Before every function call |
| `check::PostCall` | After every function call |
| `check::PreStmt<X>` | Before statement type X executes |
| `check::PostStmt<X>` | After statement type X executes |
| `check::DeadSymbols` | When symbols go out of scope (cleanup tracking) |
| `check::ASTCodeBody` | After parsing a function body (AST-based) |
| `check::Location` | When a memory location is accessed |
| `check::EndOfTranslationUnit` | After entire TU is analyzed |

## Bug Reporting

```cpp
// Generate error node (check if path is feasible)
ExplodedNode *ErrNode = C.generateErrorNode();
if (!ErrNode) return; // Infeasible path

// Create report
auto R = std::make_unique<PathSensitiveBugReport>(
    *BT, "Bug description message", ErrNode
);
// Add source range to highlight
R->addRange(Stmt->getSourceRange());
C.emitReport(std::move(R));
```

## CallDescription (function matching)

```cpp
CallDescription CD;
CD.addName("target_function");
if (CD.matches(Call)) { ... }
```
