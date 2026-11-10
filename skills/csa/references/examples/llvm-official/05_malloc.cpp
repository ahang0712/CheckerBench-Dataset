// Pattern: Memory Management (malloc/free, new/delete) -- most complex checker
// Key APIs used: check::DeadSymbols, check::PointerEscape,
//   check::ConstPointerEscape, check::PreStmt<ReturnStmt>, check::EndFunction,
//   check::PreCall, check::PostCall, check::NewAllocator,
//   check::PostStmt<BlockExpr>, check::PostObjCMessage, check::Location,
//   eval::Assume, REGISTER_MAP_WITH_PROGRAMSTATE, REGISTER_SET_WITH_PROGRAMSTATE,
//   CallDescriptionMap, SVB.getConjuredHeapSymbolVal(), State->BindExpr(),
//   setDynamicExtent(), BugReporterVisitor, SymbolReaper
//
// This checker demonstrates:
//   - Complex resource tracking with 4 program state maps/sets
//   - Multiple bug types (double-free, use-after-free, leak, mismatched dealloc, etc.)
//   - CallDescriptionMap for dispatching to handler methods by function name
//   - Custom BugReporterVisitor (MallocBugVisitor) for detailed path notes
//   - Multiple sub-checkers registered via the same checker instance
//   - Symbol lifetime management (addSymbolDependency)
//   - The full exploded graph interaction pattern

//=== MallocChecker.cpp - A malloc/free checker -------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a variety of memory management related checkers, such as
// leak, double free, and use-after-free.
//
// The following checkers are defined here:
//
//   * MallocChecker
//       Despite its name, it models all sorts of memory allocations and
//       de- or reallocation, including but not limited to malloc, free,
//       relloc, new, delete. It also reports on a variety of memory misuse
//       errors.
//       Many other checkers interact very closely with this checker, in fact,
//       most are merely options to this one. Other checkers may register
//       MallocChecker, but do not enable MallocChecker's reports (more details
//       to follow around its field, ChecksEnabled).
//       It also has a boolean "Optimistic" checker option, which if set to true
//       will cause the checker to model user defined memory management related
//       functions annotated via the attribute ownership_takes, ownership_holds
//       and ownership_returns.
//
//   * NewDeleteChecker
//       Enables the modeling of new, new[], delete, delete[] in MallocChecker,
//       and checks for related double-free and use-after-free errors.
//
//   * NewDeleteLeaksChecker
//       Checks for leaks related to new, new[], delete, delete[].
//       Depends on NewDeleteChecker.
//
//   * MismatchedDeallocatorChecker
//       Enables checking whether memory is deallocated with the correspending
//       allocation function in MallocChecker, such as malloc() allocated
//       regions are only freed by free(), new by delete, new[] by delete[].
//
//  InnerPointerChecker interacts very closely with MallocChecker, but unlike
//  the above checkers, it has it's own file, hence the many InnerPointerChecker
//  related headers and non-static functions.
//
//===----------------------------------------------------------------------===//

#include "AllocationState.h"
#include "InterCheckerAPI.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <functional>
#include <optional>
#include <utility>

using namespace clang;
using namespace ento;
using namespace std::placeholders;

//===----------------------------------------------------------------------===//
// The types of allocation we're modeling. This is used to check whether a
// dynamically allocated object is deallocated with the correct function, like
// not using operator delete on an object created by malloc(), or alloca regions
// aren't ever deallocated manually.
//===----------------------------------------------------------------------===//

namespace {

// Used to check correspondence between allocators and deallocators.
enum AllocationFamily {
  AF_None,
  AF_Malloc,
  AF_CXXNew,
  AF_CXXNewArray,
  AF_IfNameIndex,
  AF_Alloca,
  AF_InnerBuffer
};

} // end of anonymous namespace

/// Print names of allocators and deallocators.
static bool printMemFnName(raw_ostream &os, CheckerContext &C, const Expr *E);

/// Print expected name of an allocator based on the deallocator's family
static void printExpectedAllocName(raw_ostream &os, AllocationFamily Family);

/// Print expected name of a deallocator based on the allocator's family.
static void printExpectedDeallocName(raw_ostream &os, AllocationFamily Family);

//===----------------------------------------------------------------------===//
// The state of a symbol, in terms of memory management.
//===----------------------------------------------------------------------===//

namespace {

// KEY STATE OBJECT: RefState
// This tracks the lifecycle of an allocated pointer.
// States: Allocated -> Released | Relinquished | Escaped
//         AllocatedOfSizeZero -> (special case for zero-sized allocations)
class RefState {
  enum Kind {
    Allocated,
    AllocatedOfSizeZero,
    Released,
    Relinquished,  // Ownership transferred (e.g., passed to a "hold" function)
    Escaped        // Pointer escaped to opaque code
  };

  const Stmt *S;
  Kind K;
  AllocationFamily Family;

  RefState(Kind k, const Stmt *s, AllocationFamily family)
      : S(s), K(k), Family(family) {
    assert(family != AF_None);
  }

public:
  bool isAllocated() const { return K == Allocated; }
  bool isAllocatedOfSizeZero() const { return K == AllocatedOfSizeZero; }
  bool isReleased() const { return K == Released; }
  bool isRelinquished() const { return K == Relinquished; }
  bool isEscaped() const { return K == Escaped; }
  AllocationFamily getAllocationFamily() const { return Family; }
  const Stmt *getStmt() const { return S; }

  bool operator==(const RefState &X) const {
    return K == X.K && S == X.S && Family == X.Family;
  }

  static RefState getAllocated(AllocationFamily family, const Stmt *s) {
    return RefState(Allocated, s, family);
  }
  static RefState getAllocatedOfSizeZero(const RefState *RS) {
    return RefState(AllocatedOfSizeZero, RS->getStmt(),
                    RS->getAllocationFamily());
  }
  static RefState getReleased(AllocationFamily family, const Stmt *s) {
    return RefState(Released, s, family);
  }
  static RefState getRelinquished(AllocationFamily family, const Stmt *s) {
    return RefState(Relinquished, s, family);
  }
  static RefState getEscaped(const RefState *RS) {
    return RefState(Escaped, RS->getStmt(), RS->getAllocationFamily());
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(S);
    ID.AddInteger(Family);
  }
};

} // end of anonymous namespace

// KEY STATE MACRO #1: RegionState -- the core tracking map
// Maps SymbolRef (a pointer value) to its RefState (Allocated/Released/etc.)
REGISTER_MAP_WITH_PROGRAMSTATE(RegionState, SymbolRef, RefState)

static bool isReleased(SymbolRef Sym, CheckerContext &C);

static ProgramStateRef
MallocUpdateRefState(CheckerContext &C, const Expr *E, ProgramStateRef State,
                     AllocationFamily Family,
                     std::optional<SVal> RetVal = std::nullopt);

//===----------------------------------------------------------------------===//
// The modeling of memory reallocation.
//===----------------------------------------------------------------------===//

// KEY STATE MACRO #2: ReallocSizeZeroSymbols -- tracks zero-size realloc results
REGISTER_SET_WITH_PROGRAMSTATE(ReallocSizeZeroSymbols, SymbolRef)

