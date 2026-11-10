// Pattern: Nullability Violation Detection (_Nonnull/_Nullable)
// Key APIs used: check::Bind, check::PreCall, check::PreStmt<ReturnStmt>,
//   check::PostCall, check::PostStmt<ExplicitCastExpr>, check::PostObjCMessage,
//   check::DeadSymbols, eval::Assume, check::Location,
//   check::Event<ImplicitNullDerefEvent>, check::BeginFunction,
//   REGISTER_MAP_WITH_PROGRAMSTATE, REGISTER_TRAIT_WITH_PROGRAMSTATE,
//   getNullabilityAnnotation(), State->isNull(), BugReporterVisitor,
//   C.generateErrorNode()
//
// This checker demonstrates:
//   - Multiple check kinds registered via the same checker class
//   - REGISTER_MAP_WITH_PROGRAMSTATE for tracking nullability of regions
//   - REGISTER_TRAIT_WITH_PROGRAMSTATE for a boolean invariant flag
//   - eval::Assume to react to branch conditions
//   - check::Event<ImplicitNullDerefEvent> for null dereference detection
//   - Custom BugReporterVisitor for path notes
//   - Multiple BugType objects (one per CheckKind)
//   - The "invariant violation suppression" pattern

//===-- NullabilityChecker.cpp - Nullability checker ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker tries to find nullability violations. There are several kinds of
// possible violations:
// * Null pointer is passed to a pointer which has a _Nonnull type.
// * Null pointer is returned from a function which has a _Nonnull return type.
// * Nullable pointer is passed to a pointer which has a _Nonnull type.
// * Nullable pointer is returned from a function which has a _Nonnull return
//   type.
// * Nullable pointer is dereferenced.
//
// This checker propagates the nullability information of the pointers and looks
// for the patterns that are described above. Explicit casts are trusted and are
// considered a way to suppress false positives for this checker. The other way
// to suppress warnings would be to add asserts or guarding if statements to the
// code. In addition to the nullability propagation this checker also uses some
// heuristics to suppress potential false positives.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/Analysis/AnyCall.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace ento;

namespace {

/// Returns the most nullable nullability. This is used for message expressions
/// like [receiver method], where the nullability of this expression is either
/// the nullability of the receiver or the nullability of the return type of the
/// method, depending on which is more nullable. Contradicted is considered to
/// be the most nullable, to avoid false positive results.
Nullability getMostNullable(Nullability Lhs, Nullability Rhs) {
  return static_cast<Nullability>(
      std::min(static_cast<char>(Lhs), static_cast<char>(Rhs)));
}

const char *getNullabilityString(Nullability Nullab) {
  switch (Nullab) {
  case Nullability::Contradicted:
    return "contradicted";
  case Nullability::Nullable:
    return "nullable";
  case Nullability::Unspecified:
    return "unspecified";
  case Nullability::Nonnull:
    return "nonnull";
  }
  llvm_unreachable("Unexpected enumeration.");
  return "";
}

// These enums are used as an index to ErrorMessages array.
enum class ErrorKind : int {
  NilAssignedToNonnull,
  NilPassedToNonnull,
  NilReturnedToNonnull,
  NullableAssignedToNonnull,
  NullableReturnedToNonnull,
  NullableDereferenced,
  NullablePassedToNonnull
};

class NullabilityChecker
    : public Checker<check::Bind, check::PreCall, check::PreStmt<ReturnStmt>,
                     check::PostCall, check::PostStmt<ExplicitCastExpr>,
                     check::PostObjCMessage, check::DeadSymbols, eval::Assume,
                     check::Location, check::Event<ImplicitNullDerefEvent>,
                     check::BeginFunction> {

public:
  // If true, the checker will not diagnose nullabilility issues for calls
  // to system headers. This option is motivated by the observation that large
  // projects may have many nullability warnings. These projects may
  // find warnings about nullability annotations that they have explicitly
  // added themselves higher priority to fix than warnings on calls to system
  // libraries.
  bool NoDiagnoseCallsToSystemHeaders = false;

  void checkBind(SVal L, SVal V, const Stmt *S, CheckerContext &C) const;
  void checkPostStmt(const ExplicitCastExpr *CE, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
  void checkEvent(ImplicitNullDerefEvent Event) const;
  void checkLocation(SVal Location, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;
  void checkBeginFunction(CheckerContext &Ctx) const;
  ProgramStateRef evalAssume(ProgramStateRef State, SVal Cond,
                             bool Assumption) const;

  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

  // Multiple check kinds can be registered independently, all sharing the
  // same checker instance. Each has its own BugType and enabled flag.
  enum CheckKind {
    CK_NullPassedToNonnull,
    CK_NullReturnedFromNonnull,
    CK_NullableDereferenced,
    CK_NullablePassedToNonnull,
    CK_NullableReturnedFromNonnull,
    CK_NumCheckKinds
  };

  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BTs[CK_NumCheckKinds];

  const std::unique_ptr<BugType> &getBugType(CheckKind Kind) const {
    if (!BTs[Kind])
      BTs[Kind].reset(new BugType(CheckNames[Kind], "Nullability",
                                  categories::MemoryError));
    return BTs[Kind];
  }

  // When set to false no nullability information will be tracked in
  // NullabilityMap. It is possible to catch errors like passing a null pointer
  // to a callee that expects nonnull argument without the information that is
  // stored in the NullabilityMap. This is an optimization.
  bool NeedTracking = false;

private:
  // Custom BugReporterVisitor: adds path notes when nullability changes.
  class NullabilityBugVisitor : public BugReporterVisitor {
  public:
    NullabilityBugVisitor(const MemRegion *M) : Region(M) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Region);
    }

    PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                     BugReporterContext &BRC,
                                     PathSensitiveBugReport &BR) override;

  private:
    const MemRegion *Region;
  };

