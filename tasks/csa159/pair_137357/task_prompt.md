# Task: Synthesize a CSA Checker from a CVE Patch

## Your Role

You are a static analysis expert. Synthesize a Clang Static Analyzer (CSA) checker
that detects the bug pattern fixed by the CVE patch in this working directory.

You have full filesystem and bash access — use them.

### Skills

Many skills are available under `.claude/skills/` — covering vulnerability analysis,
checker design, debugging, code audit, security patterns, and more.  Each skill
encodes reusable expertise: take time to explore them, read their SKILL.md files,
and invoke those that look useful via the Skill tool.  Let the task guide which
ones you reach for.

## File Layout

```
/work/
├── compile.sh               ← RUN ME: compile checker.cpp → checker.so
├── checker_template.cpp     ← skeleton, start here
├── patch.diff               ← the CVE fix
├── pre_functions.txt        ← buggy source (before_commit)
├── post_functions.txt       ← patched source (after_commit)
├── config.json              ← paths, commit_message, commit hashes, flags
├── scan/
│   ├── scan_before.sh       ← RUN ME: scan before_commit → N_buggy
│   ├── scan_after.sh        ← RUN ME: scan after_commit → N_patched
│   └── flags_*.txt          ← pre-extracted compile flags (-I, -D)
├── references/
│   ├── repo_knowledge.md            ← project-specific API/convention reference
│   ├── patterns_summary.md       ← (optional) historical bug pattern summary
│   ├── cwe_cards/                ← CWE knowledge library (all categories)
│   │   ├── index.json             ← lookup index — use to find relevant cards
│   │   ├── CWE-{id}(-variant).md  ← Tier 1/2 per-CWE detection patterns
│   │   ├── Category-{name}.md     ← Tier 3 umbrella cards (rare CWEs grouped)
│   ├── csa_headers_api.md   ← API quick reference (extracted from LLVM 18 headers)
│   ├── csa_checker_patterns.md ← pattern catalog (resource tracking, null check, etc.)
│   ├── csa_utility_functions.h ← copy-paste C++ helpers for checker.cpp (AST traversal, expression eval, etc.)
│   └── examples/
│       └── llvm-official/   ← 5 official checker sources (with comments)
│           ├── 01_div_zero.cpp
│           ├── 02_simple_stream.cpp
│           ├── 03_undef_result.cpp
│           ├── 04_nullability.cpp
│           └── 05_malloc.cpp
│
├── checker.cpp              ← YOUR OUTPUT: synthesized checker
├── checker.so               ← YOUR OUTPUT: compiled checker
├── state.json               ← YOUR OUTPUT: structured task state
├── scope_statement.md       ← YOUR OUTPUT: checker scope analysis (Level 1/2/3)
└── summary.json             ← YOUR OUTPUT: final report
```

## Resources

**Read these before writing any code:**

1. **`references/csa_headers_api.md`** — CSA API quick reference. Extracted directly from
   LLVM 18 headers. Covers all checker callbacks, ProgramState operations, SVal types,
   MemRegion hierarchy, and bug reporting. **Read this first whenever you need API info.**

2. **`references/csa_checker_patterns.md`** — Pattern catalog. Documents 5 battle-tested
   patterns from official LLVM checkers: resource tracking, null checking, undefined
   value detection, division-by-zero, and malloc/free. Each pattern includes skeleton
   code, state traits, and key API usage.

3. **`references/examples/llvm-official/`** — 5 official LLVM 18 checker implementations
   with explanatory comments:
   - `01_div_zero.cpp` — simplest checker (169 lines, 1 callback)
   - `02_simple_stream.cpp` — resource tracking (292 lines, fopen/fclose)
   - `03_undef_result.cpp` — PostStmt pattern (160 lines)
   - `04_nullability.cpp` — multi-callback checker (1247 lines)
   - `05_malloc.cpp` — comprehensive malloc/free (1511 lines)

4. **`checker_template.cpp`** — Skeleton with placeholders. Start here.

5. **`references/repo_knowledge.md`** — Project-specific reference. Contains this
   repository's memory allocation API, key data types, source layout, and typical
   CVE patterns. **Read this before writing the checker** — knowing the project's
   alloc/free conventions and common bug patterns saves many grep rounds.

