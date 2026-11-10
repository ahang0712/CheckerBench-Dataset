// Pattern: Resource Tracking (fopen/fclose) -- canonical REGISTER_MAP_WITH_PROGRAMSTATE example
// Key APIs used: check::PostCall, check::PreCall, check::DeadSymbols,
//   check::PointerEscape, REGISTER_MAP_WITH_PROGRAMSTATE, CallDescription,
//   SymbolReaper, C.generateNonFatalErrorNode(), markInteresting,
//   State->set/get/remove<StreamMap>
//
// This is the SIMPLEST checker that uses persistent program state.
// It demonstrates the fundamental resource tracking pattern:
//   1. checkPostCall: Track newly allocated resource (fopen)
//   2. checkPreCall:  Validate state before operation (fclose) - prevents double-close
//   3. checkDeadSymbols: Detect leaks when symbols go out of scope
//   4. checkPointerEscape: Stop tracking when pointer escapes to unknown code
//
// Use this as a template for ANY resource tracking checker.

//===-- SimpleStreamChecker.cpp -----------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a checker for proper use of fopen/fclose APIs.
//   - If a file has been closed with fclose, it should not be accessed again.
//   Accessing a closed file results in undefined behavior.
//   - If a file was opened with fopen, it must be closed with fclose before
//   the execution ends. Failing to do so results in a resource leak.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {
typedef SmallVector<SymbolRef, 2> SymbolVector;

// THE STATE OBJECT
// This struct represents the state of a tracked resource.
// Requirements:
//   - Must have a private enum Kind and constructors to enforce valid states.
//   - Must have static factory methods (getOpened, getClosed).
//   - Must have operator== for comparison.
//   - Must have Profile() for FoldingSetNodeID (used to unique states).
//   - isOpened()/isClosed() query methods.
struct StreamState {
private:
  enum Kind { Opened, Closed } K;
  StreamState(Kind InK) : K(InK) { }

public:
  bool isOpened() const { return K == Opened; }
  bool isClosed() const { return K == Closed; }

  static StreamState getOpened() { return StreamState(Opened); }
  static StreamState getClosed() { return StreamState(Closed); }

  bool operator==(const StreamState &X) const {
    return K == X.K;
  }
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
  }
};

// THE CHECKER CLASS
// Callbacks used:
//   check::PostCall  - after fopen() is called, mark the return value as Opened
//   check::PreCall   - before fclose() is called, check for double-close
//   check::DeadSymbols - when symbols die, detect if they're still Opened (leak)
//   check::PointerEscape - when pointer escapes, stop tracking it
class SimpleStreamChecker : public Checker<check::PostCall,
                                           check::PreCall,
                                           check::DeadSymbols,
                                           check::PointerEscape> {
  // CallDescription matches a function by name and argument count.
  // {{"fopen"}, 2} matches any function named "fopen" with 2 arguments.
  // This is how CSA checkers identify specific API calls without string
  // comparisons on every call.
  const CallDescription OpenFn{{"fopen"}, 2};
  const CallDescription CloseFn{{"fclose"}, 1};

  // BugType objects. The second argument is the bug name, third is category.
  const BugType DoubleCloseBugType{this, "Double fclose",
                                   "Unix Stream API Error"};
  // SuppressOnSink=true means "don't report a leak if the path hits a sink
  // (e.g., assert or noreturn function) before the leak point."
  const BugType LeakBugType{this, "Resource Leak", "Unix Stream API Error",
                            /*SuppressOnSink=*/true};

  void reportDoubleClose(SymbolRef FileDescSym,
                         const CallEvent &Call,
                         CheckerContext &C) const;

  void reportLeaks(ArrayRef<SymbolRef> LeakedStreams, CheckerContext &C,
                   ExplodedNode *ErrNode) const;

  bool guaranteedNotToCloseFile(const CallEvent &Call) const;

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                    const InvalidatedSymbols &Escaped,
                                    const CallEvent *Call,
                                    PointerEscapeKind Kind) const;
};

} // end anonymous namespace

// REGISTER_MAP_WITH_PROGRAMSTATE -- THIS IS THE KEY MACRO
// This creates a typed map that lives in the ProgramState.
// It provides:
//   State->get<StreamMap>(key)       -> read
//   State->set<StreamMap>(key, val)  -> write (returns new State)
//   State->remove<StreamMap>(key)    -> remove (returns new State)
//
// The first template argument (StreamMap) is the tag name.
// You access it: State->get<StreamMap>(sym), State->set<StreamMap>(sym, val)
//
// The map key is SymbolRef (an opaque symbol representing a pointer value).
// The value is our custom StreamState enum wrapper.
REGISTER_MAP_WITH_PROGRAMSTATE(StreamMap, SymbolRef, StreamState)

// checkPostCall: After fopen() returns, record the file handle as "Opened".
// This callback fires after EVERY function call, so we must filter to fopen.
void SimpleStreamChecker::checkPostCall(const CallEvent &Call,
                                        CheckerContext &C) const {
  // Step 1: Only interested in C functions (not C++ methods, blocks, etc.).
  if (!Call.isGlobalCFunction())
    return;

  // Step 2: Match specific function (fopen with 2 args).
  if (!OpenFn.matches(Call))
    return;

  // Step 3: Get the symbolic value of the return value (the FILE* handle).
  SymbolRef FileDesc = Call.getReturnValue().getAsSymbol();
  if (!FileDesc)
    return;

  // Step 4: Update program state to record this symbol as Opened.
  ProgramStateRef State = C.getState();
  State = State->set<StreamMap>(FileDesc, StreamState::getOpened());
  C.addTransition(State);
}