namespace {

// The state of 'fromPtr' after reallocation is known to have failed.
enum OwnershipAfterReallocKind {
  OAR_ToBeFreedAfterFailure,   // realloc: need to free manually on failure
  OAR_FreeOnFailure,           // reallocf: freed on failure
  OAR_DoNotTrackAfterFailure   // don't know, don't track
};

// KEY STATE OBJECT: ReallocPair
// Stores info about what happens to the original pointer when realloc fails.
struct ReallocPair {
  SymbolRef ReallocatedSym;
  OwnershipAfterReallocKind Kind;

  ReallocPair(SymbolRef S, OwnershipAfterReallocKind K)
      : ReallocatedSym(S), Kind(K) {}
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(Kind);
    ID.AddPointer(ReallocatedSym);
  }
  bool operator==(const ReallocPair &X) const {
    return ReallocatedSym == X.ReallocatedSym &&
           Kind == X.Kind;
  }
};

} // end of anonymous namespace

// KEY STATE MACRO #3: ReallocPairs -- maps new symbol to old symbol info
REGISTER_MAP_WITH_PROGRAMSTATE(ReallocPairs, SymbolRef, ReallocPair)

static bool isStandardNewDelete(const FunctionDecl *FD);
static bool isStandardNewDelete(const CallEvent &Call) {
  if (!Call.getDecl() || !isa<FunctionDecl>(Call.getDecl()))
    return false;
  return isStandardNewDelete(cast<FunctionDecl>(Call.getDecl()));
}

//===----------------------------------------------------------------------===//
// Definition of the MallocChecker class.
//===----------------------------------------------------------------------===//

