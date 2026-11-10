# Go CodeQL API navigation (codeql/go-all 7.3.0)

The environment uses CodeQL 2.26.4 with `codeql/go-all` 7.3.0. Search the
installed official sources with `scripts/search_codeql_api.sh PATTERN`; do not
browse the network or guess a predicate name.

## Import and core AST types

The top-level `import go` exposes `CallExpr`, `FuncDecl`, `FuncLit`, `Ident`,
`SelectorExpr`, `IndexExpr`, `BasicLit`, `Expr`, `Stmt`, `File`, and the
standard `Location` predicates. Use `import go` plus, when needed, the data
flow modules:

```ql
import go
import semmle.go.dataflow.DataFlow
import semmle.go.dataflow.TaintTracking
```

## Matching a call by callee name or package path

```ql
import go

from CallExpr call
where call.getCallee().(Ident).getName() = "Open"
select call, "call to Open"
```

For a selector call (`pkg.Func`):

```ql
import go

from CallExpr call, SelectorExpr sel, Ident qualifier, Ident callee
where
  call.getCallee() = sel and
  sel.getBase() = qualifier and
  sel.getSelector() = callee and
  qualifier.getName() = "filepath" and
  callee.getName() = "Join"
select call, "filepath.Join call"
```

For package-aware matching, load the callee's package via
`call.getCallee().(SelectorExpr).getBase()` and compare `ImportSpec` paths —
the installed `semmle/go/` sources have `getPackage`/`getImportPath` helpers
(search for them; do not hardcode a repo-local path).

## The safety argument for guard-style fixes

Many Go security fixes add an explicit check or a sanitising call before a
dangerous operation (path cleaning, bounds checks, error returns). The query
must represent that condition; the common shapes are:

```ql
import go

// a call whose argument is not first cleaned by filepath.Clean/Rel
from CallExpr sink
where
  sink.getCallee().(SelectorExpr).getSelector().(Ident).getName() = "Open"
  and not exists(CallExpr clean |
    clean.getCallee().(SelectorExpr).getSelector().(Ident).getName() = "Clean" and
    DataFlow::localFlow(DataFlow::exprNode(clean), DataFlow::exprNode(sink.getArgument(0)))
  )
select sink, "Open without a Clean barrier"
```

## Value and taint flow

Go data flow is SSA-based: `DataFlow::Node` wraps both expressions and
SSA definitions, so always use `asExpr()` (may be empty for SSA-only nodes)
and prefer `DataFlow::localFlow(source, sink)` before reaching for a taint
configuration.

```ql
import go
import semmle.go.dataflow.DataFlow

from DataFlow::Node source, DataFlow::Node sink
where
  DataFlow::localFlow(source, sink)
  and source.asExpr().(CallExpr).getCallee().(Ident).getName() = "Input"
select sink, "flows to here"
```

For interprocedural taint, subclass the taint configuration in
`semmle.go.dataflow.TaintTracking` (search `class Configuration` /
`isSource` / `isSink` for the exact 7.3.0 signature).

## String constants

A string literal is a `BasicLit`; call `getValue()` (the raw literal,
including quotes where present) and `getText()` — do not regex the file
contents or compare `Expr.toString()`.

## Avoid patch-local matching

Do not use a patch-local file path, line number, commit hash, label, or local
variable name as a primary matching condition. Report at the vulnerable call,
path operation, or sink. A project/framework public API is allowed as the
intended vulnerability family only when `scope_statement.md` justifies it.

## Search recipes

```bash
scripts/search_codeql_api.sh 'class CallExpr'
scripts/search_codeql_api.sh 'predicate localFlow'
scripts/search_codeql_api.sh 'class Configuration' 
scripts/search_codeql_api.sh 'string getCallee'
scripts/search_codeql_api.sh 'class BasicLit'
scripts/search_codeql_api.sh 'predicate getImportPath'
```

Read one declaration and one official use site, then implement the main query.
Do not create a new inspection query merely to rediscover an API already
present in the installed sources.
