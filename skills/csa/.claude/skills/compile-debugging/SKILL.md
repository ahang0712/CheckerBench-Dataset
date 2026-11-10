---
name: compile-debugging
description: Use when compile.sh fails — diagnose and fix the 5 most common LLVM 18 checker compilation errors: unknown type names (missing #include), no member named (API mismatch), RTTI errors (-fno-rtti), REGISTER macro placement, and wrong argument counts.
---

# CSA Checker Compile Debugging

Common clang++ errors when compiling checker plugins and how to fix them.

## Error Patterns

### 1. "unknown type name 'XXX'"
- Missing `#include`. Check `references/csa_headers_api.md` for the correct header path.
- `CheckerBase` → `#include "clang/StaticAnalyzer/Core/Checker.h"`
- `BugType` → `#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"`
- `CallEvent` → `#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"`

### 2. "no member named 'XXX'"
- The API method does not exist in LLVM 18. Check `references/csa_headers_api.md`.
- Common mistake: using newer LLVM APIs that were added after LLVM 18.

### 3. "undefined reference to typeinfo for XXX"
- RTTI mismatch. The checker MUST be compiled with `-fno-rtti` (compile.sh already has this).
- If you see this, verify you're using `bash compile.sh` not a manual clang++ command.

### 4. REGISTER_MAP_WITH_PROGRAMSTATE / REGISTER_SET_WITH_PROGRAMSTATE
- Must be at **namespace scope** (outside anonymous namespace), NOT inside a class.

### 5. "too few arguments to function call"
- Check the LLVM 18 API reference — signatures may differ from newer LLVM versions.
- `PathDiagnosticLocation::create` requires specific parameters in LLVM 18.

## Strategy

Max **3 compile-fix cycles**. If still failing:
1. Comment out the problematic logic
2. Get a minimal checker compiling
3. Add complexity back incrementally
