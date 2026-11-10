# LLVM 18 CSA Checker API Reference

> Quick-reference extracted from LLVM 18 header files. Grouped by header, then by class. Public API only.

---

## 1. Checker.h -- `clang/StaticAnalyzer/Core/Checker.h`

### Namespace `clang::ento::check` -- Callback Mixin Templates

All callback mixins are CRTP. A checker inherits from `Checker<check::X, check::Y, ...>` to register for those callbacks.

| Mixin | Override Signature |
|---|---|
| `ASTDecl<DECL>` | `void checkASTDecl(const DECL &D, AnalysisManager &mgr, BugReporter &BR)` |
| `ASTCodeBody` | `void checkASTCodeBody(const Decl *D, AnalysisManager &mgr, BugReporter &BR)` |
| `EndOfTranslationUnit` | `void checkEndOfTranslationUnit(const TranslationUnitDecl *TU, AnalysisManager &mgr, BugReporter &BR)` |
| `PreStmt<STMT>` | `void checkPreStmt(const STMT *S, CheckerContext &C)` |
| `PostStmt<STMT>` | `void checkPostStmt(const STMT *S, CheckerContext &C)` |
| `PreObjCMessage` | `void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C)` |
| `PostObjCMessage` | `void checkPostObjCMessage(const ObjCMethodCall &msg, CheckerContext &C)` |
| `ObjCMessageNil` | `void checkObjCMessageNil(const ObjCMethodCall &msg, CheckerContext &C)` |
| `PreCall` | `void checkPreCall(const CallEvent &Call, CheckerContext &C)` |
| `PostCall` | `void checkPostCall(const CallEvent &Call, CheckerContext &C)` |
| `Location` | `void checkLocation(SVal location, bool isLoad, const Stmt *S, CheckerContext &C)` |
| `Bind` | `void checkBind(SVal location, SVal val, const Stmt *S, CheckerContext &C)` |
| `EndAnalysis` | `void checkEndAnalysis(ExplodedGraph &G, BugReporter &BR, ExprEngine &Eng)` |
| `BeginFunction` | `void checkBeginFunction(CheckerContext &C)` |
| `EndFunction` | `void checkEndFunction(const ReturnStmt *RS, CheckerContext &C)` |
| `BranchCondition` | `void checkBranchCondition(const Stmt *Condition, CheckerContext &C)` |
| `NewAllocator` | `void checkNewAllocator(const CXXAllocatorCall &Call, CheckerContext &C)` |
| `LiveSymbols` | `void checkLiveSymbols(ProgramStateRef state, SymbolReaper &SR)` |
| `DeadSymbols` | `void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C)` |
| `RegionChanges` | `ProgramStateRef checkRegionChanges(ProgramStateRef state, const InvalidatedSymbols *invalidated, ArrayRef<const MemRegion *> Explicits, ArrayRef<const MemRegion *> Regions, const LocationContext *LCtx, const CallEvent *Call)` |
| `PointerEscape` | `ProgramStateRef checkPointerEscape(ProgramStateRef State, const InvalidatedSymbols &Escaped, const CallEvent *Call, PointerEscapeKind Kind)` |
| `ConstPointerEscape` | `ProgramStateRef checkConstPointerEscape(ProgramStateRef State, const InvalidatedSymbols &Escaped, const CallEvent *Call, PointerEscapeKind Kind)` |
| `Event<EVENT>` | `void checkEvent(const EVENT &event)` |

### Namespace `clang::ento::eval` -- Evaluation Callbacks

| Mixin | Override Signature |
|---|---|
| `eval::Assume` | `ProgramStateRef evalAssume(ProgramStateRef state, SVal cond, bool assumption)` |
| `eval::Call` | `bool evalCall(const CallEvent &Call, CheckerContext &C)` -- return true if fully handled |

### `class CheckerBase : public ProgramPointTag`

Base for all checkers. Provides tagging and name access.

```cpp
StringRef getTagDescription() const override;
CheckerNameRef getCheckerName() const;
virtual void printState(raw_ostream &Out, ProgramStateRef State,
                        const char *NL, const char *Sep) const;
```

### `class CheckerProgramPointTag : public SimpleProgramPointTag`

Tag that embeds a checker name as message provider.

```cpp
CheckerProgramPointTag(StringRef CheckerName, StringRef Msg);
CheckerProgramPointTag(const CheckerBase *Checker, StringRef Msg);
```

### `template<typename CHECK1, typename... CHECKs> class Checker : public CHECK1, public CHECKs..., public CheckerBase`

Primary checker class. Inherit from desired callback mixins.

```cpp
// Example: class MyChecker : public Checker<check::PreStmt<BinaryOperator>, check::PostCall> { ... };
```

### `template<typename EVENT> class EventDispatcher<EVENT>`

Lets a checker dispatch custom events that other checkers can listen to.

```cpp
void dispatchEvent(const EVENT &event) const;  // Fire event to all listeners
```

### `struct ImplicitNullDerefEvent`

Data structure for null dereference events.

```cpp
struct ImplicitNullDerefEvent {
  SVal Location;
  bool IsLoad;
  ExplodedNode *SinkNode;
  BugReporter *BR;
  bool IsDirectDereference;  // true = dereference in source code
  static int Tag;
};
```