// checkPreCall: Before fclose(), check if already closed (double-close).
void SimpleStreamChecker::checkPreCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  if (!Call.isGlobalCFunction())
    return;

  if (!CloseFn.matches(Call))
    return;

  // Get the symbolic value of the first argument (the FILE* handle).
  SymbolRef FileDesc = Call.getArgSVal(0).getAsSymbol();
  if (!FileDesc)
    return;

  // Look up the current state of this file handle.
  ProgramStateRef State = C.getState();
  const StreamState *SS = State->get<StreamMap>(FileDesc);
  if (SS && SS->isClosed()) {
    // Already closed -> double close bug.
    reportDoubleClose(FileDesc, Call, C);
    return;
  }

  // Otherwise, mark it as closed.
  State = State->set<StreamMap>(FileDesc, StreamState::getClosed());
  C.addTransition(State);
}

// Helper: determine if a symbol should be considered a leak.
// A leak is: symbol is dead, still marked Opened, and not NULL.
// (NULL means fopen() failed on this path, so there's nothing to leak.)
static bool isLeaked(SymbolRef Sym, const StreamState &SS,
                     bool IsSymDead, ProgramStateRef State) {
  if (IsSymDead && SS.isOpened()) {
    ConstraintManager &CMgr = State->getConstraintManager();
    ConditionTruthVal OpenFailed = CMgr.isNull(State, Sym);
    return !OpenFailed.isConstrainedTrue();
  }
  return false;
}

// checkDeadSymbols: Detect resource leaks.
// This callback fires when symbols become dead (go out of scope).
void SimpleStreamChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                           CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SymbolVector LeakedStreams;
  StreamMapTy TrackedStreams = State->get<StreamMap>();
  for (auto [Sym, StreamStatus] : TrackedStreams) {
    bool IsSymDead = SymReaper.isDead(Sym);

    // Collect leaked symbols (dead + still opened + non-null).
    if (isLeaked(Sym, StreamStatus, IsSymDead, State))
      LeakedStreams.push_back(Sym);

    // Remove dead symbols from the tracking map to prevent unbounded growth.
    if (IsSymDead)
      State = State->remove<StreamMap>(Sym);
  }

  // If no leaks, nothing to report.
  if (LeakedStreams.empty()) {
    C.addTransition(State);
    return;
  }

  // generateNonFatalErrorNode creates a non-sink error node.
  // This allows analysis to continue even after a leak is found.
  ExplodedNode *N = C.generateNonFatalErrorNode(State);
  if (!N)
    return;
  reportLeaks(LeakedStreams, C, N);
}

// Report a double-close bug (this is a SINK -- analysis stops here).
void SimpleStreamChecker::reportDoubleClose(SymbolRef FileDescSym,
                                            const CallEvent &Call,
                                            CheckerContext &C) const {
  ExplodedNode *ErrNode = C.generateErrorNode();
  if (!ErrNode)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(
      DoubleCloseBugType, "Closing a previously closed file stream", ErrNode);
  R->addRange(Call.getSourceRange());
  R->markInteresting(FileDescSym);
  C.emitReport(std::move(R));
}

// Report one or more leaks (attached to the same error node).
void SimpleStreamChecker::reportLeaks(ArrayRef<SymbolRef> LeakedStreams,
                                      CheckerContext &C,
                                      ExplodedNode *ErrNode) const {
  for (SymbolRef LeakedStream : LeakedStreams) {
    auto R = std::make_unique<PathSensitiveBugReport>(
        LeakBugType, "Opened file is never closed; potential resource leak",
        ErrNode);
    R->markInteresting(LeakedStream);
    C.emitReport(std::move(R));
  }
}

// Helper: determine if a call is guaranteed not to close a file.
bool SimpleStreamChecker::guaranteedNotToCloseFile(const CallEvent &Call) const{
  if (!Call.isInSystemHeader())
    return false;
  if (Call.argumentsMayEscape())
    return false;
  return true;
}

// checkPointerEscape: When a tracked pointer escapes to code we can't
// analyze (e.g., passed to an opaque function), stop tracking it.
// This prevents false positives.
ProgramStateRef
SimpleStreamChecker::checkPointerEscape(ProgramStateRef State,
                                        const InvalidatedSymbols &Escaped,
                                        const CallEvent *Call,
                                        PointerEscapeKind Kind) const {
  if (Kind == PSK_DirectEscapeOnCall && guaranteedNotToCloseFile(*Call)) {
    return State;
  }

  for (SymbolRef Sym : Escaped) {
    State = State->remove<StreamMap>(Sym);
  }
  return State;
}

void ento::registerSimpleStreamChecker(CheckerManager &mgr) {
  mgr.registerChecker<SimpleStreamChecker>();
}

bool ento::shouldRegisterSimpleStreamChecker(const CheckerManager &mgr) {
  return true;
}