  void reportBugIfInvariantHolds(StringRef Msg, ErrorKind Error, CheckKind CK,
                                 ExplodedNode *N, const MemRegion *Region,
                                 CheckerContext &C,
                                 const Stmt *ValueExpr = nullptr,
                                 bool SuppressPath = false) const;

  void reportBug(StringRef Msg, ErrorKind Error, CheckKind CK, ExplodedNode *N,
                 const MemRegion *Region, BugReporter &BR,
                 const Stmt *ValueExpr = nullptr) const {
    const std::unique_ptr<BugType> &BT = getBugType(CK);
    auto R = std::make_unique<PathSensitiveBugReport>(*BT, Msg, N);
    if (Region) {
      R->markInteresting(Region);
      R->addVisitor<NullabilityBugVisitor>(Region);
    }
    if (ValueExpr) {
      R->addRange(ValueExpr->getSourceRange());
      if (Error == ErrorKind::NilAssignedToNonnull ||
          Error == ErrorKind::NilPassedToNonnull ||
          Error == ErrorKind::NilReturnedToNonnull)
        if (const auto *Ex = dyn_cast<Expr>(ValueExpr))
          bugreporter::trackExpressionValue(N, Ex, *R);
    }
    BR.emitReport(std::move(R));
  }

  const SymbolicRegion *getTrackRegion(SVal Val,
                                       bool CheckSuperRegion = false) const;

  bool isDiagnosableCall(const CallEvent &Call) const {
    if (NoDiagnoseCallsToSystemHeaders && Call.isInSystemHeader())
      return false;
    return true;
  }
};

// The state object for nullability tracking.
// Tracks: Nullability enum value + the source expression that determined it.
class NullabilityState {
public:
  NullabilityState(Nullability Nullab, const Stmt *Source = nullptr)
      : Nullab(Nullab), Source(Source) {}

  const Stmt *getNullabilitySource() const { return Source; }
  Nullability getValue() const { return Nullab; }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(static_cast<char>(Nullab));
    ID.AddPointer(Source);
  }

  void print(raw_ostream &Out) const {
    Out << getNullabilityString(Nullab) << "\n";
  }

private:
  Nullability Nullab;
  const Stmt *Source;
};

bool operator==(NullabilityState Lhs, NullabilityState Rhs) {
  return Lhs.getValue() == Rhs.getValue() &&
         Lhs.getNullabilitySource() == Rhs.getNullabilitySource();
}

using ObjectPropPair = std::pair<const MemRegion *, const IdentifierInfo *>;

struct ConstrainedPropertyVal {
  DefinedOrUnknownSVal Value;
  bool isConstrainedNonnull;

  ConstrainedPropertyVal(DefinedOrUnknownSVal SV)
      : Value(SV), isConstrainedNonnull(false) {}

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Value.Profile(ID);
    ID.AddInteger(isConstrainedNonnull ? 1 : 0);
  }
};

bool operator==(const ConstrainedPropertyVal &Lhs,
                const ConstrainedPropertyVal &Rhs) {
  return Lhs.Value == Rhs.Value &&
         Lhs.isConstrainedNonnull == Rhs.isConstrainedNonnull;
}

} // end anonymous namespace

// Two program state maps:
//   1) NullabilityMap: maps memory regions to their tracked nullability.
//   2) PropertyAccessesMap: tracks property access history for heuristics.
REGISTER_MAP_WITH_PROGRAMSTATE(NullabilityMap, const MemRegion *,
                               NullabilityState)
REGISTER_MAP_WITH_PROGRAMSTATE(PropertyAccessesMap, ObjectPropPair,
                               ConstrainedPropertyVal)

// A TRAIT (boolean flag) in ProgramState:
// InvariantViolated tracks whether the nullability invariant has already been
// violated on this path. Once violated, all further nullability warnings are
// suppressed on this path.
REGISTER_TRAIT_WITH_PROGRAMSTATE(InvariantViolated, bool)

enum class NullConstraint { IsNull, IsNotNull, Unknown };

static NullConstraint getNullConstraint(DefinedOrUnknownSVal Val,
                                        ProgramStateRef State) {
  ConditionTruthVal Nullness = State->isNull(Val);
  if (Nullness.isConstrainedFalse())
    return NullConstraint::IsNotNull;
  if (Nullness.isConstrainedTrue())
    return NullConstraint::IsNull;
  return NullConstraint::Unknown;
}

static bool isValidPointerType(QualType T) {
  return T->isAnyPointerType() || T->isBlockPointerType();
}

const SymbolicRegion *
NullabilityChecker::getTrackRegion(SVal Val, bool CheckSuperRegion) const {
  if (!NeedTracking)
    return nullptr;

  auto RegionSVal = Val.getAs<loc::MemRegionVal>();
  if (!RegionSVal)
    return nullptr;

  const MemRegion *Region = RegionSVal->getRegion();

  if (CheckSuperRegion) {
    if (const SubRegion *FieldReg = Region->getAs<FieldRegion>()) {
      if (const auto *ER = dyn_cast<ElementRegion>(FieldReg->getSuperRegion()))
        FieldReg = ER;
      return dyn_cast<SymbolicRegion>(FieldReg->getSuperRegion());
    }
    if (auto ElementReg = Region->getAs<ElementRegion>())
      return dyn_cast<SymbolicRegion>(ElementReg->getSuperRegion());
  }

  return dyn_cast<SymbolicRegion>(Region);
}