namespace {

// THE MAIN CHECKER CLASS
// Demonstrates 12 different callback registrations.
class MallocChecker
    : public Checker<check::DeadSymbols, check::PointerEscape,
                     check::ConstPointerEscape, check::PreStmt<ReturnStmt>,
                     check::EndFunction, check::PreCall, check::PostCall,
                     check::NewAllocator, check::PostStmt<BlockExpr>,
                     check::PostObjCMessage, check::Location, eval::Assume> {
public:
  bool ShouldIncludeOwnershipAnnotatedFunctions = false;
  bool ShouldRegisterNoOwnershipChangeVisitor = false;

  // Sub-checker kinds -- each can be independently enabled.
  enum CheckKind {
    CK_MallocChecker,
    CK_NewDeleteChecker,
    CK_NewDeleteLeaksChecker,
    CK_MismatchedDeallocatorChecker,
    CK_InnerPointerChecker,
    CK_NumCheckKinds
  };

  using LeakInfo = std::pair<const ExplodedNode *, const MemRegion *>;

  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkNewAllocator(const CXXAllocatorCall &Call, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &Call, CheckerContext &C) const;
  void checkPostStmt(const BlockExpr *BE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *S, CheckerContext &C) const;
  ProgramStateRef evalAssume(ProgramStateRef state, SVal Cond,
                            bool Assumption) const;
  void checkLocation(SVal l, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                    const InvalidatedSymbols &Escaped,
                                    const CallEvent *Call,
                                    PointerEscapeKind Kind) const;
  ProgramStateRef checkConstPointerEscape(ProgramStateRef State,
                                          const InvalidatedSymbols &Escaped,
                                          const CallEvent *Call,
                                          PointerEscapeKind Kind) const;
  void printState(raw_ostream &Out, ProgramStateRef State,
                  const char *NL, const char *Sep) const override;

private:
  // One BugType per error kind, per sub-checker.
  mutable std::unique_ptr<BugType> BT_DoubleFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_DoubleDelete;
  mutable std::unique_ptr<BugType> BT_Leak[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_UseFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_BadFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_FreeAlloca[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_MismatchedDealloc;
  mutable std::unique_ptr<BugType> BT_OffsetFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_UseZerroAllocated[CK_NumCheckKinds];

#define CHECK_FN(NAME)                                                         \
  void NAME(const CallEvent &Call, CheckerContext &C) const;

  // Individual handler methods for specific allocation functions.
  CHECK_FN(checkFree)
  CHECK_FN(checkIfNameIndex)
  CHECK_FN(checkBasicAlloc)
  CHECK_FN(checkKernelMalloc)
  CHECK_FN(checkCalloc)
  CHECK_FN(checkAlloca)
  CHECK_FN(checkStrdup)
  CHECK_FN(checkIfFreeNameIndex)
  CHECK_FN(checkCXXNewOrCXXDelete)
  CHECK_FN(checkGMalloc0)
  CHECK_FN(checkGMemdup)
  CHECK_FN(checkGMallocN)
  CHECK_FN(checkGMallocN0)
  CHECK_FN(checkReallocN)
  CHECK_FN(checkOwnershipAttr)

  void checkRealloc(const CallEvent &Call, CheckerContext &C,
                    bool ShouldFreeOnFail) const;

  using CheckFn = std::function<void(const MallocChecker *,
                                     const CallEvent &Call, CheckerContext &C)>;

  // KEY PATTERN: CallDescriptionMap -- maps function signatures to handlers.
  // This is the standard way to dispatch calls to specific processing functions.
  const CallDescriptionMap<CheckFn> FreeingMemFnMap{
      {{{"free"}, 1}, &MallocChecker::checkFree},
      {{{"if_freenameindex"}, 1}, &MallocChecker::checkIfFreeNameIndex},
      {{{"kfree"}, 1}, &MallocChecker::checkFree},
      {{{"g_free"}, 1}, &MallocChecker::checkFree},
  };

  bool isFreeingCall(const CallEvent &Call) const;
  static bool isFreeingOwnershipAttrCall(const FunctionDecl *Func);

  friend class NoOwnershipChangeVisitor;

  CallDescriptionMap<CheckFn> AllocatingMemFnMap{
      {{{"alloca"}, 1}, &MallocChecker::checkAlloca},
      {{{"_alloca"}, 1}, &MallocChecker::checkAlloca},
      {{{"malloc"}, 1}, &MallocChecker::checkBasicAlloc},
      {{{"malloc"}, 3}, &MallocChecker::checkKernelMalloc},
      {{{"calloc"}, 2}, &MallocChecker::checkCalloc},
      {{{"valloc"}, 1}, &MallocChecker::checkBasicAlloc},
      {{CDF_MaybeBuiltin, {"strndup"}, 2}, &MallocChecker::checkStrdup},
      {{CDF_MaybeBuiltin, {"strdup"}, 1}, &MallocChecker::checkStrdup},
      {{{"_strdup"}, 1}, &MallocChecker::checkStrdup},
      {{{"kmalloc"}, 2}, &MallocChecker::checkKernelMalloc},
      {{{"if_nameindex"}, 1}, &MallocChecker::checkIfNameIndex},
      {{CDF_MaybeBuiltin, {"wcsdup"}, 1}, &MallocChecker::checkStrdup},
      {{CDF_MaybeBuiltin, {"_wcsdup"}, 1}, &MallocChecker::checkStrdup},
      {{{"g_malloc"}, 1}, &MallocChecker::checkBasicAlloc},
      {{{"g_malloc0"}, 1}, &MallocChecker::checkGMalloc0},
      {{{"g_try_malloc"}, 1}, &MallocChecker::checkBasicAlloc},
      {{{"g_try_malloc0"}, 1}, &MallocChecker::checkGMalloc0},
      {{{"g_memdup"}, 2}, &MallocChecker::checkGMemdup},
      {{{"g_malloc_n"}, 2}, &MallocChecker::checkGMallocN},
      {{{"g_malloc0_n"}, 2}, &MallocChecker::checkGMallocN0},
      {{{"g_try_malloc_n"}, 2}, &MallocChecker::checkGMallocN},
      {{{"g_try_malloc0_n"}, 2}, &MallocChecker::checkGMallocN0},
  };

  CallDescriptionMap<CheckFn> ReallocatingMemFnMap{
      {{{"realloc"}, 2},
       std::bind(&MallocChecker::checkRealloc, _1, _2, _3, false)},
      {{{"reallocf"}, 2},
       std::bind(&MallocChecker::checkRealloc, _1, _2, _3, true)},
      {{{"g_realloc"}, 2},
       std::bind(&MallocChecker::checkRealloc, _1, _2, _3, false)},
      {{{"g_try_realloc"}, 2},
       std::bind(&MallocChecker::checkRealloc, _1, _2, _3, false)},
      {{{"g_realloc_n"}, 3}, &MallocChecker::checkReallocN},
      {{{"g_try_realloc_n"}, 3}, &MallocChecker::checkReallocN},
  };

  bool isMemCall(const CallEvent &Call) const;

  mutable std::optional<uint64_t> KernelZeroFlagVal;

  using KernelZeroSizePtrValueTy = std::optional<int>;
  mutable std::optional<KernelZeroSizePtrValueTy> KernelZeroSizePtrValue;

  [[nodiscard]] ProgramStateRef
  processNewAllocation(const CXXAllocatorCall &Call, CheckerContext &C,
                       AllocationFamily Family) const;

  [[nodiscard]] static ProgramStateRef
  ProcessZeroAllocCheck(const CallEvent &Call, const unsigned IndexOfSizeArg,
                        ProgramStateRef State,
                        std::optional<SVal> RetVal = std::nullopt);

  [[nodiscard]] ProgramStateRef
  MallocMemReturnsAttr(CheckerContext &C, const CallEvent &Call,
                       const OwnershipAttr *Att, ProgramStateRef State) const;

  [[nodiscard]] static ProgramStateRef
  MallocMemAux(CheckerContext &C, const CallEvent &Call, const Expr *SizeEx,
               SVal Init, ProgramStateRef State, AllocationFamily Family);

  [[nodiscard]] static ProgramStateRef
  MallocMemAux(CheckerContext &C, const CallEvent &Call, SVal Size, SVal Init,
               ProgramStateRef State, AllocationFamily Family);

  [[nodiscard]] std::optional<ProgramStateRef>
  performKernelMalloc(const CallEvent &Call, CheckerContext &C,
                      const ProgramStateRef &State) const;

  [[nodiscard]] ProgramStateRef
  FreeMemAttr(CheckerContext &C, const CallEvent &Call,
              const OwnershipAttr *Att, ProgramStateRef State) const;

  [[nodiscard]] ProgramStateRef
  FreeMemAux(CheckerContext &C, const CallEvent &Call, ProgramStateRef State,
             unsigned Num, bool Hold, bool &IsKnownToBeAllocated,
             AllocationFamily Family, bool ReturnsNullOnFailure = false) const;

  [[nodiscard]] ProgramStateRef
  FreeMemAux(CheckerContext &C, const Expr *ArgExpr, const CallEvent &Call,
             ProgramStateRef State, bool Hold, bool &IsKnownToBeAllocated,
             AllocationFamily Family, bool ReturnsNullOnFailure = false) const;

  [[nodiscard]] ProgramStateRef
  ReallocMemAux(CheckerContext &C, const CallEvent &Call, bool ShouldFreeOnFail,
                ProgramStateRef State, AllocationFamily Family,
                bool SuffixWithN = false) const;

  [[nodiscard]] static SVal evalMulForBufferSize(CheckerContext &C,
                                                 const Expr *Blocks,
                                                 const Expr *BlockBytes);

  [[nodiscard]] static ProgramStateRef
  CallocMem(CheckerContext &C, const CallEvent &Call, ProgramStateRef State);

  bool suppressDeallocationsInSuspiciousContexts(const CallEvent &Call,
                                                 CheckerContext &C) const;

  bool checkUseAfterFree(SymbolRef Sym, CheckerContext &C, const Stmt *S) const;
  void checkUseZeroAllocated(SymbolRef Sym, CheckerContext &C,
                             const Stmt *S) const;
  bool checkDoubleDelete(SymbolRef Sym, CheckerContext &C) const;

  bool mayFreeAnyEscapedMemoryOrIsModeledExplicitly(const CallEvent *Call,
                                   ProgramStateRef State,
                                   SymbolRef &EscapingSymbol) const;

  [[nodiscard]] ProgramStateRef
  checkPointerEscapeAux(ProgramStateRef State,
                        const InvalidatedSymbols &Escaped,
                        const CallEvent *Call, PointerEscapeKind Kind,
                        bool IsConstPointerEscape) const;

  void checkEscapeOnReturn(const ReturnStmt *S, CheckerContext &C) const;

  std::optional<CheckKind> getCheckIfTracked(AllocationFamily Family,
                                             bool IsALeakCheck = false) const;
  std::optional<CheckKind> getCheckIfTracked(CheckerContext &C, SymbolRef Sym,
                                             bool IsALeakCheck = false) const;

  static bool SummarizeValue(raw_ostream &os, SVal V);
  static bool SummarizeRegion(raw_ostream &os, const MemRegion *MR);

  void HandleNonHeapDealloc(CheckerContext &C, SVal ArgVal, SourceRange Range,
                            const Expr *DeallocExpr,
                            AllocationFamily Family) const;
  void HandleFreeAlloca(CheckerContext &C, SVal ArgVal,
                        SourceRange Range) const;
  void HandleMismatchedDealloc(CheckerContext &C, SourceRange Range,
                               const Expr *DeallocExpr, const RefState *RS,
                               SymbolRef Sym, bool OwnershipTransferred) const;
  void HandleOffsetFree(CheckerContext &C, SVal ArgVal, SourceRange Range,
                        const Expr *DeallocExpr, AllocationFamily Family,
                        const Expr *AllocExpr = nullptr) const;
  void HandleUseAfterFree(CheckerContext &C, SourceRange Range,
                          SymbolRef Sym) const;
  void HandleDoubleFree(CheckerContext &C, SourceRange Range, bool Released,
                        SymbolRef Sym, SymbolRef PrevSym) const;
  void HandleDoubleDelete(CheckerContext &C, SymbolRef Sym) const;
  void HandleUseZeroAlloc(CheckerContext &C, SourceRange Range,
                          SymbolRef Sym) const;
  void HandleFunctionPtrFree(CheckerContext &C, SVal ArgVal, SourceRange Range,
                             const Expr *FreeExpr,
                             AllocationFamily Family) const;

  static LeakInfo getAllocationSite(const ExplodedNode *N, SymbolRef Sym,
                                    CheckerContext &C);
  void HandleLeak(SymbolRef Sym, ExplodedNode *N, CheckerContext &C) const;

  bool isArgZERO_SIZE_PTR(ProgramStateRef State, CheckerContext &C,
                          SVal ArgVal) const;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// NoOwnershipChangeVisitor -- path note visitor for leak reports
//===----------------------------------------------------------------------===//

namespace {
// This visitor checks whether ownership of a symbol changed inside a
// function call. If not, it adds a note saying "Returning without
// deallocating memory or storing the pointer for later deallocation".
class NoOwnershipChangeVisitor final : public NoStateChangeFuncVisitor {
  SymbolRef Sym;
  const MallocChecker &Checker;
  using OwnerSet = llvm::SmallPtrSet<const MemRegion *, 8>;

  class OwnershipBindingsHandler : public StoreManager::BindingsHandler {
    SymbolRef Sym;
    OwnerSet &Owners;
  public:
    OwnershipBindingsHandler(SymbolRef Sym, OwnerSet &Owners)
        : Sym(Sym), Owners(Owners) {}
    bool HandleBinding(StoreManager &SMgr, Store Store, const MemRegion *Region,
                       SVal Val) override {
      if (Val.getAsSymbol() == Sym)
        Owners.insert(Region);
      return true;
    }
  };

protected:
  OwnerSet getOwnersAtNode(const ExplodedNode *N) {
    OwnerSet Ret;
    ProgramStateRef State = N->getState();
    OwnershipBindingsHandler Handler{Sym, Ret};
    State->getStateManager().getStoreManager().iterBindings(State->getStore(),
                                                            Handler);
    return Ret;
  }

  static std::string getFunctionName(const ExplodedNode *CallEnterN) {
    if (const CallExpr *CE = llvm::dyn_cast_or_null<CallExpr>(
            CallEnterN->getLocationAs<CallEnter>()->getCallExpr()))
      if (const FunctionDecl *FD = CE->getDirectCallee())
        return FD->getQualifiedNameAsString();
    return "";
  }

  bool isFreeingCallAsWritten(const CallExpr &Call) const {
    if (Checker.FreeingMemFnMap.lookupAsWritten(Call) ||
        Checker.ReallocatingMemFnMap.lookupAsWritten(Call))
      return true;
    if (const auto *Func =
            llvm::dyn_cast_or_null<FunctionDecl>(Call.getCalleeDecl()))
      return MallocChecker::isFreeingOwnershipAttrCall(Func);
    return false;
  }

  bool doesFnIntendToHandleOwnership(const Decl *Callee, ASTContext &ACtx) {
    using namespace clang::ast_matchers;
    const FunctionDecl *FD = dyn_cast<FunctionDecl>(Callee);
    if (!FD || !FD->hasBody())
      return false;
    auto Matches = match(findAll(stmt(anyOf(cxxDeleteExpr().bind("delete"),
                                            callExpr().bind("call")))),
                         *FD->getBody(), ACtx);
    for (BoundNodes Match : Matches) {
      if (Match.getNodeAs<CXXDeleteExpr>("delete"))
        return true;
      if (const auto *Call = Match.getNodeAs<CallExpr>("call"))
        if (isFreeingCallAsWritten(*Call))
          return true;
    }
    return false;
  }

  bool wasModifiedInFunction(const ExplodedNode *CallEnterN,
                             const ExplodedNode *CallExitEndN) override {
    if (!doesFnIntendToHandleOwnership(
            CallExitEndN->getFirstPred()->getLocationContext()->getDecl(),
            CallExitEndN->getState()->getAnalysisManager().getASTContext()))
      return true;
    if (CallEnterN->getState()->get<RegionState>(Sym) !=
        CallExitEndN->getState()->get<RegionState>(Sym))
      return true;
    OwnerSet CurrOwners = getOwnersAtNode(CallEnterN);
    OwnerSet ExitOwners = getOwnersAtNode(CallExitEndN);
    return !llvm::set_is_subset(ExitOwners, CurrOwners);
  }

  static PathDiagnosticPieceRef emitNote(const ExplodedNode *N) {
    PathDiagnosticLocation L = PathDiagnosticLocation::create(
        N->getLocation(),
        N->getState()->getStateManager().getContext().getSourceManager());
    return std::make_shared<PathDiagnosticEventPiece>(
        L, "Returning without deallocating memory or storing the pointer for "
           "later deallocation");
  }

  PathDiagnosticPieceRef
  maybeEmitNoteForObjCSelf(PathSensitiveBugReport &R,
                           const ObjCMethodCall &Call,
                           const ExplodedNode *N) override { return nullptr; }

  PathDiagnosticPieceRef
  maybeEmitNoteForCXXThis(PathSensitiveBugReport &R,
                          const CXXConstructorCall &Call,
                          const ExplodedNode *N) override { return nullptr; }

  PathDiagnosticPieceRef
  maybeEmitNoteForParameters(PathSensitiveBugReport &R, const CallEvent &Call,
                             const ExplodedNode *N) override {
    ArrayRef<ParmVarDecl *> Parameters = Call.parameters();
    for (unsigned I = 0; I < Call.getNumArgs() && I < Parameters.size(); ++I) {
      SVal V = Call.getArgSVal(I);
      if (V.getAsSymbol() == Sym)
        return emitNote(N);
    }
    return nullptr;
  }

public:
  NoOwnershipChangeVisitor(SymbolRef Sym, const MallocChecker *Checker)
      : NoStateChangeFuncVisitor(bugreporter::TrackingKind::Thorough), Sym(Sym),
        Checker(*Checker) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int Tag = 0;
    ID.AddPointer(&Tag);
    ID.AddPointer(Sym);
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// MallocBugVisitor -- detailed path note visitor for all memory bugs
//===----------------------------------------------------------------------===//

namespace {
// KEY PATTERN: Custom BugReporterVisitor for path notes.
// This visitor walks the exploded graph and emits notes at key points:
//   - "Memory is allocated" at the allocation site
//   - "Memory is released" at the free/delete site
//   - "Memory ownership is transferred" at a hold/take site
//   - "Reallocation failed" at realloc failure points
class MallocBugVisitor final : public BugReporterVisitor {
protected:
  enum NotificationMode { Normal, ReallocationFailed };
  SymbolRef Sym;
  NotificationMode Mode;
  SymbolRef FailedReallocSymbol;
  const StackFrameContext *ReleaseDestructorLC;
  bool IsLeak;

public:
  MallocBugVisitor(SymbolRef S, bool isLeak = false)
      : Sym(S), Mode(Normal), FailedReallocSymbol(nullptr),
        ReleaseDestructorLC(nullptr), IsLeak(isLeak) {}

  static void *getTag() {
    static int Tag = 0;
    return &Tag;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.AddPointer(getTag());
    ID.AddPointer(Sym);
  }

  static inline bool isAllocated(const RefState *RSCurr, const RefState *RSPrev,
                                 const Stmt *Stmt) {
    return (isa_and_nonnull<CallExpr, CXXNewExpr>(Stmt) &&
            (RSCurr &&
             (RSCurr->isAllocated() || RSCurr->isAllocatedOfSizeZero())) &&
            (!RSPrev ||
             !(RSPrev->isAllocated() || RSPrev->isAllocatedOfSizeZero())));
  }

  static inline bool isReleased(const RefState *RSCurr, const RefState *RSPrev,
                                const Stmt *Stmt) {
    bool IsReleased =
        (RSCurr && RSCurr->isReleased()) && (!RSPrev || !RSPrev->isReleased());
    return IsReleased;
  }

  static inline bool isRelinquished(const RefState *RSCurr,
                                    const RefState *RSPrev, const Stmt *Stmt) {
    return (
        isa_and_nonnull<CallExpr, ObjCMessageExpr, ObjCPropertyRefExpr>(Stmt) &&
        (RSCurr && RSCurr->isRelinquished()) &&
        (!RSPrev || !RSPrev->isRelinquished()));
  }

  static inline bool hasReallocFailed(const RefState *RSCurr,
                                      const RefState *RSPrev,
                                      const Stmt *Stmt) {
    return ((!isa_and_nonnull<CallExpr>(Stmt)) &&
            (RSCurr &&
             (RSCurr->isAllocated() || RSCurr->isAllocatedOfSizeZero())) &&
            (RSPrev &&
             !(RSPrev->isAllocated() || RSPrev->isAllocatedOfSizeZero())));
  }

  PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                   BugReporterContext &BRC,
                                   PathSensitiveBugReport &BR) override;

  PathDiagnosticPieceRef getEndPath(BugReporterContext &BRC,
                                    const ExplodedNode *EndPathNode,
                                    PathSensitiveBugReport &BR) override {
    if (!IsLeak)
      return nullptr;
    PathDiagnosticLocation L = BR.getLocation();
    return std::make_shared<PathDiagnosticEventPiece>(L, BR.getDescription(),
                                                      false);
  }

private:
  class StackHintGeneratorForReallocationFailed
      : public StackHintGeneratorForSymbol {
  public:
    StackHintGeneratorForReallocationFailed(SymbolRef S, StringRef M)
        : StackHintGeneratorForSymbol(S, M) {}
    std::string getMessageForArg(const Expr *ArgE, unsigned ArgIndex) override {
      ++ArgIndex;
      SmallString<200> buf;
      llvm::raw_svector_ostream os(buf);
      os << "Reallocation of " << ArgIndex << llvm::getOrdinalSuffix(ArgIndex)
         << " parameter failed";
      return std::string(os.str());
    }
    std::string getMessageForReturn(const CallExpr *CallExpr) override {
      return "Reallocation of returned value failed";
    }
  };
};

} // end anonymous namespace

// KEY STATE MACRO #4: FreeReturnValue -- tracks free() return value for failure analysis
REGISTER_MAP_WITH_PROGRAMSTATE(FreeReturnValue, SymbolRef, SymbolRef)

namespace {
class StopTrackingCallback final : public SymbolVisitor {
  ProgramStateRef state;
public:
  StopTrackingCallback(ProgramStateRef st) : state(std::move(st)) {}
  ProgramStateRef getState() const { return state; }
  bool VisitSymbol(SymbolRef sym) override {
    state = state->remove<RegionState>(sym);
    return true;
  }
};
} // end anonymous namespace

static bool isStandardNewDelete(const FunctionDecl *FD) {
  if (!FD) return false;
  OverloadedOperatorKind Kind = FD->getOverloadedOperator();
  if (Kind != OO_New && Kind != OO_Array_New && Kind != OO_Delete &&
      Kind != OO_Array_Delete)
    return false;
  SourceLocation L = FD->getLocation();
  return !L.isValid() ||
         FD->getASTContext().getSourceManager().isInSystemHeader(L);
}

//===----------------------------------------------------------------------===//
// Methods of MallocChecker and MallocBugVisitor.
//===----------------------------------------------------------------------===//

bool MallocChecker::isFreeingOwnershipAttrCall(const FunctionDecl *Func) {
  if (Func->hasAttrs()) {
    for (const auto *I : Func->specific_attrs<OwnershipAttr>()) {
      OwnershipAttr::OwnershipKind OwnKind = I->getOwnKind();
      if (OwnKind == OwnershipAttr::Takes || OwnKind == OwnershipAttr::Holds)
        return true;
    }
  }
  return false;
}

bool MallocChecker::isFreeingCall(const CallEvent &Call) const {
  if (FreeingMemFnMap.lookup(Call) || ReallocatingMemFnMap.lookup(Call))
    return true;
  if (const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl()))
    return isFreeingOwnershipAttrCall(Func);
  return false;
}

bool MallocChecker::isMemCall(const CallEvent &Call) const {
  if (FreeingMemFnMap.lookup(Call) || AllocatingMemFnMap.lookup(Call) ||
      ReallocatingMemFnMap.lookup(Call))
    return true;
  if (!ShouldIncludeOwnershipAnnotatedFunctions)
    return false;
  const auto *Func = dyn_cast<FunctionDecl>(Call.getDecl());
  return Func && Func->hasAttr<OwnershipAttr>();
}

//===----------------------------------------------------------------------===//
// Allocation modeling
//===----------------------------------------------------------------------===//

// KEY FUNCTION: MallocMemAux -- core allocation modeling
// This function:
//   1. Creates a conjured heap symbol for the return value
//   2. Binds the return value to the expression
//   3. Sets the default initial value
//   4. Records the dynamic extent (allocation size)
//   5. Marks the symbol as Allocated in RegionState
ProgramStateRef MallocChecker::MallocMemAux(CheckerContext &C,
                                            const CallEvent &Call, SVal Size,
                                            SVal Init, ProgramStateRef State,
                                            AllocationFamily Family) {
  if (!State)
    return nullptr;

  const Expr *CE = Call.getOriginExpr();

  if (!Loc::isLocType(CE->getType()))
    return nullptr;

  // KEY API: getConjuredHeapSymbolVal creates a fresh symbolic region
  // representing heap-allocated memory.
  unsigned Count = C.blockCount();
  SValBuilder &SVB = C.getSValBuilder();
  const LocationContext *LCtx = C.getPredecessor()->getLocationContext();
  DefinedSVal RetVal =
      ((Family == AF_Alloca) ? SVB.getAllocaRegionVal(CE, LCtx, Count)
                             : SVB.getConjuredHeapSymbolVal(CE, LCtx, Count)
                                   .castAs<DefinedSVal>());

  // Bind the return value of the call to the new symbol.
  State = State->BindExpr(CE, C.getLocationContext(), RetVal);

  // Set the default initial value (UndefinedVal for malloc, 0 for calloc).
  State = State->bindDefaultInitial(RetVal, Init, LCtx);

  // Record the allocation size (dynamic extent).
  if (Size.isUndef())
    Size = UnknownVal();
  State = setDynamicExtent(State, RetVal.getAsRegion(),
                           Size.castAs<DefinedOrUnknownSVal>(), SVB);

  // Mark the symbol as Allocated.
  return MallocUpdateRefState(C, CE, State, Family);
}

// Helper: update RefState for a newly allocated symbol.
static ProgramStateRef MallocUpdateRefState(CheckerContext &C, const Expr *E,
                                            ProgramStateRef State,
                                            AllocationFamily Family,
                                            std::optional<SVal> RetVal) {
  if (!State)
    return nullptr;
  if (!RetVal)
    RetVal = C.getSVal(E);
  if (!RetVal->getAs<Loc>())
    return nullptr;

  SymbolRef Sym = RetVal->getAsLocSymbol();
  assert(Sym);

  return State->set<RegionState>(Sym, RefState::getAllocated(Family, E));
}

//===----------------------------------------------------------------------===//
// Deallocation modeling
//===----------------------------------------------------------------------===//

// KEY FUNCTION: FreeMemAux -- core deallocation modeling
// This function performs ALL of the following checks:
//   1. Non-heap region -> HandleNonHeapDealloc
//   2. Alloca region -> HandleFreeAlloca
//   3. Already released -> HandleDoubleFree
//   4. Mismatched deallocator -> HandleMismatchedDealloc
//   5. Offset pointer -> HandleOffsetFree
//   6. Function pointer -> HandleFunctionPtrFree
//   7. Normal case: set state to Released or Relinquished
ProgramStateRef MallocChecker::FreeMemAux(
    CheckerContext &C, const Expr *ArgExpr, const CallEvent &Call,
    ProgramStateRef State, bool Hold, bool &IsKnownToBeAllocated,
    AllocationFamily Family, bool ReturnsNullOnFailure) const {

  if (!State)
    return nullptr;

  SVal ArgVal = C.getSVal(ArgExpr);
  if (!isa<DefinedOrUnknownSVal>(ArgVal))
    return nullptr;
  DefinedOrUnknownSVal location = ArgVal.castAs<DefinedOrUnknownSVal>();

  if (!isa<Loc>(location))
    return nullptr;

  // Check for explicit NULL case (free(NULL) is valid, no-op).
  ProgramStateRef notNullState, nullState;
  std::tie(notNullState, nullState) = State->assume(location);
  if (nullState && !notNullState)
    return nullptr;

  if (ArgVal.isUnknownOrUndef())
    return nullptr;

  const MemRegion *R = ArgVal.getAsRegion();
  const Expr *ParentExpr = Call.getOriginExpr();

  // Non-region locations (labels, fixed addresses) can't be freed.
  if (!R) {
    if (Family != AF_Malloc || !isArgZERO_SIZE_PTR(State, C, ArgVal))
      HandleNonHeapDealloc(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                           Family);
    return nullptr;
  }

  R = R->StripCasts();

  // Block data shouldn't be freed.
  if (isa<BlockDataRegion>(R)) {
    HandleNonHeapDealloc(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                         Family);
    return nullptr;
  }

  const MemSpaceRegion *MS = R->getMemorySpace();

  // Non-heap regions (stack, global) shouldn't be freed.
  if (!isa<UnknownSpaceRegion, HeapSpaceRegion>(MS)) {
    if (isa<AllocaRegion>(R))
      HandleFreeAlloca(C, ArgVal, ArgExpr->getSourceRange());
    else
      HandleNonHeapDealloc(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                           Family);
    return nullptr;
  }

  const SymbolicRegion *SrBase = dyn_cast<SymbolicRegion>(R->getBaseRegion());
  if (!SrBase)
    return nullptr;

  SymbolRef SymBase = SrBase->getSymbol();
  const RefState *RsBase = State->get<RegionState>(SymBase);
  SymbolRef PreviousRetStatusSymbol = nullptr;

  IsKnownToBeAllocated =
      RsBase && (RsBase->isAllocated() || RsBase->isAllocatedOfSizeZero());

  if (RsBase) {
    // Check: freeing alloca memory.
    if (RsBase->getAllocationFamily() == AF_Alloca) {
      HandleFreeAlloca(C, ArgVal, ArgExpr->getSourceRange());
      return nullptr;
    }

    // Check: double free.
    if ((RsBase->isReleased() || RsBase->isRelinquished()) &&
        !didPreviousFreeFail(State, SymBase, PreviousRetStatusSymbol)) {
      HandleDoubleFree(C, ParentExpr->getSourceRange(), RsBase->isReleased(),
                       SymBase, PreviousRetStatusSymbol);
      return nullptr;
    }

    // Check: mismatched deallocator.
    if (RsBase->isAllocated() || RsBase->isAllocatedOfSizeZero() ||
        RsBase->isEscaped()) {
      bool DeallocMatchesAlloc = RsBase->getAllocationFamily() == Family;
      if (!DeallocMatchesAlloc) {
        HandleMismatchedDealloc(C, ArgExpr->getSourceRange(), ParentExpr,
                                RsBase, SymBase, Hold);
        return nullptr;
      }

      // Check: offset free (freeing ptr+offset instead of ptr).
      RegionOffset Offset = R->getAsOffset();
      if (Offset.isValid() &&
          !Offset.hasSymbolicOffset() &&
          Offset.getOffset() != 0) {
        const Expr *AllocExpr = cast<Expr>(RsBase->getStmt());
        HandleOffsetFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                         Family, AllocExpr);
        return nullptr;
      }
    }
  }

  // Function pointer check.
  if (SymBase->getType()->isFunctionPointerType()) {
    HandleFunctionPtrFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                          Family);
    return nullptr;
  }

  // Clean up previous free return value info.
  State = State->remove<FreeReturnValue>(SymBase);

  // Track free() return value (for functions that return NULL on failure).
  if (ReturnsNullOnFailure) {
    SVal RetVal = C.getSVal(ParentExpr);
    SymbolRef RetStatusSymbol = RetVal.getAsSymbol();
    if (RetStatusSymbol) {
      C.getSymbolManager().addSymbolDependency(SymBase, RetStatusSymbol);
      State = State->set<FreeReturnValue>(SymBase, RetStatusSymbol);
    }
  }

  // Normal free: change state to Released or Relinquished.
  if (Hold)
    return State->set<RegionState>(SymBase,
                                   RefState::getRelinquished(Family, ParentExpr));
  return State->set<RegionState>(SymBase,
                                 RefState::getReleased(Family, ParentExpr));
}

//===----------------------------------------------------------------------===//
// checkPostCall -- main dispatch for allocation/free/realloc modeling
//===----------------------------------------------------------------------===//

void MallocChecker::checkPostCall(const CallEvent &Call,
                                  CheckerContext &C) const {
  if (C.wasInlined)
    return;
  if (!Call.getOriginExpr())
    return;

  // Dispatch to the appropriate handler based on function name.
  // This is the standard dispatch pattern using CallDescriptionMap.
  if (const CheckFn *Callback = FreeingMemFnMap.lookup(Call)) {
    (*Callback)(this, Call, C);
    return;
  }

  if (const CheckFn *Callback = AllocatingMemFnMap.lookup(Call)) {
    (*Callback)(this, Call, C);
    return;
  }

  if (const CheckFn *Callback = ReallocatingMemFnMap.lookup(Call)) {
    (*Callback)(this, Call, C);
    return;
  }

  if (isStandardNewDelete(Call)) {
    checkCXXNewOrCXXDelete(Call, C);
    return;
  }

  checkOwnershipAttr(Call, C);
}

//===----------------------------------------------------------------------===//
// checkDeadSymbols -- leak detection
//===----------------------------------------------------------------------===//

void MallocChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                     CheckerContext &C) const
{
  ProgramStateRef state = C.getState();
  RegionStateTy OldRS = state->get<RegionState>();
  RegionStateTy::Factory &F = state->get_context<RegionState>();

  RegionStateTy RS = OldRS;
  SmallVector<SymbolRef, 2> Errors;
  for (auto [Sym, State] : RS) {
    if (SymReaper.isDead(Sym)) {
      // KEY: If symbol is dead and still Allocated -> leak.
      if (State.isAllocated() || State.isAllocatedOfSizeZero())
        Errors.push_back(Sym);
      RS = F.remove(RS, Sym);
    }
  }

  if (RS == OldRS)
    return;

  // Cleanup other maps.
  ReallocPairsTy RP = state->get<ReallocPairs>();
  for (auto [Sym, ReallocPair] : RP) {
    if (SymReaper.isDead(Sym) || SymReaper.isDead(ReallocPair.ReallocatedSym)) {
      state = state->remove<ReallocPairs>(Sym);
    }
  }

  FreeReturnValueTy FR = state->get<FreeReturnValue>();
  for (auto [Sym, RetSym] : FR) {
    if (SymReaper.isDead(Sym) || SymReaper.isDead(RetSym)) {
      state = state->remove<FreeReturnValue>(Sym);
    }
  }

  if (!Errors.empty()) {
    static CheckerProgramPointTag Tag("MallocChecker", "DeadSymbolsLeak");
    ExplodedNode *N = C.generateNonFatalErrorNode(C.getState(), &Tag);
    if (N) {
      for (SymbolRef Sym : Errors) {
        HandleLeak(Sym, N, C);
      }
    }
  }

  C.addTransition(state->set<RegionState>(RS), N);
}

//===----------------------------------------------------------------------===//
// checkLocation -- use-after-free detection
//===----------------------------------------------------------------------===//

void MallocChecker::checkLocation(SVal l, bool isLoad, const Stmt *S,
                                  CheckerContext &C) const {
  SymbolRef Sym = l.getLocSymbolInBase();
  if (Sym) {
    checkUseAfterFree(Sym, C, S);
    checkUseZeroAllocated(Sym, C, S);
  }
}

//===----------------------------------------------------------------------===//
// checkPreCall -- use-after-free on arguments, delete modeling
//===----------------------------------------------------------------------===//

void MallocChecker::checkPreCall(const CallEvent &Call,
                                 CheckerContext &C) const {
  // Handle C++ delete expressions.
  if (const auto *DC = dyn_cast<CXXDeallocatorCall>(&Call)) {
    const CXXDeleteExpr *DE = DC->getOriginExpr();
    if (!ChecksEnabled[CK_NewDeleteChecker])
      if (SymbolRef Sym = C.getSVal(DE->getArgument()).getAsSymbol())
        checkUseAfterFree(Sym, C, DE->getArgument());
    if (!isStandardNewDelete(DC->getDecl()))
      return;
    ProgramStateRef State = C.getState();
    bool IsKnownToBeAllocated;
    State = FreeMemAux(C, DE->getArgument(), Call, State,
                       /*Hold*/ false, IsKnownToBeAllocated,
                       (DE->isArrayForm() ? AF_CXXNewArray : AF_CXXNew));
    C.addTransition(State);
    return;
  }

  // Handle destructors (check double delete).
  if (const auto *DC = dyn_cast<CXXDestructorCall>(&Call)) {
    SymbolRef Sym = DC->getCXXThisVal().getAsSymbol();
    if (!Sym || checkDoubleDelete(Sym, C))
      return;
  }

  // Check arguments for use-after-free.
  if (const AnyFunctionCall *FC = dyn_cast<AnyFunctionCall>(&Call)) {
    const FunctionDecl *FD = FC->getDecl();
    if (!FD)
      return;
    if (ChecksEnabled[CK_MallocChecker] && isFreeingCall(Call))
      return;
  }

  // Check C++ method 'this' for use-after-free.
  if (const CXXInstanceCall *CC = dyn_cast<CXXInstanceCall>(&Call)) {
    SymbolRef Sym = CC->getCXXThisVal().getAsSymbol();
    if (!Sym || checkUseAfterFree(Sym, C, CC->getCXXThisExpr()))
      return;
  }

  // Check all arguments for use-after-free.
  for (unsigned I = 0, E = Call.getNumArgs(); I != E; ++I) {
    SVal ArgSVal = Call.getArgSVal(I);
    if (isa<Loc>(ArgSVal)) {
      SymbolRef Sym = ArgSVal.getAsSymbol();
      if (!Sym)
        continue;
      if (checkUseAfterFree(Sym, C, Call.getArgExpr(I)))
        return;
    }
  }
}

//===----------------------------------------------------------------------===//
// evalAssume -- handle failed allocations
//===----------------------------------------------------------------------===//

ProgramStateRef MallocChecker::evalAssume(ProgramStateRef state,
                                              SVal Cond,
                                              bool Assumption) const {
  // If a tracked symbol is assumed NULL, assume allocation failed and
  // remove from tracking.
  RegionStateTy RS = state->get<RegionState>();
  for (SymbolRef Sym : llvm::make_first_range(RS)) {
    ConstraintManager &CMgr = state->getConstraintManager();
    ConditionTruthVal AllocFailed = CMgr.isNull(state, Sym);
    if (AllocFailed.isConstrainedTrue())
      state = state->remove<RegionState>(Sym);
  }

  // Handle realloc failure: if realloc returns NULL, restore the original
  // pointer's state to Allocated (since realloc didn't free it).
  ReallocPairsTy RP = state->get<ReallocPairs>();
  for (auto [Sym, ReallocPair] : RP) {
    ConstraintManager &CMgr = state->getConstraintManager();
    ConditionTruthVal AllocFailed = CMgr.isNull(state, Sym);
    if (!AllocFailed.isConstrainedTrue())
      continue;

    SymbolRef ReallocSym = ReallocPair.ReallocatedSym;
    if (const RefState *RS = state->get<RegionState>(ReallocSym)) {
      if (RS->isReleased()) {
        switch (ReallocPair.Kind) {
        case OAR_ToBeFreedAfterFailure:
          state = state->set<RegionState>(ReallocSym,
              RefState::getAllocated(RS->getAllocationFamily(), RS->getStmt()));
          break;
        case OAR_DoNotTrackAfterFailure:
          state = state->remove<RegionState>(ReallocSym);
          break;
        default:
          assert(ReallocPair.Kind == OAR_FreeOnFailure);
        }
      }
    }
    state = state->remove<ReallocPairs>(Sym);
  }

  return state;
}

//===----------------------------------------------------------------------===//
// checkPointerEscape -- stop tracking escaped pointers
//===----------------------------------------------------------------------===//

ProgramStateRef MallocChecker::checkPointerEscapeAux(
    ProgramStateRef State, const InvalidatedSymbols &Escaped,
    const CallEvent *Call, PointerEscapeKind Kind,
    bool IsConstPointerEscape) const {

  SymbolRef EscapingSymbol = nullptr;
  if (Kind == PSK_DirectEscapeOnCall &&
      !mayFreeAnyEscapedMemoryOrIsModeledExplicitly(Call, State,
                                                    EscapingSymbol) &&
      !EscapingSymbol) {
    return State;
  }

  for (SymbolRef sym : Escaped) {
    if (EscapingSymbol && EscapingSymbol != sym)
      continue;
    if (const RefState *RS = State->get<RegionState>(sym))
      if (RS->isAllocated() || RS->isAllocatedOfSizeZero())
        if (!IsConstPointerEscape || checkIfNewOrNewArrayFamily(RS))
          State = State->set<RegionState>(sym, RefState::getEscaped(RS));
  }
  return State;
}

//===----------------------------------------------------------------------===//
// Handle* methods -- bug report emission
//===----------------------------------------------------------------------===//

// KEY PATTERN: Each Handle* method follows the same pattern:
//   1. Check if the relevant check kind is enabled
//   2. C.generateErrorNode() to create a sink node
//   3. Create PathSensitiveBugReport with descriptive message
//   4. markInteresting + addRange + addVisitor
//   5. C.emitReport()

void MallocChecker::HandleUseAfterFree(CheckerContext &C, SourceRange Range,
                                       SymbolRef Sym) const {
  if (!ChecksEnabled[CK_MallocChecker] && !ChecksEnabled[CK_NewDeleteChecker] &&
      !ChecksEnabled[CK_InnerPointerChecker]) {
    C.addSink();
    return;
  }
  std::optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);
  if (!CheckKind)
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_UseFree[*CheckKind])
      BT_UseFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Use-after-free", categories::MemoryError));

    AllocationFamily AF =
        C.getState()->get<RegionState>(Sym)->getAllocationFamily();

    auto R = std::make_unique<PathSensitiveBugReport>(
        *BT_UseFree[*CheckKind],
        AF == AF_InnerBuffer
            ? "Inner pointer of container used after re/deallocation"
            : "Use of memory after it is freed",
        N);
    R->markInteresting(Sym);
    R->addRange(Range);
    R->addVisitor<MallocBugVisitor>(Sym);  // KEY: Add visitor for path notes
    C.emitReport(std::move(R));
  }
}