5b. **`references/patterns_summary.md`** — (may not exist) Historical synthesis
   patterns for this project.  If present, it contains a concise summary of the
   most common bug patterns, frequent CWE categories, and reusable checker
   architectures observed in previous synthesis runs.  Read after repo_knowledge.md.

6. **`references/cwe_cards/`** — Complete CWE knowledge base covering all
   vulnerability categories.  **The framework provides the full library —
   you must identify the vulnerability type yourself.**  Workflow:

   1. Analyse the patch diff and commit message to determine the vulnerability
      type (buffer overflow, UAF, null deref, integer overflow, race condition,
      etc.).  Do NOT rely on the CWE label — treat it as a hint at most.
   2. Open ``index.json`` to discover available cards and their tiers.
   3. Read the relevant card(s) for **detection strategies**, **checker
      architecture suggestions**, and **known false-positive patterns**.

   Card tiers:
   - ``CWE-{id}(-{variant}).md`` — Tier 1/2 cards for common CWEs; kernel /
     userspace variants when the detection strategy differs substantially.
   - ``Category-{name}.md`` — Tier 3 umbrella cards grouping rarer CWEs by
     category (input validation, resource management, etc.).

7. **`references/csa_utility_functions.h`** — Copy-paste C++ helper functions for
   checker.cpp. Includes AST traversal (`findSpecificTypeInParents/Children`),
   expression evaluation (`EvaluateExprToInt`, `inferSymbolMaxVal`), array/string
   size extraction, MemRegion helpers, and `ExprHasName()` for robust function
   matching. All verified against LLVM 18. **Read before implementing detection
   logic** — these save writing boilerplate that's easy to get wrong.

## Environment

Key paths are in ``config.json``.

### Pre-built scripts

| Script | What it does |
|--------|-------------|
| `bash compile.sh` | Compile checker.cpp → checker.so (with correct flags) |
| `CHECKER_NAME=<name> bash scan/scan_before.sh` | Scan before_commit, count warnings → N_buggy |
| `CHECKER_NAME=<name> bash scan/scan_after.sh` | Scan after_commit, count warnings → N_patched |

```bash
# Compile (run from workdir root)
bash compile.sh

# Scan — set CHECKER_NAME to the string passed to addChecker<>()
CHECKER_NAME=your.CheckerName bash scan/scan_before.sh   # → N_buggy
CHECKER_NAME=your.CheckerName bash scan/scan_after.sh    # → N_patched
```

- ``compile.sh`` already has the correct ``-I`` / ``-fno-rtti`` flags.
- ``scan/scan_*.sh`` count only warnings tagged with your checker name.
- Scan environments (source files + compile flags) are pre-built in ``scan/``.

### Scan Script Integrity (CRITICAL)

The scan scripts (``scan/scan_before.sh``, ``scan/scan_after.sh``) are the **only
valid measurement** of N_buggy and N_patched.  They are the ground truth that the
Python-side validator will also use.  You MUST obey these rules:

**1. Never bypass the scan scripts.**  Do NOT run ``clang --analyze`` directly
   to measure warning counts.  The Python validator runs the same scan scripts
   — if you measured with a different command, your numbers will not reproduce
   and the synthesis will be rejected.

**2. If the scan script prints COMPILE_ERROR** (missing headers, undeclared
   identifiers, etc.), the scan environment needs repair.  You MUST fix it:
   - Add missing ``-I`` include paths to the flags files in ``scan/``
   - Copy needed generated headers (e.g. ``autoconf.h``) from the snapshot
   - If the file fundamentally cannot be compiled by clang, stop and write
     ``summary.json`` with ``status: "failed"`` and an honest ``failure_reason``

**3. Only trust scan script output.**  The ``Total <name> warnings: N`` line
   printed by the scan script is the number that goes into ``summary.json``.
   If the scan script says 0, write 0.  Fabricating numbers that the scan
   script did not produce will cause the Python validator to reject the
   synthesis.

- If compile fails: read the error, fix ``checker.cpp``, retry (max 3 cycles).
- If validation fails: revise checker logic, re-compile, re-scan.

## MCP Tools

