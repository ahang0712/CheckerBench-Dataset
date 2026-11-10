// ─────────────────────────────────────────────────────────────────────────
// Reusable CSA checker utility functions (LLVM 18 compatible)
//
// These are standalone helpers — copy the ones you need into checker.cpp.
// All functions verified to compile against LLVM 18 with:
//   clang++ -std=c++17 -fPIC -shared -fno-rtti -fno-exceptions -I/opt/llvm/include
//
// Source: adapted from KNighter project, re-validated for LLVM 18.
// ─────────────────────────────────────────────────────────────────────────

// ── Required includes for these utilities ──
// #include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
// #include "clang/AST/ParentMapContext.h"
// #include "clang/AST/ParentMap.h"
// #include "clang/Analysis/AnalysisDeclContext.h"
// #include "clang/Analysis/Analyses/Dominators.h"
// #include "clang/Analysis/CFG.h"
// #include "clang/Analysis/CFGStmtMap.h"
// #include "clang/Basic/SourceManager.h"
// #include "clang/Lex/Lexer.h"
// #include "llvm/ADT/APSInt.h"
//
// NOTE: copy the functions you need into checker.cpp, which already has:
//   using namespace clang;
//   using namespace ento;

// ─────────────────────────────────────────────────────────────────────────
// AST Tree Traversal
// ─────────────────────────────────────────────────────────────────────────

/// Walk UP the AST tree to find the nearest ancestor of type T.
/// Returns nullptr if no such ancestor exists.
template <typename T>
const T* findSpecificTypeInParents(const Stmt *S, CheckerContext &C) {
  if (!S) return nullptr;
  while (!dyn_cast<T>(S)) {
    S = C.getLocationContext()->getParentMap().getParent(S);
    if (!S) return nullptr;
  }
  return dyn_cast<T>(S);
}

/// Walk DOWN the AST tree to find a descendant of type T.
/// Returns the first match (depth-first), or nullptr.
template <typename T>
const T* findSpecificTypeInChildren(const Stmt *S) {
  if (!S) return nullptr;
  if (dyn_cast<T>(S)) return dyn_cast<T>(S);
  for (const Stmt *SubStmt : S->children()) {
    if (dyn_cast<T>(SubStmt)) return dyn_cast<T>(SubStmt);
    const T* RetVal = findSpecificTypeInChildren<T>(SubStmt);
    if (RetVal) return RetVal;
  }
  return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
// Expression Evaluation
// ─────────────────────────────────────────────────────────────────────────

/// Evaluate a compile-time constant integer expression.
/// Returns true and stores the result in EvalRes on success.
/// Only works for expressions that Clang can constant-fold.
bool EvaluateExprToInt(llvm::APSInt &EvalRes, const Expr *expr,
                       CheckerContext &C) {
  Expr::EvalResult ExprRes;
  if (expr->EvaluateAsInt(ExprRes, C.getASTContext())) {
    EvalRes = ExprRes.Val.getInt();
    return true;
  }
  return false;
}

/// Get the maximal possible value of a symbol from the current path
/// constraints.  Returns nullptr if no upper bound is known.
/// Useful for: bounds checking, overflow detection, buffer size validation.
const llvm::APSInt* inferSymbolMaxVal(SymbolRef Sym, CheckerContext &C) {
  ProgramStateRef State = C.getState();
  return State->getConstraintManager().getSymMaxVal(State, Sym);
}

/// Extract the compile-time size of a constant array from a DeclRefExpr.
/// E should be a reference to a constant-size array variable.
bool getArraySizeFromExpr(llvm::APInt &ArraySize, const Expr *E) {
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E->IgnoreImplicit())) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      QualType QT = VD->getType();
      if (const auto *ArrayType =
              dyn_cast<ConstantArrayType>(QT.getTypePtr())) {
        ArraySize = ArrayType->getSize();
        return true;
      }
    }
  }
  return false;
}

