// Pattern: Division by Zero Detection
// Key APIs used: check::PreStmt<BinaryOperator>, ConstraintManager::assumeDual,
//   C.getSVal(), C.generateErrorNode(), PathSensitiveBugReport,
//   bugreporter::trackExpressionValue, C.emitReport()
//
// This is the SIMPLEST possible CSA checker pattern.
// Key observations:
//   - No REGISTER_MAP_WITH_PROGRAMSTATE -- no persistent state needed.
//   - Only ONE callback: checkPreStmt.
//   - Uses ConstraintManager::assumeDual to split the analysis state
//     into "denominator is zero" and "denominator is non-zero" paths.
//   - If only the zero path is feasible -> bug.
//   - BugType is constructed inline as a member (no separate BugType object
//     created during registration).

//== DivZeroChecker.cpp - Division by zero checker --------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines DivZeroChecker, a builtin check in ExprEngine that performs
// checks for division by zeros.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace taint;

namespace {
// The checker class inherits from Checker<check::PreStmt<BinaryOperator>>.
// This template argument registers checkPreStmt as the callback for
// BinaryOperator statements, invoked BEFORE the operator is evaluated.
class DivZeroChecker : public Checker< check::PreStmt<BinaryOperator> > {
  // Two BugType instances: one for regular division by zero, one for
  // tainted values that might be zero. The BugType constructor takes:
  //   - this (the checker pointer, for uniquing)
  //   - name of the bug
  //   - optional category (defaults to "Logic Error")
  const BugType BT{this, "Division by zero"};
  const BugType TaintBT{this, "Division by zero", categories::TaintedData};

  // Helper to emit a standard division-by-zero report.
  void reportBug(StringRef Msg, ProgramStateRef StateZero,
                 CheckerContext &C) const;
  // Helper to emit a taint-related division-by-zero report, also marking
  // the tainted symbols as interesting for path tracking.
  void reportTaintBug(StringRef Msg, ProgramStateRef StateZero,
                      CheckerContext &C,
                      llvm::ArrayRef<SymbolRef> TaintedSyms) const;

public:
  // The callback that the analyzer calls before evaluating a BinaryOperator.
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

// Helper: extract the denominator expression from the exploded node.
// The ExplodedNode stores the program point (PreStmt for this operator)
// and the program state.
static const Expr *getDenomExpr(const ExplodedNode *N) {
  const Stmt *S = N->getLocationAs<PreStmt>()->getStmt();
  if (const auto *BE = dyn_cast<BinaryOperator>(S))
    return BE->getRHS();
  return nullptr;
}

// Report a standard division-by-zero bug.
// C.generateErrorNode(StateZero) creates a sink node -- analysis stops here.
void DivZeroChecker::reportBug(StringRef Msg, ProgramStateRef StateZero,
                               CheckerContext &C) const {
  if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    // trackExpressionValue adds path notes showing where the denominator
    // came from and how it became zero.
    bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
    C.emitReport(std::move(R));
  }
}

// Report a taint-related division-by-zero bug.
void DivZeroChecker::reportTaintBug(
    StringRef Msg, ProgramStateRef StateZero, CheckerContext &C,
    llvm::ArrayRef<SymbolRef> TaintedSyms) const {
  if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
    auto R = std::make_unique<PathSensitiveBugReport>(TaintBT, Msg, N);
    bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
    // Mark tainted symbols so the report path highlights their origin.
    for (auto Sym : TaintedSyms)
      R->markInteresting(Sym);
    C.emitReport(std::move(R));
  }
}

// CORE DETECTION LOGIC
//
// The analyzer maintains a set of possible program states (the exploded
// graph). Each state includes constraints on symbolic values. This checker
// checks whether the denominator is constrained to be zero.
void DivZeroChecker::checkPreStmt(const BinaryOperator *B,
                                  CheckerContext &C) const {
  // Step 1: Filter to only division and remainder operators.
  BinaryOperator::Opcode Op = B->getOpcode();
  if (Op != BO_Div &&
      Op != BO_Rem &&
      Op != BO_DivAssign &&
      Op != BO_RemAssign)
    return;

  // Step 2: RHS must be scalar type.
  if (!B->getRHS()->getType()->isScalarType())
    return;

  // Step 3: Get the symbolic value of the denominator.
  SVal Denom = C.getSVal(B->getRHS());
  std::optional<DefinedSVal> DV = Denom.getAs<DefinedSVal>();

  // If undefined, the generic undefined value checker handles it.
  if (!DV)
    return;

  // Step 4: Use assumeDual to split the state.
  //   stateNotZero -> denominator is non-zero
  //   stateZero     -> denominator is zero
  ConstraintManager &CM = C.getConstraintManager();
  ProgramStateRef stateNotZero, stateZero;
  std::tie(stateNotZero, stateZero) = CM.assumeDual(C.getState(), *DV);

  // Step 5: If only stateZero is feasible, division by zero is guaranteed.
  if (!stateNotZero) {
    assert(stateZero);
    reportBug("Division by zero", stateZero, C);
    return;
  }

  // Step 6: If BOTH paths are feasible and denominator is tainted, warn.
  if ((stateNotZero && stateZero)) {
    std::vector<SymbolRef> taintedSyms = getTaintedSymbols(C.getState(), *DV);
    if (!taintedSyms.empty()) {
      reportTaintBug("Division by a tainted value, possibly zero", stateZero, C,
                     taintedSyms);
      return;
    }
  }

  // Step 7: Otherwise, continue analysis assuming non-zero denominator.
  C.addTransition(stateNotZero);
}

// Registration functions (boilerplate required for every checker).
void ento::registerDivZeroChecker(CheckerManager &mgr) {
  mgr.registerChecker<DivZeroChecker>();
}

bool ento::shouldRegisterDivZeroChecker(const CheckerManager &mgr) {
  return true;
}
