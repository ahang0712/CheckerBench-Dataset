// ============================================================================
// CSA Checker Template
// ============================================================================
// Fill in the sections marked with TODO to implement your checker.
// The checker is compiled with -fno-rtti to match the LLVM 18 build config.
// It is loaded via: clang -cc1 -load checker.so -analyzer-checker=your.CheckerName
//
// Compilation:
//   clang++ -std=c++17 -fPIC -shared -fno-rtti -fno-exceptions \
//           -I${LLVM_BUILD_DIR}/include \
//           checker.cpp -o checker.so \
//           -Wl,--allow-shlib-undefined
//
// Usage:
//   clang -cc1 -load ./checker.so -analyzer-checker=your.CheckerName -analyze <file>
// ============================================================================

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

using namespace clang;
using namespace ento;

namespace {

// ────────────────────────────────────────────────────────────────
// TODO: Rename this class to describe your checker's purpose.
// ────────────────────────────────────────────────────────────────
class {{CHECKER_CLASS_NAME}}
    : public Checker<check::PreCall,      // for checking before function calls
                     check::PostCall>     // for checking after function calls
{
  // TODO: Customize the bug type name and category.
  mutable std::unique_ptr<BugType> BT;

public:
  {{CHECKER_CLASS_NAME}}()
      : BT(new BugType(this, "{{BUG_NAME}}", "{{BUG_CATEGORY}}")) {}

  // ── PreCall callback (fires before every function call) ──────
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    // TODO: Implement your pre-call analysis logic here.
    //
    // Common patterns:
    //
    // 1. Check if the called function matches a specific name:
    //    if (Call.getCalleeIdentifier()) {
    //      StringRef name = Call.getCalleeIdentifier()->getName();
    //      if (name == "target_function") { ... }
    //    }
    //
    // 2. Get the program state:
    //    ProgramStateRef State = C.getState();
    //
    // 3. Get arguments:
    //    SVal Arg = Call.getArgSVal(0);
    //
    // 4. Generate a bug report:
    //    ExplodedNode *ErrNode = C.generateErrorNode();
    //    if (ErrNode) {
    //      auto R = std::make_unique<PathSensitiveBugReport>(
    //          *BT, "Description of the bug", ErrNode);
    //      C.emitReport(std::move(R));
    //    }
  }

  // ── PostCall callback (fires after every function call) ─────
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    // TODO: Implement your post-call analysis logic here.
    //
    // Common pattern — check the return value:
    //   SVal RetVal = Call.getReturnValue();
    //   if (RetVal.isUnknownOrUndef()) return;
    //
    //   ProgramStateRef State = C.getState();
    //   // Modify state based on post-call conditions...
  }
};

} // anonymous namespace

// ────────────────────────────────────────────────────────────────
// Registration — DO NOT modify the function signatures.
// ────────────────────────────────────────────────────────────────

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<{{CHECKER_CLASS_NAME}}>(
      "{{CHECKER_FULL_NAME}}",  // e.g., "your.NullDereferenceChecker"
      "{{CHECKER_DESC}}",       // e.g., "Detects null pointer dereference"
      "{{CHECKER_DOC_URI}}");   // e.g., "https://example.com/docs"
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;