---

## 2. CheckerContext.h -- `clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h`

### `class CheckerContext`

Provides contextual information for path-sensitive checkers. The primary interface for interacting with the analysis.

```cpp
// Constructor
CheckerContext(NodeBuilder &builder, ExprEngine &eng, ExplodedNode *pred,
               const ProgramPoint &loc, bool wasInlined = false);

// Accessors -- Analysis infrastructure
AnalysisManager &getAnalysisManager();
ConstraintManager &getConstraintManager();
StoreManager &getStoreManager();
ASTContext &getASTContext();
const LangOptions &getLangOpts() const;
BugReporter &getBugReporter();
const SourceManager &getSourceManager();
Preprocessor &getPreprocessor();
SValBuilder &getSValBuilder();
SymbolManager &getSymbolManager();
ProgramStateManager &getStateManager();
AnalysisDeclContext *getCurrentAnalysisDeclContext() const;

// Accessors -- Current path state
ExplodedNode *getPredecessor();           // Previous node in exploded graph
const ProgramStateRef &getState() const;  // Current program state (const ref!)
const LocationContext *getLocationContext() const;
const StackFrameContext *getStackFrame() const;
bool inTopFrame() const;                  // True if no caller context
unsigned blockCount() const;              // Visited count of current block along path
unsigned getBlockID() const;             // Current block ID
bool isDifferent();                       // True if checker modified state or emitted report

// State transitions
ExplodedNode *addTransition(ProgramStateRef State = nullptr,
                            const ProgramPointTag *Tag = nullptr);
ExplodedNode *addTransition(ProgramStateRef State, ExplodedNode *Pred,
                            const ProgramPointTag *Tag = nullptr);

// Sink nodes (stop path exploration)
ExplodedNode *generateSink(ProgramStateRef State, ExplodedNode *Pred,
                           const ProgramPointTag *Tag = nullptr);
void addSink(ProgramStateRef State = nullptr,
             const ProgramPointTag *Tag = nullptr);

// Error nodes (sink nodes for bug reports)
ExplodedNode *generateErrorNode(ProgramStateRef State = nullptr,
                                const ProgramPointTag *Tag = nullptr);
ExplodedNode *generateErrorNode(ProgramStateRef State, ExplodedNode *Pred,
                                const ProgramPointTag *Tag = nullptr);

// Non-fatal error nodes (exploration continues)
ExplodedNode *generateNonFatalErrorNode(ProgramStateRef State = nullptr,
                                        const ProgramPointTag *Tag = nullptr);
ExplodedNode *generateNonFatalErrorNode(ProgramStateRef State,
                                        ExplodedNode *Pred,
                                        const ProgramPointTag *Tag = nullptr);

// Value queries
SVal getSVal(const Stmt *S) const;  // Value of expression at this path point
static const MemRegion *getLocationRegionIfPostStore(const ExplodedNode *N);
bool isGreaterOrEqual(const Expr *E, unsigned long long Val);
bool isNegative(const Expr *E);

// Diagnostics
void emitReport(std::unique_ptr<BugReport> R);  // Emit a bug report

// Path notes (lightweight alternative to BugReporterVisitor)
// Four overloads:
const NoteTag *getNoteTag(NoteTag::Callback &&Cb, bool IsPrunable = false);
const NoteTag *getNoteTag(std::function<std::string(PathSensitiveBugReport &)> &&Cb,
                          bool IsPrunable = false);
const NoteTag *getNoteTag(std::function<std::string()> &&Cb,
                          bool IsPrunable = false);
const NoteTag *getNoteTag(StringRef Note, bool IsPrunable = false);
const NoteTag *getNoteTag(
    std::function<void(PathSensitiveBugReport &BR, llvm::raw_ostream &OS)> &&Cb,
    bool IsPrunable = false);

// Call introspection
const FunctionDecl *getCalleeDecl(const CallExpr *CE) const;
StringRef getCalleeName(const FunctionDecl *FunDecl) const;
const IdentifierInfo *getCalleeIdentifier(const CallExpr *CE) const;
StringRef getCalleeName(const CallExpr *CE) const;
static bool isCLibraryFunction(const FunctionDecl *FD,
                               StringRef Name = StringRef());

// Misc
StringRef getDeclDescription(const Decl *D);      // Word for decl in diagnostics
StringRef getMacroNameOrSpelling(SourceLocation &Loc);  // Macro or token spelling
```

---

## 3. ProgramState.h -- `clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h`

### Typedefs

```cpp
typedef std::unique_ptr<ConstraintManager>(*ConstraintManagerCreator)(ProgramStateManager &, ExprEngine *);
typedef std::unique_ptr<StoreManager>(*StoreManagerCreator)(ProgramStateManager &);
```

### `template<typename T> struct ProgramStateTrait`

Trait for using the Generic Data Map (GDM). Define a specialization with `data_type`, `key_type`, `value_type`, `lookup_type`, `context_type`, `GDMIndex()`, `MakeVoidPtr()`, `MakeData()`.

### `class ProgramState : public llvm::FoldingSetNode`

Encapsulates the abstract state: (1) expression-to-value mapping (Environment), (2) location-to-value mapping (Store), (3) constraints on symbolic values (GenericDataMap). Immutable -- operations return new `ProgramStateRef`.