// NullabilityBugVisitor: adds path notes when nullability changes.
PathDiagnosticPieceRef NullabilityChecker::NullabilityBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC,
    PathSensitiveBugReport &BR) {
  ProgramStateRef State = N->getState();
  ProgramStateRef StatePrev = N->getFirstPred()->getState();

  const NullabilityState *TrackedNullab = State->get<NullabilityMap>(Region);
  const NullabilityState *TrackedNullabPrev =
      StatePrev->get<NullabilityMap>(Region);
  if (!TrackedNullab)
    return nullptr;

  if (TrackedNullabPrev &&
      TrackedNullabPrev->getValue() == TrackedNullab->getValue())
    return nullptr;

  const Stmt *S = TrackedNullab->getNullabilitySource();
  if (!S || S->getBeginLoc().isInvalid()) {
    S = N->getStmtForDiagnostics();
  }

  if (!S)
    return nullptr;

  std::string InfoText =
      (llvm::Twine("Nullability '") +
       getNullabilityString(TrackedNullab->getValue()) + "' is inferred")
          .str();

  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, InfoText, true);
}

// Check if a value stored at a _Nonnull location has been constrained to null.
static bool checkValueAtLValForInvariantViolation(ProgramStateRef State,
                                                  SVal LV, QualType T) {
  if (getNullabilityAnnotation(T) != Nullability::Nonnull)
    return false;

  auto RegionVal = LV.getAs<loc::MemRegionVal>();
  if (!RegionVal)
    return false;

  auto StoredVal = State->getSVal(*RegionVal).getAs<loc::MemRegionVal>();
  if (!StoredVal || !isa<SymbolicRegion>(StoredVal->getRegion()))
    return false;

  if (getNullConstraint(*StoredVal, State) == NullConstraint::IsNull)
    return true;

  return false;
}

// Check if any parameter is a _Nonnull pointer that has been constrained null.
static bool
checkParamsForPreconditionViolation(ArrayRef<ParmVarDecl *> Params,
                                    ProgramStateRef State,
                                    const LocationContext *LocCtxt) {
  for (const auto *ParamDecl : Params) {
    if (ParamDecl->isParameterPack())
      break;

    SVal LV = State->getLValue(ParamDecl, LocCtxt);
    if (checkValueAtLValForInvariantViolation(State, LV,
                                              ParamDecl->getType())) {
      return true;
    }
  }
  return false;
}

static bool
checkSelfIvarsForInvariantViolation(ProgramStateRef State,
                                    const LocationContext *LocCtxt) {
  auto *MD = dyn_cast<ObjCMethodDecl>(LocCtxt->getDecl());
  if (!MD || !MD->isInstanceMethod())
    return false;

  const ImplicitParamDecl *SelfDecl = LocCtxt->getSelfDecl();
  if (!SelfDecl)
    return false;

  SVal SelfVal = State->getSVal(State->getRegion(SelfDecl, LocCtxt));

  const ObjCObjectPointerType *SelfType =
      dyn_cast<ObjCObjectPointerType>(SelfDecl->getType());
  if (!SelfType)
    return false;

  const ObjCInterfaceDecl *ID = SelfType->getInterfaceDecl();
  if (!ID)
    return false;

  for (const auto *IvarDecl : ID->ivars()) {
    SVal LV = State->getLValue(IvarDecl, SelfVal);
    if (checkValueAtLValForInvariantViolation(State, LV, IvarDecl->getType())) {
      return true;
    }
  }
  return false;
}

// Check if the invariant has already been violated on this path.
// Also checks preconditions: if a _Nonnull parameter is null, the invariant
// was violated before the function even started.
static bool checkInvariantViolation(ProgramStateRef State, ExplodedNode *N,
                                    CheckerContext &C) {
  if (State->get<InvariantViolated>())
    return true;

  const LocationContext *LocCtxt = C.getLocationContext();
  const Decl *D = LocCtxt->getDecl();
  if (!D)
    return false;

  ArrayRef<ParmVarDecl*> Params;
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    Params = BD->parameters();
  else if (const auto *FD = dyn_cast<FunctionDecl>(D))
    Params = FD->parameters();
  else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    Params = MD->parameters();
  else
    return false;

  if (checkParamsForPreconditionViolation(Params, State, LocCtxt) ||
      checkSelfIvarsForInvariantViolation(State, LocCtxt)) {
    if (!N->isSink())
      C.addTransition(State->set<InvariantViolated>(true), N);
    return true;
  }
  return false;
}

// Emit a bug report only if the invariant hasn't been violated yet.
void NullabilityChecker::reportBugIfInvariantHolds(
    StringRef Msg, ErrorKind Error, CheckKind CK, ExplodedNode *N,
    const MemRegion *Region, CheckerContext &C, const Stmt *ValueExpr,
    bool SuppressPath) const {
  ProgramStateRef OriginalState = N->getState();

  if (checkInvariantViolation(OriginalState, N, C))
    return;
  if (SuppressPath) {
    OriginalState = OriginalState->set<InvariantViolated>(true);
    N = C.addTransition(OriginalState, N);
  }

  reportBug(Msg, Error, CK, N, Region, C.getBugReporter(), ValueExpr);
}

