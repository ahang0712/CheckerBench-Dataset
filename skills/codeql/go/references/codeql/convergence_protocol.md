# Mandatory synthesis convergence protocol

The Agent has a 30-minute hard wall-clock ceiling. It must finish all owned outputs and exit by
minute 27; minutes 27–30 are reserved for API/command latency and emergency shutdown only. Optimize
for a verified semantic detector, not for exhaustive exploration.

## Hard limits

- Modify `query/vulnerability.ql` early; do not leave the placeholder untouched while exploring.
- Use at most **three distinct scratch `.ql` files**. Prefer one reusable
  `query/scratch.ql`.
- After **two failed variants of the same API assumption**, stop guessing. Use
  `scripts/search_codeql_api.sh` to read the declaration and an official use site, or simplify to
  a lower mechanism that still proves the required semantics.
- Never dump the entire database AST or enumerate every library class. Ask one bounded question per
  scratch query.
- Once before/after evidence satisfies the mechanism contract, stop exploring and finalize.

## Wall-clock checkpoints

Run `scripts/check_progress.sh` at each transition.

| Elapsed target | Required state |
| --- | --- |
| 0–4 min | Read patch/context and relevant references; fill patch analysis and mechanism choice |
| by 6 min | Replace the placeholder and compile a minimal main query |
| by 12 min | Produce and inspect at least one intended before finding |
| by 20 min | Run after; explain elimination or make one bounded correction |
| by 24 min | Freeze mechanism; check sink locality, guard/barrier semantics, hardcoding, and `@id` |
| by 27 min | Finish state, scope, valid `summary.json`, and exit normally |
| 27–30 min | Emergency buffer only: no exploration or redesign; finalize immediately if still active |

If a checkpoint is missed, reduce scope before increasing complexity. Preserve the semantic core:
the vulnerable operation, trigger, and patch safety condition must still be represented.

`scripts/check_progress.sh` reports both `remaining_to_finalize_seconds` and
`remaining_to_hard_timeout_seconds`. Treat `deadline_phase=freeze_and_finalize` as a ban on new
mechanisms and `deadline_phase=finalize_now` as an instruction to stop all exploration immediately.

## Iteration loop

```text
write minimal main query
  → compile
  → run before
  → inspect exact intended sink
  → run after
  → change one predicate justified by evidence
  → repeat
```

Do not branch into many unrelated candidates. A scratch query answers an API-shape question; it is
not an alternative detector branch. Transfer a successful discovery into
`query/vulnerability.ql` immediately.

## Failure-aware fallback

When the intended global mechanism does not compile quickly:

1. confirm imports and types in the pinned library;
2. make source and sink predicates compile separately;
3. test local flow before global flow;
4. add barriers or additional flow steps one at a time;
5. do not fall back to filename, line, commit, patch-local name, or source-text substring matching.

A smaller semantically justified query is preferable to an elaborate unfinished query. A broad
syntactic proxy that ignores the patch safety condition is not an acceptable fallback.
