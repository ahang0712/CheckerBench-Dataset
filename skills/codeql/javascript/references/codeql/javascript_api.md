# JavaScript/TypeScript CodeQL API navigation (codeql/javascript-all 2.10.0)

The environment uses CodeQL 2.26.4 with `codeql/javascript-all` 2.10.0. Search the
installed official sources with `scripts/search_codeql_api.sh PATTERN`; do not
browse the network or guess a predicate name.

## Import and core call / property-access types

The top-level `import javascript` exposes `Call`, `Function`, `PropertyAccess`,
`Expr`, `VarAccess`, `StringLiteral`, `Module`, `File`, and the standard
`Location` predicates. Use `import javascript` plus, when needed, the dedicated
library modules:

```ql
import javascript
import semmle.javascript.dataflow.DataFlow
import semmle.javascript.dataflow.TaintTracking
import semmle.javascript.ApiGraphs
```

## Matching a call by callee name or library module

```ql
import javascript

from Call call
where call.getCalleeName() = "eval"
select call, "eval call"
```

For library APIs use the API graph (preferred: it survives aliasing and
re-exports):

```ql
import javascript
import semmle.javascript.ApiGraphs

from API::CallNode call
where call = API::moduleImport("child_process").getMember("exec").getACall()
select call, "child_process.exec invocation"
```

`API::moduleImport("name")` matches both `require("name")` and ES imports;
`.getMember(...)` walks property accesses, `.getACall()` lands on the call.
`Module.getPath()` gives the module path for CommonJS/ESM imports.

## Write options and missing-permission patterns

Many fixes add an explicit safe argument that was previously absent — e.g. a
file written without restrictive permissions:

```ql
import javascript

from Call call
where
  call.getCalleeName() = "writeFileSync" and
  not exists(call.getArgument(2))
select call, "writeFileSync without an explicit options argument"
```

`Call.getArgument(n)` / `call.getNumArgument()` mirror the call shape; the
safety condition introduced by the patch must be represented by the query.

## Value flow from a source to a sink

```ql
import javascript
import semmle.javascript.dataflow.DataFlow

from DataFlow::Node source, DataFlow::Node sink, Call call
where
  source.asExpr() = call.getArgument(0) and
  sink.asExpr() = call and
  DataFlow::localFlow(source, sink)
select call, "Argument flows to the call"
```

## Taint tracking configurations

Modern `javascript-all` exports global taint flow. Define a config module and
instantiate the flow (search for `TaintTracking::Global` and `ConfigSig` in the
installed sources for the exact 2.10.0 signature):

```ql
import javascript
import semmle.javascript.dataflow.TaintTracking

module MyConfig implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node node) { /* untrusted input */ }
  predicate isSink(DataFlow::Node node) { /* dangerous sink */ }
}

module MyFlow = TaintTracking::Global<MyConfig>;
```

## String constants

A string literal is a `StringLiteral`; call `getValue()` for its value. Do not
compare `Expr.toString()`, raw source text, or regex the file contents.

## Avoid patch-local matching

Do not use a patch-local file path, line number, commit hash, label, or local
variable name as a primary matching condition. Report at the vulnerable call /
property write / sink. A project/framework public API is allowed as the intended
vulnerability family only when `scope_statement.md` justifies it.

## Search recipes

```bash
scripts/search_codeql_api.sh 'class Call'
scripts/search_codeql_api.sh 'string getCalleeName'
scripts/search_codeql_api.sh 'predicate moduleImport'
scripts/search_codeql_api.sh 'class ConfigSig'
scripts/search_codeql_api.sh 'module Global'
scripts/search_codeql_api.sh 'class StringLiteral'
```

Read one declaration and one official use site, then implement the main query.
Do not create a new inspection query merely to rediscover an API already
present in the installed sources.