// Clean up dead symbols from the NullabilityMap.
void NullabilityChecker::checkDeadSymbols(SymbolReaper &SR,
                                          CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  NullabilityMapTy Nullabilities = State->get<NullabilityMap>();
  for (const MemRegion *Reg : llvm::make_first_range(Nullabilities)) {
    const auto *Region = Reg->getAs<SymbolicRegion>();
    assert(Region && "Non-symbolic region is tracked.");
    if (SR.isDead(Region->getSymbol())) {
      State = State->remove<NullabilityMap>(Reg);
    }
  }

  PropertyAccessesMapTy PropertyAccesses = State->get<PropertyAccessesMap>();
  for (ObjectPropPair PropKey : llvm::make_first_range(PropertyAccesses)) {
    const MemRegion *ReceiverRegion = PropKey.first;
    if (!SR.isLiveRegion(ReceiverRegion)) {
      State = State->remove<PropertyAccessesMap>(PropKey);
    }
  }

  if (checkInvariantViolation(State, C.getPredecessor(), C))
    return;
  C.addTransition(State);
}

// checkEvent: React to an ImplicitNullDerefEvent.
// This is triggered when the analyzer detects a null dereference but doesn't
// know the exact value of the pointer.
void NullabilityChecker::checkEvent(ImplicitNullDerefEvent Event) const {
  if (Event.SinkNode->getState()->get<InvariantViolated>())
    return;

  const MemRegion *Region =
      getTrackRegion(Event.Location, /*CheckSuperRegion=*/true);
  if (!Region)
    return;

  ProgramStateRef State = Event.SinkNode->getState();
  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  if (!TrackedNullability)
    return;

  if (ChecksEnabled[CK_NullableDereferenced] &&
      TrackedNullability->getValue() == Nullability::Nullable) {
    BugReporter &BR = *Event.BR;
    if (Event.IsDirectDereference)
      reportBug("Nullable pointer is dereferenced",
                ErrorKind::NullableDereferenced, CK_NullableDereferenced,
                Event.SinkNode, Region, BR);
    else {
      reportBug("Nullable pointer is passed to a callee that requires a "
                "non-null",
                ErrorKind::NullablePassedToNonnull, CK_NullableDereferenced,
                Event.SinkNode, Region, BR);
    }
  }
}

// checkBeginFunction: Track nullable parameters at function start.
void NullabilityChecker::checkBeginFunction(CheckerContext &C) const {
  if (!C.inTopFrame())
    return;

  const LocationContext *LCtx = C.getLocationContext();
  auto AbstractCall = AnyCall::forDecl(LCtx->getDecl());
  if (!AbstractCall || AbstractCall->parameters().empty())
    return;

  ProgramStateRef State = C.getState();
  for (const ParmVarDecl *Param : AbstractCall->parameters()) {
    if (!isValidPointerType(Param->getType()))
      continue;

    Nullability RequiredNullability =
        getNullabilityAnnotation(Param->getType());
    if (RequiredNullability != Nullability::Nullable)
      continue;

    const VarRegion *ParamRegion = State->getRegion(Param, LCtx);
    const MemRegion *ParamPointeeRegion =
        State->getSVal(ParamRegion).getAsRegion();
    if (!ParamPointeeRegion)
      continue;

    State = State->set<NullabilityMap>(ParamPointeeRegion,
                                       NullabilityState(RequiredNullability));
  }
  C.addTransition(State);
}

// checkLocation: When loading from a _Nonnull type, assume the loaded value
// is non-null (trust the annotation).
void NullabilityChecker::checkLocation(SVal Location, bool IsLoad,
                                       const Stmt *S,
                                       CheckerContext &Context) const {
  if (!IsLoad)
    return;

  const auto *Region =
      dyn_cast_or_null<TypedValueRegion>(Location.getAsRegion());
  if (!Region)
    return;

  ProgramStateRef State = Context.getState();

  auto StoredVal = State->getSVal(Region).getAs<loc::MemRegionVal>();
  if (!StoredVal)
    return;

  Nullability NullabilityOfTheLoadedValue =
      getNullabilityAnnotation(Region->getValueType());

  if (NullabilityOfTheLoadedValue == Nullability::Nonnull) {
    // Assume the loaded value is non-null, as annotated.
    if (ProgramStateRef NewState = State->assume(*StoredVal, true)) {
      Context.addTransition(NewState);
    }
  }
}

static const Expr *lookThroughImplicitCasts(const Expr *E) {
  return E->IgnoreImpCasts();
}

