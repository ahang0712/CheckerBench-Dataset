---
name: checker-implementation-suggestions
description: Use when writing or debugging checker.cpp — avoid the most common CSA implementation mistakes: always call getBaseRegion(), use generateNonFatalErrorNode (not generateErrorNode), verify function names with ExprHasName(), and properly track pointer vs value state. Each rule prevents a specific class of silent failure or false negative.
---

# CSA Checker Coding Patterns

Proven patterns for writing correct, effective CSA checkers. Each rule prevents
a specific class of implementation bugs. Use when writing or debugging checker.cpp.

## Pointer & State Management

### 1. Always call `getBaseRegion()` after `getAsRegion()`

```cpp
// ❌ Wrong
const MemRegion *Reg = Loc.getAsRegion();

// ✅ Correct
const MemRegion *Reg = Loc.getAsRegion();
if (Reg) Reg = Reg->getBaseRegion();
```

Without `getBaseRegion()`, you may operate on a field-level region instead of the
base variable region — causing the checker to miss cases where the same variable is
accessed through different sub-paths.

### 2. Track pointers with `MemRegion*`, basic types with `SymbolRef`

When tracking return values or program state:
- **Pointer types** (`int*`, `struct foo*`) → use `MemRegion*` to identify the memory location
- **Basic types** (`int`, `char`) → use `SymbolRef` to represent the symbolic value

```cpp
// Pointer → MemRegion
const MemRegion *PtrReg = State->getSVal(E, LC).getAsRegion();

// Integer → SymbolRef
SymbolRef Sym = State->getSVal(E, LC).getAsSymbol();
```

### 3. Use `checkBind` + program state map for pointer aliasing

```cpp
REGISTER_MAP_WITH_PROGRAMSTATE(PtrAliasMap, const MemRegion*, const MemRegion*)

void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const {
  // Track which pointer points to which memory
  ...
}
```

The `checkBind` callback fires every time a value is assigned — the only reliable
way to track pointer aliasing information through the program.

### 4. Do NOT call `IgnoreImplicit()` before `getMemRegionFromExpr()`

`getMemRegionFromExpr()` handles implicit casts internally. Calling `IgnoreImplicit()`
before it strips information the function needs. After calling it, DO call
`getBaseRegion()` on the result.

```cpp
const MemRegion *Reg = getMemRegionFromExpr(E, C);  // no IgnoreImplicit!
if (Reg) Reg = Reg->getBaseRegion();
```

## Bug Reporting

### 5. Use `generateNonFatalErrorNode()` — not `generateErrorNode()`

```cpp
// ❌ generateErrorNode() — stops after first bug on a path
ExplodedNode *N = C.generateErrorNode();

// ✅ generateNonFatalErrorNode() — reports ALL bugs on a path
ExplodedNode *N = C.generateNonFatalErrorNode();
if (!N) return;
```

`generateErrorNode()` stops analysis of the current path after one bug.
`generateNonFatalErrorNode()` allows the analyzer to continue and find
additional bugs in the same file.

### 6. Use `std::make_unique` for bug reports, keep messages short

```cpp
auto Report = std::make_unique<PathSensitiveBugReport>(
    *BT, "null pointer dereference: ptr->field accessed without null check", N);

// or for AST-based checkers:
auto Report = std::make_unique<BasicBugReport>(
    *BT, "division by zero: denominator may be zero", N);
```

Bug report messages should be **short** and describe the concrete issue,
not the checker name or internal state.

### 7. Do NOT report when information is missing

If you cannot determine whether a value is constrained (e.g., array size is
undecidable, or a symbol has no known range), **do not emit a warning**.
It's better to have a silent false negative than a noisy false positive
that erodes trust in the checker.

### 8. No placeholder logic — implement completely

Every code path in the checker must have a real implementation.
DO NOT leave `// TODO` or `return;` stubs — they cause silent failures
that are extremely hard to debug.

## Function & Expression Verification

### 9. Use `ExprHasName()` instead of `getCalleeIdentifier()->getName()`

```cpp
// ❌ Unreliable — only matches simple identifiers
const IdentifierInfo *Callee = Call.getCalleeIdentifier();
if (!Callee || Callee->getName() != "target_func") return;

// ✅ Robust — matches even when the function is called through a macro
const Expr *OriginExpr = Call.getOriginExpr();
if (!OriginExpr || !ExprHasName(OriginExpr, "target_func", C)) return;
```

`ExprHasName()` checks the actual source text of the expression, so it works
when the callee is accessed through a macro expansion, typedef, or
other indirection that `getCalleeIdentifier()` cannot handle.

For type checking, use the Clang AST API directly:
```cpp
C.getASTContext().getTypeSize(Ty);
```

## Symbolic Value Operations

### 10. Use `inferSymbolMaxVal()` to bound symbolic values

```cpp
// Infer the maximum possible value of a symbol from current constraints
const llvm::APSInt *MaxVal = inferSymbolMaxVal(Sym, C);
if (MaxVal) {
  // Now you know the upper bound — use it for overflow/bounds checking
}
```

### 11. Always NULL-check after retrieving pointer types

```cpp
const MemRegion *Reg = State->getSVal(E, LC).getAsRegion();
if (!Reg) return;  // always check — may be UnknownVal or UndefinedVal
```

## Utility Functions Reference

These functions are available in `references/csa_utility_functions.h`
(if present).  You can inline the ones you need into checker.cpp.

| Function | Purpose |
|----------|---------|
| `findSpecificTypeInParents<T>(Stmt*, CheckerContext&)` | Walk UP the AST to find a parent of type T |
| `findSpecificTypeInChildren<T>(Stmt*)` | Walk DOWN the AST to find a child of type T |
| `EvaluateExprToInt(APSInt&, Expr*, CheckerContext&)` | Evaluate constant integer expression |
| `inferSymbolMaxVal(SymbolRef, CheckerContext&)` | Get maximum possible value of a symbol |
| `getArraySizeFromExpr(APInt&, Expr*)` | Extract array size from DeclRefExpr |
| `getStringSize(APInt&, Expr*)` | Extract string length from StringLiteral |
| `getMemRegionFromExpr(Expr*, CheckerContext&)` | Get MemRegion from expression |
| `ExprHasName(Expr*, StringRef, CheckerContext&)` | Check if expression source text contains a name |
| `functionKnownToDeref(CallEvent&, SmallVectorImpl<unsigned>&)` | Check if function is known to dereference a parameter |