You have access to 3 MCP tools under the `csa-tools` server (prefixed `mcp__csa-tools__`).
Each returns **source context embedded in the result** — no need to follow up with Read.

| Tool | What it does | Key advantage |
|------|-------------|---------------|
| `search(pattern, scope)` | Search for a pattern in source / LLVM headers / workdir | Returns file:line + 3 lines context — grep + Read in one call |
| `compile()` | Run compile.sh, return parsed diagnostics | Errors include checker.cpp context — fix without reading the file |
| `scan(checker_name)` | Run before/after scan, return N_buggy + N_patched + warning locations | Each warning includes source context — no manual clang --analyze needed |

### When to use each

- **`search`**: Whenever you would type `grep -rn "X"` or `find ... -name`. Use `scope="source"` for CVE code, `scope="llvm"` for CSA API headers, `scope="all"` when unsure.
- **`compile`**: Instead of `bash compile.sh` followed by reading checker.cpp at error lines.
- **`scan`**: Instead of running scan_before.sh + scan_after.sh + manual clang inspection. Returns everything you need to decide next action.

## Semantic Design Contract — before writing checker.cpp

Create the analysis portions of `state.json` and `scope_statement.md` **before**
writing checker.cpp. They are a design contract, not a retrospective summary.
After compilation, update only implementation facts such as `build_attempts`.

In `patch_analysis`, distinguish the vulnerable operation, its trigger, and the
semantic fact introduced by the patch that makes it safe. In `detection_plan`,
make `report_condition`, `safe_condition`, and `source_level_signal` concrete
CSA-observable facts. In `api_choice`, record the mechanism level and explain
why the selected callback can observe and prove those facts.

In `scope_statement.md`, include this mapping table and check it again after
implementation:

| Semantic condition | CSA evidence | checker.cpp implementation |
| --- | --- | --- |
| report condition | AST / SVal / MemRegion / CFG evidence | callback, helper, and branch that reports |
| safe condition | path constraint, guard, state discharge, or structural fact | branch that suppresses the report |

Every condition in the plan must have an implementation entry. Do not claim a
condition that checker.cpp does not implement, and do not silently add a code
condition absent from the plan.

## Mechanism Selection — diagnose before coding

Choose the weakest CSA mechanism that can prove the vulnerability property:

1. **syntax-only AST**: only when local AST shape itself is the defect;
2. **control flow / dominance**: when guard placement relative to a sink matters;
3. **path-sensitive ProgramState/SVal**: when an unsafe state must reach a use;
4. **cross-procedure/dataflow**: when the required fact crosses function boundaries.

For a guard-based patch, prove both that the guard rejects the unsafe value and
that it dominates the vulnerable use. For path-sensitive or cross-procedure
defects, do not replace the required state or flow proof with local AST matching
or source-text substring matching merely because it passes the differential scan.
Record the selected level as `api_choice.mechanism_level`.

## Semantic identity, locality, and final validation

Before accepting the checker, verify that its primary matching logic does not
depend on patch-local variable names, labels, line numbers, commit IDs, or unique
literals. Project APIs, types, and stable semantic call relationships are valid
scope anchors when they define the vulnerability class. Attach the report to the
actual vulnerable operation, and implement every suppression as a real safe
condition rather than the mere presence of a nearby guard-shaped statement.

The required final sequence is: **compile → before scan → after scan → inspect
the final checker name, artifacts, and report locations → write summary.json**.
Always run the final compile and both scans. `MAX_IN_SESSION_REDESIGNS` limits only
additional redesign cycles after a failed validation; if it is zero, preserve the
real validation evidence and report failure honestly rather than fabricating counts.

## Validation Criteria

```
VALID   : N_buggy > N_patched  AND  N_patched < 50
INVALID : otherwise → revise and re-validate (max 1 attempts)
```

## Refinement

Do NOT self-refine the checker repeatedly. Your goal is to produce the first
checker that passes the Validation Criteria above — fix compile errors and
validation failures only. False positives are triaged and repaired by the
framework's refinement stage: you will receive a concrete FP list via
session resume (if needed) and fix only those. Reserve budget for that stage.

## Output

Produce all of the following **only when completely done**:

| File | Content |
|------|---------|
| ``checker.cpp`` | Synthesized checker source code |
| ``checker.so`` | Compiled checker plugin |
| ``state.json`` | Structured task state (see schema below) |
| ``scope_statement.md`` | Checker scope analysis in prose (Level 1/2/3) |
| ``summary.json`` | Final report (see schema below) |

Writing ``summary.json`` signals completion.  Do NOT write it early.

### ``state.json``

Create this before writing checker.cpp to capture the design contract. After the
checker compiles, update only implementation facts and retain the original
semantic plan.

```json
{
  "patch_analysis": {
    "summary":           "One sentence describing the vulnerability this patch fixes",
    "changed_files":     ["file1.c", "file2.h"],
    "changed_functions": ["func_a", "func_b"],
    "bug_type":          "buffer_overflow | use_after_free | null_deref | integer_overflow | race_condition | resource_leak | double_free | input_validation | other",
    "fix_strategy":      "add_bounds_check | add_null_check | add_size_check | add_early_return | reorder_operations | change_api_signature | other",
    "affected_types":    ["GF_BitStream", "custom_struct_t"],
    "critical_calls":    ["gf_strdup", "memcpy"]
  },
  "detection_plan": {
    "bug_pattern":         "High-level description of the vulnerability pattern (e.g. 'heap buffer overflow when reading variable-length data into a fixed-size stack buffer without size bound')",
    "report_condition":    "When should the checker emit a warning? (e.g. 'call to gf_strdup where the argument originates from a fixed-size local array that was populated by variable-length bitstream reads')",
    "safe_condition":      "When should the checker NOT emit a warning? (e.g. 'argument originates from a heap allocation or a size-bounded source')",
    "source_level_signal": "What AST / MemRegion / SVal property does the checker look for? (e.g. 'MemRegion extent < parameter size at a PreCall to gf_strdup')"
  },
  "api_choice": {
    "mechanism_level":          "syntactic_ast | control_flow | path_sensitive | interprocedural_dataflow",
    "callback":                 "check::PreCall | check::PostCall | check::PreStmt | check::PostStmt | check::Bind | check::DeadSymbols | check::EndFunction | ...",
    "reasoning":                "Why this callback and its AST/state/CFG evidence prove the report and safe conditions",
    "alternatives_considered":  ["check::PostCall"]
  },
  "implementation": {
    "scope_level":    "patch_specific | project_general | cross_project",
    "checker_name":   "project.CheckerName",
    "build_attempts": 1
  }
}
```

### ``summary.json``

```json
{
  "status":           "plausible | failed",
  "bug_pattern":      "One sentence describing the bug pattern",
  "scope_level":      "patch_specific | project_general | cross_project",
  "checker_cpp":      "checker.cpp",
  "checker_so":       "checker.so",
  "n_buggy":          0,
  "n_patched":        0,
  "total_reports":    0,
  "fp_rate_estimate": 0.0,
  "failure_reason":   null
}
```

## Feedback-derived requirements for this fresh synthesis

A prior independent rollout exposed the risks below. Start again from the patch
in this new session. Do not reconstruct, edit, or assume the prior checker.

Required mechanism level: `path_sensitive`

### Current failure diagnosis

- The rollout failed only the process-quality threshold, scoring 73.91 despite passing compilation, differential detection, code verification, objective gate G6, refinement, and semantic checks.
- The detection plan and implementation did not encode successful lexer_string_is_directive validation as a distinct prerequisite state, so an enable between token advancement and directive validation could be reported even though the stated contract required validation first.
- Strict-enable recognition was broader than the plan: fallback checks on the bound value or symbolic OR expression could classify stores beyond the specified status_flags OR-assignment with the strict bit.
- The first build placed the ProgramState trait registration in an invalid namespace; recovery succeeded, but the avoidable compile cycle reduced process quality.

### Preserve from prior rollouts

- Maintain successful checker compilation, paired before/after scanning, and code verification.
- Retain objective eligibility gate G6 and an accepted or clean refinement outcome.
- Retain the accepted path-sensitive CSA mechanism and a semantic score at or above the configured threshold.
- Remain independent of patch line anchors, changed-function allowlists, and other patch-local hardcoding.
- Keep project-general scope so equivalent directive-processing sites can be detected without restricting analysis to the two patched functions.