```cpp
// Core accessors
ProgramStateManager &getStateManager() const;
AnalysisManager &getAnalysisManager() const;
ConstraintManager &getConstraintManager() const;
const Environment &getEnvironment() const;
Store getStore() const;
GenericDataMap getGDM() const;
BasicValueFactory &getBasicVals() const;
SymbolManager &getSymbolManager() const;
int64_t getID() const;

// Constraint assumptions
[[nodiscard]] ProgramStateRef assume(DefinedOrUnknownSVal cond, bool assumption) const;
[[nodiscard]] std::pair<ProgramStateRef, ProgramStateRef> assume(DefinedOrUnknownSVal cond) const;
[[nodiscard]] ProgramStateRef assumeInBound(DefinedOrUnknownSVal idx, DefinedOrUnknownSVal upperBound,
                                            bool assumption, QualType IndexType = QualType()) const;
[[nodiscard]] std::pair<ProgramStateRef, ProgramStateRef>
    assumeInBoundDual(DefinedOrUnknownSVal idx, DefinedOrUnknownSVal upperBound,
                      QualType IndexType = QualType()) const;
[[nodiscard]] ProgramStateRef assumeInclusiveRange(DefinedOrUnknownSVal Val,
                                                   const llvm::APSInt &From, const llvm::APSInt &To,
                                                   bool assumption) const;
[[nodiscard]] std::pair<ProgramStateRef, ProgramStateRef>
    assumeInclusiveRange(DefinedOrUnknownSVal Val, const llvm::APSInt &From,
                         const llvm::APSInt &To) const;
ConditionTruthVal isNonNull(SVal V) const;
ConditionTruthVal isNull(SVal V) const;
ConditionTruthVal areEqual(SVal Lhs, SVal Rhs) const;

// Region access
const VarRegion *getRegion(const VarDecl *D, const LocationContext *LC) const;

// Binding values to expressions (environment)
[[nodiscard]] ProgramStateRef BindExpr(const Stmt *S, const LocationContext *LCtx,
                                       SVal V, bool Invalidate = true) const;

// Binding values to locations (store)
[[nodiscard]] ProgramStateRef bindLoc(Loc location, SVal V,
                                      const LocationContext *LCtx,
                                      bool notifyChanges = true) const;
[[nodiscard]] ProgramStateRef bindLoc(SVal location, SVal V,
                                      const LocationContext *LCtx) const;
[[nodiscard]] ProgramStateRef bindDefaultInitial(SVal loc, SVal V,
                                                  const LocationContext *LCtx) const;
[[nodiscard]] ProgramStateRef bindDefaultZero(SVal loc,
                                               const LocationContext *LCtx) const;
[[nodiscard]] ProgramStateRef killBinding(Loc LV) const;

// Invalidation
[[nodiscard]] ProgramStateRef invalidateRegions(ArrayRef<const MemRegion *> Regions,
                                                 const Expr *E, unsigned BlockCount,
                                                 const LocationContext *LCtx,
                                                 bool CausesPointerEscape,
                                                 InvalidatedSymbols *IS = nullptr,
                                                 const CallEvent *Call = nullptr,
                                                 RegionAndSymbolInvalidationTraits *ITraits = nullptr) const;
[[nodiscard]] ProgramStateRef invalidateRegions(ArrayRef<SVal> Regions,
                                                 const Expr *E, unsigned BlockCount,
                                                 const LocationContext *LCtx,
                                                 bool CausesPointerEscape,
                                                 InvalidatedSymbols *IS = nullptr,
                                                 const CallEvent *Call = nullptr,
                                                 RegionAndSymbolInvalidationTraits *ITraits = nullptr) const;

// Stack frame
[[nodiscard]] ProgramStateRef enterStackFrame(const CallEvent &Call,
                                               const StackFrameContext *CalleeCtx) const;
SVal getSelfSVal(const LocationContext *LC) const;

// getLValue -- Compute lvalues
Loc getLValue(const CXXBaseSpecifier &BaseSpec, const SubRegion *Super) const;
Loc getLValue(const CXXRecordDecl *BaseClass, const SubRegion *Super, bool IsVirtual) const;
Loc getLValue(const VarDecl *D, const LocationContext *LC) const;
Loc getLValue(const CompoundLiteralExpr *literal, const LocationContext *LC) const;
SVal getLValue(const ObjCIvarDecl *decl, SVal base) const;
SVal getLValue(const FieldDecl *decl, SVal Base) const;
SVal getLValue(const IndirectFieldDecl *decl, SVal Base) const;
SVal getLValue(QualType ElementType, SVal Idx, SVal Base) const;

// getSVal -- Read values
SVal getSVal(const Stmt *S, const LocationContext *LCtx) const;
SVal getSValAsScalarOrLoc(const Stmt *Ex, const LocationContext *LCtx) const;
SVal getSVal(Loc LV, QualType T = QualType()) const;
SVal getRawSVal(Loc LV, QualType T = QualType()) const;
SVal getSVal(const MemRegion *R, QualType T = QualType()) const;
SVal getSValAsScalarOrLoc(const MemRegion *R) const;

// Reachable symbols
bool scanReachableSymbols(SVal val, SymbolVisitor &visitor) const;
bool scanReachableSymbols(llvm::iterator_range<region_iterator> Reachable,
                          SymbolVisitor &visitor) const;
template <typename CB> CB scanReachableSymbols(SVal val) const;
template <typename CB> CB scanReachableSymbols(
    llvm::iterator_range<region_iterator> Reachable) const;

// Generic Data Map (GDM) -- custom checker state
// Usage: State->set<MyTrait>(value) / State->get<MyTrait>() / etc.
template <typename T>
[[nodiscard]] ProgramStateRef add(typename ProgramStateTrait<T>::key_type K) const;
template <typename T>
typename ProgramStateTrait<T>::data_type get() const;
template <typename T>
typename ProgramStateTrait<T>::lookup_type get(typename ProgramStateTrait<T>::key_type key) const;
template <typename T>
typename ProgramStateTrait<T>::context_type get_context() const;
template <typename T>
[[nodiscard]] ProgramStateRef remove(typename ProgramStateTrait<T>::key_type K) const;
template <typename T>
[[nodiscard]] ProgramStateRef remove() const;
template <typename T>
[[nodiscard]] ProgramStateRef set(typename ProgramStateTrait<T>::data_type D) const;
template <typename T>
[[nodiscard]] ProgramStateRef set(typename ProgramStateTrait<T>::key_type K,
                                   typename ProgramStateTrait<T>::value_type E) const;
template <typename T>
bool contains(typename ProgramStateTrait<T>::key_type key) const;

// Pretty printing
void printJson(raw_ostream &Out, const LocationContext *LCtx = nullptr,
               const char *NL = "\n", unsigned int Space = 0,
               bool IsDot = false) const;
void printDOT(raw_ostream &Out, const LocationContext *LCtx = nullptr,
              unsigned int Space = 0) const;
void dump() const;

// Static profiling
static void Profile(llvm::FoldingSetNodeID &ID, const ProgramState *V);
void Profile(llvm::FoldingSetNodeID &ID) const;
```

