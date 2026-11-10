# Compilable generic examples

These examples compile against the pinned `codeql/python-all` 7.2.4 pack and intentionally use
fictional package names, so they contain no task-specific answer.

- `api_call_problem.ql`: stable imported API identity and sink-local reporting;
- `local_flow_problem.ql`: local flow from an API return to a call argument;
- `global_taint_path.ql`: modular global taint configuration and path-query select signature.

Copy the smallest relevant structure into `query/vulnerability.ql` and replace its semantic
predicates. Do not combine all examples by default.
