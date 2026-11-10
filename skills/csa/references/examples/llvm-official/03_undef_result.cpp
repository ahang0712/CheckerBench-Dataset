// Pattern: Undefined Value Detection in Binary Operations
// Key APIs used: check::PostStmt<BinaryOperator>, C.getSVal(Expr).isUndef(),
//   C.generateErrorNode(), bugreporter::trackExpressionValue,
//   State->assumeInBoundDual(), getDynamicElementCount()
//
// This checker demonstrates:
//   - Use of check::PostStmt<BinaryOperator> to inspect results AFTER evaluation.
//   - Checking C.getSVal(Expr).isUndef() to detect undefined/garbage values.
//   - The "sink node" pattern: generateErrorNode() creates a sink.
//   - Dynamic message generation with SmallString + raw_svector_ostream.
//   - Integration with array-out-of-bounds detection as root cause analysis.
//
// No REGISTER_MAP_WITH_PROGRAMSTATE needed -- this is a pure "observe and
// report" checker.

//=== UndefResultChecker.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines UndefResultChecker, a builtin check in ExprEngine that
// performs checks for undefined results of non-assignment binary operators.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
// Note the template argument: check::PostStmt<BinaryOperator>
// This means checkPostStmt is called AFTER the binary operator is evaluated.
// At this point, the result value is available via C.getSVal(B).
class UndefResultChecker
  : public Checker< check::PostStmt<BinaryOperator> > {

  const BugType BT{this, "Result of operation is garbage or undefined"};

public:
  void checkPostStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

// Helper: Check if an undefined value came from an array index out of bounds.
// This provides more specific diagnostics to the user.
//
// Key API: State->assumeInBoundDual(Index, ElementCount) splits the state
// into in-bounds and out-of-bounds paths.
static bool isArrayIndexOutOfBounds(CheckerContext &C, const Expr *Ex) {
  ProgramStateRef state = C.getState();

  if (!isa<ArraySubscriptExpr>(Ex))
    return false;

  SVal Loc = C.getSVal(Ex);
  if (!Loc.isValid())
    return false;

  const MemRegion *MR = Loc.castAs<loc::MemRegionVal>().getRegion();
  const ElementRegion *ER = dyn_cast<ElementRegion>(MR);
  if (!ER)
    return false;

  // Get the symbolic index and the dynamic element count.
  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal ElementCount = getDynamicElementCount(
      state, ER->getSuperRegion(), C.getSValBuilder(), ER->getValueType());

  // Check if index is definitely out of bounds.
  ProgramStateRef StInBound, StOutBound;
  std::tie(StInBound, StOutBound) = state->assumeInBoundDual(Idx, ElementCount);
  return StOutBound && !StInBound;
}

// CORE DETECTION LOGIC
//
// After a binary operator is evaluated, check if the result is undefined
// (i.e., one or both operands were uninitialized).
void UndefResultChecker::checkPostStmt(const BinaryOperator *B,
                                       CheckerContext &C) const {
  // The key check: is the result of this expression undefined?
  // C.getSVal(B) evaluates the binary operator expression in the current
  // program state. If any operand is uninitialized, the result is UndefinedVal.
  if (C.getSVal(B).isUndef()) {

    // False positive suppression: don't report inside swap() functions.
    if (const FunctionDecl *EnclosingFunctionDecl =
        dyn_cast<FunctionDecl>(C.getStackFrame()->getDecl()))
      if (C.getCalleeName(EnclosingFunctionDecl) == "swap")
        return;

    // Create a sink error node (analysis stops on this path).
    ExplodedNode *N = C.generateErrorNode();
    if (!N)
      return;

    // Build a diagnostic message explaining which operand was undefined.
    SmallString<256> sbuf;
    llvm::raw_svector_ostream OS(sbuf);
    const Expr *Ex = nullptr;
    bool isLeft = true;

    if (C.getSVal(B->getLHS()).isUndef()) {
      Ex = B->getLHS()->IgnoreParenCasts();
      isLeft = true;
    }
    else if (C.getSVal(B->getRHS()).isUndef()) {
      Ex = B->getRHS()->IgnoreParenCasts();
      isLeft = false;
    }

    if (Ex) {
      // "The left operand of '+' is a garbage value"
      OS << "The " << (isLeft ? "left" : "right") << " operand of '"
         << BinaryOperator::getOpcodeStr(B->getOpcode())
         << "' is a garbage value";
      // If we can determine the root cause, add it to the message.
      if (isArrayIndexOutOfBounds(C, Ex))
        OS << " due to array index out of bounds";
    } else {
      // Neither operand is undef, but the combination produces undef.
      // (This can happen e.g., with shifts by large values.)
      OS << "The result of the '"
         << BinaryOperator::getOpcodeStr(B->getOpcode())
         << "' expression is undefined";
    }

    // Create and emit the bug report.
    auto report = std::make_unique<PathSensitiveBugReport>(BT, OS.str(), N);
    if (Ex) {
      report->addRange(Ex->getSourceRange());
      // Track where the undefined value came from (adds path notes).
      bugreporter::trackExpressionValue(N, Ex, *report);
    }
    else
      bugreporter::trackExpressionValue(N, B, *report);

    C.emitReport(std::move(report));
  }
}

void ento::registerUndefResultChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefResultChecker>();
}

bool ento::shouldRegisterUndefResultChecker(const CheckerManager &mgr) {
  return true;
}