### `class ProgramStateManager`

Factory and manager for `ProgramState` objects. Manages the Store, ConstraintManager, SValBuilder, etc.

```cpp
ProgramStateManager(ASTContext &Ctx, StoreManagerCreator CreateStoreManager,
                    ConstraintManagerCreator CreateConstraintManager,
                    llvm::BumpPtrAllocator &alloc, ExprEngine *expreng);

ProgramStateRef getInitialState(const LocationContext *InitLoc);

ASTContext &getContext();
BasicValueFactory &getBasicVals();
SValBuilder &getSValBuilder();
SymbolManager &getSymbolManager();
llvm::BumpPtrAllocator &getAllocator();
MemRegionManager &getRegionManager();
CallEventManager &getCallEventManager();
StoreManager &getStoreManager();
ConstraintManager &getConstraintManager();
ExprEngine &getOwningEngine();

ProgramStateRef removeDeadBindingsFromEnvironmentAndStore(
    ProgramStateRef St, const StackFrameContext *LCtx, SymbolReaper &SymReaper);

SVal ArrayToPointer(Loc Array, QualType ElementTy);

ProgramStateRef addGDM(ProgramStateRef St, void *Key, void *Data);
ProgramStateRef removeGDM(ProgramStateRef state, void *Key);

void iterBindings(ProgramStateRef state, StoreManager::BindingsHandler &F);

ProgramStateRef getPersistentState(ProgramState &Impl);
ProgramStateRef getPersistentStateWithGDM(ProgramStateRef FromState,
                                          ProgramStateRef GDMState);

bool haveEqualConstraints(ProgramStateRef S1, ProgramStateRef S2) const;
bool haveEqualEnvironments(ProgramStateRef S1, ProgramStateRef S2) const;
bool haveEqualStores(ProgramStateRef S1, ProgramStateRef S2) const;

// Template GDM methods mirroring ProgramState's
template <typename T> ProgramStateRef set(ProgramStateRef st, ...);
template <typename T> ProgramStateRef add(ProgramStateRef st, ...);
template <typename T> ProgramStateRef remove(ProgramStateRef st, ...);
template <typename T> typename ProgramStateTrait<T>::context_type get_context();
```

### `class ScanReachableSymbols`

Utility for traversing reachable symbols from a starting value.

```cpp
ScanReachableSymbols(ProgramStateRef st, SymbolVisitor &v);
bool scan(nonloc::LazyCompoundVal val);
bool scan(nonloc::CompoundVal val);
bool scan(SVal val);
bool scan(const MemRegion *R);
bool scan(const SymExpr *sym);
```

---

## 4. SVals.h -- `clang/StaticAnalyzer/Core/PathSensitive/SVals.h`

### SVal Hierarchy

```
SVal
  +-- UndefinedVal           (undefined/uninitialized value)
  +-- UnknownVal             (unknown value)
  +-- KnownSVal              (guaranteed not Unknown)
  +-- DefinedOrUnknownSVal   (not Undefined)
  +-- DefinedSVal            (not Unknown or Undef)
       +-- Loc               (l-value: address)
       |     +-- loc::GotoLabel
       |     +-- loc::MemRegionVal
       |     +-- loc::ConcreteInt
       +-- NonLoc            (r-value: non-address)
             +-- nonloc::SymbolVal
             +-- nonloc::ConcreteInt
             +-- nonloc::LocAsInteger
             +-- nonloc::CompoundVal
             +-- nonloc::LazyCompoundVal
             +-- nonloc::PointerToMember
```