/// Extract the compile-time length of a string literal.
bool getStringSize(llvm::APInt &StringSize, const Expr *E) {
  if (const auto *SL = dyn_cast<StringLiteral>(E->IgnoreImpCasts())) {
    StringSize = llvm::APInt(32, SL->getLength());
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────
// MemRegion & State
// ─────────────────────────────────────────────────────────────────────────

/// Get the MemRegion that an expression evaluates to.
/// IMPORTANT: Do NOT call IgnoreImplicit() on E before passing it here.
/// Always call getBaseRegion() on the result.
const MemRegion* getMemRegionFromExpr(const Expr* E, CheckerContext &C) {
  ProgramStateRef State = C.getState();
  return State->getSVal(E, C.getLocationContext()).getAsRegion();
}

// ─────────────────────────────────────────────────────────────────────────
// Control Flow & Dominance
// ─────────────────────────────────────────────────────────────────────────

/// Position of a statement in a CFG block. ElementIndex follows execution
/// order; a block terminator is positioned after all ordinary elements.
struct CFGStmtPosition {
  const CFGBlock *Block = nullptr;
  unsigned ElementIndex = 0;
  bool IsValid = false;
};

/// Resolve S (including a nested sub-expression) to its executable CFG
/// position. CFGStmtMap finds the block; ParentMap then walks to the nearest
/// ancestor represented as a CFG element or block terminator.
CFGStmtPosition getCFGStmtPosition(const Stmt *S,
                                   AnalysisDeclContext &AC) {
  CFGStmtPosition Result;
  if (!S) return Result;

  CFG *Cfg = AC.getCFG();
  CFGStmtMap *StmtMap = AC.getCFGStmtMap();
  if (!Cfg || !StmtMap) return Result;

  const CFGBlock *Block = StmtMap->getBlock(S);
  if (!Block) return Result;
  Result.Block = Block;

  const ParentMap &Parents = AC.getParentMap();
  for (const Stmt *Current = S; Current;
       Current = Parents.getParent(Current)) {
    unsigned Index = 0;
    for (const CFGElement &Element : *Block) {
      if (auto CFGS = Element.getAs<CFGStmt>()) {
        if (CFGS->getStmt() == Current)
          return {Block, Index, true};
      }
      ++Index;
    }

    if (Block->getTerminatorStmt() == Current)
      return {Block, static_cast<unsigned>(Block->size()), true};
  }

  return Result;
}

/// Return whether GuardStmt dominates TargetStmt in the current function CFG.
/// Unlike block-only checks, this also respects execution order when both
/// statements map to the same block, so a guard after a vulnerable use does
/// not count. A statement dominates itself, matching standard dominance.
///
/// This proves only placement. The caller must still verify that GuardStmt's
/// condition rejects the unsafe value and that its unsafe branch terminates
/// (return/throw/noreturn) before treating TargetStmt as protected.
bool stmtDominates(const Stmt *GuardStmt, const Stmt *TargetStmt,
                   AnalysisDeclContext &AC) {
  if (!GuardStmt || !TargetStmt) return false;
  if (GuardStmt == TargetStmt) return true;

  CFG *Cfg = AC.getCFG();
  if (!Cfg) return false;

  CFGStmtPosition Guard = getCFGStmtPosition(GuardStmt, AC);
  CFGStmtPosition Target = getCFGStmtPosition(TargetStmt, AC);
  if (!Guard.Block || !Target.Block) return false;

  if (Guard.Block == Target.Block) {
    return Guard.IsValid && Target.IsValid &&
           Guard.ElementIndex < Target.ElementIndex;
  }

  CFGDomTree DT(Cfg);
  return DT.dominates(Guard.Block, Target.Block);
}

/// CheckerContext convenience overload for path-sensitive checker callbacks.
bool stmtDominates(const Stmt *GuardStmt, const Stmt *TargetStmt,
                   CheckerContext &C) {
  AnalysisDeclContext *AC = C.getCurrentAnalysisDeclContext();
  return AC && stmtDominates(GuardStmt, TargetStmt, *AC);
}

// ─────────────────────────────────────────────────────────────────────────
// Function Call Identification
// ─────────────────────────────────────────────────────────────────────────

/// Check whether the source text of an expression contains a given name.
/// LAST RESORT ONLY: substring matching is not semantic and can match comments,
/// unrelated identifiers, or patch-specific names. Prefer CallEvent's
/// getCalleeIdentifier(), direct Decl inspection, types, and symbolic state.
bool ExprHasName(const Expr *E, StringRef Name, CheckerContext &C) {
  if (!E) return false;
  const SourceManager &SM = C.getSourceManager();
  const LangOptions &LangOpts = C.getLangOpts();
  CharSourceRange Range =
      CharSourceRange::getTokenRange(E->getSourceRange());
  StringRef ExprText = Lexer::getSourceText(Range, SM, LangOpts);
  return ExprText.contains(Name);
}

/// Describes a function known to dereference one or more of its pointer
/// parameters.  Use with functionKnownToDeref().
struct KnownDerefFunction {
  const char *Name;                         ///< Function name
  llvm::SmallVector<unsigned, 4> Params;    ///< 0-based indices of deref'd params
};

/// Check whether a call targets a function that is known to dereference
/// specific pointer parameters.  If found, appends the parameter indices
/// to DerefParams and returns true.
bool functionKnownToDeref(
    const CallEvent &Call,
    llvm::SmallVectorImpl<unsigned> &DerefParams,
    llvm::ArrayRef<KnownDerefFunction> DerefTable) {
  if (const IdentifierInfo *ID = Call.getCalleeIdentifier()) {
    StringRef FnName = ID->getName();
    for (const auto &Entry : DerefTable) {
      if (FnName.equals(Entry.Name)) {
        DerefParams.append(Entry.Params.begin(), Entry.Params.end());
        return true;
      }
    }
  }
  return false;
}