// checkPreStmt (ReturnStmt): Warn when returning null from _Nonnull function.
void NullabilityChecker::checkPreStmt(const ReturnStmt *S,
                                      CheckerContext &C) const {
  auto RetExpr = S->getRetValue();
  if (!RetExpr)
    return;

  if (!isValidPointerType(RetExpr->getType()))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  auto RetSVal = C.getSVal(S).getAs<DefinedOrUnknownSVal>();
  if (!RetSVal)
    return;

  bool InSuppressedMethodFamily = false;

  QualType RequiredRetType;
  AnalysisDeclContext *DeclCtxt =
      C.getLocationContext()->getAnalysisDeclContext();
  const Decl *D = DeclCtxt->getDecl();
  if (auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    ObjCMethodFamily Family = MD->getMethodFamily();
    if (OMF_init == Family || OMF_copy == Family || OMF_mutableCopy == Family)
      InSuppressedMethodFamily = true;

    RequiredRetType = MD->getReturnType();
  } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    RequiredRetType = FD->getReturnType();
  } else {
    return;
  }

  NullConstraint Nullness = getNullConstraint(*RetSVal, State);
  Nullability RequiredNullability = getNullabilityAnnotation(RequiredRetType);
  Nullability RetExprTypeLevelNullability =
        getNullabilityAnnotation(lookThroughImplicitCasts(RetExpr)->getType());

  bool NullReturnedFromNonNull = (RequiredNullability == Nullability::Nonnull &&
                                  Nullness == NullConstraint::IsNull);
  if (ChecksEnabled[CK_NullReturnedFromNonnull] && NullReturnedFromNonNull &&
      RetExprTypeLevelNullability != Nullability::Nonnull &&
      !InSuppressedMethodFamily && C.getLocationContext()->inTopFrame()) {
    static CheckerProgramPointTag Tag(this, "NullReturnedFromNonnull");
    ExplodedNode *N = C.generateErrorNode(State, &Tag);
    if (!N)
      return;

    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << (RetExpr->getType()->isObjCObjectPointerType() ? "nil" : "Null");
    OS << " returned from a " << C.getDeclDescription(D) <<
          " that is expected to return a non-null value";
    reportBugIfInvariantHolds(OS.str(), ErrorKind::NilReturnedToNonnull,
                              CK_NullReturnedFromNonnull, N, nullptr, C,
                              RetExpr);
    return;
  }

  if (NullReturnedFromNonNull) {
    State = State->set<InvariantViolated>(true);
    C.addTransition(State);
    return;
  }

  // Track nullable return values for later checks.
  const MemRegion *Region = getTrackRegion(*RetSVal);
  if (!Region)
    return;

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);
  if (TrackedNullability) {
    Nullability TrackedNullabValue = TrackedNullability->getValue();
    if (ChecksEnabled[CK_NullableReturnedFromNonnull] &&
        Nullness != NullConstraint::IsNotNull &&
        TrackedNullabValue == Nullability::Nullable &&
        RequiredNullability == Nullability::Nonnull) {
      static CheckerProgramPointTag Tag(this, "NullableReturnedFromNonnull");
      ExplodedNode *N = C.addTransition(State, C.getPredecessor(), &Tag);

      SmallString<256> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << "Nullable pointer is returned from a " << C.getDeclDescription(D) <<
            " that is expected to return a non-null value";

      reportBugIfInvariantHolds(OS.str(), ErrorKind::NullableReturnedToNonnull,
                                CK_NullableReturnedFromNonnull, N, Region, C);
    }
    return;
  }
  if (RequiredNullability == Nullability::Nullable) {
    State = State->set<NullabilityMap>(Region,
                                       NullabilityState(RequiredNullability,
                                                        S));
    C.addTransition(State);
  }
}

// checkPreCall: Warn when null or nullable pointer is passed to _Nonnull param.
void NullabilityChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  if (!Call.getDecl())
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  ProgramStateRef OrigState = State;

  unsigned Idx = 0;
  for (const ParmVarDecl *Param : Call.parameters()) {
    if (Param->isParameterPack())
      break;

    if (Idx >= Call.getNumArgs())
      break;

    const Expr *ArgExpr = Call.getArgExpr(Idx);
    auto ArgSVal = Call.getArgSVal(Idx++).getAs<DefinedOrUnknownSVal>();
    if (!ArgSVal)
      continue;

    if (!isValidPointerType(Param->getType()) &&
        !Param->getType()->isReferenceType())
      continue;

    NullConstraint Nullness = getNullConstraint(*ArgSVal, State);

    Nullability RequiredNullability =
        getNullabilityAnnotation(Param->getType());
    Nullability ArgExprTypeLevelNullability =
        getNullabilityAnnotation(lookThroughImplicitCasts(ArgExpr)->getType());

    unsigned ParamIdx = Param->getFunctionScopeIndex() + 1;

    // Null passed to _Nonnull parameter.
    if (ChecksEnabled[CK_NullPassedToNonnull] &&
        Nullness == NullConstraint::IsNull &&
        ArgExprTypeLevelNullability != Nullability::Nonnull &&
        RequiredNullability == Nullability::Nonnull &&
        isDiagnosableCall(Call)) {
      ExplodedNode *N = C.generateErrorNode(State);
      if (!N)
        return;

      SmallString<256> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << (Param->getType()->isObjCObjectPointerType() ? "nil" : "Null");
      OS << " passed to a callee that requires a non-null " << ParamIdx
         << llvm::getOrdinalSuffix(ParamIdx) << " parameter";
      reportBugIfInvariantHolds(OS.str(), ErrorKind::NilPassedToNonnull,
                                CK_NullPassedToNonnull, N, nullptr, C, ArgExpr,
                                /*SuppressPath=*/false);
      return;
    }

    // Nullable pointer passed to _Nonnull parameter.
    const MemRegion *Region = getTrackRegion(*ArgSVal);
    if (!Region)
      continue;

    const NullabilityState *TrackedNullability =
        State->get<NullabilityMap>(Region);

    if (TrackedNullability) {
      if (Nullness == NullConstraint::IsNotNull ||
          TrackedNullability->getValue() != Nullability::Nullable)
        continue;

      if (ChecksEnabled[CK_NullablePassedToNonnull] &&
          RequiredNullability == Nullability::Nonnull &&
          isDiagnosableCall(Call)) {
        ExplodedNode *N = C.addTransition(State);
        SmallString<256> SBuf;
        llvm::raw_svector_ostream OS(SBuf);
        OS << "Nullable pointer is passed to a callee that requires a non-null "
           << ParamIdx << llvm::getOrdinalSuffix(ParamIdx) << " parameter";
        reportBugIfInvariantHolds(OS.str(), ErrorKind::NullablePassedToNonnull,
                                  CK_NullablePassedToNonnull, N, Region, C,
                                  ArgExpr, /*SuppressPath=*/true);
        return;
      }
      if (ChecksEnabled[CK_NullableDereferenced] &&
          Param->getType()->isReferenceType()) {
        ExplodedNode *N = C.addTransition(State);
        reportBugIfInvariantHolds("Nullable pointer is dereferenced",
                                  ErrorKind::NullableDereferenced,
                                  CK_NullableDereferenced, N, Region, C,
                                  ArgExpr, /*SuppressPath=*/true);
        return;
      }
      continue;
    }
  }
  if (State != OrigState)
    C.addTransition(State);
}