### `class SVal`

```cpp
enum SValKind : unsigned char {
  UndefinedValKind, UnknownValKind,
  LocConcreteIntKind, LocGotoLabelKind, LocMemRegionValKind,
  NonLocSymbolValKind, NonLocConcreteIntKind, NonLocLocAsIntegerKind,
  NonLocCompoundValKind, NonLocLazyCompoundValKind, NonLocPointerToMemberKind
};

SVal();  // Creates UndefinedVal
SValKind getKind() const;

// Casting
template <typename T> T castAs() const;       // Asserting cast
template <typename T> std::optional<T> getAs() const;  // Safe cast

// Type checks
bool isUnknown() const;
bool isUndef() const;
bool isUnknownOrUndef() const;
bool isValid() const;
bool isConstant() const;
bool isConstant(int I) const;
bool isZeroConstant() const;

// Extraction
const FunctionDecl *getAsFunctionDecl() const;
SymbolRef getAsLocSymbol(bool IncludeBaseRegions = false) const;
SymbolRef getLocSymbolInBase() const;
SymbolRef getAsSymbol(bool IncludeBaseRegions = false) const;
const llvm::APSInt *getAsInteger() const;
const MemRegion *getAsRegion() const;

// Type
QualType getType(const ASTContext &) const;

// Iteration over contained symbols
llvm::iterator_range<SymExpr::symbol_iterator> symbols() const;

// Debug
void printJson(raw_ostream &Out, bool AddQuotes) const;
void dumpToStream(raw_ostream &OS) const;
void dump() const;

// Comparison / hashing
bool operator==(SVal R) const;
bool operator!=(SVal R) const;
void Profile(llvm::FoldingSetNodeID &ID) const;
```

### Loc subclasses

#### `loc::MemRegionVal`

```cpp
explicit MemRegionVal(const MemRegion *r);
const MemRegion *getRegion() const;
const MemRegion *stripCasts(bool StripBaseCasts = true) const;
template <typename REGION> const REGION *getRegionAs() const;
bool operator==(const MemRegionVal &R) const;
bool operator!=(const MemRegionVal &R) const;
```

#### `loc::ConcreteInt`

```cpp
explicit ConcreteInt(const llvm::APSInt &V);
const llvm::APSInt &getValue() const;
```

#### `loc::GotoLabel`

```cpp
explicit GotoLabel(const LabelDecl *Label);
const LabelDecl *getLabel() const;
```

### NonLoc subclasses

#### `nonloc::SymbolVal`

```cpp
explicit SymbolVal(SymbolRef Sym);
SymbolRef getSymbol() const;       // Cannot be null
bool isExpression() const;         // True if symbolic expression (not a SymbolData)
```

#### `nonloc::ConcreteInt`

```cpp
explicit ConcreteInt(const llvm::APSInt &V);
const llvm::APSInt &getValue() const;
```

#### `nonloc::LocAsInteger`

Treats a location as an integer (e.g., after pointer-to-int cast).

```cpp
Loc getLoc() const;
unsigned getNumBits() const;
```

#### `nonloc::CompoundVal`

Represents compound values (structs, arrays) with list of sub-values.

```cpp
const CompoundValData *getValue() const;
using iterator = llvm::ImmutableList<SVal>::iterator;
iterator begin() const;
iterator end() const;
```

#### `nonloc::LazyCompoundVal`

Lazy reference to a stored compound value.

```cpp
const LazyCompoundValData *getCVData() const;
const void *getStore() const;            // Might return null
const TypedValueRegion *getRegion() const;  // Cannot be null
```

#### `nonloc::PointerToMember`

```cpp
using PTMDataType = llvm::PointerUnion<const NamedDecl *, const PointerToMemberData *>;
const PTMDataType getPTMData() const;
bool isNullMemberPointer() const;
const NamedDecl *getDecl() const;
template<typename AdjustedDecl> const AdjustedDecl *getDeclAs() const;
using iterator = llvm::ImmutableList<const CXXBaseSpecifier *>::iterator;
iterator begin() const;
iterator end() const;
```

---

## 5. MemRegion.h -- `clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h`

### Region Hierarchy

```
MemRegion
  +-- MemSpaceRegion (not boundable)
  |     +-- CodeSpaceRegion
  |     +-- GlobalsSpaceRegion
  |     |     +-- StaticGlobalSpaceRegion
  |     |     +-- NonStaticGlobalSpaceRegion
  |     |           +-- GlobalSystemSpaceRegion
  |     |           +-- GlobalImmutableSpaceRegion
  |     |           +-- GlobalInternalSpaceRegion
  |     +-- HeapSpaceRegion
  |     +-- UnknownSpaceRegion
  |     +-- StackSpaceRegion
  |           +-- StackLocalsSpaceRegion
  |           +-- StackArgumentsSpaceRegion
  +-- SubRegion
        +-- AllocaRegion
        +-- TypedRegion
        |     +-- CodeTextRegion (not boundable)
        |     |     +-- FunctionCodeRegion
        |     |     +-- BlockCodeRegion
        |     +-- TypedValueRegion
        |           +-- StringRegion (not boundable)
        |           +-- ObjCStringRegion (not boundable)
        |           +-- CompoundLiteralRegion
        |           +-- DeclRegion
        |           |     +-- VarRegion
        |           |     |     +-- NonParamVarRegion
        |           |     |     +-- ParamVarRegion
        |           |     +-- FieldRegion
        |           |     +-- ObjCIvarRegion
        |           +-- CXXThisRegion
        |           +-- ElementRegion
        |           +-- CXXTempObjectRegion
        |           +-- CXXLifetimeExtendedObjectRegion
        |           +-- CXXBaseObjectRegion
        |           +-- CXXDerivedObjectRegion
        +-- BlockDataRegion
        +-- SymbolicRegion
```

