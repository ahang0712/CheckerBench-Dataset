# Pinned Python CodeQL API navigation

The environment uses CodeQL 2.26.4, `codeql/python-all` 7.2.4, and
`codeql/python-queries` 1.8.9. Search these installed sources with
`scripts/search_codeql_api.sh PATTERN`; do not browse the network or guess an API.

## Prefer API graphs for imported and framework APIs

```ql
import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow

from API::CallNode call
where
  call = API::moduleImport("package")
              .getMember("dangerous_operation")
              .getACall()
select call.asExpr(), "Potentially unsafe call."
```

Key accessors:

- `API::moduleImport("pkg")`: imported module;
- `.getMember("name")` or `.getMember(["a", "b"])`: member identity;
- `.getACall()`: a correlated invocation represented by `API::CallNode`;
- `call.getArg(i)`, `getArgByName(name)`: actual argument data-flow nodes;
- `call.getParameter(i, name)`: the correlated positional-or-keyword API parameter;
- `call.getReturn()`: correlated return API node;
- `API::builtin("len").getACall()`: builtin invocation;
- `.getAValueReachableFromSource()`: values reachable from an API source;
- `.getAValueReachingSink()`: values reaching an API sink.

Use `call.asExpr()` as the report location. Do not use only
`Attribute.getName() = "dangerous_operation"` when imported API identity matters.

## Actual arguments versus API parameters

`API::CallNode` inherits the actual-call accessors of `DataFlow::CallCfgNode`:

```ql
call.getArg(0)                 // positional argument 0
call.getArgByName("option")   // explicit keyword argument
call.getStarArg()              // *args value
call.getKwargs()               // **kwargs value
```

When one logical parameter may be passed positionally or by name:

```ql
from API::CallNode call, API::Node parameter, DataFlow::Node value
where
  parameter = call.getParameter(1, "option") and
  value = parameter.getAValueReachingSink()
select call.asExpr(), value
```

Do not confuse AST `Call.getPositionalArg(i)` with data-flow
`API::CallNode.getArg(i)`.

## Project-defined calls

API graphs are strongest at library boundaries. For a project-defined function, use the raw AST,
data-flow call graph, or a stable public project API justified in `scope_statement.md`. Do not
replace failed call resolution with patch-local path, line, or local-name matching.

## Search recipes

```bash
# Find exact declarations.
scripts/search_codeql_api.sh 'class CallNode'
scripts/search_codeql_api.sh 'Node getParameter\(int i, string name\)'
scripts/search_codeql_api.sh 'predicate localFlow'
scripts/search_codeql_api.sh 'predicate controlsBlock'

# Find current official usage patterns.
scripts/search_codeql_api.sh 'API::moduleImport'
scripts/search_codeql_api.sh 'TaintTracking::Global<'
scripts/search_codeql_api.sh 'import .*::PathGraph'
```

Read one declaration and one official use site, then implement the main query. Do not create a new
inspection query merely to rediscover an API signature already present in the installed sources.

The `examples/` directory contains three generic, compile-checked starting points. Choose the
smallest one that matches the required mechanism; do not combine them speculatively.