### Required semantic proof

- On one feasible intraprocedural path, prove that lexer_string_is_use_strict(ctx) returns true while the same parser context is not provably already strict.
- Prove that lexer_next_token(ctx) then classifies the token immediately following that recognized string before PARSER_IS_STRICT is enabled on ctx.
- Require lexer_string_is_directive(ctx) to return true for that same pending directive operation before permitting a report; its false branch must invalidate the candidate.
- Prove that a subsequent store enables PARSER_IS_STRICT in ctx->status_flags for the identical canonical parser-context region, and report only this late enable.
- Treat an enable observed after recognition but before the relevant token advance as safe, including the patched speculative-enable design and its later rollback when directive validation fails.
- Do not report for failed use-strict recognition, rejected directives, observably already-strict contexts, unrelated parser-context objects, or unrelated flag updates.

### Implementation constraints

- Use ProgramState to carry a per-context protocol with distinct phases for recognized, token-advanced, directive-validated, safely resolved, and cleared candidates; a reportable phase must be reachable only after all required successful events.
- Use check::PostCall to split and constrain the boolean results of lexer_string_is_use_strict and lexer_string_is_directive, check::PreCall to observe lexer_next_token before classification, and check::Bind to inspect the destination region of the strict-enable store.
- Key state by the canonical base MemRegion of the parser-context argument and require every recognition, advance, validation, and store event to resolve to the same key.
- Recognize the sink through the parser_context status_flags FieldRegion and AST/constant semantics showing an OR-assignment that enables the PARSER_IS_STRICT bit; do not broaden sink matching merely because a resulting value has bit 0 set.
- Anchor the diagnostic at the late strict-enable store, mark the associated context region interesting, and discharge the candidate so one path does not emit duplicate reports.
- Keep analysis intraprocedural and project-general: stable JerryScript parser/lexer APIs and strict-bit semantics are permitted, but source paths, function names, patch coordinates, and before/after text are not.
- Place REGISTER_MAP_WITH_PROGRAMSTATE where its ProgramStateTrait specialization is in a namespace enclosing clang::ento, consistent with the Clang API used by the build environment.

### Validation checklist

- Compile the fresh checker plugin against the target Clang Static Analyzer API with no diagnostics before running scans.
- Scan the vulnerable revision and require exactly the two relevant warnings in parser_parse_statements and scanner_check_directives, each located at the late status_flags strict-enable store.
- Scan the patched revision and require zero warnings, demonstrating that enable-before-lexer_next_token transitions the candidate to a safe state.
- Run paired code verification and confirm both vulnerable warnings disappear in the patched functions, no warnings appear in non-patched functions, and median report distance from the relevant patch site remains zero.
- Exercise refinement cases for failed use-strict recognition, lexer_string_is_directive returning false, an already-strict context, a different context object, unrelated status_flags updates, and speculative enable followed by rollback; all must remain clean.
- Add a focused ordering case where strict enable occurs after lexer_next_token but before directive validation; it must not report because successful validation has not yet been established.
- Add a positive focused case with recognition true, token advance, directive validation true, and late strict enable on one context; it must report once at the store.
- Reconfirm objective gate G6, accepted or clean refinement, anchor-free and hardcoding-free checks, and the required semantic threshold after validation.

### Avoid and forbidden regressions

- Do not use syntax-only matching, AST adjacency, CFG dominance alone, or untracked control-flow ordering; these cannot establish all feasible-path and same-operation conditions.
- Do not reuse a state model in which token advancement alone makes a candidate reportable while directive validation merely clears the false branch.
- Do not infer a strict-enable sink from arbitrary concrete or symbolic bound values without proving the specified status_flags OR-assignment semantics.
- Do not suppress unknown preexisting strict status as definitely safe; only a concretely established strict bit proves the already-strict exclusion.
- Do not match the unrelated parser_parse_block_expression change or generalize the checker to arbitrary mode flags outside this vulnerability pattern.
- Do not specialize detection to parser_parse_statements, scanner_check_directives, their files, changed lines, diagnostic coordinates, or patch text.
- Do not place ProgramState registration inside an anonymous namespace that cannot legally specialize clang::ento::ProgramStateTrait.
