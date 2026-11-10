# CSA Checker Pattern Catalog

A structured reference of common checker patterns extracted from 5 official LLVM 18
Clang Static Analyzer checkers. Use this as a pattern library when designing new
checkers.

---

## Table of Contents

1. [Division by Zero Detection](#1-division-by-zero-detection)
2. [Undefined Value Detection](#2-undefined-value-detection)
3. [Resource Tracking (malloc/free pattern)](#3-resource-tracking-mallocfree-pattern)
4. [Null Checking / Nullability Propagation](#4-null-checking--nullability-propagation)
5. [Complex Resource Tracking with SymbolRef Maps](#5-complex-resource-tracking-with-symbolref-maps)
6. [Pattern Quick-Reference Table](#6-pattern-quick-reference-table)

---

## 1. Division by Zero Detection

**Source:** `DivZeroChecker.cpp`
**Bug pattern:** The program divides or takes remainder with a value that is
constrained to zero.

### Callbacks

```
check::PreStmt<BinaryOperator>
```

Only ONE callback -- the simplest possible checker structure.

### Program State Traits

**None.** This checker does not use `REGISTER_MAP_WITH_PROGRAMSTATE`. It is a
pure "look at the current state" checker with no persistent state of its own.

### Skeleton Code

```cpp
class DivZeroChecker : public Checker<check::PreStmt<BinaryOperator>> {
  const BugType BT{this, "Division by zero"};
public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
```

### Core Detection Logic

1. Filter to `BO_Div`, `BO_Rem`, `BO_DivAssign`, `BO_RemAssign`.
2. Get the RHS (denominator) SVal via `C.getSVal(B->getRHS())`.
3. Use `ConstraintManager::assumeDual` to split the state:
   - `stateZero` -- denominator is zero
   - `stateNotZero` -- denominator is non-zero
4. If only `stateZero` is feasible → **bug** (division by zero definitely happens).
5. If both are feasible AND the denominator is tainted → **taint-related bug**.
6. Otherwise, add transition with `stateNotZero`.

### Bug Report Emission

```cpp
ExplodedNode *N = C.generateErrorNode(StateZero);
auto R = std::make_unique<PathSensitiveBugReport>(BT, "Division by zero", N);
bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
C.emitReport(std::move(R));
```

### Key APIs Used

| API | Purpose |
|-----|---------|
| `C.getSVal(Expr)` | Get the symbolic value of an expression |
| `ConstraintManager::assumeDual` | Split state on a boolean condition |
| `C.generateErrorNode(State)` | Create a sink error node |
| `PathSensitiveBugReport` | The report object |
| `bugreporter::trackExpressionValue` | Add path notes tracking a value |
| `C.emitReport()` | Submit the report |
| `@ - llvm::ArrayRef<SymbolRef>` | Tainted symbol reporting |

---

## 2. Undefined Value Detection

**Source:** `UndefResultChecker.cpp`
**Bug pattern:** The result of a binary operator is undefined because one or
both operands are uninitialized/garbage values.

### Callbacks

```
check::PostStmt<BinaryOperator>
```

### Program State Traits

**None.**

### Skeleton Code

```cpp
class UndefResultChecker : public Checker<check::PostStmt<BinaryOperator>> {
  const BugType BT{this, "Result of operation is garbage or undefined"};
public:
  void checkPostStmt(const BinaryOperator *B, CheckerContext &C) const;
};
```

### Core Detection Logic

1. Evaluate `C.getSVal(B).isUndef()` -- is the binary operator's result
   undefined?
2. If yes:
   a. Skip `swap` functions (false positive suppression).
   b. Determine which operand is undefined (left, right, or both).
   c. Optionally check for array-out-of-bounds as the root cause.
   d. Build a descriptive message ("The left operand of '+' is a garbage value").

### Bug Report Emission

```cpp
ExplodedNode *N = C.generateErrorNode();
// Build diagnostic string
auto report = std::make_unique<PathSensitiveBugReport>(BT, OS.str(), N);
report->addRange(Ex->getSourceRange());
bugreporter::trackExpressionValue(N, Ex, *report);
C.emitReport(std::move(report));
```

### Key APIs Used

| API | Purpose |
|-----|---------|
| `C.getSVal(Expr).isUndef()` | Test if a value is undefined |
| `C.generateErrorNode()` | Create sink node for the bug |
| `getDynamicElementCount()` | Get array bound for OOB check |
| `State->assumeInBoundDual()` | Check array index bounds |
| `bugreporter::trackExpressionValue` | Track undefined value source |

---

## 3. Resource Tracking (malloc/free pattern)

**Source:** `SimpleStreamChecker.cpp`
**Bug pattern:** Double-close, use-after-close, and resource leak for file
streams opened with `fopen`/`fclose`.

This is the canonical example of the **resource tracking pattern** -- the
simplest checker that uses `REGISTER_MAP_WITH_PROGRAMSTATE`.

### Callbacks

```
check::PostCall     -> track newly allocated resource
check::PreCall      -> check/update state before deallocation
check::DeadSymbols  -> detect leaks when symbols die
check::PointerEscape -> stop tracking when pointer escapes
```

### Program State Traits

```cpp
REGISTER_MAP_WITH_PROGRAMSTATE(StreamMap, SymbolRef, StreamState)
```

The state enum:
```cpp
struct StreamState {
  enum Kind { Opened, Closed } K;
  static StreamState getOpened() { return StreamState(Opened); }
  static StreamState getClosed() { return StreamState(Closed); }
  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(K); }
};
```

### Skeleton Code

```cpp
class SimpleStreamChecker : public Checker<check::PostCall, check::PreCall,
                                           check::DeadSymbols,
                                           check::PointerEscape> {
  const CallDescription OpenFn{{"fopen"}, 2};
  const CallDescription CloseFn{{"fclose"}, 1};
  const BugType DoubleCloseBugType{this, "Double fclose", "Unix Stream API Error"};
  const BugType LeakBugType{this, "Resource Leak", "Unix Stream API Error",
                            /*SuppressOnSink=*/true};
public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const;
};
```

### Core Detection Logic

**checkPostCall (allocation):**
1. Match `fopen` via `OpenFn.matches(Call)`.
2. Get return symbol: `Call.getReturnValue().getAsSymbol()`.
3. Set `StreamMap[Symbol] = Opened` in program state.
4. `C.addTransition(State)`.

**checkPreCall (deallocation):**
1. Match `fclose` via `CloseFn.matches(Call)`.
2. Get argument 0 symbol: `Call.getArgSVal(0).getAsSymbol()`.
3. Look up state: `State->get<StreamMap>(FileDesc)`.
4. If already `Closed` → **double close bug**.
5. Otherwise set `StreamMap[Symbol] = Closed`.

**checkDeadSymbols (leak detection):**
1. Iterate `StreamMapTy TrackedStreams = State->get<StreamMap>()`.
2. For each (Symbol, State) pair: if symbol is dead AND state is Opened AND
   symbol is non-null → **leak**.
3. Remove dead symbols from the map.

**checkPointerEscape:**
1. When a pointer escapes (e.g., passed to unknown function), remove it from
   the map to stop tracking.

### Bug Report Emission

```cpp
// Double close (sink node):
ExplodedNode *ErrNode = C.generateErrorNode();
auto R = std::make_unique<PathSensitiveBugReport>(
    DoubleCloseBugType, "Closing a previously closed file stream", ErrNode);
R->addRange(Call.getSourceRange());
R->markInteresting(FileDescSym);
C.emitReport(std::move(R));

// Leak (non-fatal error node):
ExplodedNode *N = C.generateNonFatalErrorNode(State);
for (SymbolRef LeakedStream : LeakedStreams) {
  auto R = std::make_unique<PathSensitiveBugReport>(
      LeakBugType, "Opened file is never closed; potential resource leak", N);
  R->markInteresting(LeakedStream);
  C.emitReport(std::move(R));
}
```

### Key APIs Used

| API | Purpose |
|-----|---------|
| `REGISTER_MAP_WITH_PROGRAMSTATE` | Create a persistent map in ProgramState |
| `CallDescription` | Match specific function calls by name/arity |
| `CallEvent::getReturnValue()` | Get return value SVal |
| `CallEvent::getArgSVal(n)` | Get argument SVal |
| `State->set<Key>(k, v)` | Update map in state |
| `State->get<Key>(k)` | Read from map |
| `State->remove<Key>(k)` | Remove from map |
| `SymbolReaper::isDead(Sym)` | Check if symbol is dead |
| `C.generateNonFatalErrorNode()` | Create non-sink error node (for leaks) |
| `C.generateErrorNode()` | Create sink error node (for double close) |
| `R->markInteresting(Sym)` | Tag symbol for path tracking |
| `R->addRange(SourceRange)` | Highlight source range in report |

---

## 4. Null Checking / Nullability Propagation

**Source:** `NullabilityChecker.cpp`
**Bug pattern:** Null pointer passed to/returned from `_Nonnull` context;
nullable pointer dereferenced.

This is the most complex pattern in terms of callback count and state
management.

### Callbacks

```
check::Bind              -> assignment tracking
check::PreCall           -> passing null to nonnull param
check::PreStmt<ReturnStmt> -> returning null from nonnull function
check::PostCall          -> propagate nullability from call return
check::PostStmt<ExplicitCastExpr> -> trust explicit casts
check::PostObjCMessage   -> ObjC message nullability
check::DeadSymbols       -> cleanup
eval::Assume             -> property access constraints
check::Location          -> loads from nonnull memory
check::Event<ImplicitNullDerefEvent> -> implicit null deref
check::BeginFunction     -> init nullable params
```

### Program State Traits

```cpp
REGISTER_MAP_WITH_PROGRAMSTATE(NullabilityMap, const MemRegion *, NullabilityState)
REGISTER_MAP_WITH_PROGRAMSTATE(PropertyAccessesMap, ObjectPropPair, ConstrainedPropertyVal)
REGISTER_TRAIT_WITH_PROGRAMSTATE(InvariantViolated, bool)
```

### Skeleton (abbreviated)

```cpp
class NullabilityChecker
    : public Checker<check::Bind, check::PreCall, check::PreStmt<ReturnStmt>,
                     check::PostCall, check::PostStmt<ExplicitCastExpr>,
                     check::PostObjCMessage, check::DeadSymbols, eval::Assume,
                     check::Location, check::Event<ImplicitNullDerefEvent>,
                     check::BeginFunction> {
  enum CheckKind { CK_NullPassedToNonnull, CK_NullReturnedFromNonnull,
                   CK_NullableDereferenced, CK_NullablePassedToNonnull,
                   CK_NullableReturnedFromNonnull, CK_NumCheckKinds };
  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BTs[CK_NumCheckKinds];
};
```

### Core Detection Logic (key checks)

**checkPreCall -- null passed to nonnull:**
1. Iterate call parameters.
2. For each pointer param with `_Nonnull` annotation:
   - If arg is constrained null → report.
   - If arg has tracked nullable state → report.

**checkPreStmt (ReturnStmt) -- null returned from nonnull:**
1. Get return value SVal and check if null.
2. If function's return type is `_Nonnull` → report.

**checkLocation -- load from nonnull pointer:**
1. On load from a `TypedValueRegion` with `_Nonnull` type, assume the loaded
   value is non-null (add constraint to state).

**checkEvent (ImplicitNullDerefEvent) -- nullable pointer dereference:**
1. If a pointer with tracked `Nullable` state is dereferenced → report.

**evalAssume** -- track property access constraints:
1. When a property access value is constrained non-null, record it so future
   accesses can be assumed non-null.

### Bug Report Emission

```cpp
if (ExplodedNode *N = C.generateErrorNode(State, &Tag)) {
  reportBugIfInvariantHolds(OS.str(), ErrorKind::NilPassedToNonnull,
                            CK_NullPassedToNonnull, N, nullptr, C, ArgExpr);
}

// Inside reportBugIfInvariantHolds:
// 1. Check if invariant already violated (suppress if so)
// 2. If SuppressPath, set InvariantViolated flag
// 3. Call reportBug() which creates PathSensitiveBugReport
```

### Key APIs Used

| API | Purpose |
|-----|---------|
| `getNullabilityAnnotation(T)` | Extract _Nonnull/_Nullable from type |
| `ConditionTruthVal` / `State->isNull(Val)` | Test null constraint |
| `REGISTER_TRAIT_WITH_PROGRAMSTATE` | Boolean flag in state |
| `eval::Assume` | React to assumptions/branching |
| `check::Event<ImplicitNullDerefEvent>` | React to implicit null deref |
| `BugReporterVisitor` | Custom path notes (NullabilityBugVisitor) |

---

## 5. Complex Resource Tracking with SymbolRef Maps

**Source:** `MallocChecker.cpp` (3631 lines)
**Bug pattern:** Memory leaks, double-free, use-after-free, mismatched
deallocator, offset-free, zero-size allocation use.

This is the most comprehensive checker -- supports malloc/free, new/delete,
realloc, calloc, strdup, alloca, kernel allocators, and ownership attributes.

### Callbacks

```
check::DeadSymbols       -> leak detection
check::PointerEscape     -> stop tracking escaped pointers
check::ConstPointerEscape -> same for const pointers
check::PreStmt<ReturnStmt> -> check return of freed memory
check::EndFunction       -> check return of freed memory (after destructors)
check::PreCall           -> use-after-free on args, delete modeling
check::PostCall          -> model alloc/free/realloc functions
check::NewAllocator      -> C++ new expression allocation
check::PostStmt<BlockExpr> -> block capture handling
check::PostObjCMessage   -> ObjC "NoCopy" methods
check::Location          -> use-after-free on pointer access
eval::Assume             -> handle failed allocation assumption
```

### Program State Traits

```cpp
REGISTER_MAP_WITH_PROGRAMSTATE(RegionState, SymbolRef, RefState)
REGISTER_SET_WITH_PROGRAMSTATE(ReallocSizeZeroSymbols, SymbolRef)
REGISTER_MAP_WITH_PROGRAMSTATE(ReallocPairs, SymbolRef, ReallocPair)
REGISTER_MAP_WITH_PROGRAMSTATE(FreeReturnValue, SymbolRef, SymbolRef)
```

The `RefState` enum:
```
Allocated, AllocatedOfSizeZero, Released, Relinquished, Escaped
```

### Skeleton (abbreviated)

```cpp
class MallocChecker
    : public Checker<check::DeadSymbols, check::PointerEscape,
                     check::ConstPointerEscape, check::PreStmt<ReturnStmt>,
                     check::EndFunction, check::PreCall, check::PostCall,
                     check::NewAllocator, check::PostStmt<BlockExpr>,
                     check::PostObjCMessage, check::Location, eval::Assume> {
  // Maps function names to handler methods:
  CallDescriptionMap<CheckFn> FreeingMemFnMap{ ... };
  CallDescriptionMap<CheckFn> AllocatingMemFnMap{ ... };
  CallDescriptionMap<CheckFn> ReallocatingMemFnMap{ ... };
};
```

### Core Detection Logic

**checkPostCall -- modeling alloc/free/realloc:**
1. Look up the call in `FreeingMemFnMap`, `AllocatingMemFnMap`, or
   `ReallocatingMemFnMap`.
2. Dispatch to handler (e.g., `checkFree`, `checkBasicAlloc`, `checkRealloc`).

**MallocMemAux -- allocation modeling:**
1. Create a `conjured heap symbol` for the return value.
2. Bind the return value in the state: `State->BindExpr(CE, LCtx, RetVal)`.
3. Bind default initial value: `State->bindDefaultInitial(RetVal, Init, LCtx)`.
4. Set dynamic extent: `State = setDynamicExtent(State, ...)`.
5. Call `MallocUpdateRefState` to set `RegionState[Sym] = Allocated`.

**FreeMemAux -- deallocation modeling:**
1. Get the argument SVal and its base region.
2. Check: is it non-heap? → HandleNonHeapDealloc.
3. Check: is it alloca? → HandleFreeAlloca.
4. Check: is it already released? → HandleDoubleFree.
5. Check: does deallocator match allocator? → HandleMismatchedDealloc.
6. Check: is there an offset? → HandleOffsetFree.
7. Check: is it a function pointer? → HandleFunctionPtrFree.
8. Set `RegionState[Sym] = Released` (or Relinquished if Hold).

**checkDeadSymbols -- leak detection:**
1. Iterate `RegionState` map.
2. For each (Sym, State) where Sym is dead:
   - If `Allocated` or `AllocatedOfSizeZero` → add to leak list.
   - Remove from map.

**checkLocation -- use-after-free:**
1. Get `Sym = l.getLocSymbolInBase()`.
2. If `isReleased(Sym, C)` → HandleUseAfterFree.

**evalAssume -- handle failed allocation:**
1. If a tracked symbol is assumed NULL → remove from tracking (allocation
   failed on this path).
2. For realloc symbols assumed NULL → restore the original pointer's state.

### Bug Report Emission

```cpp
if (ExplodedNode *N = C.generateErrorNode()) {
  auto R = std::make_unique<PathSensitiveBugReport>(*BT, msg, N);
  R->markInteresting(Sym);
  R->addRange(Range);
  R->addVisitor<MallocBugVisitor>(Sym);  // Adds path notes
  C.emitReport(std::move(R));
}
```

### Key APIs Used

| API | Purpose |
|-----|---------|
| `CallDescriptionMap<Fn>` | Map of function signatures to handlers |
| `SVB.getConjuredHeapSymbolVal()` | Create fresh symbolic heap region |
| `State->BindExpr()` | Bind return value to expression |
| `State->bindDefaultInitial()` | Set default value for region |
| `setDynamicExtent()` | Record allocation size |
| `getLocSymbolInBase()` | Extract symbol from location |
| `RegionOffset` | Check pointer arithmetic |
| `BugReporterVisitor` | Custom path notes (MallocBugVisitor) |
| `eval::Assume` | Track allocation failure paths |
| `SymbolReaper` | Detect dead symbols for leak checking |

---

## 6. Pattern Quick-Reference Table

| Pattern | Checker | Callback Count | State Traits | Complexity |
|---------|---------|----------------|--------------|------------|
| Division by zero | DivZeroChecker | 1 | None | Simplest |
| Undefined value | UndefResultChecker | 1 | None | Simple |
| Resource tracking | SimpleStreamChecker | 4 | 1 MAP | Medium |
| Nullability | NullabilityChecker | 11 | 2 MAP + 1 TRAIT | Complex |
| Memory management | MallocChecker | 12 | 4 MAP/SET | Most complex |

### Common `REGISTER_*` Patterns

```cpp
// Map: SymbolRef -> CustomState (most common for resource tracking)
REGISTER_MAP_WITH_PROGRAMSTATE(MyMap, SymbolRef, MyState)

// Set: collection of symbols
REGISTER_SET_WITH_PROGRAMSTATE(MySet, SymbolRef)

// Boolean flag in state
REGISTER_TRAIT_WITH_PROGRAMSTATE(MyFlag, bool)
```

### Common Callback Patterns

| Callback | When to Use |
|----------|-------------|
| `check::PreStmt<BinaryOperator>` | Check before binary op evaluation |
| `check::PostStmt<BinaryOperator>` | Check after binary op evaluation |
| `check::PreCall` | Check before a function call |
| `check::PostCall` | Check/model after a function call |
| `check::DeadSymbols` | Detect leaks; cleanup state |
| `check::PointerEscape` | Stop tracking escaped pointers |
| `check::Location` | React to pointer dereference/access |
| `check::Bind` | React to assignment |
| `eval::Assume` | React to branch assumptions |
| `check::EndFunction` | Check at function exit |

### Common Bug Report Flow

```cpp
1. ExplodedNode *N = C.generateErrorNode(State);
   // or C.generateNonFatalErrorNode(State) for non-sink
2. auto R = std::make_unique<PathSensitiveBugReport>(BugType, msg, N);
3. R->markInteresting(Sym);        // Ensure symbol tracked in report
4. R->addRange(SourceRange);        // Highlight source code
5. R->addVisitor<MyVisitor>(args);  // Add path notes (optional)
6. C.emitReport(std::move(R));      // Submit
```

### Path Note Visitor Pattern

```cpp
class MyBugVisitor final : public BugReporterVisitor {
  SymbolRef Sym;
public:
  MyBugVisitor(SymbolRef S) : Sym(S) {}

  PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                   BugReporterContext &BRC,
                                   PathSensitiveBugReport &BR) override {
    // Check state changes between N and its predecessor
    // Return a PathDiagnosticEventPiece if the change is interesting
  }

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.AddPointer(Sym);
  }
};
```