### `struct RegionOffset`

Represents an offset within the top-level base region.

```cpp
const MemRegion *getRegion() const;  // Might return null
bool hasSymbolicOffset() const;
int64_t getOffset() const;           // Fails if hasSymbolicOffset()
bool isValid() const;
static const int64_t Symbolic;       // Sentinel for symbolic offset
```

### `class MemRegion : public llvm::FoldingSetNode`

Root abstract class for memory regions.

```cpp
enum Kind { /* one per subclass, generated from Regions.def */ };

Kind getKind() const;
ASTContext &getContext() const;
virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;
virtual MemRegionManager &getMemRegionManager() const = 0;

const MemSpaceRegion *getMemorySpace() const;
const MemRegion *getBaseRegion() const;         // Strip to top-level region
const MemRegion *getMostDerivedObjectRegion() const;  // Most derived C++ class instance
virtual bool isSubRegionOf(const MemRegion *R) const;
const MemRegion *StripCasts(bool StripBaseAndDerivedCasts = true) const;
const SymbolicRegion *getSymbolicBase() const;  // Might return null

bool hasStackStorage() const;
bool hasStackNonParametersStorage() const;
bool hasStackParametersStorage() const;

RegionOffset getAsOffset() const;  // Offset within top-level memory object
virtual bool isBoundable() const;

// Pretty-printing and diagnostics
std::string getString() const;
std::string getDescriptiveName(bool UseQuotes = true) const;
SourceRange sourceRange() const;
virtual bool canPrintPretty() const;
virtual void printPretty(raw_ostream &os) const;
virtual bool canPrintPrettyAsExpr() const;
virtual void printPrettyAsExpr(raw_ostream &os) const;
virtual void dumpToStream(raw_ostream &os) const;
void dump() const;

// Template casting
template <typename RegionTy> const RegionTy *getAs() const;
template <typename RegionTy> const RegionTy *castAs() const;
```

### `class MemSpaceRegion : public MemRegion`

Memory space -- not boundable. Subclasses: `CodeSpaceRegion`, `GlobalsSpaceRegion` (+ `StaticGlobalSpaceRegion`, `GlobalSystemSpaceRegion`, `GlobalImmutableSpaceRegion`, `GlobalInternalSpaceRegion`), `HeapSpaceRegion`, `UnknownSpaceRegion`, `StackSpaceRegion` (+ `StackLocalsSpaceRegion`, `StackArgumentsSpaceRegion`).

Key subclass methods:

```cpp
// StaticGlobalSpaceRegion
const CodeTextRegion *getCodeRegion() const;

// StackSpaceRegion
const StackFrameContext *getStackFrame() const;
```

### `class SubRegion : public MemRegion`

A region that is a subset of another region.

```cpp
const MemRegion *getSuperRegion() const;  // Cannot be null
MemRegionManager &getMemRegionManager() const override;
bool isSubRegionOf(const MemRegion *R) const override;
```

### `class TypedRegion : public SubRegion`

Abstract region with a type.

```cpp
virtual QualType getLocationType() const = 0;
QualType getDesugaredLocationType(ASTContext &Context) const;
bool isBoundable() const override;
```

### `class TypedValueRegion : public TypedRegion`

Abstract region with a value type.

```cpp
virtual QualType getValueType() const = 0;
QualType getLocationType() const override;  // Pointer to value type
QualType getDesugaredValueType(ASTContext &Context) const;
```

### `class SymbolicRegion : public SubRegion`

Symbolic (unknown) region representing a pointer value.

```cpp
SymbolRef getSymbol() const;           // Might return null
QualType getPointeeStaticType() const; // Type the symbol points to
bool isBoundable() const override;
```

### `class AllocaRegion : public SubRegion`

Region for `alloca`-allocated memory.

```cpp
const Expr *getExpr() const;  // Cannot be null
bool isBoundable() const override;
```

### `class StringRegion : public TypedValueRegion`

Region for string literals (not boundable).

```cpp
const StringLiteral *getStringLiteral() const;  // Cannot be null
QualType getValueType() const override;
bool isBoundable() const override;
```

### `class ObjCStringRegion : public TypedValueRegion`

Region for Objective-C string literals (not boundable).

```cpp
const ObjCStringLiteral *getObjCStringLiteral() const;  // Cannot be null
QualType getValueType() const override;
bool isBoundable() const override;
```

### `class CompoundLiteralRegion : public TypedValueRegion`