void MallocChecker::HandleDoubleFree(CheckerContext &C, SourceRange Range,
                                     bool Released, SymbolRef Sym,
                                     SymbolRef PrevSym) const {
  if (!ChecksEnabled[CK_MallocChecker] && !ChecksEnabled[CK_NewDeleteChecker]) {
    C.addSink();
    return;
  }
  std::optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);
  if (!CheckKind)
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_DoubleFree[*CheckKind])
      BT_DoubleFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Double free", categories::MemoryError));

    auto R = std::make_unique<PathSensitiveBugReport>(
        *BT_DoubleFree[*CheckKind],
        (Released ? "Attempt to free released memory"
                  : "Attempt to free non-owned memory"),
        N);
    R->addRange(Range);
    R->markInteresting(Sym);
    if (PrevSym)
      R->markInteresting(PrevSym);
    R->addVisitor<MallocBugVisitor>(Sym);
    C.emitReport(std::move(R));
  }
}

void MallocChecker::HandleLeak(SymbolRef Sym, ExplodedNode *N,
                               CheckerContext &C) const {
  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteLeaksChecker])
    return;

  const RefState *RS = C.getState()->get<RegionState>(Sym);
  assert(RS && "cannot leak an untracked symbol");
  AllocationFamily Family = RS->getAllocationFamily();

  if (Family == AF_Alloca)
    return;

  std::optional<MallocChecker::CheckKind> CheckKind =
      getCheckIfTracked(Family, true);
  if (!CheckKind)
    return;

  assert(N);
  if (!BT_Leak[*CheckKind]) {
    BT_Leak[*CheckKind].reset(new BugType(CheckNames[*CheckKind], "Memory leak",
                                          categories::MemoryError,
                                          /*SuppressOnSink=*/true));
  }

  PathDiagnosticLocation LocUsedForUniqueing;
  const ExplodedNode *AllocNode = nullptr;
  const MemRegion *Region = nullptr;
  std::tie(AllocNode, Region) = getAllocationSite(N, Sym, C);

  const Stmt *AllocationStmt = AllocNode->getStmtForDiagnostics();
  if (AllocationStmt)
    LocUsedForUniqueing = PathDiagnosticLocation::createBegin(AllocationStmt,
                                              C.getSourceManager(),
                                              AllocNode->getLocationContext());

  SmallString<200> buf;
  llvm::raw_svector_ostream os(buf);
  if (Region && Region->canPrintPretty()) {
    os << "Potential leak of memory pointed to by ";
    Region->printPretty(os);
  } else {
    os << "Potential memory leak";
  }

  auto R = std::make_unique<PathSensitiveBugReport>(
      *BT_Leak[*CheckKind], os.str(), N, LocUsedForUniqueing,
      AllocNode->getLocationContext()->getDecl());
  R->markInteresting(Sym);
  R->addVisitor<MallocBugVisitor>(Sym, true);
  if (ShouldRegisterNoOwnershipChangeVisitor)
    R->addVisitor<NoOwnershipChangeVisitor>(Sym, this);
  C.emitReport(std::move(R));
}

