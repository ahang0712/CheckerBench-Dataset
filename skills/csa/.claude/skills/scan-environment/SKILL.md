---
name: scan-environment
description: Use when scan/scan_before.sh does not exist or returns COMPILE_ERROR — locate worktree sources under scan/tmp/, borrow compile flags from compile_commands.json, generate missing config.h, and run clang --analyze manually.
---

# Scan Environment Setup

## When scan scripts exist

Run them directly:
```bash
CHECKER_NAME=<checker_name> bash scan/scan_before.sh
CHECKER_NAME=<checker_name> bash scan/scan_after.sh
```

## When scan scripts are missing

### Step 1: Locate source files
```bash
find scan/tmp/ -name "$(basename <changed_file>)"
```
Worktrees are under `scan/tmp/wt_<commit>_*/`.

### Step 2: Get compile flags
If `compile_commands.json` exists (paths in `config.json`):
```bash
python3 -c "
import json, shlex
with open('<compile_commands.json>') as f:
    entries = json.load(f)
args = shlex.split(entries[0]['command'])
print(' '.join(a for a in args if a.startswith(('-I','-D','-isystem'))))
"
```

### Step 3: If no compile_commands.json
Generate one with cmake:
```bash
cd <repo_path> && mkdir -p build_scan && cd build_scan
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Step 4: Create stub files for missing headers

When clang reports `fatal error: '<HEADER>' file not found`, you can create a
**minimal stub** to satisfy the include chain and continue scanning.  This is NOT
a full replacement — just enough to let clang parse the TU and run the checker.

**Where to place stubs:**

The stub must be findable via the ``-I`` include paths.  Common strategies:

| Scenario | Where to create stub | Example |
|----------|---------------------|---------|
| Header in source-tree directory | Next to the source file (simplest) | `touch scan/tmp/<wt>/<dir>/event.h` |
| Header expected in a ``-I`` path | Create the missing directory under a writable -I path | `mkdir -p /tmp/stubs && echo "// stub" > /tmp/stubs/jemalloc/jemalloc.h` and add `-I/tmp/stubs` |
| Generated config header | Create at the include path location | `echo "// stub config" > scan/tmp/<wt>/include/config.h` |

**Stub content by header type:**

| Header type | Stub content | Example |
|------------|-------------|---------|
| Project config (`config.h`, `revision.h`, `version.h`) | Empty file or `#define` for known values | `#define PACKAGE_VERSION "static"` |
| External library header (`event.h`, `pixman.h`, `uv.h`) | Empty file unless the checker needs types from it | `touch event.h` |
| Internal project header (same repo, wrong path) | Find the actual file in the worktree and add the right ``-I`` path instead of stubbing | ``find scan/tmp/ -name "missing_file.h"`` |
| Arch-specific header (`asm/rwonce.h`) | Empty file — the include is usually conditional | `touch asm/rwonce.h` | | System header or unknown | Empty file — clang has builtins for most system headers |

**Iterative process:**

```bash
# 1. Try scan, capture the first missing header
bash scan/scan_before.sh 2>/tmp/scan_err.txt
# Parse the first fatal error
MISSING=$(grep "fatal error:" /tmp/scan_err.txt | head -1 | sed "s/.*'\(.*\)'.*/\1/")

# 2. Find the -I path where this header is expected
# The clang error line shows which file tried to include it and from which
# directory. Use this to determine where to place the stub.

# 3. Create the stub
touch /path/to/$MISSING

# 4. Retry — repeat until all missing headers are stubbed or clang can parse
CHECKER_NAME=x bash scan/scan_before.sh 2>&1 | grep -E "(warnings|COMPILE_ERROR|fatal error:)"
```

**When to stop stubbing:**

- clang parses the file and runs the checker: done
- Stubbing leads to cascading errors (missing types/structs): the file cannot be scanned; write summary.json with `status: "failed"` and explain which headers were missing
- More than 5 stubs needed: the scan environment is fundamentally broken; report as failed

### Step 5: Re-run the scan scripts — do NOT run clang --analyze directly

**CRITICAL**: Do NOT run ``clang --analyze`` directly for measurement.  The Python
validator will run the exact same scan scripts (``scan/scan_before.sh`` /
``scan/scan_after.sh``) — if you measured with a different command, your
``n_buggy`` / ``n_patched`` will not reproduce and the synthesis will be rejected.

After fixing headers / include paths, re-run the scan scripts:
```bash
CHECKER_NAME=your.CheckerName bash scan/scan_before.sh
CHECKER_NAME=your.CheckerName bash scan/scan_after.sh
```

The ``Total <checker_name> warnings: N`` line is the ground truth.  Use that
number in ``summary.json``.  If the scan script still prints ``COMPILE_ERROR``
after reasonable fixes (≤ 5 stubs), the file cannot be scanned: write
``summary.json`` with ``status: "failed"`` and honest ``failure_reason``.

### If source file is absent from worktrees

If the source file is build-generated and absent from worktrees, the scan
cannot be performed.  Write ``summary.json`` with ``status: "failed"`` and
``failure_reason`` explaining the situation — do NOT guess ``n_buggy`` or
``n_patched``.

## Interpreting results
- `COMPILE_ERROR`: clang cannot parse the file (missing headers/flags)
- `0`: checker found no bugs (or file didn't compile — check stderr)
- `>0`: checker fired