```cpp
const CompoundLiteralExpr *getLiteralExpr() const;  // Cannot be null
QualType getValueType() const override;
bool isBoundable() const override;
```

### `class DeclRegion : public TypedValueRegion`

Base for regions associated with declarations.

```cpp
virtual const ValueDecl *getDecl() const = 0;
```

### `class VarRegion : public DeclRegion`

Variable region -- base for local/global variables.

```cpp
const VarDecl *getDecl() const override = 0;
const StackFrameContext *getStackFrame() const;  // Might return null
QualType getValueType() const override;
```

#### `class NonParamVarRegion : public VarRegion`

Region for non-parameter variables (locals, globals, statics).

```cpp
const VarDecl *getDecl() const override;  // Cannot be null
bool canPrintPrettyAsExpr() const override;
void printPrettyAsExpr(raw_ostream &os) const override;
```

#### `class ParamVarRegion : public VarRegion`

Region for function parameters.

```cpp
const Expr *getOriginExpr() const;  // Cannot be null
unsigned getIndex() const;          // Parameter index
const ParmVarDecl *getDecl() const override;
QualType getValueType() const override;
```

### `class CXXThisRegion : public TypedValueRegion`

Region for the implicit `this` parameter.

```cpp
QualType getValueType() const override;
```

### `class FieldRegion : public DeclRegion`

Region for a struct/class field.

```cpp
const FieldDecl *getDecl() const override;  // Cannot be null
QualType getValueType() const override;
bool canPrintPretty() const override;
void printPretty(raw_ostream &os) const override;
bool canPrintPrettyAsExpr() const override;
void printPrettyAsExpr(raw_ostream &os) const override;
```

### `class ObjCIvarRegion : public DeclRegion`

Region for an Objective-C instance variable.

```cpp
const ObjCIvarDecl *getDecl() const override;  // Cannot be null
QualType getValueType() const override;
```

### `class ElementRegion : public TypedValueRegion`

Represents array elements and casts.

```cpp
NonLoc getIndex() const;
QualType getValueType() const override;
QualType getElementType() const;
RegionRawOffset getAsArrayOffset() const;  // Compute offset within array
```

### `class CXXTempObjectRegion : public TypedValueRegion`

C++ temporary object associated with an expression.

```cpp
const Expr *getExpr() const;                    // Cannot be null
const StackFrameContext *getStackFrame() const;  // Cannot be null
QualType getValueType() const override;
```

### `class CXXLifetimeExtendedObjectRegion : public TypedValueRegion`

C++ temporary with lifetime extended to match a reference variable.

```cpp
const Expr *getExpr() const;                          // Cannot be null
const ValueDecl *getExtendingDecl() const;            // Cannot be null
const StackFrameContext *getStackFrame() const;        // Might return null
QualType getValueType() const override;
```

### `class CXXBaseObjectRegion : public TypedValueRegion`

Base sub-object within a C++ derived object.

```cpp
const CXXRecordDecl *getDecl() const;  // Cannot be null
bool isVirtual() const;
QualType getValueType() const override;
bool canPrintPrettyAsExpr() const override;
void printPrettyAsExpr(raw_ostream &os) const override;
```

### `class CXXDerivedObjectRegion : public TypedValueRegion`

Derived-class object surrounding a C++ object (reverse direction).

```cpp
const CXXRecordDecl *getDecl() const;  // Cannot be null
QualType getValueType() const override;
```

### `class CodeTextRegion : public TypedRegion`

Abstract region for executable code (not boundable).

#### `class FunctionCodeRegion : public CodeTextRegion`

```cpp
QualType getLocationType() const override;
const NamedDecl *getDecl() const;
```

#### `class BlockCodeRegion : public CodeTextRegion`

```cpp
QualType getLocationType() const override;
const BlockDecl *getDecl() const;             // Cannot be null
AnalysisDeclContext *getAnalysisDeclContext() const;  // Cannot be null
```

### `class BlockDataRegion : public TypedRegion`

Block instance -- "code + data" (captured variables).

```cpp
const BlockCodeRegion *getCodeRegion() const;  // Cannot be null
const BlockDecl *getDecl() const;              // Cannot be null
QualType getLocationType() const override;
const VarRegion *getOriginalRegion(const VarRegion *VR) const;  // Might return null
referenced_vars_iterator referenced_vars_begin() const;
referenced_vars_iterator referenced_vars_end() const;
llvm::iterator_range<referenced_vars_iterator> referenced_vars() const;

// Iterator with:
//   const VarRegion *getCapturedRegion() const;   // The captured copy
//   const VarRegion *getOriginalRegion() const;   // The original variable
```

### `struct RegionRawOffset`

Computed byte offset within the base region.

```cpp
CharUnits getOffset() const;
const MemRegion *getRegion() const;  // Might return null
void dumpToStream(raw_ostream &os) const;
void dump() const;
```

### `class MemRegionManager`

Factory for creating `MemRegion` objects. Regions are uniqued via `FoldingSet`.

