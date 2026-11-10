# CodeQL Python API orientation

This workdir is pinned to CodeQL 2.26.4 and `codeql/python-all` 7.2.4. Use
`scripts/search_codeql_api.sh` before inventing an API name. The installed library is the source of
truth; internet examples may target an older data-flow API.

## Capability ladder

1. **Stable external API identity:** prefer `API::moduleImport(...)` and `API::CallNode`.
2. **Python syntax shape:** use `Call`, `Attribute`, `Compare`, `Subscript`, `Return`, and
   related AST classes from `import python`.
3. **Local value/taint propagation:** use `DataFlow::localFlow` or
   `TaintTracking::localTaint` over `DataFlow::Node`.
4. **Path/control-flow proof:** map AST or data-flow nodes to `ControlFlowNode`, then use
   `dominates`, `strictlyReaches`, `DataFlow::GuardNode.controlsBlock`, or a barrier guard.
5. **Global/interprocedural provenance:** implement `DataFlow::ConfigSig` and instantiate
   `DataFlow::Global<Config>` or `TaintTracking::Global<Config>`.

Choose the lowest capability that proves both the vulnerable operation and the patch's safety
condition. Do not use raw AST merely because global flow is harder to write.

## Imports that exist in the pinned pack

```ql
import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow
import semmle.python.dataflow.new.TaintTracking
```

Do not use the deprecated old data-flow library. Official examples for this pinned bundle are
searchable with:

```bash
scripts/search_codeql_api.sh 'module .*Config implements DataFlow::ConfigSig'
scripts/search_codeql_api.sh 'getParameter\(int i, string name\)'
scripts/search_codeql_api.sh 'controlsBlock'
```

See `api_navigation.md` for exact call and argument accessors.