// checkPostCall: Track nullable return values from function calls.
void NullabilityChecker::checkPostCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  auto Decl = Call.getDecl();
  if (!Decl)
    return;
  if (Call.getKind() == CE_ObjCMessage)
    return;
  const FunctionType *FuncType = Decl->getFunctionType();
  if (!FuncType)
    return;
  QualType ReturnType = FuncType->getReturnType();
  if (!isValidPointerType(ReturnType))
    return;
  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  const MemRegion *Region = getTrackRegion(Call.getReturnValue());
  if (!Region)
    return;

  // CG headers are misannotated. Do not warn for symbols from CG calls.
  const SourceManager &SM = C.getSourceManager();
  StringRef FilePath = SM.getFilename(SM.getSpellingLoc(Decl->getBeginLoc()));
  if (llvm::sys::path::filename(FilePath).starts_with("CG")) {
    State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
    C.addTransition(State);
    return;
  }

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  if (const Expr *E = Call.getOriginExpr())
    ReturnType = E->getType();

  if (!TrackedNullability &&
      getNullabilityAnnotation(ReturnType) == Nullability::Nullable) {
    State = State->set<NullabilityMap>(Region, Nullability::Nullable);
    C.addTransition(State);
  }
}

static Nullability getReceiverNullability(const ObjCMethodCall &M,
                                          ProgramStateRef State) {
  if (M.isReceiverSelfOrSuper()) {
    return Nullability::Nonnull;
  }
  SVal Receiver = M.getReceiverSVal();
  if (auto DefOrUnknown = Receiver.getAs<DefinedOrUnknownSVal>()) {
    NullConstraint Nullness = getNullConstraint(*DefOrUnknown, State);
    if (Nullness == NullConstraint::IsNotNull)
      return Nullability::Nonnull;
  }
  auto ValueRegionSVal = Receiver.getAs<loc::MemRegionVal>();
  if (ValueRegionSVal) {
    const MemRegion *SelfRegion = ValueRegionSVal->getRegion();
    assert(SelfRegion);

    const NullabilityState *TrackedSelfNullability =
        State->get<NullabilityMap>(SelfRegion);
    if (TrackedSelfNullability)
      return TrackedSelfNullability->getValue();
  }
  return Nullability::Unspecified;
}

// evalAssume: Track when property access values become constrained non-null.
ProgramStateRef NullabilityChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                               bool Assumption) const {
  PropertyAccessesMapTy PropertyAccesses = State->get<PropertyAccessesMap>();
  for (auto [PropKey, PropVal] : PropertyAccesses) {
    if (!PropVal.isConstrainedNonnull) {
      ConditionTruthVal IsNonNull = State->isNonNull(PropVal.Value);
      if (IsNonNull.isConstrainedTrue()) {
        ConstrainedPropertyVal Replacement = PropVal;
        Replacement.isConstrainedNonnull = true;
        State = State->set<PropertyAccessesMap>(PropKey, Replacement);
      } else if (IsNonNull.isConstrainedFalse()) {
        State = State->remove<PropertyAccessesMap>(PropKey);
      }
    }
  }
  return State;
}

