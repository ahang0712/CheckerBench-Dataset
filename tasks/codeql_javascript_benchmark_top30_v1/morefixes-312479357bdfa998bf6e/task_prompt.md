# JavaScript CodeQL detector synthesis

You are cc operating in `/work`. Starting only from the supplied patch,
before/after JavaScript context, CodeQL references, and validation tools, synthesize a semantic CodeQL
detector for the vulnerability fixed by the patch. This is one fresh session. You may iterate inside
this session, but no later framework repair or resume will occur.

## Inputs and owned outputs

- Read `patch.diff`, `javascript_patch.diff`, `pre_context.md`, and `post_context.md`.
- Read `references/codeql/convergence_protocol.md` and
  `references/codeql/javascript_api.md` before experimenting. Read the remaining
  `references/codeql/` files for the selected mechanism.
- Use `scripts/search_codeql_api.sh` to inspect the pinned official libraries when an API detail is
  uncertain. Do not guess predicate names or build a large family of inspection queries.
- Complete the design contract in `state.json` and `scope_statement.md` before final validation.
- Replace `query/vulnerability.ql`; add only genuinely shared `.qll` helpers when useful. Keep the
  fixed offline dependency in `query/qlpack.yml`.
- Preserve the exact `expected_query_id` from `config.json` as the query's sole `@id`. This
  task-unique metadata must never be used in a query predicate.
- Use `scripts/compile_query.sh`, `scripts/run_before.sh`, and `scripts/run_after.sh`.
- Finish by writing `summary.json` with `status`, `query_path`, `query_kind`, `build_passed`,
  `before_findings`, `after_findings`, `validation_completed`, and a concise `notes` field.

## Required reasoning contract

Before coding, record this mapping in `state.json`:

```text
vulnerable operation / trigger / patch safety condition
  → CodeQL-observable evidence
  → syntax, control-flow, local value-flow, global taint/value-flow, or interprocedural mechanism
  → source, sink/report site, guard/barrier and query predicates
```

Choose the lowest mechanism that can actually prove the condition. Guard-sensitive patches require
real path/control-flow semantics; provenance-sensitive patches require value flow. Do not replace
either with source-text matching or with the mere presence of patch-added syntax.

## Mandatory convergence contract

The rollout has a 50-minute hard wall-clock ceiling and must be finalized by minute 43. Minutes
43–50 are an emergency shutdown buffer, not exploration time. A sophisticated unfinished query is
a failure; an early compilable semantic candidate can be improved. Follow the checkpoints in
`references/codeql/convergence_protocol.md`:

1. Record the mechanism contract, then replace the placeholder in `query/vulnerability.ql`
   immediately. Do not wait until every CodeQL API detail is known.
2. Use at most three distinct scratch `.ql` files. Prefer repeatedly editing one
   `query/scratch.ql`. After two failed variants of one API assumption, inspect the exact bundled
   definition with `scripts/search_codeql_api.sh` or simplify the mechanism.
3. Obtain a compiling minimal main query before broadening it. Compile the main query after every
   meaningful mechanism change.
4. Move in order from compile → relevant before finding → after elimination → final contract. Do
   not continue API exploration after the intended before/after differential is established.
5. Run `scripts/check_progress.sh` at each checkpoint. It reports elapsed time, the minute-43
   finalization deadline, and the minute-50 hard timeout. When it prints `finalize_now`, stop all
   exploration, preserve the best compiling query, finish the contract files, and exit.

## Semantic and generalization rules

- Report at the vulnerable call, operator, access, or sink.
- Do not use a patch-local path, line, commit hash, label, or local variable name as the primary
  matching condition. Stable library/framework APIs or public project APIs are allowed only when
  `scope_statement.md` explains why they define the intended vulnerability family.
- The safety condition introduced by the patch must be represented by the query.
- A lower after warning count is not enough: inspect SARIF and ensure at least one intended before
  finding is in a patch-overlapping semantic unit and the corresponding finding disappears after.
- Avoid broad proxy queries that report an enclosing function or unrelated API merely because it
  changed in the patch.

## Final validation protocol

1. Compile the final query.
2. Run it on before and inspect `raw/before.sarif` for the intended report location.
3. Run the identical query on after and inspect `raw/after.sarif`.
4. Recheck query metadata, query kind/select signature, report locality, design-to-code consistency,
   and absence of patch-local hardcoding.
5. Update `state.json`, `scope_statement.md`, and write valid `summary.json`.

Do not modify the runtime databases, references, patch, context, or validation scripts except for
normal CodeQL-generated database caches produced by the provided run scripts.