//===----------------------------------------------------------------------===//
// MallocBugVisitor::VisitNode -- path note implementation
//===----------------------------------------------------------------------===//

PathDiagnosticPieceRef MallocBugVisitor::VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   PathSensitiveBugReport &BR) {
  ProgramStateRef state = N->getState();
  ProgramStateRef statePrev = N->getFirstPred()->getState();

  const RefState *RSCurr = state->get<RegionState>(Sym);
  const RefState *RSPrev = statePrev->get<RegionState>(Sym);

  const Stmt *S = N->getStmtForDiagnostics();

  StringRef Msg;
  std::unique_ptr<StackHintGeneratorForSymbol> StackHint = nullptr;
  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);

  if (Mode == Normal) {
    if (isAllocated(RSCurr, RSPrev, S)) {
      Msg = "Memory is allocated";
      StackHint = std::make_unique<StackHintGeneratorForSymbol>(
          Sym, "Returned allocated memory");
    } else if (isReleased(RSCurr, RSPrev, S)) {
      Msg = "Memory is released";
      StackHint = std::make_unique<StackHintGeneratorForSymbol>(
          Sym, "Returning; memory was released");
    } else if (isRelinquished(RSCurr, RSPrev, S)) {
      Msg = "Memory ownership is transferred";
      StackHint = std::make_unique<StackHintGeneratorForSymbol>(Sym, "");
    } else if (hasReallocFailed(RSCurr, RSPrev, S)) {
      Mode = ReallocationFailed;
      Msg = "Reallocation failed";
      StackHint = std::make_unique<StackHintGeneratorForReallocationFailed>(
          Sym, "Reallocation failed");
    }
  }

  if (Msg.empty())
    return nullptr;

  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  auto P = std::make_shared<PathDiagnosticEventPiece>(Pos, Msg, true);
  BR.addCallStackHint(P, std::move(StackHint));
  return P;
}