// checkPostObjCMessage: Handle ObjC message nullability propagation.
void NullabilityChecker::checkPostObjCMessage(const ObjCMethodCall &M,
                                              CheckerContext &C) const {
  auto Decl = M.getDecl();
  if (!Decl)
    return;
  QualType RetType = Decl->getReturnType();
  if (!isValidPointerType(RetType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  const MemRegion *ReturnRegion = getTrackRegion(M.getReturnValue());
  if (!ReturnRegion)
    return;

  // Heuristic suppression for Cocoa APIs.
  auto Interface = Decl->getClassInterface();
  auto Name = Interface ? Interface->getName() : "";
  if (Name.starts_with("NS")) {
    if (M.isInstanceMessage() && Name.contains("Dictionary")) {
      State = State->set<NullabilityMap>(ReturnRegion, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }
    StringRef FirstSelectorSlot = M.getSelector().getNameForSlot(0);
    if (Name.contains("Array") &&
        (FirstSelectorSlot == "firstObject" ||
         FirstSelectorSlot == "lastObject")) {
      State = State->set<NullabilityMap>(ReturnRegion, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }
    if (Name.contains("String")) {
      for (auto *Param : M.parameters()) {
        if (Param->getName() == "encoding") {
          State = State->set<NullabilityMap>(ReturnRegion, Nullability::Contradicted);
          C.addTransition(State);
          return;
        }
      }
    }
  }

  const ObjCMessageExpr *Message = M.getOriginExpr();
  Nullability SelfNullability = getReceiverNullability(M, State);

  const NullabilityState *NullabilityOfReturn =
      State->get<NullabilityMap>(ReturnRegion);

  if (NullabilityOfReturn) {
    Nullability RetValTracked = NullabilityOfReturn->getValue();
    Nullability ComputedNullab =
        getMostNullable(RetValTracked, SelfNullability);
    if (ComputedNullab != RetValTracked &&
        ComputedNullab != Nullability::Unspecified) {
      const Stmt *NullabilitySource =
          ComputedNullab == RetValTracked
              ? NullabilityOfReturn->getNullabilitySource()
              : Message->getInstanceReceiver();
      State = State->set<NullabilityMap>(
          ReturnRegion, NullabilityState(ComputedNullab, NullabilitySource));
      C.addTransition(State);
    }
    return;
  }

  // Property access history for suppressing false positives.
  Nullability RetNullability = getNullabilityAnnotation(Message->getType());
  if (RetNullability != Nullability::Nonnull &&
      M.getMessageKind() == OCM_PropertyAccess && !C.wasInlined) {
    bool LookupResolved = false;
    if (const MemRegion *ReceiverRegion = getTrackRegion(M.getReceiverSVal())) {
      if (IdentifierInfo *Ident = M.getSelector().getIdentifierInfoForSlot(0)) {
        LookupResolved = true;
        ObjectPropPair Key = std::make_pair(ReceiverRegion, Ident);
        const ConstrainedPropertyVal *PrevPropVal =
            State->get<PropertyAccessesMap>(Key);
        if (PrevPropVal && PrevPropVal->isConstrainedNonnull) {
          RetNullability = Nullability::Nonnull;
        } else {
          if (auto ReturnSVal =
                  M.getReturnValue().getAs<DefinedOrUnknownSVal>()) {
            State = State->set<PropertyAccessesMap>(
                Key, ConstrainedPropertyVal(*ReturnSVal));
          }
        }
      }
    }
    if (!LookupResolved) {
      RetNullability = Nullability::Nonnull;
    }
  }

  Nullability ComputedNullab = getMostNullable(RetNullability, SelfNullability);
  if (ComputedNullab == Nullability::Nullable) {
    const Stmt *NullabilitySource = ComputedNullab == RetNullability
                                        ? Message
                                        : Message->getInstanceReceiver();
    State = State->set<NullabilityMap>(
        ReturnRegion, NullabilityState(ComputedNullab, NullabilitySource));
    C.addTransition(State);
  }
}

// Explicit casts are trusted -- they can suppress nullability warnings.
void NullabilityChecker::checkPostStmt(const ExplicitCastExpr *CE,
                                       CheckerContext &C) const {
  QualType OriginType = CE->getSubExpr()->getType();
  QualType DestType = CE->getType();
  if (!isValidPointerType(OriginType))
    return;
  if (!isValidPointerType(DestType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  Nullability DestNullability = getNullabilityAnnotation(DestType);
  if (DestNullability == Nullability::Unspecified)
    return;

  auto RegionSVal = C.getSVal(CE).getAs<DefinedOrUnknownSVal>();
  const MemRegion *Region = getTrackRegion(*RegionSVal);
  if (!Region)
    return;

  // When 0 is cast to _Nonnull, mark as contradicted.
  if (DestNullability == Nullability::Nonnull) {
    NullConstraint Nullness = getNullConstraint(*RegionSVal, State);
    if (Nullness == NullConstraint::IsNull) {
      State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }
  }

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  if (!TrackedNullability) {
    if (DestNullability != Nullability::Nullable)
      return;
    State = State->set<NullabilityMap>(Region,
                                       NullabilityState(DestNullability, CE));
    C.addTransition(State);
    return;
  }

  if (TrackedNullability->getValue() != DestNullability &&
      TrackedNullability->getValue() != Nullability::Contradicted) {
    State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
    C.addTransition(State);
  }
}

static const Expr * matchValueExprForBind(const Stmt *S) {
  if (auto *BinOp = dyn_cast<BinaryOperator>(S)) {
    if (BinOp->getOpcode() == BO_Assign)
      return BinOp->getRHS();
  }
  if (auto *DS = dyn_cast<DeclStmt>(S))  {
    if (DS->isSingleDecl()) {
      auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl());
      if (!VD)
        return nullptr;
      if (const Expr *Init = VD->getInit())
        return Init;
    }
  }
  return nullptr;
}

static bool isARCNilInitializedLocal(CheckerContext &C, const Stmt *S) {
  if (!C.getASTContext().getLangOpts().ObjCAutoRefCount)
    return false;
  auto *DS = dyn_cast<DeclStmt>(S);
  if (!DS || !DS->isSingleDecl())
    return false;
  auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl());
  if (!VD)
    return false;
  if(!VD->getType().getQualifiers().hasObjCLifetime())
    return false;
  const Expr *Init = VD->getInit();
  assert(Init && "ObjC local under ARC without initializer");
  if (!isa<ImplicitValueInitExpr>(Init))
    return false;
  return true;
}

// checkBind: Warn when null/nullable assigned to _Nonnull pointer.
void NullabilityChecker::checkBind(SVal L, SVal V, const Stmt *S,
                                   CheckerContext &C) const {
  const TypedValueRegion *TVR =
      dyn_cast_or_null<TypedValueRegion>(L.getAsRegion());
  if (!TVR)
    return;

  QualType LocType = TVR->getValueType();
  if (!isValidPointerType(LocType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  auto ValDefOrUnknown = V.getAs<DefinedOrUnknownSVal>();
  if (!ValDefOrUnknown)
    return;

  NullConstraint RhsNullness = getNullConstraint(*ValDefOrUnknown, State);

  Nullability ValNullability = Nullability::Unspecified;
  if (SymbolRef Sym = ValDefOrUnknown->getAsSymbol())
    ValNullability = getNullabilityAnnotation(Sym->getType());

  Nullability LocNullability = getNullabilityAnnotation(LocType);

  Nullability ValueExprTypeLevelNullability = Nullability::Unspecified;
  const Expr *ValueExpr = matchValueExprForBind(S);
  if (ValueExpr) {
    ValueExprTypeLevelNullability =
      getNullabilityAnnotation(lookThroughImplicitCasts(ValueExpr)->getType());
  }

  bool NullAssignedToNonNull = (LocNullability == Nullability::Nonnull &&
                                RhsNullness == NullConstraint::IsNull);
  if (ChecksEnabled[CK_NullPassedToNonnull] && NullAssignedToNonNull &&
      ValNullability != Nullability::Nonnull &&
      ValueExprTypeLevelNullability != Nullability::Nonnull &&
      !isARCNilInitializedLocal(C, S)) {
    static CheckerProgramPointTag Tag(this, "NullPassedToNonnull");
    ExplodedNode *N = C.generateErrorNode(State, &Tag);
    if (!N)
      return;

    const Stmt *ValueStmt = S;
    if (ValueExpr)
      ValueStmt = ValueExpr;

    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << (LocType->isObjCObjectPointerType() ? "nil" : "Null");
    OS << " assigned to a pointer which is expected to have non-null value";
    reportBugIfInvariantHolds(OS.str(), ErrorKind::NilAssignedToNonnull,
                              CK_NullPassedToNonnull, N, nullptr, C, ValueStmt);
    return;
  }

  if (NullAssignedToNonNull) {
    State = State->set<InvariantViolated>(true);
    C.addTransition(State);
    return;
  }

  const MemRegion *ValueRegion = getTrackRegion(*ValDefOrUnknown);
  if (!ValueRegion)
    return;

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(ValueRegion);

  if (TrackedNullability) {
    if (RhsNullness == NullConstraint::IsNotNull ||
        TrackedNullability->getValue() != Nullability::Nullable)
      return;
    if (ChecksEnabled[CK_NullablePassedToNonnull] &&
        LocNullability == Nullability::Nonnull) {
      static CheckerProgramPointTag Tag(this, "NullablePassedToNonnull");
      ExplodedNode *N = C.addTransition(State, C.getPredecessor(), &Tag);
      reportBugIfInvariantHolds("Nullable pointer is assigned to a pointer "
                                "which is expected to have non-null value",
                                ErrorKind::NullableAssignedToNonnull,
                                CK_NullablePassedToNonnull, N, ValueRegion, C);
    }
    return;
  }

  const auto *BinOp = dyn_cast<BinaryOperator>(S);

  if (ValNullability == Nullability::Nullable) {
    const Stmt *NullabilitySource = BinOp ? BinOp->getRHS() : S;
    State = State->set<NullabilityMap>(
        ValueRegion, NullabilityState(ValNullability, NullabilitySource));
    C.addTransition(State);
    return;
  }

  if (LocNullability == Nullability::Nullable) {
    const Stmt *NullabilitySource = BinOp ? BinOp->getLHS() : S;
    State = State->set<NullabilityMap>(
        ValueRegion, NullabilityState(LocNullability, NullabilitySource));
    C.addTransition(State);
  }
}

void NullabilityChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                    const char *NL, const char *Sep) const {

  NullabilityMapTy B = State->get<NullabilityMap>();

  if (State->get<InvariantViolated>())
    Out << Sep << NL
        << "Nullability invariant was violated, warnings suppressed." << NL;

  if (B.isEmpty())
    return;

  if (!State->get<InvariantViolated>())
    Out << Sep << NL;

  for (auto [Region, State] : B) {
    Out << Region << " : ";
    State.print(Out);
    Out << NL;
  }
}

void ento::registerNullabilityBase(CheckerManager &mgr) {
  mgr.registerChecker<NullabilityChecker>();
}

bool ento::shouldRegisterNullabilityBase(const CheckerManager &mgr) {
  return true;
}

// Macro to register sub-checkers that all share the same NullabilityChecker
// instance. Each sub-checker enables a specific CheckKind.
#define REGISTER_CHECKER(name, trackingRequired)                               \
  void ento::register##name##Checker(CheckerManager &mgr) {                    \
    NullabilityChecker *checker = mgr.getChecker<NullabilityChecker>();        \
    checker->ChecksEnabled[NullabilityChecker::CK_##name] = true;              \
    checker->CheckNames[NullabilityChecker::CK_##name] =                       \
        mgr.getCurrentCheckerName();                                           \
    checker->NeedTracking = checker->NeedTracking || trackingRequired;         \
    checker->NoDiagnoseCallsToSystemHeaders =                                  \
        checker->NoDiagnoseCallsToSystemHeaders ||                             \
        mgr.getAnalyzerOptions().getCheckerBooleanOption(                      \
            checker, "NoDiagnoseCallsToSystemHeaders", true);                  \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name##Checker(const CheckerManager &mgr) {        \
    return true;                                                               \
  }

REGISTER_CHECKER(NullPassedToNonnull, false)
REGISTER_CHECKER(NullReturnedFromNonnull, false)
REGISTER_CHECKER(NullableDereferenced, true)
REGISTER_CHECKER(NullablePassedToNonnull, true)
REGISTER_CHECKER(NullableReturnedFromNonnull, true)