```cpp
MemRegionManager(ASTContext &c, llvm::BumpPtrAllocator &a);

ASTContext &getContext();
llvm::BumpPtrAllocator &getAllocator();

DefinedOrUnknownSVal getStaticSize(const MemRegion *MR, SValBuilder &SVB) const;

const StackLocalsSpaceRegion *getStackLocalsRegion(const StackFrameContext *STC);
const StackArgumentsSpaceRegion *getStackArgumentsRegion(const StackFrameContext *STC);
const GlobalsSpaceRegion *getGlobalsRegion(
    MemRegion::Kind K = MemRegion::GlobalInternalSpaceRegionKind,
    const CodeTextRegion *R = nullptr);
const HeapSpaceRegion *getHeapRegion();
const UnknownSpaceRegion *getUnknownRegion();
const CodeSpaceRegion *getCodeRegion();

const AllocaRegion *getAllocaRegion(const Expr *Ex, unsigned Cnt,
                                    const LocationContext *LC);
const CompoundLiteralRegion *getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                                                       const LocationContext *LC);
const CXXThisRegion *getCXXThisRegion(QualType thisPointerTy,
                                       const LocationContext *LC);
const SymbolicRegion *getSymbolicRegion(SymbolRef Sym,
                                         const MemSpaceRegion *MemSpace = nullptr);
const SymbolicRegion *getSymbolicHeapRegion(SymbolRef sym);
const StringRegion *getStringRegion(const StringLiteral *Str);
const ObjCStringRegion *getObjCStringRegion(const ObjCStringLiteral *Str);
const VarRegion *getVarRegion(const VarDecl *VD, const LocationContext *LC);
const NonParamVarRegion *getNonParamVarRegion(const VarDecl *VD,
                                               const MemRegion *superR);
const ParamVarRegion *getParamVarRegion(const Expr *OriginExpr, unsigned Index,
                                         const LocationContext *LC);
const ElementRegion *getElementRegion(QualType elementType, NonLoc Idx,
                                       const SubRegion *superRegion, ASTContext &Ctx);
const FieldRegion *getFieldRegion(const FieldDecl *fd, const SubRegion *superRegion);
const ObjCIvarRegion *getObjCIvarRegion(const ObjCIvarDecl *ivd,
                                         const SubRegion *superRegion);
const CXXTempObjectRegion *getCXXTempObjectRegion(Expr const *Ex,
                                                   LocationContext const *LC);
const CXXLifetimeExtendedObjectRegion *
    getCXXLifetimeExtendedObjectRegion(Expr const *Ex, ValueDecl const *VD,
                                        LocationContext const *LC);
const CXXLifetimeExtendedObjectRegion *
    getCXXStaticLifetimeExtendedObjectRegion(const Expr *Ex, ValueDecl const *VD);
const CXXBaseObjectRegion *getCXXBaseObjectRegion(const CXXRecordDecl *BaseClass,
                                                   const SubRegion *Super,
                                                   bool IsVirtual);
const CXXDerivedObjectRegion *getCXXDerivedObjectRegion(const CXXRecordDecl *BaseClass,
                                                         const SubRegion *Super);
const FunctionCodeRegion *getFunctionCodeRegion(const NamedDecl *FD);
const BlockCodeRegion *getBlockCodeRegion(const BlockDecl *BD, CanQualType locTy,
                                           AnalysisDeclContext *AC);
const BlockDataRegion *getBlockDataRegion(const BlockCodeRegion *bc,
                                           const LocationContext *lc,
                                           unsigned blockCount);
```

### `class RegionAndSymbolInvalidationTraits`

Controls invalidation behavior for regions and symbols.

```cpp
enum InvalidationKinds {
  TK_PreserveContents = 0x1,        // Region's contents unchanged
  TK_SuppressEscape = 0x2,          // Suppress pointer-escaping
  TK_DoNotInvalidateSuperRegion = 0x4,
  TK_EntireMemSpace = 0x8           // Invalidate entire memory space
};

void setTrait(SymbolRef Sym, InvalidationKinds IK);
void setTrait(const MemRegion *MR, InvalidationKinds IK);
bool hasTrait(SymbolRef Sym, InvalidationKinds IK) const;
bool hasTrait(const MemRegion *MR, InvalidationKinds IK) const;
```

---

## Quick API Usage Patterns

### Registering a checker (typical pattern)

```cpp
class MyChecker : public Checker<check::PreCall, check::PostStmt<ReturnStmt>,
                                 check::EndFunction> {
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostStmt(const ReturnStmt *RS, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
};
```

### Modifying state in a checker

```cpp
// Get current state
ProgramStateRef State = C.getState();

// Add a constraint
State = State->assume(SymbolVal, true);
if (!State) return;  // Infeasible branch

// Bind a value
State = State->bindLoc(location, value, C.getLocationContext());

// Read from GDM (custom checker state)
State->get<MyTrait>() / State->set<MyTrait>(value) / State->remove<MyTrait>();

// Emit a transition
C.addTransition(State);
```

### Emitting a bug report

```cpp
if (auto *BT = std::make_unique<BugType>(this, "Title", "Category")) {
  auto R = std::make_unique<PathSensitiveBugReport>(*BT, "Description", ErrorNode);
  R->addRange(SourceRange);
  C.emitReport(std::move(R));
}
```

### Adding path notes

```cpp
C.addTransition(State, C.getNoteTag("Explanation text here"));
// Or with a dynamic callback:
C.addTransition(State, C.getNoteTag(
    [](PathSensitiveBugReport &BR) { return "dynamic note"; }));
```