//===----------------------------------------------------------------------===//
// Registration
//===----------------------------------------------------------------------===//

namespace clang {
namespace ento {
namespace allocation_state {

ProgramStateRef
markReleased(ProgramStateRef State, SymbolRef Sym, const Expr *Origin) {
  AllocationFamily Family = AF_InnerBuffer;
  return State->set<RegionState>(Sym, RefState::getReleased(Family, Origin));
}

} // end namespace allocation_state
} // end namespace ento
} // end namespace clang

void ento::registerInnerPointerCheckerAux(CheckerManager &mgr) {
  MallocChecker *checker = mgr.getChecker<MallocChecker>();
  checker->ChecksEnabled[MallocChecker::CK_InnerPointerChecker] = true;
  checker->CheckNames[MallocChecker::CK_InnerPointerChecker] =
      mgr.getCurrentCheckerName();
}

void ento::registerDynamicMemoryModeling(CheckerManager &mgr) {
  auto *checker = mgr.registerChecker<MallocChecker>();
  checker->ShouldIncludeOwnershipAnnotatedFunctions =
      mgr.getAnalyzerOptions().getCheckerBooleanOption(checker, "Optimistic");
  checker->ShouldRegisterNoOwnershipChangeVisitor =
      mgr.getAnalyzerOptions().getCheckerBooleanOption(
          checker, "AddNoOwnershipChangeNotes");
}

bool ento::shouldRegisterDynamicMemoryModeling(const CheckerManager &mgr) {
  return true;
}

// Macro to register sub-checkers that share the same MallocChecker instance.
#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    MallocChecker *checker = mgr.getChecker<MallocChecker>();                  \
    checker->ChecksEnabled[MallocChecker::CK_##name] = true;                   \
    checker->CheckNames[MallocChecker::CK_##name] =                            \
        mgr.getCurrentCheckerName();                                           \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name(const CheckerManager &mgr) { return true; }

REGISTER_CHECKER(MallocChecker)
REGISTER_CHECKER(NewDeleteChecker)
REGISTER_CHECKER(NewDeleteLeaksChecker)
REGISTER_CHECKER(MismatchedDeallocatorChecker)
