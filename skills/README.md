# Original synthesis skills and reference guidance

This directory makes the archived agent-visible guidance browsable without downloading the full runtime datasets. It contains **127 files (115 distinct contents)** in five bundles, mapped to all **300 tasks** in [task-map.csv](task-map.csv). Files were copied byte for byte from the original task workspaces. [manifest.json](manifest.json) records each asset's SHA-256 and size.

The task-specific prompts are already available under [`tasks/`](../tasks/). The `prompt` column in the task map identifies the exact published prompt for each task. The guidance export contains no task-specific reference checker, generated submission, calibration trajectory, verifier implementation, or model-service credential configuration.

## Contents

| Task population | Tasks | Bundle | Original layout within the task workspace |
| --- | ---: | --- | --- |
| C/C++, CSA | 159 | [`csa/`](csa/) | `.claude/skills/` and `references/` |
| Go, CodeQL | 30 | [`codeql/go/`](codeql/go/) | `references/codeql/` |
| Java, CodeQL | 30 | [`codeql/java/`](codeql/java/) | `references/codeql/` |
| JavaScript, CodeQL | 30 | [`codeql/javascript/`](codeql/javascript/) | `references/codeql/` |
| Python, CodeQL | 51 | [`codeql/python/`](codeql/python/) | `references/codeql/` |

All 159 CSA tasks have the same archived guidance asset contents. Each CodeQL language has a common bundle. Deduplication changes storage only; it does not introduce newly written experimental guidance.

## CSA skills

The complete CSA bundle includes 24 named skills with their original supporting files. The checker-development skills cover:

| Purpose | Skills |
| --- | --- |
| Understand the defect and patch | `vulnerability-analysis`, `patch-analysis`, `c-code-analysis` |
| Choose a mechanism and scope | `checker-architecture`, `checker-scoping` |
| Implement and inspect APIs | `checker-implementation-suggestions`, `csa-api-deep-dive`, `csa-search` |
| Recover compilation and scan setup | `compile-debugging`, `scan-environment`, `build-compile-commands` |
| Validate and refine | `checker-validation`, `checker-refinement`, `fp-triage`, `test-case-generation` |

The historical bundle additionally contains `dependency-check`, `dockerfile-scan`, `patch-dcrh`, `sast-lite`, `secret-scanner`, `security-advisor`, `threat-model-dcrh`, `triage-dcrh`, and `vuln-scan-dcrh`. Their presence does not mean every run invoked them. Keeping them preserves the actual supplied library rather than retrospectively selecting only frequently used skills.

Browse the original [`SKILL.md` files](csa/.claude/skills/). The companion [`references/`](csa/references/) directory includes CSA API material, checker patterns, utilities, CWE mechanism cards, and official LLVM checker examples. Original attribution and notices are retained; this export does not assign a new blanket license to third-party material. Secret-scanner tests contain deliberately synthetic credential examples.

## CodeQL guidance

CodeQL used task-local Markdown references rather than the CSA `SKILL.md` registry. Each language bundle includes its API reference, query metadata instructions, AST/control-flow patterns, data-flow/taint patterns, and convergence protocol. Python also includes API-navigation guidance and reusable example queries. These examples are general analyzer examples, not the task's reference solution.

## Restore and expose the guidance

Prepare a task with the existing dataset restoration instructions first. From the release repository root, merge the appropriate bundle into that prepared task directory, preserving its relative paths. For example:

```sh
# CSA example: /work/pair_134943 is the prepared task directory.
cp -R skills/csa/. /work/pair_134943/

# CodeQL example: TASK_DIR is the prepared task directory containing task.json.
cp -R skills/codeql/java/. "$TASK_DIR"/
```

For CSA, the archived launcher exposes `.claude/skills/` directly to Claude Code; it copies that same tree to `opencode/skills/` under the OpenCode configuration directory or to `skills/` under the Hermes profile directory. References remain at their task-relative paths. CodeQL guidance remains in the task workspace and is referenced by the task prompt. Preserve the read-only reference policy during generation.

This directory is a guidance supplement, not a replacement for the frozen runtime, analyzer dependencies, task inputs, or harness configuration. It does not by itself reproduce the evaluation scores.

## Historical content and interpretation

The files are the archived inputs, including their original wording and budgets. In particular, the CodeQL convergence documents contain a 30-minute schedule with a minute-27 finalization target. This should be distinguished from the outer generation deadline used by the formal evaluation controller. Some non-Python task prompts mention `api_navigation.md`, although their archived reference bundle does not contain that file; this export does not invent a historical file to fill that gap. Changes to such guidance should be versioned and evaluated as a new condition.

The paper's “without skills” comparison removes strategy references as well as skill packages, and its implementation also simplifies the CSA checker template. It should not be interpreted as an intervention on only the `SKILL.md` files.

Verify the published files from the repository root:

```sh
shasum -a 256 -c skills/SHA256SUMS
```
